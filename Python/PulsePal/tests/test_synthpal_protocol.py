"""Offline tests for the bytes SynthPalDevice sends to a Synth Pal.

These tests need no device: a simulated Synth Pal behind a fake serial port
checks each command's layout, records it, and replies as the firmware would.
They are a fast check that the class matches the Synth Pal serial protocol,
which is documented in /Firmware/SynthPal/PROTOCOL.md.

Run them with:

    python tests/test_synthpal_protocol.py

They also run under pytest, if it is installed:

    python -m pytest tests
"""
import copy
import gc
import struct
import sys
import warnings
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from pulsepal import synth_pal as SynthPal  # noqa: E402
from pulsepal import PulsePalError  # noqa: E402

OP_MENU_BYTE = 213


def samples_per_cycle(centihz):
    """As samplesPerCycleFor() in /Firmware/SynthPal/Settings.ino."""
    return 4 * (2_500_000 // centihz)


class FakeSynthPal:
    """A fake serial port with a simulated Synth Pal behind it.

    Each write must be one whole command. It is checked against the protocol,
    recorded in `writes`, and the reply the firmware would send is queued for
    reading. Set `ack` to 0 to have the device reject confirmable commands.
    Bytes in `late_reply` are sent before the reply to the next command, as
    the reply to a command an earlier session left queued on the device.
    """

    def __init__(self, handshake_reply=83, firmware_version=1, ack=1):
        self.port = "FAKE"
        self.is_open = True
        self.writes = []
        self.replies = bytearray()
        self.late_reply = b""
        self.handshake_reply = handshake_reply
        self.firmware_version = firmware_version
        self.ack = ack
        self.centihz = 10000
        self.waveform = [0] * 4
        self.amplitude = [5_000_000] * 4
        self.resting = [0] * 4
        self.mean = [0] * 4
        self.on_ramp = [0] * 4
        self.off_ramp = [0] * 4
        self.trigger_mode = [0, 0]
        self.play_duration = [1_000_000] * 4
        self.links = [1, 1, 1, 1, 0, 0, 0, 0]
        self.stored = None  # A set op 85 stored for a param sync edge
        self.status_reply = bytes(17)

    def write(self, data):
        data = bytes(data)
        self.writes.append(data)
        self.replies += self.late_reply
        self.late_reply = b""
        self.replies += self._execute(data)
        return len(data)

    def _execute(self, data):
        assert data[0] == OP_MENU_BYTE, f"command without the framing byte: {list(data)}"
        op = data[1]
        payload = data[2:]
        expected_lengths = {72: 0, 81: 0, 89: 6, 99: 3, ord("N"): 0, ord("F"): 4, ord("W"): 4,
                            ord("A"): 16, ord("V"): 16, ord("M"): 16, ord("D"): 16, ord("B"): 16,
                            ord("E"): 16, ord("I"): 8, ord("T"): 2, ord("U"): 114, ord("R"): 0,
                            ord("P"): 1, ord("X"): 1, ord("G"): 0, ord("Z"): 0}
        assert op in expected_lengths, f"unknown op {op}"
        assert len(payload) == expected_lengths[op], f"op {chr(op)!r} data length"
        if op == 72:
            return bytes([self.handshake_reply]) + struct.pack("<I", self.firmware_version)
        if op == ord("N"):
            return struct.pack("<BBIIIII", 3, 4, 100, 2_000_000, 100_000, 24_000_000, 3_600_000_000)
        if op in (ord("P"), ord("X"), 81, 89):
            return b""
        if op == ord("G"):
            return self.status_reply
        if op == ord("R"):
            # As op 82 in /Firmware/SynthPal/USBOps.ino: every setting, in op 85's layout
            return struct.pack("<I4B4i4i4i4I4I4I8B2B", self.centihz, *self.waveform, *self.amplitude, *self.mean,
                               *self.resting, *self.play_duration, *self.on_ramp, *self.off_ramp, *self.links,
                               *self.trigger_mode)
        if op == ord("D") and self.ack:
            self.play_duration = list(struct.unpack("<4I", payload))
        if op == ord("I") and self.ack:
            self.links = list(payload)
        if op == ord("F"):
            centihz = struct.unpack("<I", payload)[0]
            ok = self.ack and 100 <= centihz <= 2_000_000
            if ok:
                self.centihz = centihz
            return struct.pack("<BI", ok, samples_per_cycle(self.centihz))
        if op in (ord("W"), ord("A"), ord("V"), ord("M")):
            levels = {"waveform": self.waveform, "amplitude": self.amplitude,
                      "resting": self.resting, "mean": self.mean}
            if op == ord("W"):
                levels["waveform"] = list(payload)
            else:
                name = {ord("A"): "amplitude", ord("V"): "resting", ord("M"): "mean"}[op]
                levels[name] = list(struct.unpack("<4i", payload))
            ok = self.ack and all(map(self.is_valid_output_level, levels["waveform"], levels["resting"],
                                      levels["mean"], levels["amplitude"]))
            if ok:
                for name, values in levels.items():
                    setattr(self, name, values)
            return bytes([ok])
        if op in (ord("B"), ord("E")):
            durations = list(struct.unpack("<4I", payload))
            ok = self.ack and all(d <= 3_600_000_000 for d in durations)
            if ok:
                setattr(self, "on_ramp" if op == ord("B") else "off_ramp", durations)
            return bytes([ok])
        if op == ord("T"):
            ok = self.ack and all(mode <= 3 for mode in payload)
            if ok:
                self.trigger_mode = list(payload)
                if 3 not in self.trigger_mode:
                    self.stored = None  # Leaving param sync mode discards a stored set
            return bytes([ok])
        if op == ord("U"):
            # As op 85 in /Firmware/SynthPal/USBOps.ino: checked whole, then stored in param sync mode, or applied
            values = struct.unpack("<I4B4i4i4i4I4I4I8B2B", payload)
            settings = {"centihz": values[0], "waveform": list(values[1:5]), "amplitude": list(values[5:9]),
                        "mean": list(values[9:13]), "resting": list(values[13:17]),
                        "play_duration": list(values[17:21]), "on_ramp": list(values[21:25]),
                        "off_ramp": list(values[25:29]), "links": list(values[29:37]),
                        "trigger_mode": list(values[37:39])}
            ok = (self.ack and 100 <= settings["centihz"] <= 2_000_000
                  and all(map(self.is_valid_output_level, settings["waveform"], settings["resting"],
                              settings["mean"], settings["amplitude"]))
                  and all(d <= 3_600_000_000 for name in ("play_duration", "on_ramp", "off_ramp")
                          for d in settings[name])
                  and all(link <= 1 for link in settings["links"])
                  and all(mode <= 3 for mode in settings["trigger_mode"]))
            if ok and 3 in self.trigger_mode:
                self.stored = settings
            elif ok:
                for name, value in settings.items():
                    setattr(self, name, value)
            return bytes([ok])
        return bytes([self.ack])

    @staticmethod
    def is_valid_output_level(waveform, resting, mean, amplitude):
        """As isValidOutputLevel() in /Firmware/SynthPal/Settings.ino."""
        if waveform > 4 or abs(resting) > 10_000_000 or abs(mean) > 10_000_000:
            return False
        if waveform == 4:  # Fixed Voltage: the amplitude is the voltage
            return abs(amplitude) <= 10_000_000
        return 0 <= amplitude <= 20_000_000 and 2 * abs(mean) + amplitude <= 20_000_000

    def read(self, n):
        reply = bytes(self.replies[:n])
        del self.replies[:n]
        return reply

    @property
    def in_waiting(self):
        return len(self.replies)

    def reset_input_buffer(self):
        self.replies.clear()

    def close(self):
        self.is_open = False

    def ops(self):
        """The op codes written so far, as characters where printable."""
        return [chr(w[1]) if 33 <= w[1] < 127 else w[1] for w in self.writes]


def connect(fake=None):
    """Connect a SynthPalDevice to a simulated Synth Pal, and clear the record
    of the connection sequence."""
    fake = fake or FakeSynthPal()
    original = SynthPal.serial.Serial
    SynthPal.serial.Serial = lambda *args, **kwargs: fake
    try:
        device = SynthPal.SynthPalDevice("FAKE")
    finally:
        SynthPal.serial.Serial = original
    fake.writes.clear()
    return device, fake


def expect_error(function, *args, error=None, **kwargs):
    """Run function and check that it raises PulsePalError (or `error`)."""
    error = error or PulsePalError
    try:
        function(*args, **kwargs)
    except error as exc:
        return exc
    raise AssertionError(f"{function} did not raise {error.__name__}")


def command(op, data=b""):
    return bytes([OP_MENU_BYTE, ord(op) if isinstance(op, str) else op]) + data


def test_connection_sequence_programs_the_defaults():
    fake = FakeSynthPal()
    original = SynthPal.serial.Serial
    SynthPal.serial.Serial = lambda *args, **kwargs: fake
    try:
        device = SynthPal.SynthPalDevice("FAKE")
    finally:
        SynthPal.serial.Serial = original
    assert fake.writes == [
        command(72),
        command("N"),
        command(89, b"PYTHON"),  # Shown on the device's screen as "PYTHON Connected"
        command("X", bytes([0x0F])),
        command("F", struct.pack("<I", 10000)),
        # In this order, each is valid whatever the device holds (see the next test)
        command("M", struct.pack("<4i", 0, 0, 0, 0)),
        command("A", struct.pack("<4i", *[5_000_000] * 4)),
        command("W", bytes(4)),
        command("V", struct.pack("<4i", 0, 0, 0, 0)),
        command("D", struct.pack("<4I", *[1_000_000] * 4)),
        command("B", struct.pack("<4I", 0, 0, 0, 0)),
        command("E", struct.pack("<4I", 0, 0, 0, 0)),
        command("T", bytes(2)),
        command("I", bytes([1, 1, 1, 1, 0, 0, 0, 0])),
    ], fake.ops()
    assert device.info.firmware_version == 1
    assert device.info.min_frequency == 1
    assert device.info.max_frequency == 20000
    assert device.info.max_sampling_rate == 100000
    assert device.info.max_play_duration == 3600
    assert device.frequency == 100
    assert device.samples_per_cycle == 1000
    assert device.sampling_rate == 100000
    assert device.waveform == [None] + ["Sine"] * 4
    assert device.peak_to_peak == [None, 5, 5, 5, 5]
    assert device.fixed_voltage == [None, 5, 5, 5, 5]
    assert device.mean_voltage == [None, 0, 0, 0, 0]
    assert device.resting_voltage == [None, 0, 0, 0, 0]
    assert device.play_duration == [None, 1, 1, 1, 1]
    assert device.on_ramp_duration == [None, 0, 0, 0, 0]
    assert device.off_ramp_duration == [None, 0, 0, 0, 0]
    assert device.trigger_mode == [None, "Normal", "Normal"]
    assert device.link_trigger_channel1 == [None, True, True, True, True]
    assert device.link_trigger_channel2 == [None, False, False, False, False]


def test_connecting_resets_any_levels_the_device_holds():
    """The device keeps its settings between sessions, e.g. a fixed voltage of -5 V,
    a sine wave of 20 V peak to peak, and a triangle wave around a mean voltage of 9 V.
    A sine wave could not take the first, a fixed voltage the second, nor an amplitude
    of 5 V the third: the defaults are sent in an order that works."""
    fake = FakeSynthPal()
    fake.waveform = [4, 4, 0, 1]
    fake.amplitude = [-5_000_000, 10_000_000, 20_000_000, 2_000_000]
    fake.resting = [9_000_000, -10_000_000, 0, -3_000_000]
    fake.mean = [9_000_000, -10_000_000, 0, 9_000_000]
    device, fake = connect(fake)
    assert (fake.waveform, fake.amplitude, fake.resting, fake.mean) == ([0] * 4, [5_000_000] * 4, [0] * 4,
                                                                        [0] * 4)
    assert device.waveform == [None] + ["Sine"] * 4


def test_connecting_to_other_firmware_names_it_and_closes_the_port():
    for reply, version, name, client in ((75, 22, "Pulse Pal", "PulsePalDevice"),
                                         (87, 1, "Wave Pal", "WavePalDevice")):
        fake = FakeSynthPal(handshake_reply=reply, firmware_version=version)
        error = expect_error(connect, fake)
        assert f"{name} firmware (v{version})" in str(error), error
        assert client in str(error), error
        assert not fake.is_open
        assert fake.writes == [command(72)]  # No op 81, which means something else to them


def test_a_late_reply_to_an_earlier_session_is_skipped():
    """A command an earlier session sent just before it closed can still be waiting on the device,
    which answers it after the input was discarded, and before the handshake. The handshake's reply is
    the last 5 bytes the device sends, so the late reply is skipped, also one that looks like a
    handshake."""
    for late_reply in (bytes(range(1, 40)), bytes([83, 99, 0, 0, 0]), bytes([1])):
        fake = FakeSynthPal()
        fake.late_reply = late_reply
        device, fake = connect(fake)
        assert device.info.firmware_version == 1, late_reply[:5]
        assert fake.in_waiting == 0
        device.close()


def test_newer_firmware_warns():
    """Newer firmware only adds commands, so the class uses it, with a warning."""
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        device, fake = connect(FakeSynthPal(firmware_version=2))
    assert any("newer than this pulsepal package knows" in str(w.message) for w in caught)
    assert device.info.firmware_version == 2


def test_export_params_holds_every_setting():
    import json
    device, fake = connect()
    device.frequency = 440
    device.configure(2, waveform="Fixed Voltage", fixed_voltage=-2.5)
    params = json.loads(json.dumps(device.export_params()))
    assert list(params) == ["frequency", "waveform", "peak_to_peak", "fixed_voltage", "mean_voltage",
                            "resting_voltage", "play_duration", "on_ramp_duration", "off_ramp_duration",
                            "trigger_mode", "link_trigger_channel1", "link_trigger_channel2"]
    assert params["frequency"] == 440 and params["waveform"][1] == "Fixed Voltage"
    assert params["fixed_voltage"] == [5, -2.5, 5, 5]


def test_sync_from_device_reads_every_setting():
    """As after the joystick changed them: op 82 ('R') returns op 85's layout."""
    device, fake = connect()
    device.peak_to_peak[1] = 8
    fake.centihz = 44_000
    fake.waveform = [4, 1, 2, 0]                       # Fixed Voltage, Triangle, Square, Sine
    fake.amplitude = [-2_500_000, 4_000_000, 6_000_000, 2_000_000]
    fake.mean = [9_000_000, 1_000_000, 0, -1_000_000]
    fake.resting = [0, -1_000_000, 0, 500_000]
    fake.play_duration = [0, 2_000_000, 500_000, 1_000_000]
    fake.on_ramp = [0, 0, 100_000, 0]
    fake.off_ramp = [0, 250_000, 0, 0]
    fake.links = [1, 0, 1, 0, 0, 1, 1, 0]
    fake.trigger_mode = [1, 3]
    fake.writes.clear()
    device.sync_from_device()
    assert fake.ops() == ["R"]
    assert device.frequency == 440 and device.samples_per_cycle == samples_per_cycle(44_000)
    assert device.waveform == [None, "Fixed Voltage", "Triangle", "Square", "Sine"]
    assert device.fixed_voltage == [None, -2.5, 5, 5, 5]   # The other channels keep theirs
    # Channel 1 plays its fixed voltage: its own peak to peak voltage is kept, reduced to suit the 9 V mean
    assert device.peak_to_peak == [None, 2, 4, 6, 2]
    assert device.mean_voltage == [None, 9, 1, 0, -1] and device.resting_voltage == [None, 0, -1, 0, 0.5]
    assert device.play_duration == [None, 0, 2, 0.5, 1] and device.on_ramp_duration == [None, 0, 0, 0.1, 0]
    assert device.off_ramp_duration == [None, 0, 0.25, 0, 0]
    assert device.link_trigger_channel1 == [None, True, False, True, False]
    assert device.link_trigger_channel2 == [None, False, True, True, False]
    assert device.trigger_mode == [None, "Toggle", "Param Sync"]
    # The record of what the device holds is up to date, so a change sends only what changed
    fake.writes.clear()
    device.peak_to_peak[2] = 3
    assert fake.ops() == ["A"] and fake.amplitude[1] == 3_000_000


def test_the_public_surface_is_trimmed():
    device, fake = connect()
    expect_error(device.close, True, error=TypeError)
    assert not hasattr(device, "bytes_available")


def test_frequency_is_sent_in_hundredths_of_a_hz():
    device, fake = connect()
    device.frequency = 300
    device.frequency = 1.555  # Rounded to 0.01 Hz, halves to even
    device.frequency = np.float32(440)
    assert fake.writes == [
        command("F", struct.pack("<I", 30000)),
        command("F", struct.pack("<I", 156)),
        command("F", struct.pack("<I", 44000)),
    ]
    assert device.frequency == 440


def test_samples_per_cycle_and_sampling_rate_come_from_the_device():
    device, _ = connect()
    for hz, n, rate in ((100, 1000, 100000), (500, 200, 100000), (300, 332, 99600),
                        (20000, 4, 80000), (12600, 4, 50400), (1, 100000, 100000)):
        device.frequency = hz
        assert device.samples_per_cycle == n, (hz, device.samples_per_cycle)
        assert abs(device.sampling_rate - rate) < 1e-6, (hz, device.sampling_rate)


def test_frequencies_out_of_range_are_refused_before_sending():
    device, fake = connect()
    for hz in (0.99, 0, -5, 20000.01, float("nan"), float("inf"), "100", True, None):
        expect_error(setattr, device, "frequency", hz)
    assert fake.writes == []
    assert device.frequency == 100
    device.frequency = 0.995  # Rounds to 1.00 Hz, the lowest
    device.frequency = 20000.004
    assert fake.writes[-1] == command("F", struct.pack("<I", 2_000_000))


def test_a_rejected_frequency_is_left_unchanged():
    device, fake = connect()
    fake.ack = 0
    error = expect_error(setattr, device, "frequency", 200)
    assert "rejected" in str(error)
    assert device.frequency == 100
    assert device.samples_per_cycle == 1000


def test_waveforms_by_name():
    device, fake = connect()
    device.waveform[2] = "triangle"
    device.waveform[3:5] = ["SQUARE", "Sawtooth"]
    device.waveform[1] = "fixed voltage"
    assert fake.writes == [
        command("W", bytes([0, 1, 0, 0])),
        command("W", bytes([0, 1, 2, 3])),
        command("W", bytes([4, 1, 2, 3])),
    ]
    assert device.waveform == [None, "Fixed Voltage", "Triangle", "Square", "Sawtooth"]
    for bad in ("Ramp", "Fixed", 1, None):
        expect_error(device.waveform.__setitem__, 1, bad)
    assert len(fake.writes) == 3


def test_voltages_are_sent_in_microvolts():
    device, fake = connect()
    device.resting_voltage[1:5] = [1.5, -2.25, 0.0000004, -0.0000005]
    device.mean_voltage[1:5] = [-1.5, 2.25, 0.0000004, -0.0000005]
    device.peak_to_peak = [1, 0.0123456, 15, 20]
    assert fake.writes == [
        command("V", struct.pack("<4i", 1_500_000, -2_250_000, 0, 0)),
        command("M", struct.pack("<4i", -1_500_000, 2_250_000, 0, 0)),
        command("A", struct.pack("<4i", 1_000_000, 12_346, 15_000_000, 20_000_000)),
    ]
    assert device.peak_to_peak == [None, 1, 0.0123456, 15, 20]
    assert device.mean_voltage == [None, -1.5, 2.25, 0.0000004, -0.0000005]


def test_the_waveform_must_stay_within_10_volts():
    device, fake = connect()
    error = expect_error(device.mean_voltage.__setitem__, 1, 9)  # 9 + 5 / 2 = 11.5 V
    assert "channel 1" in str(error) and "peak_to_peak first" in str(error), error
    assert "configure()" in str(error), error
    device.peak_to_peak[1] = 2
    device.mean_voltage[1] = 9  # Reaches exactly 10 V
    fake.writes.clear()
    error = expect_error(device.peak_to_peak.__setitem__, 1, 4)  # 9 + 2 = 11 V
    assert "channel 1" in str(error) and "mean_voltage first" in str(error), error
    expect_error(device.mean_voltage.__setitem__, 1, -9.5)  # -9.5 - 1 = -10.5 V
    device.mean_voltage[1] = -9
    for bad in (-0.1, 20.5, float("nan"), "5", True):
        expect_error(device.peak_to_peak.__setitem__, 2, bad)
    for bad in (10.5, -11, float("inf")):
        expect_error(device.mean_voltage.__setitem__, 2, bad)
        expect_error(device.resting_voltage.__setitem__, 2, bad)
        expect_error(device.fixed_voltage.__setitem__, 2, bad)
    assert fake.ops() == ["M"]
    assert device.peak_to_peak == [None, 2, 5, 5, 5]
    assert device.mean_voltage == [None, -9, 0, 0, 0]


def test_the_resting_voltage_goes_with_any_waveform():
    """The resting voltage is the output between playbacks: it does not limit the
    waveform, which swings around the mean voltage."""
    device, fake = connect()
    device.peak_to_peak[1] = 20
    device.resting_voltage[1:3] = [10, -10]
    assert fake.resting == [10_000_000, -10_000_000, 0, 0] and fake.amplitude[0] == 20_000_000


def test_large_amplitude_after_moving_the_mean_voltage():
    """Going from (9 V mean, 2 V amplitude) to (0 V mean, 20 V amplitude) needs
    the mean voltage first, and the reverse needs the amplitude first."""
    device, fake = connect()
    device.peak_to_peak[3] = 2
    device.mean_voltage[3] = 9
    device.mean_voltage[3] = 0
    device.peak_to_peak[3] = 20
    device.peak_to_peak[3] = 2
    device.mean_voltage[3] = 9
    assert fake.amplitude[2] == 2_000_000 and fake.mean[2] == 9_000_000


def test_ramp_durations_are_sent_in_microseconds():
    device, fake = connect()
    device.on_ramp_duration[1:5] = [0, 0.0000015, 2.5, 3600]
    device.off_ramp_duration = [0.01] * 4
    assert fake.writes == [
        command("B", struct.pack("<4I", 0, 2, 2_500_000, 3_600_000_000)),
        command("E", struct.pack("<4I", *[10_000] * 4)),
    ]
    assert device.on_ramp_duration == [None, 0, 0.0000015, 2.5, 3600]
    for name in ("on_ramp_duration", "off_ramp_duration"):
        error = expect_error(getattr(device, name).__setitem__, 1, -1)
        assert name in str(error) and "0 (no ramp)" in str(error), error
        for bad in (3600.001, float("nan"), "1", True):
            expect_error(getattr(device, name).__setitem__, 1, bad)
    assert len(fake.writes) == 2


def test_a_fixed_voltage_channel_steps_to_its_fixed_voltage():
    """On the device, a channel has one amplitude: its fixed voltage, or its waveform's peak to peak
    voltage. The class keeps both, and sends the one that its waveform uses."""
    device, fake = connect()
    device.waveform[2] = "Fixed Voltage"   # Its fixed voltage is 5 V, as its peak to peak voltage was
    device.fixed_voltage[2] = -7.5
    device.resting_voltage[2] = 9.5        # Any resting voltage goes with a fixed voltage
    device.fixed_voltage[2] = 10
    device.fixed_voltage[1] = -1           # Kept for when channel 1 plays a fixed voltage: nothing to send
    device.peak_to_peak[2] = 20            # Kept for when channel 2 plays a periodic waveform
    assert fake.writes == [
        command("W", bytes([0, 4, 0, 0])),
        command("A", struct.pack("<4i", 5_000_000, -7_500_000, 5_000_000, 5_000_000)),
        command("V", struct.pack("<4i", 0, 9_500_000, 0, 0)),
        command("A", struct.pack("<4i", 5_000_000, 10_000_000, 5_000_000, 5_000_000)),
    ]
    for bad in (10.5, -10.000001, float("nan"), "5", True):
        expect_error(device.fixed_voltage.__setitem__, 2, bad)
    assert device.fixed_voltage == [None, -1, 10, 5, 5]
    assert device.peak_to_peak == [None, 5, 20, 5, 5]
    device.waveform[2] = "Sine"
    assert fake.amplitude == [5_000_000, 20_000_000, 5_000_000, 5_000_000] and fake.waveform == [0] * 4


def test_waveform_changes_are_sent_in_an_order_the_device_accepts():
    """A sine wave of 20 V peak to peak cannot become a fixed voltage of -5 V by either op first: the
    device would refuse a fixed voltage of 20 V, and a sine wave of -5 V peak to peak. The class goes
    through 0 V, which suits every waveform, and only when it has to."""
    device, fake = connect()
    device.peak_to_peak[3] = 20
    device.fixed_voltage[3] = -5
    fake.writes.clear()
    device.waveform[3] = "Fixed Voltage"
    assert fake.writes == [
        command("A", struct.pack("<4i", 5_000_000, 5_000_000, 0, 5_000_000)),
        command("W", bytes([0, 0, 4, 0])),
        command("A", struct.pack("<4i", 5_000_000, 5_000_000, -5_000_000, 5_000_000)),
    ]
    fake.writes.clear()
    device.waveform[3] = "Sine"
    assert fake.ops() == ["A", "W", "A"]
    assert fake.amplitude[2] == 20_000_000 and fake.waveform[2] == 0

    # 5 V suits a fixed voltage too, so the waveform changes first, and nothing goes through 0 V
    device.fixed_voltage[1] = 3
    fake.writes.clear()
    device.waveform[1] = "Fixed Voltage"
    assert fake.writes == [
        command("W", bytes([4, 0, 0, 0])),
        command("A", struct.pack("<4i", 3_000_000, 5_000_000, 20_000_000, 5_000_000)),
    ]


def test_configure_sets_several_settings_in_any_order():
    """From 2 V peak to peak around 9 V to 20 V around 0 V needs the mean voltage first, and back
    needs the peak to peak voltage first. configure() works out the order."""
    device, fake = connect()
    device.configure(3, peak_to_peak=2, mean_voltage=9)
    assert fake.ops() == ["A", "M"]
    fake.writes.clear()
    device.configure(3, peak_to_peak=20, mean_voltage=0, play_duration=0.5)
    assert fake.writes == [
        command("M", struct.pack("<4i", 0, 0, 0, 0)),
        command("A", struct.pack("<4i", 5_000_000, 5_000_000, 20_000_000, 5_000_000)),
        command("D", struct.pack("<4I", 1_000_000, 1_000_000, 500_000, 1_000_000)),
    ]
    device.configure([1, 2], waveform=["Triangle", "Fixed Voltage"], fixed_voltage=-2.5, resting_voltage=1)
    assert device.waveform == [None, "Triangle", "Fixed Voltage", "Sine", "Sine"]
    assert device.fixed_voltage == [None, -2.5, -2.5, 5, 5]
    assert device.resting_voltage == [None, 1, 1, 0, 0]
    assert fake.waveform == [1, 4, 0, 0] and fake.amplitude[:2] == [5_000_000, -2_500_000]

    fake.writes.clear()
    error = expect_error(device.configure, 1, peak_to_peak=20, mean_voltage=1)  # Reaches 11 V
    assert "would reach 11 V" in str(error), error
    expect_error(device.configure, 1, waveform="Ramp")
    expect_error(device.configure, [1, 2], peak_to_peak=[1, 2, 3])
    expect_error(device.configure, 5, peak_to_peak=1)
    expect_error(device.configure, 1, amplitude=1, error=TypeError)
    assert fake.writes == []


def test_a_stale_record_of_the_device_levels_is_recovered():
    """If the device holds other levels than the class last sent (e.g. a param sync edge loaded a
    stored set), a step it refuses makes the class start again from 0 V, which works from any state."""
    device, fake = connect()
    fake.waveform[0] = 4                    # As if channel 1 now played a fixed voltage of -5 V
    fake.amplitude[0] = -5_000_000
    device.peak_to_peak[1] = 20             # A fixed voltage of 20 V: refused, then recovered
    assert fake.ops() == ["A", "A", "W", "M", "A"]
    assert fake.waveform == [0] * 4 and fake.amplitude == [20_000_000, 5_000_000, 5_000_000, 5_000_000]


def test_play_durations_are_sent_in_microseconds():
    device, fake = connect()
    device.play_duration[1:5] = [0, 0.0000015, 2.5, 3600]
    assert fake.writes == [command("D", struct.pack("<4I", 0, 2, 2_500_000, 3_600_000_000))]
    for bad in (-1, 3600.001, float("nan"), "1", True):
        expect_error(device.play_duration.__setitem__, 1, bad)
    assert len(fake.writes) == 1


def test_trigger_modes_belong_to_the_trigger_channels():
    device, fake = connect()
    device.trigger_mode[2] = "gated"
    device.trigger_mode = ["Toggle", "Normal"]
    device.trigger_mode = ["GATED", "gated"]
    assert fake.writes == [
        command("T", bytes([0, 2])),
        command("T", bytes([1, 0])),
        command("T", bytes([2, 2])),
    ]
    assert device.trigger_mode == [None, "Gated", "Gated"]
    expect_error(device.trigger_mode.__setitem__, 3, "Normal", error=IndexError)
    expect_error(device.trigger_mode.__setitem__, 1, "Master")  # A Wave Pal mode
    expect_error(device.trigger_mode.__setitem__, 1, 0)
    expect_error(setattr, device, "trigger_mode", ["Normal"] * 4)
    assert len(fake.writes) == 3


def test_param_sync_is_trigger_mode_3():
    device, fake = connect()
    device.trigger_mode[2] = "param sync"
    assert fake.writes == [command("T", bytes([0, 3]))]
    assert device.trigger_mode == [None, "Normal", "Param Sync"]


def test_auto_sync_off_keeps_settings_here_until_sync_to_device():
    device, fake = connect()
    device.auto_sync = False
    device.frequency = 300
    device.waveform[1:3] = ["Fixed Voltage", "Square"]
    device.fixed_voltage[1] = -2.5
    device.peak_to_peak[2] = 3
    device.mean_voltage[2] = 1.25
    device.resting_voltage[3] = -0.5
    device.play_duration[4] = 0.25
    device.on_ramp_duration[1] = 0.01
    device.off_ramp_duration[2] = 0.02
    device.trigger_mode = ["Toggle", "Param Sync"]
    device.link_trigger_channel2[4] = True
    assert fake.writes == [], fake.ops()
    assert device.samples_per_cycle == 332 and device.frequency == 300  # As the device would work it out
    device.sync_to_device()
    assert fake.writes == [command("U", struct.pack(
        "<I4B4i4i4i4I4I4I8B2B", 30000, 4, 2, 0, 0, -2_500_000, 3_000_000, 5_000_000, 5_000_000,
        0, 1_250_000, 0, 0, 0, 0, -500_000, 0, 1_000_000, 1_000_000, 1_000_000, 250_000,
        10_000, 0, 0, 0, 0, 20_000, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 3))]
    assert fake.amplitude[0] == -2_500_000 and fake.trigger_mode == [1, 3]  # Applied: no channel was in param sync
    assert device.fixed_voltage == [None, -2.5, 5, 5, 5] and device.peak_to_peak == [None, 5, 3, 5, 5]


def test_sync_to_device_in_param_sync_mode_is_stored_by_the_device():
    device, fake = connect()
    device.trigger_mode[1] = "Param Sync"  # Sent at once
    device.auto_sync = False
    device.peak_to_peak[2] = 7
    device.sync_to_device()
    assert fake.stored["amplitude"] == [5_000_000, 7_000_000, 5_000_000, 5_000_000]
    assert fake.amplitude == [5_000_000] * 4  # Not applied: it waits for an edge
    device.auto_sync = True
    device.trigger_mode[1] = "Normal"
    assert fake.stored is None


def test_assignments_are_checked_with_auto_sync_off_too():
    device, fake = connect()
    device.auto_sync = False
    device.peak_to_peak[1] = 4
    error = expect_error(device.mean_voltage.__setitem__, 1, 9)  # 9 V + 2 V
    assert "would reach 11 V" in str(error), error
    device.configure(1, peak_to_peak=2, mean_voltage=9)  # Together, they go
    for bad in (-10.5, 20.5, "1"):
        expect_error(device.peak_to_peak.__setitem__, 2, bad)
    for bad in ("yes", 2, None):
        expect_error(setattr, device, "auto_sync", bad)
    assert fake.writes == []
    device.sync_to_device()
    assert fake.ops() == ["U"] and fake.mean[0] == 9_000_000


def test_batch_sends_everything_at_its_end():
    device, fake = connect()
    with device.batch():
        device.frequency = 880
        device.waveform = ["Triangle"] * 4
        device.peak_to_peak = [2, 4, 6, 8]
        assert fake.writes == []
    assert fake.ops() == ["U"]
    assert fake.centihz == 88000 and fake.waveform == [1] * 4 and fake.amplitude[3] == 8_000_000
    assert device.auto_sync is True

    fake.writes.clear()
    try:
        with device.batch():
            device.frequency = 440
            device.peak_to_peak[1] = 1
            raise RuntimeError("stop here")
    except RuntimeError:
        pass
    assert fake.writes == []
    assert device.frequency == 880 and device.peak_to_peak[1] == 2 and device.samples_per_cycle == 112


def test_set_default_params_programs_the_device_with_auto_sync_off():
    device, fake = connect()
    device.auto_sync = False
    device.set_default_params()
    assert fake.ops() == ["F", "M", "A", "W", "V", "D", "B", "E", "T", "I"]
    assert device.auto_sync is False


def test_trigger_links_travel_together():
    device, fake = connect()
    device.link_trigger_channel2[3] = True
    device.link_trigger_channel1 = [True, False, False, True]
    assert fake.writes == [
        command("I", bytes([1, 1, 1, 1, 0, 0, 1, 0])),
        command("I", bytes([1, 0, 0, 1, 0, 0, 1, 0])),
    ]


def test_channel_settings_are_indexed_by_channel_number():
    device, fake = connect()
    device.peak_to_peak = [1, 2, 3, 4]              # 4 values: channels 1-4
    device.peak_to_peak = [None, 4, 3, 2, 1]        # 5 values: index 0 unused
    device.peak_to_peak = np.full(4, 2.0)           # A NumPy array
    assert device.peak_to_peak == [None, 2, 2, 2, 2]
    fake.writes.clear()
    # A single value does not say which channels it is meant for (see "One way to use all six" in
    # /AGENTS.md). configure() takes one value for the channels it is given.
    for name, value in (("peak_to_peak", 2), ("waveform", "Sine"), ("trigger_mode", "Normal"),
                        ("play_duration", 1)):
        error = expect_error(setattr, device, name, value)
        assert f"{name}[1] = " in str(error), error
    expect_error(device.peak_to_peak.__setitem__, 0, 1)
    expect_error(device.peak_to_peak.__setitem__, slice(1, 3), [1])
    expect_error(setattr, device, "peak_to_peak", [1, 2])
    for method, args in (("append", (1,)), ("pop", ()), ("sort", ()), ("clear", ())):
        expect_error(getattr(device.peak_to_peak, method), *args, error=TypeError)
    assert fake.writes == []
    assert type(copy.copy(device.peak_to_peak)) is list


def test_a_setting_the_device_rejects_is_left_unchanged():
    device, fake = connect()
    fake.ack = 0
    expect_error(device.waveform.__setitem__, 1, "Square")
    expect_error(device.play_duration.__setitem__, 1, 3)
    expect_error(device.peak_to_peak.__setitem__, 1, 3)
    assert device.waveform == [None] + ["Sine"] * 4
    assert device.play_duration == [None, 1, 1, 1, 1]
    assert device.peak_to_peak == [None, 5, 5, 5, 5]
    # The peak to peak voltage is unchanged too, so the next check uses 5 V
    fake.ack = 1
    expect_error(device.mean_voltage.__setitem__, 1, 8)  # 8 + 2.5 > 10


def test_trigger_and_stop_messages():
    device, fake = connect()
    device.trigger(1)
    device.trigger([2, 4])
    device.trigger(np.array([3]))
    device.stop()
    device.stop(3)
    device.stop([1, 2])
    assert fake.writes == [
        command("P", bytes([0b0001])),
        command("P", bytes([0b1010])),
        command("P", bytes([0b0100])),
        command("X", bytes([0b1111])),
        command("X", bytes([0b0100])),
        command("X", bytes([0b0011])),
    ]
    device.trigger((1, 2))
    assert fake.writes[-1] == command("P", bytes([0b0011]))
    for channels in (0, 5, [], [1, 7], True):
        expect_error(device.trigger, channels)
    assert len(fake.writes) == 7


def test_status_is_unpacked():
    device, fake = connect()
    fake.status_reply = struct.pack("<BI4BII", 0b0101, 332, 0, 1, 2, 3, 7480, 2)
    status = device.status()
    assert fake.writes == [command("G")]
    assert status.playing == [1, 3]
    assert status.samples_per_cycle == 332
    assert status.output_ranges == [None, "0V:5V", "0V:10V", "-5V:5V", "-10V:10V"]
    assert status.longest_interrupt_us == 7.48
    assert status.late_updates == 2


def test_screen_saver_message():
    device, fake = connect()
    device.set_screen_saver(True)
    device.set_screen_saver(False, 60)
    assert fake.writes == [
        command(99, struct.pack("<BH", 1, 1800)),
        command(99, struct.pack("<BH", 0, 60)),
    ]
    for args in ((2,), (True, 0), (True, 65536), (True, 1.5)):
        expect_error(device.set_screen_saver, *args)
    assert len(fake.writes) == 2


def test_deleting_the_device_closes_its_port_at_once():
    """The channel settings must not hold the device in a reference cycle,
    or the port would stay open until the garbage collector runs."""
    gc.disable()
    try:
        device, fake = connect()
        del device
        assert not fake.is_open
    finally:
        gc.enable()


def test_close_sends_the_disconnect_op_once():
    """Op 81 stops playback, as Pulse Pal's op 81 does, and puts the device's own name back
    on its screen. Nothing else is sent: the firmware stops the channels itself."""
    device, fake = connect()
    device.close()
    device.close()
    assert fake.writes == [command(81)]
    assert not fake.is_open


def test_a_failed_connection_sends_the_disconnect_op_only_to_a_synth_pal():
    fake = FakeSynthPal(ack=0)  # A Synth Pal that rejects the default settings
    expect_error(connect, fake)
    assert fake.writes[-1] == command(81)
    assert not fake.is_open


def main():
    tests = [
        value for name, value in sorted(globals().items())
        if name.startswith("test_") and callable(value)
    ]
    failures = []
    for test in tests:
        try:
            test()
        except Exception as error:  # noqa: BLE001 - report and continue
            failures.append(f"{test.__name__}: {type(error).__name__}: {error}")
    print(f"{len(tests) - len(failures)}/{len(tests)} tests passed")
    for failure in failures:
        print("  FAILED " + failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
