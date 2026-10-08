"""Test a connected Synth Pal: synthesis, play durations, and timing budget.

Run it after changing the Synth Pal firmware or SynthPal.py:

    python tests/synthpal_hardware_test.py COM3
    python tests/synthpal_hardware_test.py /dev/ttyACM0 --quick

The device must be a Pulse Pal 3 running Synth Pal firmware. Nothing needs to
be connected to it, but the outputs play waveforms of up to +/-10 V, so
disconnect anything that should not receive them.

Every sample the device plays is checked: its firmware counts the samples each
channel plays and sums their DAC codes (op 90), and each test compares them with
a model of the firmware's synthesis, `ChannelModel`, which computes every code as
the firmware does, in single precision, at full amplitude (`expected_cycle()`) and
through the ramps (`playback_envelopes()`). Along the way, the script
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


def output_range(waveform, resting_uv, amplitude_uv, mean_uv=None):
    """As outputRangeFor() in /Firmware/SynthPal/Settings.ino: an index into RANGE_LIMITS_UV. The
    mean voltage is the resting voltage unless given."""
    mean_uv = resting_uv if mean_uv is None else mean_uv
    lowest2 = min(2 * resting_uv, 2 * mean_uv - amplitude_uv)
    highest2 = max(2 * resting_uv, 2 * mean_uv + amplitude_uv)
    if waveform == SynthPal.FIXED_VOLTAGE:  # The resting voltage and the fixed voltage
        lowest2, highest2 = 2 * min(resting_uv, amplitude_uv), 2 * max(resting_uv, amplitude_uv)
    for index, (low, high) in enumerate(RANGE_LIMITS_UV[:3]):
        if lowest2 >= 2 * low and highest2 <= 2 * high:
            return index
    return 3


def fused_multiply_add(a, b, c):
    """a * b + c with one rounding to single precision, as the Cortex-M7's vfma.f32."""
    return (np.asarray(a).astype(np.float64) * np.asarray(b).astype(np.float64)
            + np.asarray(c).astype(np.float64)).astype(np.float32)


def round_half_away(x):
    """As roundf()."""
    x = np.asarray(x).astype(np.float64)
    return np.sign(x) * np.floor(np.abs(x) + 0.5)


def unit_waveform(waveform, n):
    """As unitWaveform() in /Firmware/SynthPal/Playback.ino: one cycle of n samples of a periodic
    waveform, from -1 to 1, in single precision."""
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
        return np.where(q < 2, value, -value).astype(np.float32)
    if waveform == "Triangle":
        x = np.where(i <= quarter, i, np.where(i <= 3 * quarter, 2 * quarter - i, i - 4 * quarter))
        return x.astype(np.float32) / np.float32(quarter)
    if waveform == "Square":
        return np.where(i < 2 * quarter, np.float32(1), np.float32(-1))
    return (2 * i - (n - 1)).astype(np.float32) / np.float32(n - 1)  # Sawtooth


class ChannelModel:
    """A channel's DAC codes, worked out as updateChannelOutput() (Settings.ino) and
    synthesizeCode() (Playback.ino) do, at n samples per cycle. The mean voltage is the resting
    voltage unless given."""

    def __init__(self, waveform, amplitude_uv, resting_uv, n, mean_uv=None):
        mean_uv = resting_uv if mean_uv is None else mean_uv
        low, high = RANGE_LIMITS_UV[output_range(waveform, resting_uv, amplitude_uv, mean_uv)]
        codes_per_microvolt = 65536.0 / (high - low)

        def exact(uv):  # exactCode()
            return (uv - low) * codes_per_microvolt

        def nearest(code):  # nearestCode()
            return min(np.floor(code + 0.5), 65535)

        rest_exact, mean_exact = exact(resting_uv), exact(mean_uv)
        self.n = n
        self.rest_code = nearest(rest_exact)
        self.rest_fraction = np.float32(rest_exact - np.floor(rest_exact + 0.5))
        self.mean_code = nearest(mean_exact)
        self.mean_fraction = np.float32(mean_exact - np.floor(mean_exact + 0.5))
        self.fixed = waveform == SynthPal.FIXED_VOLTAGE
        if self.fixed:
            self.fixed_code = nearest(exact(amplitude_uv))
            self.ramp_offset = np.float32(exact(amplitude_uv) - rest_exact)
        else:
            self.half = np.float32(amplitude_uv * 0.5 * codes_per_microvolt)
            self.ramp_offset = np.float32(mean_exact - rest_exact)
            self.unit = unit_waveform(waveform, n)

    def codes(self, sample_index, envelope):
        """The codes of samples (counted from the start, so their phases follow) at envelopes."""
        envelope = np.asarray(envelope, dtype=np.float32)
        phase = np.asarray(sample_index) % self.n
        if self.fixed:
            full = np.full(phase.shape, self.fixed_code)
            target = np.full(phase.shape, self.ramp_offset, dtype=np.float32)
        else:
            w = self.unit[phase]
            full = self.mean_code + round_half_away(fused_multiply_add(self.half, w, self.mean_fraction))
            target = fused_multiply_add(self.half, w, self.ramp_offset)
        ramped = self.rest_code + round_half_away(fused_multiply_add(target, envelope, self.rest_fraction))
        codes = np.where(envelope <= 0, self.rest_code, np.where(envelope >= 1, full, ramped))
        return np.clip(codes, 0, 65535).astype(np.int64)


