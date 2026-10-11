"""Test a connected Wave Pal: data transfer, playback, and timing budget.

Run it after changing the Wave Pal firmware or pulsepal/wave_pal.py:

    python tests/wavepal_hardware_test.py COM3
    python tests/wavepal_hardware_test.py /dev/ttyACM0 --quick
    python tests/wavepal_hardware_test.py COM3 --driver COM4

The device must be a Pulse Pal 3 running Wave Pal firmware. Nothing needs to be
connected to it, but the outputs play random waveforms of up to +/-10 V, so
disconnect anything that should not receive them. With --driver, a Pulse Pal
running Pulse Pal firmware, whose output channels 1 and 2 are wired to the Wave
Pal's trigger channels 1 and 2, sends the TTL edges that the trigger mode and
param sync tests need; without it, those tests are skipped. The driver's outputs
pulse from 0 V to 4 V only: the trigger inputs take 3 to 5 V.

Every sample the device plays is checked: its firmware sums the DAC codes each
channel plays (op 90), and each test compares the sums with the waveforms it
loaded. Along the way, the script reports waveform transfer speed, underruns,
and the longest run of the sample clock interrupt against the sample period.

Things only a scope or the joystick can check are listed in
/Firmware/WavePal/AGENTS.md.
"""
import argparse
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from pulsepal import wave_pal as WavePal  # noqa: E402
from pulsepal import PulsePalDevice, PulsePalError, WavePalDevice  # noqa: E402

RNG = np.random.default_rng(1)


def random_waveform(n_samples, low=-10, high=10):
    return RNG.uniform(low, high, n_samples)


def expected_sum(codes, n_played):
    """Sum of the codes played by n_played samples of a (looping) waveform, mod 2**32."""
    codes = np.asarray(codes, dtype=np.uint64)
    loops, remainder = divmod(n_played, codes.size)
    total = loops * int(codes.sum()) + int(codes[:remainder].sum())
    return total % 2**32


def wait_until_stopped(W, channels, timeout):
    """Wait for channels to stop playing. Returns the longest interrupt seen, in us."""
    deadline = time.monotonic() + timeout
    longest = 0.0
    while True:
        status = W.status()
        longest = max(longest, status.longest_interrupt_us)
        if not set(channels) & set(status.playing):
            return longest
        if time.monotonic() > deadline:
            raise AssertionError(f"channels {channels} still playing after {timeout} s")
        time.sleep(0.05)


def check_played(W, channel, codes, n_expected):
    played, sums = W._playback_checksums()
    assert played[channel] == n_expected, (
        f"channel {channel} played {played[channel]} samples, expected {n_expected}")
    expected = expected_sum(codes, n_expected)
    assert sums[channel] == expected, (
        f"channel {channel}: sum of the samples played is {sums[channel]}, expected {expected}")


def load(W, number, volts, channels="same"):
    """Load waveform `number`, played by `channels`: by default, the channel of the same number;
    None changes no channel. Returns its DAC codes, as the device stores them."""
    W.load_waveform(number, volts, number if channels == "same" else channels)
    return W._volts_to_codes(volts)


def test_connection(W):
    assert W.info.firmware_version == 1
    assert W.info.hardware_version == 3
    assert W.info.max_samples == 1000000
    assert W.info.n_waveforms == 4
    status = W.status()
    assert status.playing == [], status
    assert status.current_waveform == [None, 0, 0, 0, 0], status
    assert status.stored_waveforms == [] and status.stored_current_waveform is None, status


def test_waveforms_play_exactly_once_around_the_buffer_size(W):
    """Waveform lengths at and around the buffer boundaries, played once. Short
    ones play from RAM, longer ones switch to the microSD card part way."""
    W.sampling_rate = 100000
    buffer = W.info.buffer_samples
    for n in (1, 2, buffer - 1, buffer, buffer + 1, 2 * buffer, 2 * buffer + 1, 3 * buffer + 7):
        codes = load(W, 1, random_waveform(n))
        W.trigger(1)
        wait_until_stopped(W, [1], timeout=5)
        check_played(W, 1, codes, n)


