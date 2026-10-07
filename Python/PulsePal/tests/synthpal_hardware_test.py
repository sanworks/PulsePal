"""Test a connected Synth Pal: synthesis, play durations, and timing budget.

Run it after changing the Synth Pal firmware or SynthPal.py:

    python tests/synthpal_hardware_test.py COM3
    python tests/synthpal_hardware_test.py /dev/ttyACM0 --quick

The device must be a Pulse Pal 3 running Synth Pal firmware. Nothing needs to
be connected to it, but the outputs play waveforms of up to +/-10 V, so
disconnect anything that should not receive them.

Every sample the device plays is checked: its firmware counts the samples each
channel plays and sums their DAC codes (op 90), and each test compares them with
a model of the firmware's synthesis, `expected_cycle()`, which computes every
code as the firmware does, in single precision. Along the way, the script
reports the longest run of the sample clock interrupt against the sample period.

Things only a scope, a TTL source or the joystick can check are listed in
/Firmware/SynthPal/AGENTS.md.
"""
import argparse
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import SynthPal  # noqa: E402
from SynthPal import SynthPalDevice, SynthPalError  # noqa: E402
from PulsePal import PulsePalDevice, PulsePalError  # noqa: E402
from WavePal import WavePalDevice, WavePalError  # noqa: E402

SINE_TABLE_SIZE = 4096
# As fillSineTable() in /Firmware/SynthPal/Playback.ino: computed in double, stored in single
SINE_TABLE = np.sin(np.pi / 2 * np.arange(SINE_TABLE_SIZE + 1) / SINE_TABLE_SIZE).astype(
    np.float32)
RANGE_LIMITS_UV = [(0, 5_000_000), (0, 10_000_000), (-5_000_000, 5_000_000),
                   (-10_000_000, 10_000_000)]