def ramp_envelopes(steps, positions):
    """As fetchNextSample(): the envelope at ramp positions, for a ramp of `steps` steps."""
    reciprocal = np.float32(1) / np.float32(steps) if steps else np.float32(0)
    positions = np.asarray(positions, dtype=np.int64)
    return np.where(positions >= steps, np.float32(1), positions.astype(np.float32) * reciprocal)


def playback_envelopes(on, hold, off, stopped=False):
    """The envelopes of a playback from rest to rest (fetchNextSample()): the on ramp, `hold`
    samples at full amplitude, and the off ramp. If `stopped`, the channel was stopped during its
    hold: the sample fetched then plays, and the off ramp goes on below it."""
    off_positions = np.arange(off - 1 if stopped else off, 0, -1)
    return np.concatenate([ramp_envelopes(on, np.arange(on)), np.ones(hold, dtype=np.float32),
                           ramp_envelopes(off, off_positions)])


def expected_cycle(waveform, amplitude_uv, resting_uv, n, mean_uv=None):
    """The DAC codes of one cycle of n samples at full amplitude, as the firmware plays them."""
    return ChannelModel(waveform, amplitude_uv, resting_uv, n, mean_uv).codes(np.arange(n), np.ones(n))


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


def check_sum(S, channel, codes):
    """Check the number of samples a channel played, and their sum, against their codes."""
    check_played(S, channel, codes.size)
    _, sums = S._playback_checksums()
    expected = int(codes.sum()) % 2**32
    assert sums[channel] == expected, (
        f"channel {channel}: sum of the samples played is {sums[channel]}, expected {expected}")


def configure(S, channel, waveform, amplitude, resting, mean=None):
    """Set a channel's waveform and levels, in an order the device accepts from any
    earlier settings: an amplitude of 0 goes with any waveform, resting and mean voltage.
    The mean voltage is the resting voltage unless given."""
    S.amplitude[channel] = 0
    S.waveform[channel] = waveform
    S.resting_voltage[channel] = resting
    S.mean_voltage[channel] = resting if mean is None else mean
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
    assert send(S._OP_SET_RESTING_VOLTAGE, amplitudes(10, -10, 0, 0)) == 1  # Any rest
    assert send(S._OP_SET_RESTING_VOLTAGE, amplitudes(10.000001, 0, 0, 0)) == 0
    assert send(S._OP_SET_MEAN_VOLTAGE, amplitudes(0, 0.000001, 0, 0)) == 0  # 20 Vpp at 1 uV
    assert send(S._OP_SET_MEAN_VOLTAGE, amplitudes(-10.000001, 0, 0, 0)) == 0  # Beyond 10 V
    assert send(S._OP_SET_MEAN_VOLTAGE, amplitudes(10, 0, 0, 0)) == 1  # A fixed voltage's mean
    assert send(S._OP_SET_AMPLITUDE, amplitudes(-10, 20, 5, 5)) == 1
    # The class's record of what was sent past it
    S._resting_uv[1:] = [10_000_000, -10_000_000, 0, 0]
    S._mean_uv[1:] = [10_000_000, 0, 0, 0]
    S._amplitude_uv[1:] = [-10_000_000, 20_000_000, 5_000_000, 5_000_000]
    assert S.status().output_ranges[1:3] == ["-10V:10V", "-10V:10V"]
    configure(S, 1, "Sine", 5, 0)
    configure(S, 2, "Sine", 5, 0)