def test_loop_durations_wrap_exactly_around_the_buffer_size(W):
    """Loop mode with a duration of 3.3 waveforms: the wrap from the last block
    back to the first, with the playback buffers in every arrangement."""
    W.sampling_rate = 100000
    W.loop_mode[1] = True
    buffer = W.info.buffer_samples
    try:
        for n in (1, 3, buffer - 1, buffer, buffer + 1, 2 * buffer, 2 * buffer + 1,
                  3 * buffer, 4 * buffer + 5):
            codes = load(W, 1, random_waveform(n))
            n_played = int(3.3 * n) + 1
            W.loop_duration[1] = n_played / W.actual_sampling_rate
            W.trigger(1)
            wait_until_stopped(W, [1], timeout=10)
            check_played(W, 1, codes, n_played)
    finally:
        W.loop_mode[1] = False
        W.loop_duration[1] = 0


def test_loop_duration_is_exact_in_samples(W):
    W.sampling_rate = 100000
    codes = load(W, 2, random_waveform(1000))
    W.loop_mode[2] = True
    W.loop_duration[2] = 0.12345
    try:
        W.trigger(2)
        wait_until_stopped(W, [2], timeout=2)
        check_played(W, 2, codes, 12345)
    finally:
        W.loop_mode[2] = False
        W.loop_duration[2] = 0


def test_full_length_waveform_transfer_and_playback(W, report):
    W.sampling_rate = 100000
    volts = random_waveform(W.info.max_samples)
    start = time.perf_counter()
    codes = load(W, 1, volts)
    seconds = time.perf_counter() - start
    report(f"load 1M samples: {seconds:.2f} s, {2 * volts.size / seconds / 1e6:.2f} MB/s")
    underruns_before = W.status().underruns[1]
    W.trigger(1)
    longest = wait_until_stopped(W, [1], timeout=15)
    check_played(W, 1, codes, volts.size)
    assert W.status().underruns[1] == underruns_before, "underruns during playback"
    report(f"1 channel at 100 kHz: longest interrupt {longest:.2f} us of 10 us")


def test_four_channels_at_100khz(W, report, seconds=25):
    """All four channels stream 1M sample waveforms from the card at once, and
    loop, for `seconds`."""
    W.sampling_rate = 100000
    codes = {}
    for channel in (1, 2, 3, 4):
        codes[channel] = load(W, channel, random_waveform(W.info.max_samples))
    W.loop_mode = [True] * 4
    n_played = int(seconds * W.actual_sampling_rate)
    W.loop_duration = [n_played / W.actual_sampling_rate] * 4
    try:
        underruns_before = W.status().underruns
        W.status()  # Reset the longest interrupt
        W.trigger([1, 2, 3, 4])
        longest = wait_until_stopped(W, [1, 2, 3, 4], timeout=seconds + 5)
        for channel in (1, 2, 3, 4):
            check_played(W, channel, codes[channel], n_played)
        underruns = W.status().underruns
        assert underruns == underruns_before, f"underruns: {underruns} (were {underruns_before})"
        report(f"4 channels at 100 kHz for {seconds} s: longest interrupt {longest:.2f} us "
               "of 10 us, no underruns")
        assert longest < 10, "the interrupt takes longer than the sample period"
    finally:
        W.loop_mode = [False] * 4
        W.loop_duration = [0] * 4


