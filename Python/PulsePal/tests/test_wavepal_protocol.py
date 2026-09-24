"""Offline tests for the bytes WavePalDevice sends to a Wave Pal.

These tests need no device: a simulated Wave Pal behind a fake serial port
checks each command's layout, records it, and replies as the firmware would.
They are a fast check that the class matches the Wave Pal serial protocol,
which is documented in /Firmware/WavePal/PROTOCOL.md.

Run them with:

    python tests/test_wavepal_protocol.py

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

MODULE_PATH = Path(__file__).resolve().parent.parent / "WavePal.py"
_spec = importlib.util.spec_from_file_location("WavePal_under_test", MODULE_PATH)
WavePal = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(WavePal)

OP_MENU_BYTE = 213
CLOCK_HZ = 24000000
BUFFER_SAMPLES = 16384


class FakeWavePal:
    """A fake serial port with a simulated Wave Pal behind it.

    Each write must be one whole command. It is checked against the protocol,
    recorded in `writes`, and the reply the firmware would send is queued for
    reading. Set `ack` to 0 to have the device reject confirmable commands.
    """

    def __init__(self, handshake_reply=87, firmware_version=1, ack=1):
        self.port = "FAKE"
        self.is_open = True
        self.writes = []
        self.replies = bytearray()
        self.handshake_reply = handshake_reply
        self.firmware_version = firmware_version
        self.ack = ack
        self.range_index = 3  # -10 V to 10 V
        self.loaded = {}  # channel -> DAC codes
        self.status_reply = bytes(37)

    def write(self, data):
        data = bytes(data)
        self.writes.append(data)
        self.replies += self._execute(data)
        return len(data)

    def _execute(self, data):
        assert data[0] == OP_MENU_BYTE, f"command without the framing byte: {list(data)}"
        op = data[1]
        payload = data[2:]
        expected_lengths = {72: 0, 81: 0, 89: 6, ord("N"): 0, ord("S"): 4, ord("R"): 1,
                            ord("P"): 1, ord("X"): 1, ord("!"): 3, ord("O"): 4,
                            ord("D"): 16, ord("T"): 4, ord("I"): 8, ord("G"): 0}
        if op == ord("L"):
            channel, n_samples = struct.unpack("<BI", payload[:5])
            assert len(payload) == 5 + 2 * n_samples, "load waveform length"
            if self.ack:
                self.loaded[channel] = np.frombuffer(payload[5:], dtype="<u2").copy()
            else:
                self.loaded.pop(channel, None)
            return bytes([self.ack])
        assert op in expected_lengths, f"unknown op {op}"
        assert len(payload) == expected_lengths[op], f"op {chr(op)!r} data length"
        if op == 72:
            return bytes([self.handshake_reply]) + struct.pack("<I", self.firmware_version)
        if op == ord("N"):
            return struct.pack("<BBIIII", 3, 4, 1000000, 100000, BUFFER_SAMPLES, CLOCK_HZ)
        if op in (ord("P"), ord("X"), 81, 89):
            return b""
        if op == ord("G"):
            return self.status_reply
        if op == ord("R") and self.ack and payload[0] != self.range_index:
            self.range_index = payload[0]
            self.loaded.clear()  # The firmware unloads waveforms when the range changes
        return bytes([self.ack])

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
    """Connect a WavePalDevice to a simulated Wave Pal, and clear the record
    of the connection sequence."""
    fake = fake or FakeWavePal()
    original = WavePal.serial.Serial
    WavePal.serial.Serial = lambda *args, **kwargs: fake
    try:
        device = WavePal.WavePalDevice("FAKE")
    finally:
        WavePal.serial.Serial = original
    fake.writes.clear()
    return device, fake


def expect_error(function, *args, error=None):
    """Run function and check that it raises WavePalError (or `error`)."""
    error = error or WavePal.WavePalError
    try:
        function(*args)
    except error as exc:
        return exc
    raise AssertionError(f"{function} did not raise {error.__name__}")


def command(op, data=b""):
    return bytes([OP_MENU_BYTE, ord(op) if isinstance(op, str) else op]) + data


def test_connection_sequence_programs_the_defaults():
    fake = FakeWavePal()
    original = WavePal.serial.Serial
    WavePal.serial.Serial = lambda *args, **kwargs: fake
    try:
        device = WavePal.WavePalDevice("FAKE")
    finally:
        WavePal.serial.Serial = original
    assert fake.writes == [
        command(72),
        command("N"),
        command(89, b"PYTHON"),  # Shown on the device's screen as "PYTHON Connected"
        command("X", bytes([0x0F])),
        command("S", struct.pack("<I", 10000)),
        command("R", bytes([3])),
        command("O", bytes(4)),
        command("D", bytes(16)),
        command("T", bytes(4)),
        command("I", bytes([1, 1, 1, 1, 0, 0, 0, 0])),
    ], fake.ops()
    assert device.info.firmware_version == 1
    assert device.info.max_samples == 1000000
    assert device.info.buffer_samples == BUFFER_SAMPLES
    assert device.sampling_rate == 10000
    assert device.output_range == "-10V:10V"
    assert device.loop_mode == [None, False, False, False, False]
    assert device.trigger_mode == [None] + ["Normal"] * 4
    assert device.link_trigger_channel1 == [None, True, True, True, True]
    assert device.link_trigger_channel2 == [None, False, False, False, False]


def test_connecting_to_a_pulse_pal_names_the_firmware_and_closes_the_port():
    fake = FakeWavePal(handshake_reply=75, firmware_version=22)
    error = expect_error(connect, fake)
    assert "Pulse Pal firmware (v22)" in str(error)
    assert not fake.is_open
    assert fake.writes == [command(72)]


def test_newer_firmware_is_refused():
    fake = FakeWavePal(firmware_version=2)
    error = expect_error(connect, fake)
    assert "Future firmware" in str(error)
    assert not fake.is_open


def test_load_waveform_sends_dac_codes_for_the_output_range():
    device, fake = connect()
    device.load_waveform(2, [-10, 0, 10, 5])
    assert fake.writes == [
        command("L", struct.pack("<BI", 2, 4) + struct.pack("<4H", 0, 32768, 65535, 49151))
    ]
    assert device.waveforms[2].tolist() == [-10, 0, 10, 5]
    assert device.waveforms[1] is None

    device, fake = connect()
    device.output_range = "0V:5V"
    fake.writes.clear()
    device.load_waveform(3, np.array([0, 2.5, 5]))
    assert fake.writes[0][-6:] == struct.pack("<3H", 0, 32768, 65535)


def test_load_waveform_validates_before_sending():
    device, fake = connect()
    for channel in (0, 5, True, 1.0, "1"):
        expect_error(device.load_waveform, channel, [1, 2])
    expect_error(device.load_waveform, 1, [])
    expect_error(device.load_waveform, 1, [0, 10.01])      # Outside -10 V to 10 V
    expect_error(device.load_waveform, 1, [0, float("nan")])
    device.info.max_samples = 3
    expect_error(device.load_waveform, 1, [1, 2, 3, 4])
    assert fake.writes == []


def test_a_rejected_load_leaves_the_channel_empty():
    device, fake = connect()
    device.load_waveform(1, [1, 2, 3])
    fake.ack = 0
    error = expect_error(device.load_waveform, 1, [4, 5])
    assert "rejected" in str(error)
    assert device.waveforms[1] is None


def test_waveforms_are_read_only_copies():
    device, _ = connect()
    samples = np.array([1.0, 2.0])
    device.load_waveform(1, samples)
    samples[0] = 5
    assert device.waveforms[1].tolist() == [1, 2]
    expect_error(device.waveforms[1].__setitem__, 0, 3, error=ValueError)


def test_play_stop_and_fixed_voltage_messages():
    device, fake = connect()
    device.play(1)
    device.play([2, 4])
    device.play(np.array([3]))
    device.stop()
    device.stop(3)
    device.stop([1, 2])
    device.set_fixed_voltage([1, 4], 5)
    assert fake.writes == [
        command("P", bytes([0b0001])),
        command("P", bytes([0b1010])),
        command("P", bytes([0b0100])),
        command("X", bytes([0b1111])),
        command("X", bytes([0b0100])),
        command("X", bytes([0b0011])),
        command("!", struct.pack("<BH", 0b1001, 49151)),
    ]
    for channels in (0, 5, [], [1, 7], True):
        expect_error(device.play, channels)
    expect_error(device.set_fixed_voltage, 1, 11)
    assert len(fake.writes) == 7


def test_sampling_rate_resends_loop_durations_in_samples():
    device, fake = connect()
    device.loop_duration[1] = 2
    assert fake.writes[-1] == command("D", struct.pack("<4I", 20000, 0, 0, 0))
    fake.writes.clear()
    device.sampling_rate = 50000
    assert fake.writes == [
        command("S", struct.pack("<I", 50000)),
        command("D", struct.pack("<4I", 100000, 0, 0, 0)),
    ]
    assert device.sampling_rate == 50000


def test_sampling_rate_must_be_a_whole_number_of_hz_in_range():
    device, fake = connect()
    for rate in (0, 100001, 44100.5, "44100", True, None):
        expect_error(setattr, device, "sampling_rate", rate)
    device.sampling_rate = 44100.0
    assert device.sampling_rate == 44100
    assert fake.writes[0] == command("S", struct.pack("<I", 44100))


def test_actual_sampling_rate_and_loop_durations_use_the_24mhz_divider():
    device, fake = connect()
    device.sampling_rate = 44100
    assert abs(device.actual_sampling_rate - CLOCK_HZ / 544) < 1e-9
    device.sampling_rate = 100000
    assert device.actual_sampling_rate == 100000
    device.sampling_rate = 44100
    fake.writes.clear()
    device.loop_duration[1:5] = [1, 0, 1e-9, 0.5]
    # 1 s at 44117.6 Hz, 0 = loop until stopped, a tiny duration lasts one sample
    assert fake.writes == [command("D", struct.pack("<4I", 44118, 0, 1, 22059))]
    assert device.loop_duration == [None, 1.0, 0.0, 1e-9, 0.5]


def test_loop_durations_that_do_not_fit_are_refused():
    device, fake = connect()
    expect_error(setattr, device, "loop_duration", -1)
    expect_error(setattr, device, "loop_duration", float("inf"))
    expect_error(setattr, device, "loop_duration", "1")
    device.loop_duration[1] = 400000  # 4e9 samples at 10 kHz: fits in uint32
    fake.writes.clear()
    # At 100 kHz the same duration no longer fits, so the rate is not changed
    expect_error(setattr, device, "sampling_rate", 100000)
    assert fake.writes == []
    assert device.sampling_rate == 10000


def test_channel_settings_are_indexed_by_channel_number():
    device, fake = connect()
    device.loop_mode[2] = True
    assert fake.writes == [command("O", bytes([0, 1, 0, 0]))]
    assert device.loop_mode == [None, False, True, False, False]
    device.loop_mode[1:5] = [1, 0, 0, np.bool_(True)]
    assert device.loop_mode == [None, True, False, False, True]
    device.loop_mode = [False] * 4           # 4 values: channels 1-4
    device.loop_mode = [None, True, False, True, False]  # 5 values: index 0 unused
    device.loop_mode = False                 # one value for all channels
    assert fake.writes[-1] == command("O", bytes(4))
    assert all(type(v) is bool for v in device.loop_mode[1:])

    fake.writes.clear()
    expect_error(device.loop_mode.__setitem__, 0, True)
    expect_error(device.loop_mode.__setitem__, slice(1, 3), [True])
    expect_error(setattr, device, "loop_mode", [True, False])
    expect_error(setattr, device, "loop_mode", 2)
    for method, args in (("append", (1,)), ("pop", ()), ("sort", ()), ("clear", ())):
        expect_error(getattr(device.loop_mode, method), *args, error=TypeError)
    assert fake.writes == []
    assert type(copy.copy(device.loop_mode)) is list


def test_a_setting_the_device_rejects_is_left_unchanged():
    device, fake = connect()
    fake.ack = 0
    expect_error(device.loop_mode.__setitem__, 1, True)
    assert device.loop_mode == [None, False, False, False, False]


def test_trigger_modes_and_links():
    device, fake = connect()
    device.trigger_mode[1] = "master"
    device.trigger_mode[2:4] = ["TOGGLE", "Gated"]
    assert fake.writes == [
        command("T", bytes([1, 0, 0, 0])),
        command("T", bytes([1, 2, 3, 0])),
    ]
    assert device.trigger_mode == [None, "Master", "Toggle", "Gated", "Normal"]
    expect_error(device.trigger_mode.__setitem__, 1, "Restart")
    expect_error(device.trigger_mode.__setitem__, 1, 1)

    fake.writes.clear()
    device.link_trigger_channel2[3] = True    # Both trigger channels travel together
    device.link_trigger_channel1 = [True, False, False, True]
    assert fake.writes == [
        command("I", bytes([1, 1, 1, 1, 0, 0, 1, 0])),
        command("I", bytes([1, 0, 0, 1, 0, 0, 1, 0])),
    ]


def test_changing_the_output_range_reloads_the_waveforms():
    device, fake = connect()
    device.load_waveform(1, [0, 5])
    device.load_waveform(3, [-2, 2])
    fake.writes.clear()
    device.output_range = "-5V:5V"
    assert fake.ops() == ["R", "L", "L"]
    assert fake.writes[0] == command("R", bytes([2]))
    assert fake.loaded[1].tolist() == [32768, 65535]
    assert fake.loaded[3].tolist() == [round(3 / 10 * 65535), round(7 / 10 * 65535)]

    # The same range again changes nothing on the device, so nothing is reloaded
    fake.writes.clear()
    device.output_range = "-5v:5v"
    assert fake.ops() == ["R"]
    assert device.output_range == "-5V:5V"


def test_a_range_that_does_not_fit_a_waveform_is_refused():
    device, fake = connect()
    device.load_waveform(2, [-1, 7])
    fake.writes.clear()
    error = expect_error(setattr, device, "output_range", "0V:10V")
    assert "channel 2" in str(error)
    for unknown_range in ("0V:12V", "0V:10.8V", "-10.8V:10.8V"):
        expect_error(setattr, device, "output_range", unknown_range)
    assert fake.writes == []
    assert device.output_range == "-10V:10V"


def test_status_is_unpacked():
    device, fake = connect()
    fake.status_reply = struct.pack("<B4I4II", 0b0101, 100, 0, 2000000 // 2, 7,
                                    0, 3, 0, 0, 4250)
    status = device.status()
    assert fake.writes == [command("G")]
    assert status.playing == [1, 3]
    assert status.samples_loaded == [None, 100, 0, 1000000, 7]
    assert status.underruns == [None, 0, 3, 0, 0]
    assert status.longest_interrupt_us == 4.25


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


def test_a_failed_connection_sends_the_disconnect_op_only_to_a_wave_pal():
    fake = FakeWavePal(ack=0)  # A Wave Pal that rejects the default settings
    expect_error(connect, fake)
    assert fake.writes[-1] == command(81)
    assert not fake.is_open

    # A Pulse Pal, where op 81 would stop playback
    fake = FakeWavePal(handshake_reply=75, firmware_version=22)
    expect_error(connect, fake)
    assert command(81) not in fake.writes


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