def test_means_are_the_mean_voltage(S):
    """Over whole cycles, every waveform's codes are symmetric about the mean voltage's code,
    whatever the resting voltage."""
    S.frequency = 300
    n = S.samples_per_cycle
    for waveform in ("Sine", "Triangle", "Square", "Sawtooth"):
        for resting in (0, 4.5, -4.99):
            # -5 V to 5 V range, which holds the resting voltage: the mean voltage is code 32768
            configure(S, 3, waveform, 6, resting, mean=0)
            assert S.status().output_ranges[3] == "-5V:5V"
            S.play_duration[3] = 10 * n / S.sampling_rate
            S.play(3)
            wait_until_stopped(S, [3], timeout=2)
            played, sums = S._playback_checksums()
            assert played[3] == 10 * n
            assert sums[3] == (10 * n * 32768) % 2**32, f"{waveform}: mean is not the mean voltage"


def test_mean_voltage_moves_the_waveform(S):
    """The waveform swings around its mean voltage, in the range that holds it and the resting
    voltage, and the channel rests at its resting voltage."""
    S.frequency = 1000
    cases = [  # (waveform, amplitude, resting voltage, mean voltage, range)
        ("Sine", 4, 0, 2.5, "0V:5V"), ("Triangle", 2, -1, 7, "-10V:10V"), ("Square", 3, 9, 7, "0V:10V"),
        ("Sawtooth", 10, -5, -5, "-10V:10V"), ("Sine", 1, 4, -4, "-5V:5V"), ("Fixed Voltage", -3, 2, 9, "-5V:5V"),
    ]
    for waveform, amplitude, resting, mean, range_name in cases:
        configure(S, 2, waveform, amplitude, resting, mean)
        ranges = S.status().output_ranges
        assert ranges[2] == range_name, (waveform, amplitude, resting, mean, ranges)
        S.play_duration[2] = 0.0123
        S.play(2)
        wait_until_stopped(S, [2], timeout=1)
        cycle = expected_cycle(waveform, microvolts(amplitude), microvolts(resting), S.samples_per_cycle,
                               microvolts(mean))
        check_played(S, 2, expected_samples(0.0123, centihz(S)), cycle)
    configure(S, 2, "Sine", 5, 0)


def test_ramps_play_the_modelled_envelope(S):
    """The on ramp, the play duration and the off ramp, each to the sample, with every code of
    the ramps checked against the model: for each waveform, with a mean voltage away from the
    resting voltage, and with ramps of 0 and 1 sample."""
    cases = [  # (frequency, waveform, amplitude, resting voltage, mean voltage, on ramp, play, off ramp)
        (1000, "Sine", 6, -2, 3, 0.02, 0.01, 0.03),
        (300, "Triangle", 4, 1, -2.5, 0.0123, 0.005, 0.0071),
        (12345.67, "Square", 8, 0, 0, 0.003, 0.002, 0.004),
        (777, "Sawtooth", 2, 9.5, -1, 0.01, 0.0001, 0.01),
        (1000, "Fixed Voltage", -7.5, 4, 0, 0.015, 0.005, 0.02),
        (1000, "Fixed Voltage", 9.99, -9.99, 0, 0.001, 0.001, 0),
        (1000, "Sine", 5, 0, 0, 0.00001, 0.001, 0.00001),
        (1000, "Sine", 5, 0, 0, 0, 0.001, 0.01),
    ]
    for hz, waveform, amplitude, resting, mean, on, play, off in cases:
        S.frequency = hz
        configure(S, 4, waveform, amplitude, resting, mean)
        S.play_duration[4] = play
        S.on_ramp_duration[4] = on
        S.off_ramp_duration[4] = off
        S.play(4)
        wait_until_stopped(S, [4], timeout=on + play + off + 2)
        rate = centihz(S)
        envelopes = playback_envelopes(expected_samples(on, rate), expected_samples(play, rate),
                                       expected_samples(off, rate))
        model = ChannelModel(waveform, microvolts(amplitude), microvolts(resting), S.samples_per_cycle,
                             microvolts(mean))
        check_sum(S, 4, model.codes(np.arange(envelopes.size), envelopes))
    S.on_ramp_duration[4] = 0
    S.off_ramp_duration[4] = 0