def test_loading_while_other_channels_play(W, report):
    """Channels 1-3 stream at 100 kHz while channel 4's waveform is loaded."""
    W.sampling_rate = 100000
    codes = {}
    for channel in (1, 2, 3):
        codes[channel] = load(W, channel, random_waveform(W.info.max_samples))
    W.loop_mode[1:4] = [True] * 3
    try:
        underruns_before = W.status().underruns
        W.trigger([1, 2, 3])
        time.sleep(0.5)
        start = time.perf_counter()
        load(W, 4, random_waveform(W.info.max_samples))
        load_seconds = time.perf_counter() - start
        time.sleep(0.5)
        W.stop([1, 2, 3])
        played, _ = W._playback_checksums()
        underruns = W.status().underruns
        new_underruns = [underruns[ch] - underruns_before[ch] for ch in (1, 2, 3)]
        report(f"load 1M samples while 3 channels play at 100 kHz: {load_seconds:.2f} s, "
               f"underruns on channels 1-3: {new_underruns}")
        for channel, n_underruns in zip((1, 2, 3), new_underruns):
            if n_underruns == 0:  # An underrun holds samples, so the sum could not match
                check_played(W, channel, codes[channel], played[channel])
    finally:
        W.loop_mode = [False] * 4


def test_param_sync_loads_while_four_channels_play(W, D, report):
    """The next trial's waveforms, 1M samples each (the maximum), are loaded in param sync mode while
    all four channels stream different waveforms at 100 kHz. Playback must be undisturbed: each
    channel plays its old waveform to the end, sample for sample, with no underrun, and the sample
    clock interrupt stays within the sample period. Then the edge (from the driver's output 2, or,
    without --driver, leaving param sync mode, which applies the stored loads the same way) makes the
    new waveforms current, and each is checked as played: whole, and up to random points, so that
    chunks stored out of order show too (a whole waveform's sum does not depend on their order)."""
    W.sampling_rate = 100000
    rate = W.actual_sampling_rate
    max_samples = W.info.max_samples
    old = {n: load(W, n, random_waveform(400000 + 1000 * n)) for n in (1, 2, 3, 4)}  # About 4 s each
    W.loop_mode = [False] * 4
    W.loop_duration = [0] * 4
    W.trigger_mode[2] = "Param Sync"
    try:
        underruns_before = W.status().underruns
        W.status()  # Reset the longest interrupt
        W.trigger([1, 2, 3, 4])
        time.sleep(0.2)
        new = {}
        start = time.perf_counter()
        for n in (1, 2, 3, 4):
            volts = random_waveform(max_samples)
            W.load_waveform(n, volts)
            new[n] = W._volts_to_codes(volts)
        load_seconds = time.perf_counter() - start
        status = W.status()
        longest_during_loads = status.longest_interrupt_us
        assert status.playing == [1, 2, 3, 4], f"a channel stopped during the loads: {status}"
        assert status.stored_waveforms == [1, 2, 3, 4], status
        assert status.samples_loaded[1:] == [old[n].size for n in (1, 2, 3, 4)], status
        longest = max(longest_during_loads, wait_until_stopped(W, [1, 2, 3, 4], timeout=8))
        for n in (1, 2, 3, 4):
            check_played(W, n, old[n], old[n].size)
        underruns = W.status().underruns
        assert underruns == underruns_before, f"underruns: {underruns} (were {underruns_before})"
        assert longest < 10, f"the interrupt took {longest:.2f} us, longer than the sample period"
        report(f"4 x 1M samples loaded in param sync mode while 4 channels play at 100 kHz: {load_seconds:.2f} s; "
               f"old waveforms played exactly, no underruns, longest interrupt {longest_during_loads:.2f} us "
               "during the loads")

        if D is not None:
            driver_pulse(D, channels=(2,))
        else:
            W.trigger_mode[2] = "Normal"
        status = W.status()
        assert status.stored_waveforms == [] and status.samples_loaded[1:] == [max_samples] * 4, status
        W.trigger([1, 2, 3, 4])
        wait_until_stopped(W, [1, 2, 3, 4], timeout=12)
        for n in (1, 2, 3, 4):
            check_played(W, n, new[n], max_samples)
        W.loop_mode = [True] * 4  # A loop duration shorter than the waveform plays exactly that many samples
        for _ in range(2):
            prefixes = RNG.integers(1, max_samples, 4)
            W.loop_duration = [int(k) / rate for k in prefixes]
            W.trigger([1, 2, 3, 4])
            wait_until_stopped(W, [1, 2, 3, 4], timeout=12)
            for n in (1, 2, 3, 4):
                check_played(W, n, new[n], int(prefixes[n - 1]))
        assert W.status().underruns == underruns_before, "underruns while the new waveforms played"
    finally:
        W.trigger_mode = ["Normal", "Normal"]
        W.loop_mode = [False] * 4
        W.loop_duration = [0] * 4
        W.stop()


