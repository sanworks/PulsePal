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
import importlib.util
import struct
import sys
from pathlib import Path

import numpy as np

MODULE_PATH = Path(__file__).resolve().parent.parent / "SynthPal.py"
_spec = importlib.util.spec_from_file_location("SynthPal_under_test", MODULE_PATH)
SynthPal = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(SynthPal)

OP_MENU_BYTE = 213


def samples_per_cycle(centihz):
    """As samplesPerCycleFor() in /Firmware/SynthPal/Settings.ino."""
    return 4 * (2_500_000 // centihz)


class FakeSynthPal:
    """A fake serial port with a simulated Synth Pal behind it.

    Each write must be one whole command. It is checked against the protocol,
    recorded in `writes`, and the reply the firmware would send is queued for
    reading. Set `ack` to 0 to have the device reject confirmable commands.
    """

    def __init__(self, handshake_reply=83, firmware_version=1, ack=1):
        self.port = "FAKE"
        self.is_open = True
        self.writes = []
        self.replies = bytearray()
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
        self.stored = None  # A set op 85 stored for a param sync edge
        self.status_reply = bytes(17)

    def write(self, data):
        data = bytes(data)
        self.writes.append(data)
        self.replies += self._execute(data)
        return len(data)

    def _execute(self, data):
        assert data[0] == OP_MENU_BYTE, f"command without the framing byte: {list(data)}"
        op = data[1]
        payload = data[2:]
        expected_lengths = {72: 0, 81: 0, 89: 6, 99: 3, ord("N"): 0, ord("F"): 4, ord("W"): 4,
                            ord("A"): 16, ord("V"): 16, ord("M"): 16, ord("D"): 16, ord("B"): 16,
                            ord("E"): 16, ord("I"): 8, ord("T"): 2, ord("U"): 114, ord("P"): 1,
                            ord("X"): 1, ord("G"): 0, ord("Z"): 0}
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


def expect_error(function, *args, error=None):
    """Run function and check that it raises SynthPalError (or `error`)."""
    error = error or SynthPal.SynthPalError
    try:
        function(*args)
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
        command("V", struct.pack("<4i", 0, 0, 0, 0)),
        command("A", struct.pack("<4i", *[5_000_000] * 4)),
        command("W", bytes(4)),
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
    assert device.amplitude == [None, 5, 5, 5, 5]
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


def test_newer_firmware_is_refused():
    fake = FakeSynthPal(firmware_version=2)
    error = expect_error(connect, fake)
    assert "Future firmware" in str(error)
    assert not fake.is_open


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
    device.amplitude = [1, 0.0123456, 15, 20]
    assert fake.writes == [
        command("V", struct.pack("<4i", 1_500_000, -2_250_000, 0, 0)),
        command("M", struct.pack("<4i", -1_500_000, 2_250_000, 0, 0)),
        command("A", struct.pack("<4i", 1_000_000, 12_346, 15_000_000, 20_000_000)),
    ]
    assert device.amplitude == [None, 1, 0.0123456, 15, 20]
    assert device.mean_voltage == [None, -1.5, 2.25, 0.0000004, -0.0000005]


def test_the_waveform_must_stay_within_10_volts():
    device, fake = connect()
    error = expect_error(device.mean_voltage.__setitem__, 1, 9)  # 9 + 5 / 2 = 11.5 V
    assert "channel 1" in str(error) and "amplitude first" in str(error), error
    device.amplitude[1] = 2
    device.mean_voltage[1] = 9  # Reaches exactly 10 V
    fake.writes.clear()
    error = expect_error(device.amplitude.__setitem__, 1, 4)  # 9 + 2 = 11 V
    assert "channel 1" in str(error) and "mean_voltage first" in str(error), error
    expect_error(device.mean_voltage.__setitem__, 1, -9.5)  # -9.5 - 1 = -10.5 V
    device.mean_voltage[1] = -9
    for bad in (-0.1, 20.5, float("nan"), "5", True):
        expect_error(device.amplitude.__setitem__, 2, bad)
    for bad in (10.5, -11, float("inf")):
        expect_error(device.mean_voltage.__setitem__, 2, bad)
        expect_error(device.resting_voltage.__setitem__, 2, bad)
    assert fake.ops() == ["M"]
    assert device.amplitude == [None, 2, 5, 5, 5]
    assert device.mean_voltage == [None, -9, 0, 0, 0]


def test_the_resting_voltage_goes_with_any_waveform():
    """The resting voltage is the output between playbacks: it does not limit the
    waveform, which swings around the mean voltage."""
    device, fake = connect()
    device.amplitude[1] = 20
    device.resting_voltage[1:3] = [10, -10]
    assert fake.resting == [10_000_000, -10_000_000, 0, 0] and fake.amplitude[0] == 20_000_000


def test_large_amplitude_after_moving_the_mean_voltage():
    """Going from (9 V mean, 2 V amplitude) to (0 V mean, 20 V amplitude) needs
    the mean voltage first, and the reverse needs the amplitude first."""
    device, fake = connect()
    device.amplitude[3] = 2
    device.mean_voltage[3] = 9
    device.mean_voltage[3] = 0
    device.amplitude[3] = 20
    device.amplitude[3] = 2
    device.mean_voltage[3] = 9
    assert fake.amplitude[2] == 2_000_000 and fake.mean[2] == 9_000_000


def test_ramp_durations_are_sent_in_microseconds():
    device, fake = connect()
    device.on_ramp_duration[1:5] = [0, 0.0000015, 2.5, 3600]
    device.off_ramp_duration = 0.01
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


def test_a_fixed_voltage_amplitude_is_a_signed_voltage():
    device, fake = connect()
    device.waveform[2] = "Fixed Voltage"
    device.amplitude[2] = -7.5
    device.resting_voltage[2] = 9.5  # Any resting voltage goes with a fixed voltage
    device.amplitude[2] = 10
    assert fake.writes[1:] == [
        command("A", struct.pack("<4i", 5_000_000, -7_500_000, 5_000_000, 5_000_000)),
        command("V", struct.pack("<4i", 0, 9_500_000, 0, 0)),
        command("A", struct.pack("<4i", 5_000_000, 10_000_000, 5_000_000, 5_000_000)),
    ]
    fake.writes.clear()
    for bad in (10.5, -10.000001, float("nan"), "5", True):
        error = expect_error(device.amplitude.__setitem__, 2, bad)
    assert "from -10 to 10 on a Fixed Voltage channel" in str(error), error
    error = expect_error(device.amplitude.__setitem__, 1, -1)  # Channel 1 plays a sine wave
    assert "Only a Fixed Voltage can be negative" in str(error), error
    assert fake.writes == []
    assert device.amplitude == [None, 5, 10, 5, 5]


def test_a_new_waveform_must_suit_the_amplitude():
    device, fake = connect()
    device.waveform[1] = "Fixed Voltage"
    device.amplitude[1] = -5
    device.amplitude[3] = 20
    device.waveform[4] = "Fixed Voltage"
    device.amplitude[4] = 9
    device.mean_voltage[4] = 9  # A fixed voltage ignores its mean voltage
    fake.writes.clear()
    error = expect_error(device.waveform.__setitem__, 1, "Sine")
    assert "negative" in str(error) and "Change amplitude first" in str(error), error
    error = expect_error(device.waveform.__setitem__, 3, "Fixed Voltage")
    assert "beyond -10 V to 10 V" in str(error), error
    error = expect_error(device.waveform.__setitem__, 4, "Sine")  # 9 V + 9 V / 2 = 13.5 V
    assert "13.5 V" in str(error) and "amplitude or mean_voltage first" in str(error), error
    assert fake.writes == []
    device.amplitude[1] = 5  # Valid for both: a fixed voltage of 5 V, then a sine wave of 5 V peak to peak
    device.waveform[1] = "Sine"
    assert fake.waveform == [0, 0, 0, 4]
    assert device.waveform == [None, "Sine", "Sine", "Sine", "Fixed Voltage"]


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
    device.trigger_mode = "GATED"
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
    device.amplitude[1:3] = [-2.5, 3]  # A fixed voltage of -2.5 V, before the waveform would allow it
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
    assert device.amplitude == [None, -2.5, 3, 5, 5]


def test_sync_to_device_in_param_sync_mode_is_stored_by_the_device():
    device, fake = connect()
    device.trigger_mode[1] = "Param Sync"  # Sent at once
    device.auto_sync = False
    device.amplitude[2] = 7
    device.sync_to_device()
    assert fake.stored["amplitude"] == [5_000_000, 7_000_000, 5_000_000, 5_000_000]
    assert fake.amplitude == [5_000_000] * 4  # Not applied: it waits for an edge
    device.auto_sync = True
    device.trigger_mode[1] = "Normal"
    assert fake.stored is None


def test_sync_to_device_checks_the_whole_set():
    device, fake = connect()
    device.auto_sync = False
    device.amplitude[1] = -1  # Accepted here: the waveform could still become a fixed voltage
    error = expect_error(device.sync_to_device)
    assert "channel 1" in str(error) and "negative" in str(error), error
    device.amplitude[1] = 4
    device.mean_voltage[1] = 9  # 9 V + 2 V
    error = expect_error(device.sync_to_device)
    assert "would reach 11 V" in str(error) and "Change amplitude or mean_voltage." in str(error), error
    for bad in (-10.5, 20.5, "1"):
        expect_error(device.amplitude.__setitem__, 2, bad)
    for bad in ("yes", 2, None):
        expect_error(setattr, device, "auto_sync", bad)
    assert fake.writes == []


def test_set_defaults_programs_the_device_with_auto_sync_off():
    device, fake = connect()
    device.auto_sync = False
    device.set_defaults()
    assert fake.ops() == ["F", "M", "V", "A", "W", "D", "B", "E", "T", "I"]
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
    device.amplitude = [1, 2, 3, 4]                 # 4 values: channels 1-4
    device.amplitude = [None, 4, 3, 2, 1]           # 5 values: index 0 unused
    device.amplitude = 2                            # one value for all channels
    assert device.amplitude == [None, 2, 2, 2, 2]
    fake.writes.clear()
    expect_error(device.amplitude.__setitem__, 0, 1)
    expect_error(device.amplitude.__setitem__, slice(1, 3), [1])
    expect_error(setattr, device, "amplitude", [1, 2])
    for method, args in (("append", (1,)), ("pop", ()), ("sort", ()), ("clear", ())):
        expect_error(getattr(device.amplitude, method), *args, error=TypeError)
    assert fake.writes == []
    assert type(copy.copy(device.amplitude)) is list


def test_a_setting_the_device_rejects_is_left_unchanged():
    device, fake = connect()
    fake.ack = 0
    expect_error(device.waveform.__setitem__, 1, "Square")
    expect_error(device.play_duration.__setitem__, 1, 3)
    expect_error(device.amplitude.__setitem__, 1, 3)
    assert device.waveform == [None] + ["Sine"] * 4
    assert device.play_duration == [None, 1, 1, 1, 1]
    assert device.amplitude == [None, 5, 5, 5, 5]
    # The class's record of the amplitude is unchanged too, so the next check uses 5 V
    fake.ack = 1
    expect_error(device.mean_voltage.__setitem__, 1, 8)  # 8 + 2.5 > 10


def test_play_and_stop_messages():
    device, fake = connect()
    device.play(1)
    device.play([2, 4])
    device.play(np.array([3]))
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
    for channels in (0, 5, [], [1, 7], True):
        expect_error(device.play, channels)
    assert len(fake.writes) == 6


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
    """Op 81 puts the device's own name back on its screen. Unlike Pulse Pal's op 81,
    it does not stop playback, and nothing else is sent (e.g. no stop op)."""
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