def test_a_stop_starts_the_off_ramp(S):
    """A channel stopped while it plays at full amplitude fades out over its off ramp, from the
    sample it had fetched: every code is checked, with the time it held found from the count."""
    S.frequency = 1000
    for waveform, amplitude, resting, mean in (("Sine", 6, 1, -1), ("Fixed Voltage", 5, -5, 0)):
        configure(S, 1, waveform, amplitude, resting, mean)
        S.play_duration[1] = 0
        S.on_ramp_duration[1] = 0.01
        S.off_ramp_duration[1] = 0.02
        S.play(1)
        time.sleep(0.1)
        S.stop(1)
        assert 1 in S.status().playing, "the channel stopped before its off ramp"
        wait_until_stopped(S, [1], timeout=1)
        played, _ = S._playback_checksums()
        on, off = expected_samples(0.01, centihz(S)), expected_samples(0.02, centihz(S))
        held = played[1] - on - (off - 1)
        assert held > 0, f"{played[1]} samples"
        envelopes = playback_envelopes(on, held, off, stopped=True)
        model = ChannelModel(waveform, microvolts(amplitude), microvolts(resting), S.samples_per_cycle,
                             microvolts(mean))
        check_sum(S, 1, model.codes(np.arange(envelopes.size), envelopes))
    S.on_ramp_duration[1] = 0
    S.off_ramp_duration[1] = 0
    S.play_duration[1] = 1


def test_a_trigger_during_the_off_ramp_fades_back_in(S):
    """A channel triggered again during its off ramp rises from where it is, at the on ramp's
    rate, without restarting its cycle, and plays its play duration again. The step it was at
    is found from the count, and every code is checked."""
    S.frequency = 1000
    configure(S, 3, "Sine", 4, 0, 1)
    on, play, off = 0.1, 0.1, 0.5
    S.on_ramp_duration[3] = on
    S.play_duration[3] = play
    S.off_ramp_duration[3] = off
    S.play(3)
    time.sleep(0.4)  # 0.2 s into the off ramp
    assert 3 in S.status().playing
    S.play(3)
    wait_until_stopped(S, [3], timeout=3)
    played, sums = S._playback_checksums()
    rate = centihz(S)
    n_on, n_play, n_off = (expected_samples(t, rate) for t in (on, play, off))
    model = ChannelModel("Sine", 4_000_000, 0, S.samples_per_cycle, 1_000_000)
    whole = playback_envelopes(n_on, n_play, n_off)
    matches = []
    for last in range(1, n_off + 1):  # The off ramp step of the sample fetched before the trigger
        envelope = ramp_envelopes(n_off, [last])[0]
        # As continueRamp(): the on ramp goes on one step above it (at or past its end: at full amplitude)
        rise_from = min(int(np.float64(envelope) * n_on + 0.5) + 1, n_on)
        before = n_on + n_play + n_off - last + 1
        if before + whole.size - rise_from == played[3]:
            envelopes = np.concatenate([whole[:before], whole[rise_from:]])
            matches.append(int(model.codes(np.arange(envelopes.size), envelopes).sum()) % 2**32)
    assert matches, f"no off ramp step gives {played[3]} samples"
    assert sums[3] in matches, f"sum {sums[3]}, expected one of {matches}"
    S.on_ramp_duration[3] = 0
    S.off_ramp_duration[3] = 0
    S.play_duration[3] = 1


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
    """All four channels play a sine wave at 100 kHz for `seconds`, the first and last fifth of
    it in their ramps: the interrupt must leave time for the rest of the firmware, and update the
    outputs on time."""
    S.frequency = 1000
    for channel in (1, 2, 3, 4):
        # Resting voltages -1.5 to 1.5 V, and means -0.75 to 0.75 V
        configure(S, channel, "Sine", 4 * channel, channel - 2.5, (channel - 2.5) / 2)
    S.on_ramp_duration = seconds / 5
    S.play_duration = seconds * 3 / 5
    S.off_ramp_duration = seconds / 5
    S.status()  # Reset the longest interrupt and the late updates
    S.play([1, 2, 3, 4])
    longest = wait_until_stopped(S, [1, 2, 3, 4], timeout=seconds + 3)
    late = S.status().late_updates
    envelopes = playback_envelopes(seconds * 20000, seconds * 60000, seconds * 20000)
    for channel in (1, 2, 3, 4):
        model = ChannelModel("Sine", 4_000_000 * channel, 1_000_000 * channel - 2_500_000, S.samples_per_cycle,
                             500_000 * channel - 1_250_000)
        check_sum(S, channel, model.codes(np.arange(envelopes.size), envelopes))
    S.on_ramp_duration = 0
    S.off_ramp_duration = 0
    report(f"4 channels at 100 kHz for {seconds} s, with ramps: longest interrupt {longest:.2f} us of "
           f"10 us, {late} late output updates")
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
        test_means_are_the_mean_voltage,
        test_mean_voltage_moves_the_waveform,
        test_ramps_play_the_modelled_envelope,
        test_a_stop_starts_the_off_ramp,
        test_a_trigger_during_the_off_ramp_fades_back_in,
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