def test_one_waveform_on_several_channels(W, report):
    """A waveform loaded once plays on every channel it is given to: four channels stream the same
    memory block from the card at 100 kHz, each reading it for itself."""
    W.sampling_rate = 100000
    codes = load(W, 2, random_waveform(20 * W.info.buffer_samples + 11), [1, 2, 3, 4])  # 3.3 s
    assert W.current_waveform == [None, 2, 2, 2, 2]
    assert W.status().current_waveform == [None, 2, 2, 2, 2]
    underruns_before = W.status().underruns
    W.status()  # Reset the longest interrupt
    W.trigger([1, 2, 3, 4])
    longest = wait_until_stopped(W, [1, 2, 3, 4], timeout=6)
    for channel in (1, 2, 3, 4):
        check_played(W, channel, codes, codes.size)
    assert W.status().underruns == underruns_before, "underruns during playback"
    report(f"one waveform on 4 channels at 100 kHz: longest interrupt {longest:.2f} us of 10 us, no underruns")


def test_current_waveform_changes_at_the_next_start(W):
    """A playing channel finishes its waveform; the new current waveform plays from its next start.
    The playback buffers then hold chunks of the first waveform at the chunk numbers the second one
    needs, which must not be played as the second's."""
    W.sampling_rate = 100000
    buffer = W.info.buffer_samples
    codes1 = load(W, 1, random_waveform(5 * buffer))          # 0.8 s
    codes3 = load(W, 3, random_waveform(4 * buffer + 3), None)  # Played by no channel yet
    W.trigger(1)
    time.sleep(0.3)
    W.current_waveform[1] = 3
    assert W.status().current_waveform[1] == 3
    wait_until_stopped(W, [1], timeout=3)
    check_played(W, 1, codes1, codes1.size)
    for _ in range(2):
        W.trigger(1)
        wait_until_stopped(W, [1], timeout=3)
        check_played(W, 1, codes3, codes3.size)
    W.current_waveform[1] = 1  # And back: chunks of waveform 3 are now buffered
    W.trigger(1)
    wait_until_stopped(W, [1], timeout=3)
    check_played(W, 1, codes1, codes1.size)


def test_loading_stops_only_the_channels_playing_that_waveform(W):
    """Outside param sync mode, loading a waveform stops the channels playing it. Others play on."""
    W.sampling_rate = 10000
    load(W, 1, random_waveform(50000), [1, 3])  # 5 s
    load(W, 2, random_waveform(50000))
    W.trigger([1, 2, 3])
    time.sleep(0.2)
    codes = load(W, 1, random_waveform(300), None)
    assert W.status().playing == [2], W.status()
    W.trigger([1, 3])
    wait_until_stopped(W, [1, 3], timeout=1)
    check_played(W, 1, codes, codes.size)
    check_played(W, 3, codes, codes.size)
    W.stop()