def samples_per_cycle(centihz):
    """As samplesPerCycleFor() in /Firmware/SynthPal/Settings.ino."""
    return 4 * (2_500_000 // centihz)


def output_range(waveform, resting_uv, amplitude_uv):
    """As outputRangeFor() in /Firmware/SynthPal/Settings.ino: an index into RANGE_LIMITS_UV."""
    lowest2, highest2 = 2 * resting_uv - amplitude_uv, 2 * resting_uv + amplitude_uv
    if waveform == SynthPal.FIXED_VOLTAGE:  # The resting voltage and the fixed voltage
        lowest2, highest2 = 2 * min(resting_uv, amplitude_uv), 2 * max(resting_uv, amplitude_uv)
    for index, (low, high) in enumerate(RANGE_LIMITS_UV[:3]):
        if lowest2 >= 2 * low and highest2 <= 2 * high:
            return index
    return 3


def fused_multiply_add(a, b, c):
    """a * b + c with one rounding to single precision, as the Cortex-M7's vfma.f32."""
    return (a.astype(np.float64) * b.astype(np.float64) + c.astype(np.float64)).astype(np.float32)


def expected_cycle(waveform, amplitude_uv, resting_uv, n):
    """The DAC codes of one cycle of n samples, computed as the firmware does (Playback.ino)."""
    low, high = RANGE_LIMITS_UV[output_range(waveform, resting_uv, amplitude_uv)]
    codes_per_microvolt = 65536.0 / (high - low)
    if waveform == SynthPal.FIXED_VOLTAGE:  # The same code on every sample: the amplitude's
        fixed_code = np.floor((amplitude_uv - low) * codes_per_microvolt + 0.5)
        return np.full(n, min(fixed_code, 65535), dtype=np.int64)
    # As updateChannelOutput() in /Firmware/SynthPal/Settings.ino
    resting_code = (resting_uv - low) * codes_per_microvolt
    nearest_code = np.floor(resting_code + 0.5)
    rest_code = min(nearest_code, 65535)
    rest_fraction = np.float32(resting_code - nearest_code)
    half = np.float32(amplitude_uv * 0.5 * codes_per_microvolt)
    i = np.arange(n, dtype=np.int64)
    quarter = n // 4
    if waveform == "Sine":
        q = i // quarter
        r = i - q * quarter
        r = np.where(q % 2 == 1, quarter - r, r)  # quarterSine(r)
        scaled = r * SINE_TABLE_SIZE
        index = scaled // quarter
        step = (scaled - index * quarter).astype(np.float32) / np.float32(quarter)
        lower = SINE_TABLE[np.minimum(index, SINE_TABLE_SIZE)]
        upper = SINE_TABLE[np.minimum(index + 1, SINE_TABLE_SIZE)]
        value = np.where(index >= SINE_TABLE_SIZE, np.float32(1),
                         fused_multiply_add(step, upper - lower, lower))
        value = np.where(q < 2, value, -value)
    elif waveform == "Triangle":
        x = np.where(i <= quarter, i, np.where(i <= 3 * quarter, 2 * quarter - i, i - 4 * quarter))
        value = x.astype(np.float32) / np.float32(quarter)
    elif waveform == "Square":
        value = np.where(i < 2 * quarter, np.float32(1), np.float32(-1))
    else:  # Sawtooth
        value = (2 * i - (n - 1)).astype(np.float32) / np.float32(n - 1)
    # Rounded twice, to single precision: the compiler does not fuse these
    offset = (rest_fraction + half * value.astype(np.float32)).astype(np.float64)
    rounded = np.sign(offset) * np.floor(np.abs(offset) + 0.5)  # Half away from zero, as roundf()
    return np.clip(rest_code + rounded, 0, 65535).astype(np.int64)


def expected_sum(cycle, n_played):
    """Sum of the codes of n_played samples of a repeating cycle, mod 2**32."""
    loops, remainder = divmod(n_played, cycle.size)
    return (loops * int(cycle.sum()) + int(cycle[:remainder].sum())) % 2**32


def expected_samples(duration_s, centihz):
    """As durationToSamples() in /Firmware/SynthPal/Settings.ino."""
    microseconds = round(duration_s * 1e6)
    if microseconds == 0:
        return 0
    rate_centihz = centihz * samples_per_cycle(centihz)
    return max(1, (microseconds * rate_centihz + 50_000_000) // 100_000_000)


def microvolts(volts):
    return round(volts * 1e6)


def wait_until_stopped(S, channels, timeout):
    """Wait for channels to stop playing. Returns the longest interrupt seen, in us."""
    deadline = time.monotonic() + timeout
    longest = 0.0
    while True:
        status = S.status()
        longest = max(longest, status.longest_interrupt_us)
        if not set(channels) & set(status.playing):
            return longest
        if time.monotonic() > deadline:
            raise AssertionError(f"channels {channels} still playing after {timeout} s")
        time.sleep(0.02)


def check_played(S, channel, n_expected, cycle=None):
    """Check the number of samples a channel played, and their sum if cycle is given."""
    played, sums = S._playback_checksums()
    assert played[channel] == n_expected, (
        f"channel {channel} played {played[channel]} samples, expected {n_expected}")
    if cycle is not None:
        expected = expected_sum(cycle, n_expected)
        assert sums[channel] == expected, (
            f"channel {channel}: sum of the samples played is {sums[channel]}, expected {expected}")


def configure(S, channel, waveform, amplitude, resting):
    """Set a channel's waveform and levels, in an order the device accepts from any
    earlier settings: an amplitude of 0 goes with any waveform and resting voltage."""
    S.amplitude[channel] = 0
    S.waveform[channel] = waveform
    S.resting_voltage[channel] = resting
    S.amplitude[channel] = amplitude


def centihz(S):
    return round(S.frequency * 100)


def test_connection(S):
    assert S.info.firmware_version == 1
    assert S.info.hardware_version == 3
    assert S.info.n_channels == 4
    assert (S.info.min_frequency, S.info.max_frequency) == (1, 20000)
    assert S.info.max_sampling_rate == 100000
    assert S.status().playing == []


def test_samples_per_cycle_is_the_largest_multiple_of_4_at_or_below_100khz(S):
    for hz in (1, 1.55, 100, 300, 333.33, 1000, 12345.67, 12600, 16700, 20000):
        S.frequency = hz
        n = S.samples_per_cycle
        assert n == samples_per_cycle(round(hz * 100)), (hz, n)
        assert n % 4 == 0 and n * hz <= 100000 + 1e-6 and (n + 4) * hz > 100000, (hz, n)
        assert S.status().samples_per_cycle == n


def test_every_waveform_plays_the_modelled_samples(S):
    """Each waveform, at a high and a low number of samples per cycle, played for a few
    cycles and part of one: every code is checked against the model, through their sum."""
    for hz in (20000, 3000, 7.77):
        S.frequency = hz
        n = S.samples_per_cycle
        for waveform in SynthPal.WAVEFORMS:
            configure(S, 1, waveform, 7.3, -1.2)
            duration = (3.37 * n) / S.sampling_rate
            S.play_duration[1] = duration
            S.play(1)
            wait_until_stopped(S, [1], timeout=duration + 2)
            cycle = expected_cycle(waveform, microvolts(7.3), microvolts(-1.2), n)
            check_played(S, 1, expected_samples(duration, centihz(S)), cycle)


def test_output_ranges_follow_the_levels(S):
    """Each channel takes the range with the finest steps that holds its waveform, and plays
    codes in that range."""
    S.frequency = 1000
    cases = [  # (amplitude, resting voltage, range)
        (2, 2.5, "0V:5V"), (5, 2.5, "0V:5V"), (5.0001, 2.5, "-10V:10V"), (2, 7, "0V:10V"),
        (10, 5, "0V:10V"), (1, 0, "-5V:5V"), (10, 0, "-5V:5V"), (2, -4, "-5V:5V"),
        (12, 0, "-10V:10V"), (20, 0, "-10V:10V"), (0, 0, "0V:5V"), (0, -10, "-10V:10V"),
        (0.5, 9.75, "0V:10V"), (0.5, -9.75, "-10V:10V"),
    ]
    for amplitude, resting, range_name in cases:
        configure(S, 2, "Sine", amplitude, resting)
        ranges = S.status().output_ranges
        assert ranges[2] == range_name, (amplitude, resting, ranges)
        S.play_duration[2] = 0.01
        S.play(2)
        wait_until_stopped(S, [2], timeout=1)
        cycle = expected_cycle("Sine", microvolts(amplitude), microvolts(resting),
                               S.samples_per_cycle)
        check_played(S, 2, expected_samples(0.01, centihz(S)), cycle)


def test_fixed_voltage_steps_to_its_amplitude(S):
    """A fixed voltage plays one code, its amplitude's, in the range that holds it and the
    resting voltage, for its play duration."""
    S.frequency = 777
    cases = [  # (fixed voltage, resting voltage, range)
        (5, 0, "0V:5V"), (-5, 0, "-5V:5V"), (2.5, 4.99, "0V:5V"), (10, 0, "0V:10V"),
        (0, 7, "0V:10V"), (-10, 0, "-10V:10V"), (10, -10, "-10V:10V"), (-0.001, 9.75, "-10V:10V"),
        (-4.2, 4.2, "-5V:5V"), (0, 0, "0V:5V"), (3.3333333, 1.2345678, "0V:5V"),
    ]
    for fixed, resting, range_name in cases:
        configure(S, 3, "Fixed Voltage", fixed, resting)
        ranges = S.status().output_ranges
        assert ranges[3] == range_name, (fixed, resting, ranges)
        duration = 0.0123
        S.play_duration[3] = duration
        S.play(3)
        wait_until_stopped(S, [3], timeout=1)
        cycle = expected_cycle("Fixed Voltage", microvolts(fixed), microvolts(resting),
                               S.samples_per_cycle)
        check_played(S, 3, expected_samples(duration, centihz(S)), cycle)


def test_levels_must_suit_the_waveform(S):
    """The firmware's own checks, with commands sent past the class's: only a fixed voltage
    takes a negative amplitude, and a waveform change must suit the amplitude it finds."""
    def send(op, data):
        S._write_command(op, data)
        return S._read_raw(1)[0]

    def amplitudes(*volts):
        return b"".join(microvolts(v).to_bytes(4, "little", signed=True) for v in volts)

    configure(S, 1, "Fixed Voltage", -5, 0)
    configure(S, 2, "Sine", 20, 0)
    assert send(S._OP_SET_AMPLITUDE, amplitudes(-5, -1, 5, 5)) == 0  # -1 V on a sine wave
    assert send(S._OP_SET_AMPLITUDE, amplitudes(10.000001, 20, 5, 5)) == 0  # Beyond 10 V fixed
    assert send(S._OP_SET_WAVEFORM, bytes([0, 0, 0, 0])) == 0  # A sine wave of -5 V
    assert send(S._OP_SET_WAVEFORM, bytes([4, 4, 0, 0])) == 0  # A fixed voltage of 20 V
    assert send(S._OP_SET_WAVEFORM, bytes([4, 0, 0, 5])) == 0  # No waveform 5
    assert send(S._OP_SET_RESTING_VOLTAGE, amplitudes(10, 0, 0, 0)) == 1  # Any rest, fixed
    assert send(S._OP_SET_RESTING_VOLTAGE, amplitudes(10.000001, 0, 0, 0)) == 0
    assert send(S._OP_SET_AMPLITUDE, amplitudes(-10, 20, 5, 5)) == 1
    S._resting_uv[1:] = [10_000_000, 0, 0, 0]  # The class's record of what was sent past it
    S._amplitude_uv[1:] = [-10_000_000, 20_000_000, 5_000_000, 5_000_000]
    assert S.status().output_ranges[1:3] == ["-10V:10V", "-10V:10V"]
    configure(S, 1, "Sine", 5, 0)
    configure(S, 2, "Sine", 5, 0)


def test_means_are_the_resting_voltage(S):
    """Over whole cycles, every waveform's codes are symmetric about the resting voltage's code."""
    S.frequency = 300
    n = S.samples_per_cycle
    for waveform in ("Sine", "Triangle", "Square", "Sawtooth"):
        configure(S, 3, waveform, 6, 0)  # -5 V to 5 V range: the resting voltage is code 32768
        S.play_duration[3] = 10 * n / S.sampling_rate
        S.play(3)
        wait_until_stopped(S, [3], timeout=2)
        played, sums = S._playback_checksums()
        assert played[3] == 10 * n
        assert sums[3] == (10 * n * 32768) % 2**32, f"{waveform}: mean is not the resting voltage"


def test_play_durations_are_exact_in_samples(S):
    for hz, duration in ((100, 1), (300, 0.12345), (12345.67, 0.25), (1, 0.00001), (20000, 0.5)):
        S.frequency = hz
        configure(S, 4, "Triangle", 4, 0)
        S.play_duration[4] = duration
        S.play(4)
        wait_until_stopped(S, [4], timeout=duration + 2)
        check_played(S, 4, expected_samples(duration, centihz(S)))


def test_infinite_duration_plays_until_stopped(S):
    S.frequency = 1000
    S.play_duration[1] = 0
    S.play(1)
    time.sleep(0.3)
    assert 1 in S.status().playing
    S.stop(1)
    time.sleep(0.01)
    assert 1 not in S.status().playing
    S.play_duration[1] = 1


def test_soft_trigger_is_ignored_while_playing(S):
    S.frequency = 1000
    S.play_duration[1] = 1
    S.play(1)
    time.sleep(0.5)
    S.play(1)  # Ignored: the channel plays on, without restarting
    time.sleep(0.1)
    played, _ = S._playback_checksums()
    assert played[1] > 55000, f"the second soft trigger restarted the channel ({played[1]} samples)"
    wait_until_stopped(S, [1], timeout=2)
    check_played(S, 1, 100000)


def test_a_channel_joining_a_running_clock_plays_exactly(S):
    S.frequency = 440
    configure(S, 1, "Sine", 4, 0)
    configure(S, 2, "Sawtooth", 4, 0)
    S.play_duration[1:3] = [2, 0.3]
    S.play(1)
    time.sleep(0.4)
    S.play(2)
    wait_until_stopped(S, [1, 2], timeout=4)
    n = S.samples_per_cycle
    check_played(S, 1, expected_samples(2, centihz(S)), expected_cycle("Sine", 4_000_000, 0, n))
    check_played(S, 2, expected_samples(0.3, centihz(S)),
                 expected_cycle("Sawtooth", 4_000_000, 0, n))


def test_frequency_change_keeps_the_time_left_to_play(S):
    """A channel playing 1 s at 100 Hz changes to 1 kHz after 0.3 s: it stops 1 s after it
    started, and its count of samples played is rescaled to the new rate."""
    S.frequency = 100
    S.play_duration[3] = 1
    start = time.monotonic()
    S.play(3)
    time.sleep(0.3)
    S.frequency = 1000
    wait_until_stopped(S, [3], timeout=2)
    elapsed = time.monotonic() - start
    assert 0.95 < elapsed < 1.1, f"played for {elapsed:.3f} s"
    check_played(S, 3, expected_samples(1, centihz(S)))


def test_settings_change_during_playback(S):
    """Waveform, amplitude, resting voltage and range changes while a channel plays.
    The channel keeps playing, and stops at the end of its play duration."""
    S.frequency = 500
    configure(S, 1, "Sine", 2, 0)
    S.play_duration[1] = 1
    S.play(1)
    for waveform, amplitude, resting in (("Triangle", 2, 2.5), ("Square", 8, 5),
                                         ("Fixed Voltage", -7, 1), ("Sawtooth", 16, 0),
                                         ("Sine", 1, -3)):
        time.sleep(0.1)
        configure(S, 1, waveform, amplitude, resting)
        assert 1 in S.status().playing
    wait_until_stopped(S, [1], timeout=2)
    check_played(S, 1, expected_samples(1, centihz(S)))


def test_four_channels_at_100khz(S, report, seconds=10):
    """All four channels play a sine wave at 100 kHz for `seconds`: the interrupt must
    leave time for the rest of the firmware, and update the outputs on time."""
    S.frequency = 1000
    for channel in (1, 2, 3, 4):
        configure(S, channel, "Sine", 4 * channel, 0)
    S.play_duration = seconds
    S.status()  # Reset the longest interrupt and the late updates
    S.play([1, 2, 3, 4])
    longest = wait_until_stopped(S, [1, 2, 3, 4], timeout=seconds + 3)
    late = S.status().late_updates
    for channel in (1, 2, 3, 4):
        check_played(S, channel, seconds * 100000,
                     expected_cycle("Sine", 4_000_000 * channel, 0, S.samples_per_cycle))
    report(f"4 channels at 100 kHz for {seconds} s: longest interrupt {longest:.2f} us of 10 us, "
           f"{late} late output updates")
    assert longest < 10, "the interrupt takes longer than the sample period"
    assert late == 0, f"{late} late output updates"


def test_other_classes_are_refused(S):
    port = S.port.port
    S.close()
    for device_class, error_class in ((PulsePalDevice, PulsePalError),
                                      (WavePalDevice, WavePalError)):
        try:
            device_class(port)
            raise AssertionError(f"{device_class.__name__} connected to a Synth Pal")
        except error_class as error:
            assert "runs Synth Pal firmware" in str(error), error


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", help="Serial port of the Synth Pal, e.g. COM3 or /dev/ttyACM0")
    parser.add_argument("--quick", action="store_true",
                        help="Skip the test that plays four channels for 10 s")
    arguments = parser.parse_args()

    notes = []
    tests = [
        test_connection,
        test_samples_per_cycle_is_the_largest_multiple_of_4_at_or_below_100khz,
        test_every_waveform_plays_the_modelled_samples,
        test_output_ranges_follow_the_levels,
        test_fixed_voltage_steps_to_its_amplitude,
        test_levels_must_suit_the_waveform,
        test_means_are_the_resting_voltage,
        test_play_durations_are_exact_in_samples,
        test_infinite_duration_plays_until_stopped,
        test_soft_trigger_is_ignored_while_playing,
        test_a_channel_joining_a_running_clock_plays_exactly,
        test_frequency_change_keeps_the_time_left_to_play,
        test_settings_change_during_playback,
    ]
    if not arguments.quick:
        tests.append(lambda S: test_four_channels_at_100khz(S, notes.append))
    tests.append(test_other_classes_are_refused)  # Last: it closes the connection

    failures = 0
    with SynthPalDevice(arguments.port) as S:
        for test in tests:
            name = getattr(test, "__name__", "test")
            if name == "<lambda>":
                name = test.__code__.co_names[0]
            start = time.perf_counter()
            try:
                test(S)
                result = "ok"
            except (AssertionError, SynthPalError) as error:
                failures += 1
                result = f"FAILED: {error}"
                if not S._closed:
                    S.stop()
            print(f"{name:<75} {time.perf_counter() - start:6.1f} s  {result}")
            while notes:
                print("    " + notes.pop(0))
        if not S._closed:
            S.set_defaults()
    print(f"\n{len(tests) - failures}/{len(tests)} tests passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
