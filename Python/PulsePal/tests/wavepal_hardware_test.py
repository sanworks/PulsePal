"""Test a connected Wave Pal: data transfer, playback, and timing budget.

Run it after changing the Wave Pal firmware or WavePal.py:

    python tests/wavepal_hardware_test.py COM3
    python tests/wavepal_hardware_test.py /dev/ttyACM0 --quick

The device must be a Pulse Pal 3 running Wave Pal firmware. Nothing needs to be
connected to it, but the outputs play random waveforms of up to +/-10 V, so
disconnect anything that should not receive them.

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
import WavePal  # noqa: E402
from WavePal import WavePalDevice, WavePalError  # noqa: E402
from PulsePal import PulsePalDevice, PulsePalError  # noqa: E402

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


def load(W, channel, volts):
    """Load a waveform and return its DAC codes, as the device stores them."""
    W.load_waveform(channel, volts)
    return W._volts_to_codes(volts)


def test_connection(W):
    assert W.info.firmware_version == 1
    assert W.info.hardware_version == 3
    assert W.info.max_samples == 1000000
    status = W.status()
    assert status.playing == [], status


def test_waveforms_play_exactly_once_around_the_buffer_size(W):
    """Waveform lengths at and around the buffer boundaries, played once. Short
    ones play from RAM, longer ones switch to the microSD card part way."""
    W.sampling_rate = 100000
    buffer = W.info.buffer_samples
    for n in (1, 2, buffer - 1, buffer, buffer + 1, 2 * buffer, 2 * buffer + 1, 3 * buffer + 7):
        codes = load(W, 1, random_waveform(n))
        W.play(1)
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
            W.play(1)
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
        W.play(2)
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
    W.play(1)
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
    W.loop_mode = True
    n_played = int(seconds * W.actual_sampling_rate)
    W.loop_duration = n_played / W.actual_sampling_rate
    try:
        underruns_before = W.status().underruns
        W.status()  # Reset the longest interrupt
        W.play([1, 2, 3, 4])
        longest = wait_until_stopped(W, [1, 2, 3, 4], timeout=seconds + 5)
        for channel in (1, 2, 3, 4):
            check_played(W, channel, codes[channel], n_played)
        underruns = W.status().underruns
        assert underruns == underruns_before, f"underruns: {underruns} (were {underruns_before})"
        report(f"4 channels at 100 kHz for {seconds} s: longest interrupt {longest:.2f} us "
               "of 10 us, no underruns")
        assert longest < 10, "the interrupt takes longer than the sample period"
    finally:
        W.loop_mode = False
        W.loop_duration = 0


def test_loading_while_other_channels_play(W, report):
    """Channels 1-3 stream at 100 kHz while channel 4's waveform is loaded."""
    W.sampling_rate = 100000
    codes = {}
    for channel in (1, 2, 3):
        codes[channel] = load(W, channel, random_waveform(W.info.max_samples))
    W.loop_mode[1:4] = [True] * 3
    try:
        underruns_before = W.status().underruns
        W.play([1, 2, 3])
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
        W.loop_mode = False


def test_trigger_modes(W):
    W.sampling_rate = 10000
    load(W, 1, random_waveform(100000))  # 10 s

    W.trigger_mode[1] = "Normal"          # A second trigger is ignored
    W.play(1)
    time.sleep(0.5)
    W.play(1)
    time.sleep(0.2)
    played, _ = W._playback_checksums()
    assert played[1] > 6000, f"normal mode restarted the waveform ({played[1]} samples)"

    W.trigger_mode[1] = "Master"          # A second trigger restarts it
    W.play(1)
    time.sleep(0.2)
    played, _ = W._playback_checksums()
    assert played[1] < 4000, f"master mode did not restart the waveform ({played[1]} samples)"

    W.trigger_mode[1] = "Toggle"          # A second trigger stops it
    W.play(1)
    time.sleep(0.05)
    assert 1 not in W.status().playing, "toggle mode did not stop the waveform"
    W.play(1)
    time.sleep(0.05)
    assert 1 in W.status().playing, "toggle mode did not start the waveform"
    W.stop(1)
    assert 1 not in W.status().playing
    W.trigger_mode[1] = "Normal"


def test_restarts_part_way_through_a_streamed_waveform(W):
    """Master mode restarts at points where the playback buffers hold blocks from
    the middle of the waveform. After each restart the whole waveform must play
    again, exactly."""
    W.sampling_rate = 100000
    codes = load(W, 1, random_waveform(7 * W.info.buffer_samples + 100))  # 1.15 s
    W.trigger_mode[1] = "Master"
    try:
        for restart_after in (0.05, 0.2, 0.35, 0.6, 0.9):
            W.play(1)
            time.sleep(restart_after)
            W.play(1)
            wait_until_stopped(W, [1], timeout=3)
            check_played(W, 1, codes, codes.size)
    finally:
        W.trigger_mode[1] = "Normal"


def test_sampling_rate_change_during_playback(W):
    """The rate can change while a waveform plays: no sample is lost or repeated."""
    W.sampling_rate = 20000
    codes = load(W, 3, random_waveform(60000))  # 3 s at 20 kHz
    W.play(3)
    time.sleep(0.5)
    W.sampling_rate = 100000
    wait_until_stopped(W, [3], timeout=3)
    check_played(W, 3, codes, codes.size)


def test_channel_joining_a_running_clock_plays_exactly(W):
    """A channel started while another plays starts on the running sample clock."""
    W.sampling_rate = 50000
    codes1 = load(W, 1, random_waveform(200000))   # 4 s
    codes2 = load(W, 2, random_waveform(30000))    # 0.6 s
    W.play(1)
    time.sleep(0.3)
    W.play(2)
    wait_until_stopped(W, [1, 2], timeout=6)
    check_played(W, 1, codes1, codes1.size)
    check_played(W, 2, codes2, codes2.size)


def test_fixed_voltage_and_stop(W):
    W.set_fixed_voltage([1, 3], 2.5)
    assert W.status().playing == []
    W.stop()


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
            W.play(channel)
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
            W.play(1)
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
    arguments = parser.parse_args()

    notes = []
    tests = [
        test_connection,
        test_waveforms_play_exactly_once_around_the_buffer_size,
        test_loop_durations_wrap_exactly_around_the_buffer_size,
        test_loop_duration_is_exact_in_samples,
        test_trigger_modes,
        test_restarts_part_way_through_a_streamed_waveform,
        test_sampling_rate_change_during_playback,
        test_channel_joining_a_running_clock_plays_exactly,
        test_fixed_voltage_and_stop,
        test_output_range_change_reloads_waveforms,
        test_every_output_range_plays,
    ]
    if not arguments.quick:
        tests += [
            lambda W: test_full_length_waveform_transfer_and_playback(W, notes.append),
            lambda W: test_four_channels_at_100khz(W, notes.append),
            lambda W: test_loading_while_other_channels_play(W, notes.append),
        ]
    tests.append(test_pulse_pal_class_is_refused)  # Last: it closes the connection

    failures = 0
    with WavePalDevice(arguments.port) as W:
        for test in tests:
            name = getattr(test, "__name__", "test")
            if name == "<lambda>":
                name = test.__code__.co_names[0]
            start = time.perf_counter()
            try:
                test(W)
                result = "ok"
            except (AssertionError, WavePalError) as error:
                failures += 1
                result = f"FAILED: {error}"
                if not W._closed:
                    W.stop()
            print(f"{name:<60} {time.perf_counter() - start:6.1f} s  {result}")
            while notes:
                print("    " + notes.pop(0))
    print(f"\n{len(tests) - failures}/{len(tests)} tests passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