def test_param_sync_without_an_edge(W):
    """In param sync mode, loads and current waveforms wait for an edge, which never comes on an
    unconnected trigger channel: channels play on, and triggers play, the old waveform. Leaving the
    mode makes them current, as an edge would. A later load stops a channel still playing the memory
    block it reuses."""
    W.sampling_rate = 100000
    buffer = W.info.buffer_samples
    W.current_waveform = [0] * 4
    old = load(W, 1, random_waveform(6 * buffer))  # 1 s
    W.trigger_mode[2] = "Param Sync"
    try:
        W.trigger(1)
        new = load(W, 1, random_waveform(3 * buffer + 1), [1, 2])
        status = W.status()
        assert status.playing == [1], f"a param sync load stopped the channel playing the old waveform: {status}"
        assert status.stored_waveforms == [1] and status.stored_current_waveform == [None, 1, 1, 0, 0], status
        assert status.samples_loaded[1] == old.size and status.current_waveform == [None, 1, 0, 0, 0], status
        try:
            W.output_range = "-5V:5V"
            raise AssertionError("output_range changed in param sync mode")
        except PulsePalError:
            pass
        wait_until_stopped(W, [1], timeout=3)
        check_played(W, 1, old, old.size)
        W.trigger(1)  # Until an edge, triggers play the old waveform
        time.sleep(0.2)
        W.trigger_mode[2] = "Normal"  # Leaving the mode makes the stored load current, as an edge would
        status = W.status()
        assert status.stored_waveforms == [] and status.stored_current_waveform is None, status
        assert status.samples_loaded[1] == new.size and status.current_waveform == [None, 1, 1, 0, 0], status
        assert status.playing == [1], f"the channel playing the old waveform was stopped: {status}"
        wait_until_stopped(W, [1], timeout=3)
        check_played(W, 1, old, old.size)

        # Channel 1 plays the new waveform's memory block. Two loads later (one current, one stored)
        # that block is reused, so the second load stops the channel.
        W.trigger(1)
        time.sleep(0.05)
        W.trigger_mode[2] = "Param Sync"
        newer = load(W, 1, random_waveform(2 * buffer), None)
        W.trigger_mode[2] = "Normal"
        assert W.status().playing == [1], "the channel playing the older block was stopped"
        W.trigger_mode[2] = "Param Sync"
        newest = load(W, 1, random_waveform(4 * buffer + 2), None)
        assert W.status().playing == [], "a load into the block a channel plays did not stop it"
        W.trigger_mode[2] = "Normal"
        W.trigger([1, 2])
        wait_until_stopped(W, [1, 2], timeout=3)
        check_played(W, 1, newest, newest.size)
        check_played(W, 2, newest, newest.size)
        assert newer.size  # Replaced before it played
    finally:
        W.trigger_mode[2] = "Normal"
        W.stop()


class Skipped(Exception):
    """Raised by a test that needs something this run does not have."""


def driver_pulse(D, width=0.001, channels=(1,)):
    """A TTL pulse on the driver's output channels (1, 2 or both, together), 0 V to 4 V: the Wave
    Pal's trigger inputs take 3 to 5 V. Returns once it has ended."""
    for channel in channels:
        D.phase1_duration[channel] = width
        D.pulse_train_duration[channel] = width
    D.trigger(list(channels))
    time.sleep(width + 0.02)


def test_param_sync_edges(W, D):
    """Edges on trigger channel 2 in param sync mode, from the driver's output 2: an edge makes the
    stored load and current waveforms current, starts nothing, and lets a playing channel finish.
    With the same TTL on both trigger channels, the channels trigger channel 1 starts play the new
    waveform, whichever trigger interrupt runs first."""
    if D is None:
        raise Skipped("needs --driver")
    W.sampling_rate = 100000
    buffer = W.info.buffer_samples
    W.current_waveform = [0] * 4
    first = load(W, 1, random_waveform(3 * buffer))
    W.link_trigger_channel1 = [True, False, False, False]
    W.link_trigger_channel2 = [False, False, True, False]  # Ignored in param sync mode
    load(W, 3, random_waveform(1000))
    W.trigger_mode[2] = "Param Sync"
    try:
        second = load(W, 1, random_waveform(2 * buffer + 9))
        driver_pulse(D, channels=(2,))
        status = W.status()
        assert status.stored_waveforms == [] and status.samples_loaded[1] == second.size, status
        assert status.playing == [], f"the param sync edge started a channel: {status}"
        driver_pulse(D, channels=(1,))
        wait_until_stopped(W, [1], timeout=3)
        check_played(W, 1, second, second.size)

        # A channel playing at the edge finishes its waveform, and plays the new one from its next start
        long_one = load(W, 1, random_waveform(8 * buffer))  # 1.3 s
        driver_pulse(D, channels=(2,))
        W.trigger(1)
        time.sleep(0.2)
        third = load(W, 1, random_waveform(buffer + 1))
        driver_pulse(D, channels=(2,))
        assert W.status().playing == [1], "the param sync edge stopped the channel"
        wait_until_stopped(W, [1], timeout=3)
        check_played(W, 1, long_one, long_one.size)
        W.trigger(1)
        wait_until_stopped(W, [1], timeout=3)
        check_played(W, 1, third, third.size)

        # The same TTL on both trigger channels: the new waveform and current waveform apply first. Either
        # trigger channel in param sync mode, so that either trigger interrupt is the one that runs first.
        for sync_channel, start_channel in ((2, 1), (1, 2)):
            W.trigger_mode = ["Normal", "Normal"]
            W.current_waveform[1] = 1
            W.link_trigger_channel1 = [start_channel == 1, False, False, False]
            W.link_trigger_channel2 = [start_channel == 2, False, False, False]
            W.trigger_mode[sync_channel] = "Param Sync"
            for attempt in range(10):
                codes = load(W, 2, random_waveform(2000 + attempt), 1)  # Channel 1 moves to waveform 2 at the edge
                assert W.status().current_waveform[1] == 1
                driver_pulse(D, channels=(1, 2))
                wait_until_stopped(W, [1], timeout=1)
                check_played(W, 1, codes, codes.size)
                W.current_waveform[1] = 1  # Stored too: the next edge moves channel 1 back to waveform 1
                driver_pulse(D, channels=(sync_channel,))
    finally:
        W.trigger_mode = ["Normal", "Normal"]
        W.link_trigger_channel1 = [True] * 4
        W.link_trigger_channel2 = [False] * 4
        W.stop()


def open_driver(port):
    """The driving Pulse Pal, set up for single positive pulses on output channel 1."""
    D = PulsePalDevice(port)
    with D.batch():
        D.phase1_voltage = [4] * 4
        D.inter_pulse_interval = [0.001] * 4
        D.link_trigger_channel1 = [False] * 4
    return D


def test_soft_triggers_start_idle_channels_only(W):
    """As in Pulse Pal, whatever the trigger mode: a soft trigger is ignored while the channel plays."""
    W.sampling_rate = 10000
    load(W, 1, random_waveform(100000))  # 10 s
    try:
        for mode in W.info.trigger_modes:
            W.trigger_mode[1] = mode
            W.trigger(1)
            time.sleep(0.5)
            W.trigger(1)
            time.sleep(0.2)
            played, _ = W._playback_checksums()
            assert played[1] > 6000, f"a soft trigger in {mode} mode restarted the waveform ({played[1]} samples)"
            assert 1 in W.status().playing, f"a soft trigger in {mode} mode stopped the waveform"
            W.stop(1)
            assert 1 not in W.status().playing
    finally:
        W.trigger_mode[1] = "Normal"


def test_trigger_modes(W, D):
    """Each mode of trigger channel 1, with TTL pulses from the driver."""
    if D is None:
        raise Skipped("needs --driver")
    W.sampling_rate = 10000
    load(W, 1, random_waveform(100000))  # 10 s
    W.link_trigger_channel1[1] = True
    try:
        W.trigger_mode[1] = "Normal"          # A second edge is ignored
        driver_pulse(D)
        time.sleep(0.5)
        driver_pulse(D)
        played, _ = W._playback_checksums()
        assert played[1] > 5000, f"normal mode restarted the waveform ({played[1]} samples)"
        W.stop(1)

        W.trigger_mode[1] = "Master"          # A second edge restarts it
        driver_pulse(D)
        time.sleep(0.5)
        driver_pulse(D)
        played, _ = W._playback_checksums()
        assert played[1] < 2000, f"master mode did not restart the waveform ({played[1]} samples)"
        W.stop(1)

        W.trigger_mode[1] = "Toggle"          # A second edge stops it
        driver_pulse(D)
        assert 1 in W.status().playing, "toggle mode did not start the waveform"
        driver_pulse(D)
        assert 1 not in W.status().playing, "toggle mode did not stop the waveform"

        W.trigger_mode[1] = "Gated"           # It plays while the TTL is high
        driver_pulse(D, width=0.3)
        played, _ = W._playback_checksums()
        assert 1 not in W.status().playing, "gated mode did not stop the waveform at the falling edge"
        assert 2900 <= played[1] <= 3100, f"gated mode played {played[1]} samples for a 0.3 s pulse"
    finally:
        W.trigger_mode[1] = "Normal"
        W.stop(1)


def test_restarts_part_way_through_a_streamed_waveform(W, D):
    """Master mode restarts at points where the playback buffers hold blocks from
    the middle of the waveform. After each restart the whole waveform must play
    again, exactly."""
    if D is None:
        raise Skipped("needs --driver")
    W.sampling_rate = 100000
    codes = load(W, 1, random_waveform(7 * W.info.buffer_samples + 100))  # 1.15 s
    W.trigger_mode[1] = "Master"
    W.link_trigger_channel1[1] = True
    try:
        for restart_after in (0.05, 0.2, 0.35, 0.6, 0.9):
            W.trigger(1)
            time.sleep(restart_after)
            driver_pulse(D)
            wait_until_stopped(W, [1], timeout=3)
            check_played(W, 1, codes, codes.size)
    finally:
        W.trigger_mode[1] = "Normal"


def test_sampling_rate_change_during_playback(W):
    """The rate can change while a waveform plays: no sample is lost or repeated."""
    W.sampling_rate = 20000
    codes = load(W, 3, random_waveform(60000))  # 3 s at 20 kHz
    W.trigger(3)
    time.sleep(0.5)
    W.sampling_rate = 100000
    wait_until_stopped(W, [3], timeout=3)
    check_played(W, 3, codes, codes.size)


def test_channel_joining_a_running_clock_plays_exactly(W):
    """A channel started while another plays starts on the running sample clock."""
    W.sampling_rate = 50000
    codes1 = load(W, 1, random_waveform(200000))   # 4 s
    codes2 = load(W, 2, random_waveform(30000))    # 0.6 s
    W.trigger(1)
    time.sleep(0.3)
    W.trigger(2)
    wait_until_stopped(W, [1, 2], timeout=6)
    check_played(W, 1, codes1, codes1.size)
    check_played(W, 2, codes2, codes2.size)


def test_fixed_voltage_and_stop(W):
    W.set_fixed_voltage([1, 3], 2.5)
    assert W.status().playing == []
    W.stop()


def test_screen_saver_and_export_params(W):
    """Op 99 takes the screen saver's state and timeout, as on Pulse Pal and Synth Pal. Left on,
    with 1800 s: a new device's settings. export_params() holds every setting."""
    W.set_screen_saver(False)
    W.set_screen_saver(True, 1800)
    for enabled, timeout in ((2, 1800), (True, 0), (True, 65536)):
        try:
            W.set_screen_saver(enabled, timeout)
        except PulsePalError:
            continue
        raise AssertionError(f"set_screen_saver({enabled!r}, {timeout!r}) was not refused")
    W.loop_mode[2] = True
    W.loop_duration[2] = 0.5
    params = W.export_params()
    assert params["loop_mode"] == [False, True, False, False] and params["loop_duration"][1] == 0.5, params
    assert params["sampling_rate"] == W.sampling_rate and params["output_range"] == W.output_range, params
    assert params["current_waveform"] == W.status().current_waveform[1:], params
    W.loop_mode[2] = False
    W.loop_duration[2] = 0


def test_output_range_change_reloads_waveforms(W):
    W.sampling_rate = 100000
    lengths = [None, 5000, 40000, 1, 20000]
    for channel in (1, 2, 3, 4):  # Every waveform must fit the new range
        load(W, channel, random_waveform(lengths[channel], -4, 4))
    W.output_range = "-5V:5V"
    try:
        assert W.status().samples_loaded == lengths
        for channel in (1, 2, 3, 4):
            codes = W._volts_to_codes(W.waveforms[channel])
            W.trigger(channel)
            wait_until_stopped(W, [channel], timeout=3)
            check_played(W, channel, codes, codes.size)
    finally:
        W.output_range = "-10V:10V"


def test_every_output_range_plays(W):
    """The device accepts each range, and plays a waveform encoded for it."""
    W.sampling_rate = 100000
    for channel in (1, 2, 3, 4):
        load(W, channel, [0.0])  # 0 V fits every range, so no range change is refused
    try:
        for range_name in W.info.output_ranges:
            W.output_range = range_name  # The class loads the waveforms again, re-encoded
            low, high = WavePal.OUTPUT_RANGES[range_name]
            codes = load(W, 1, random_waveform(20000, low, high))
            W.trigger(1)
            wait_until_stopped(W, [1], timeout=3)
            check_played(W, 1, codes, codes.size)
            load(W, 1, [0.0])
    finally:
        W.output_range = "-10V:10V"  # The widest range, which fits any waveform left loaded


def test_pulse_pal_class_is_refused(W):
    port = W.port.port
    W.close()
    try:
        PulsePalDevice(port)
        raise AssertionError("PulsePalDevice connected to a Wave Pal")
    except PulsePalError as error:
        assert "runs Wave Pal firmware" in str(error), error


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", help="Serial port of the Wave Pal, e.g. COM3 or /dev/ttyACM0")
    parser.add_argument("--quick", action="store_true",
                        help="Skip the tests that stream 1M sample waveforms")
    parser.add_argument("--driver", metavar="PORT",
                        help="A Pulse Pal whose outputs 1 and 2 drive trigger channels 1 and 2, for the trigger "
                             "mode and param sync tests")
    arguments = parser.parse_args()
    D = open_driver(arguments.driver) if arguments.driver else None

    notes = []
    tests = [
        test_connection,
        test_waveforms_play_exactly_once_around_the_buffer_size,
        test_loop_durations_wrap_exactly_around_the_buffer_size,
        test_loop_duration_is_exact_in_samples,
        test_soft_triggers_start_idle_channels_only,
        lambda W: test_one_waveform_on_several_channels(W, notes.append),
        test_current_waveform_changes_at_the_next_start,
        test_loading_stops_only_the_channels_playing_that_waveform,
        test_param_sync_without_an_edge,
        lambda W: test_trigger_modes(W, D),
        lambda W: test_restarts_part_way_through_a_streamed_waveform(W, D),
        lambda W: test_param_sync_edges(W, D),
        test_sampling_rate_change_during_playback,
        test_channel_joining_a_running_clock_plays_exactly,
        test_fixed_voltage_and_stop,
        test_screen_saver_and_export_params,
        test_output_range_change_reloads_waveforms,
        test_every_output_range_plays,
    ]
    if not arguments.quick:
        tests += [
            lambda W: test_full_length_waveform_transfer_and_playback(W, notes.append),
            lambda W: test_four_channels_at_100khz(W, notes.append),
            lambda W: test_loading_while_other_channels_play(W, notes.append),
            lambda W: test_param_sync_loads_while_four_channels_play(W, D, notes.append),
        ]
    tests.append(test_pulse_pal_class_is_refused)  # Last: it closes the connection

    failures = skipped = 0
    with WavePalDevice(arguments.port) as W:
        for test in tests:
            name = getattr(test, "__name__", "test")
            if name == "<lambda>":
                name = test.__code__.co_names[0]
            start = time.perf_counter()
            try:
                test(W)
                result = "ok"
            except Skipped as reason:
                skipped += 1
                result = f"skipped ({reason})"
            except (AssertionError, PulsePalError) as error:
                failures += 1
                result = f"FAILED: {error}"
                if not W._closed:
                    W.stop()
            print(f"{name:<60} {time.perf_counter() - start:6.1f} s  {result}")
            while notes:
                print("    " + notes.pop(0))
    if D is not None:
        D.close()
    print(f"\n{len(tests) - failures - skipped}/{len(tests) - skipped} tests passed"
          + (f", {skipped} skipped" if skipped else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
