"""Offline tests for the bytes PulsePalDevice sends to the device.

These tests need no Pulse Pal: a fake serial port records what the class writes,
and replies to acknowledgement reads. They are a fast check that the class still
matches the firmware's serial protocol, which is documented in
/Firmware/PROTOCOL.md.

Run them with:

    python tests/test_protocol.py

They also run under pytest, if it is installed:

    python -m pytest tests
"""
import struct
import sys
import warnings
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from pulsepal import _common, pulse_pal  # noqa: E402
from pulsepal import PulsePalDevice, PulsePalError  # noqa: E402

OP_MENU_BYTE = 213
# The replies of a Pulse Pal 3 to op 94 (hardware info), op 100 (circuit board revision, here 5) and op 102 (USB
# serial number, here 20345420)
PULSE_PAL_3_INFO = struct.pack("<BIBI", 3, 50, 4, 10000) + bytes([5]) + bytes([8]) + b"20345420"


class FakePort:
    """Records written bytes and read sizes, and replies to reads.

    Bytes placed in `response` are returned first; after that, reads return the
    acknowledgement byte. They count as sent by the device only when they are
    read, so `in_waiting` is 0: the handshake reads exactly its 5 bytes.

    With `answers_op_103` False, the port models firmware that ignores op 103 (Pulse Pal firmware from before
    October 2026, Wave Pal and Synth Pal): after op 103, reads return nothing until op 72 is written.
    """

    in_waiting = 0

    def __init__(self, ack=1, answers_op_103=True):
        self.port = "FAKE"
        self.timeout = 1
        self.writes = []
        self.reads = []
        self.ack = ack
        self.answers_op_103 = answers_op_103
        self.silent = False
        self.response = bytearray()
        self.resets = []  # The number of writes before each reset_input_buffer()

    def reset_input_buffer(self):
        # `response` holds replies to commands not yet sent, so it is kept
        self.resets.append(len(self.writes))

    def write(self, data):
        self.writes.append(bytes(data))
        if bytes(data) == bytes([OP_MENU_BYTE, 103]):
            self.silent = not self.answers_op_103
        elif bytes(data) == bytes([OP_MENU_BYTE, 72]):
            self.silent = False
        return len(data)

    def read(self, n):
        self.reads.append(n)
        if self.silent:
            return b""
        if self.response:
            reply = bytes(self.response[:n])
            del self.response[:n]
            if len(reply) < n:
                reply += bytes([self.ack]) * (n - len(reply))
            return reply
        return bytes([self.ack]) * n


def make_device(firmware_version=22, hardware_version=3, n_trains=4, max_pulses=10000, ack=1):
    """Build a PulsePalDevice that talks to a fake port, without connecting."""
    device = PulsePalDevice.__new__(PulsePalDevice)
    device._gui = None
    device._auto_sync = True
    device.info = pulse_pal.DeviceInfo()
    device.info.firmware_version = firmware_version
    device.info.hardware_version = hardware_version
    device.info.cycle_frequency = 20000
    device.info.cycle_period_us = 50
    device.info.min_pulse_width_us = PulsePalDevice._MIN_PULSE_CYCLES[hardware_version] * 50
    device.info.n_custom_pulse_trains = n_trains
    device.info.max_custom_pulses = max_pulses
    if not device._has_param_sync():
        device.info.trigger_modes = pulse_pal.TRIGGER_MODES[:3]
    device._create_settings()
    device.port = FakePort(ack=ack)
    device._closed = True  # Stops __del__ from writing to the port
    return device


def volts_to_bits(volts):
    return round((volts + 10) / 20 * 65535)


def expect_error(function, *args, error=PulsePalError, **kwargs):
    try:
        function(*args, **kwargs)
    except error as exc:
        return exc
    raise AssertionError(f"{function} did not raise {error}")


def test_parameter_names_match_parameter_codes():
    """The names and the codes must agree, or commands carry the wrong code. The codes are fixed
    (see "Parameter codes" in /Firmware/PROTOCOL.md)."""
    device = make_device()
    expected = ["is_biphasic", "phase1_voltage", "phase2_voltage", "phase1_duration", "inter_phase_interval",
                "phase2_duration", "inter_pulse_interval", "burst_duration", "inter_burst_interval",
                "pulse_train_duration", "pulse_train_delay", "link_trigger_channel1", "link_trigger_channel2",
                "custom_train_id", "custom_train_target", "custom_train_loop", "resting_voltage",
                "continuous_loop"]
    assert list(device.info.output_parameter_names) == expected
    assert {code: name for code, (name, _) in pulse_pal._OUTPUT_PARAMETERS.items()} == \
        dict(enumerate(expected, start=1))
    assert pulse_pal._TRIGGER_MODE_CODE == 128


def test_connection_programs_the_defaults():
    """Connecting reads the hardware info (op 94) and the circuit board revision (op 100), takes both
    trigger channels out of param sync mode (Pulse Pal 3), then sends the default parameters in one
    op 92."""
    port = ClosablePort()
    port.response = bytearray([75]) + struct.pack("<I", 22) + PULSE_PAL_3_INFO
    original_serial = pulse_pal.serial.Serial
    opened = []
    pulse_pal.serial.Serial = lambda *args, **kwargs: opened.append(kwargs) or port
    try:
        device = PulsePalDevice("COM9")
    finally:
        pulse_pal.serial.Serial = original_serial
    # Locked for this session on Linux and macOS too: another session that locks the port cannot open it
    assert opened[0].get("exclusive") is True, opened
    assert port.writes[:6] == [
        bytes([OP_MENU_BYTE, 103]),  # Times in the device's own cycles (see test_handshake_falls_back_to_op_72())
        bytes([OP_MENU_BYTE, 94]),
        bytes([OP_MENU_BYTE, 100]),
        bytes([OP_MENU_BYTE, 102]),
        bytes([OP_MENU_BYTE, 89]) + b"PYTHON",
        bytes([OP_MENU_BYTE, 91, 128, 0, 0]),
    ], port.writes
    assert port.writes[6][:2] == bytes([OP_MENU_BYTE, 92]) and len(port.writes) == 7
    assert port.resets == [0], "unread input was not discarded before the handshake"
    assert device.info.hardware_version == 3 and device.info.min_pulse_width_us == 50
    assert device.info.circuit_board_revision == 5 and not port.response, "a reply was left unread"
    assert device.info.serial_number == "20345420"
    assert device.info.trigger_modes == ("Normal", "Toggle", "Gated", "Param Sync")
    assert device.is_biphasic == [None, False, False, False, False]
    assert device.phase1_voltage == [None, 5, 5, 5, 5]
    assert device.custom_train_target == [None] + ["Pulses"] * 4
    assert device.trigger_mode == [None, "Normal", "Normal"]
    device.close()
    assert port.writes[-1] == bytes([OP_MENU_BYTE, 81]) and not port.is_open


def test_handshake_falls_back_to_op_72():
    """The class connects with op 103, so that the session's times are in the device's own cycles (10 us on Pulse
    Pal 3). Firmware from before October 2026 ignores op 103, and its times are in 50 us cycles: the class then
    connects with op 72, and takes the cycle that op 94 reports, 50 us."""
    port = ClosablePort(answers_op_103=False)
    port.response = bytearray([75]) + struct.pack("<I", 22) + PULSE_PAL_3_INFO
    device = connect_to(port)
    assert port.writes[:3] == [bytes([OP_MENU_BYTE, 103]), bytes([OP_MENU_BYTE, 72]), bytes([OP_MENU_BYTE, 94])], \
        port.writes
    assert port.timeout == 1, "the port's timeout was not restored after op 103"
    assert device.info.cycle_period_us == 50 and device.info.cycle_frequency == 20000
    port.writes.clear()
    device.phase1_duration[1] = 0.0002
    assert port.writes == [bytes([OP_MENU_BYTE, 74, 4, 1]) + struct.pack("<I", 4)], port.writes
    device.close()


def test_a_10_us_cycle_converts_exactly():
    """Pulse Pal 3 runs a 10 us cycle. The cycle rate is 1e6 / period, exactly 100000: 1 / (10 / 1e6) is
    99999.99999999999, and 10 cycles read back from the device would be 0.00010000000000000002 s."""
    port = ClosablePort()
    port.response = bytearray([75]) + struct.pack("<I", 22) + struct.pack("<BIBI", 3, 10, 4, 10000) \
        + bytes([1]) + bytes([0])
    device = connect_to(port)
    assert device.info.cycle_frequency == 100000 and device.info.min_pulse_width_us == 10
    assert device._cycles_to_seconds(10) == 0.0001 and device._cycles_to_seconds(12) == 0.00012
    assert device._max_time_cycles() == 999999990  # 9999.9999 s, as the firmware's MAX_TIME_CYCLES
    port.writes.clear()
    device.phase1_duration[1] = 0.000123  # Rounds to 12 cycles
    assert port.writes == [bytes([OP_MENU_BYTE, 74, 4, 1]) + struct.pack("<I", 12)], port.writes
    assert device.phase1_duration[1] == 0.00012
    port.writes.clear()
    device.send_custom_pulse_train(1, [0, 0.00001, 0.00003], [1, 2, 3])
    assert port.writes[0][:7] == bytes([OP_MENU_BYTE, 95, 0]) + struct.pack("<I", 3), port.writes
    assert port.writes[0][7:19] == struct.pack("<III", 0, 1, 3), port.writes
    device.close()


def connect_to(port, *args, **kwargs):
    """A PulsePalDevice connected to a fake port."""
    original_serial = pulse_pal.serial.Serial
    pulse_pal.serial.Serial = lambda *a, **k: port
    try:
        return PulsePalDevice("COM9", *args, **kwargs)
    finally:
        pulse_pal.serial.Serial = original_serial


def test_connecting_by_usb_serial_number():
    """serial_number= opens the port whose device has that USB serial number (an int or a str), and
    info.serial_number holds the one the device reports (op 102). Exactly one of the port name and the
    serial number is given."""
    class PortInfo:
        def __init__(self, device, serial_number):
            self.device = device
            self.serial_number = serial_number

    original_comports = _common.serial.tools.list_ports.comports
    original_serial = pulse_pal.serial.Serial
    opened = []
    port = ClosablePort()
    port.response = bytearray([75]) + struct.pack("<I", 22) + PULSE_PAL_3_INFO
    _common.serial.tools.list_ports.comports = lambda: [PortInfo("COM4", None), PortInfo("COM9", "20345420"),
                                                         PortInfo("COM12", "20885790")]
    pulse_pal.serial.Serial = lambda name, *args, **kwargs: opened.append(name) or port
    try:
        device = PulsePalDevice(serial_number=20345420)
        assert opened == ["COM9"] and device.info.serial_number == "20345420"
        device.close()
        error = expect_error(PulsePalDevice, serial_number="1234")
        assert "COM9 (20345420), COM12 (20885790)" in str(error), error
        for args, kwargs in (((), {}), (("COM9",), {"serial_number": "20345420"})):
            error = expect_error(PulsePalDevice, *args, **kwargs)
            assert "not both" in str(error), error
        assert opened == ["COM9"], "a port was opened for a refused connection"
    finally:
        _common.serial.tools.list_ports.comports = original_comports
        pulse_pal.serial.Serial = original_serial


def test_serialportlist_skips_ports_another_session_holds():
    """serialportlist() ("available") probes each USB port with an exclusive open, as the classes open theirs: on
    Linux and macOS, a port another session holds, locked, then counts as in use, as any open port does on Windows."""
    class PortInfo:
        def __init__(self, device):
            self.device = device
            self.vid = 0x16C0
            self.hwid = "USB VID:PID=16C0:0483"
            self.serial_number = None

    probes = []

    class ProbePort:
        def __init__(self):
            self.port = None
            self.dtr = self.rts = True
            self.exclusive = None

        def open(self):
            probes.append((self.port, self.exclusive))
            if self.port == "COM5":
                raise _common.serial.SerialException("Could not exclusively lock port COM5")

        def close(self):
            pass

    original_comports = _common.serial.tools.list_ports.comports
    original_serial = _common.serial.Serial
    _common.serial.tools.list_ports.comports = lambda: [PortInfo("COM4"), PortInfo("COM5")]
    _common.serial.Serial = ProbePort
    try:
        assert PulsePalDevice.serialportlist() == ["COM4"]
        assert PulsePalDevice.serialportlist("all") == ["COM4", "COM5"]
    finally:
        _common.serial.tools.list_ports.comports = original_comports
        _common.serial.Serial = original_serial
    assert probes == [("COM4", True), ("COM5", True)], probes


def test_other_firmware_versions_warn():
    """Newer firmware only adds commands, so the class uses it, with a warning. Older supported firmware
    warns that an update is available. Neither prints."""
    for version, text in ((23, "newer than this pulsepal package knows"), (21, "v22 is available")):
        port = ClosablePort()
        hardware = PULSE_PAL_3_INFO if version > 21 else b""
        port.response = bytearray([75]) + struct.pack("<I", version) + hardware
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            device = connect_to(port)
        assert any(text in str(w.message) for w in caught), [str(w.message) for w in caught]
        assert device.info.firmware_version == version
        # Firmware v21 has no op 100, and runs on Pulse Pal 2, whose circuit board encodes no revision
        assert device.info.circuit_board_revision == (5 if version > 21 else 0)
        assert (bytes([OP_MENU_BYTE, 100]) in port.writes) == (version > 21)
        device.close()


def test_the_public_surface_is_trimmed():
    """No baud rate (USB ignores it), a keyword-only timeout, close() without arguments, and no
    bytes_available()."""
    port = ClosablePort()
    port.response = bytearray([75]) + struct.pack("<I", 22) + PULSE_PAL_3_INFO
    expect_error(connect_to, port, 12000000, error=TypeError)
    device = connect_to(port, timeout=2)
    expect_error(device.close, True, error=TypeError)
    for name in ("bytes_available", "set_output_param", "set_trigger_param"):
        assert not hasattr(device, name), name
    device.close()


def test_assigning_a_parameter_programs_the_device():
    """With auto_sync on (the default), an assignment sends the channels whose value it changes: op 74 for
    each, or op 91 when all four change, whether it is to one element, a slice or the whole list. The
    other channels keep what the device holds, e.g. values set with the joystick, which sending all four
    would undo."""
    device = make_device()
    device.phase1_voltage[2] = 7
    device.inter_pulse_interval[1:5] = [0.2] * 4
    device.phase1_duration = [0.002] * 4           # The whole list
    device.is_biphasic[3] = True
    device.trigger_mode[2] = "Toggle"
    device.phase1_voltage[1:5] = [6, 7, 5.00001, 4]  # Channels 1 and 4: 5.00001 V is 5 V's DAC code
    device.trigger_mode = ["Gated", "Normal"]
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 74, 2, 2]) + struct.pack("<H", volts_to_bits(7)),
        bytes([OP_MENU_BYTE, 91, 7]) + struct.pack("<4I", *[4000] * 4),
        bytes([OP_MENU_BYTE, 91, 4]) + struct.pack("<4I", *[40] * 4),
        bytes([OP_MENU_BYTE, 74, 1, 3, 1]),
        bytes([OP_MENU_BYTE, 74, 128, 2, 1]),
        bytes([OP_MENU_BYTE, 74, 2, 1]) + struct.pack("<H", volts_to_bits(6)),
        bytes([OP_MENU_BYTE, 74, 2, 4]) + struct.pack("<H", volts_to_bits(4)),
        bytes([OP_MENU_BYTE, 91, 128, 2, 0]),
    ]
    assert device.phase1_voltage == [None, 6, 7, 5.00001, 4]
    assert device.phase1_duration == [None, 0.002, 0.002, 0.002, 0.002]

    # A value a channel already holds sends nothing
    n_writes = len(device.port.writes)
    device.phase1_voltage[2] = 7
    device.trigger_mode[1] = "gated"
    device.is_biphasic = [False, False, True, False]
    assert len(device.port.writes) == n_writes


def test_assigning_on_firmware_v21_uses_op_74_per_channel():
    device = make_device(firmware_version=21, hardware_version=2, n_trains=2, max_pulses=5000)
    device.resting_voltage[1] = 1
    device.trigger_mode[1] = "Gated"
    device.phase1_voltage = [1, 2, 3, 4]
    assert [w[:4] for w in device.port.writes] == [
        bytes([OP_MENU_BYTE, 74, 17, 1]), bytes([OP_MENU_BYTE, 74, 128, 1])
    ] + [bytes([OP_MENU_BYTE, 74, 2, channel]) for channel in (1, 2, 3, 4)]
    expect_error(device.continuous_loop.__setitem__, 1, True)  # Firmware v22 or newer


def test_auto_sync_off_keeps_assignments_until_sync_to_device():
    device = make_device()
    device.auto_sync = False
    device.phase1_voltage[1] = 2.5
    device.custom_train_target[2] = "Bursts"     # Before its burst duration: checked by sync_to_device()
    device.burst_duration[2] = 0.5
    device.trigger_mode = ["Gated", "Gated"]
    assert device.port.writes == []
    device.sync_to_device()
    assert len(device.port.writes) == 1 and device.port.writes[0][:2] == bytes([OP_MENU_BYTE, 92])
    message = device.port.writes[0][2:]
    assert len(message) == 182
    times, voltages, single_bytes = message[:128], message[128:152], message[152:]
    assert struct.unpack("<4H", voltages[:8])[0] == volts_to_bits(2.5)
    assert list(single_bytes[8:12]) == [0, 1, 0, 0]          # Custom train targets
    assert list(single_bytes[-2:]) == [2, 2]                  # Trigger modes
    expect_error(setattr, device, "auto_sync", "yes")


def test_batch_sends_everything_in_one_command():
    device = make_device()
    with device.batch():
        device.phase1_voltage = [1, 2, 3, 4]
        device.burst_duration[2] = 0.5
        assert device.port.writes == []
    assert len(device.port.writes) == 1 and device.port.writes[0][:2] == bytes([OP_MENU_BYTE, 92])
    assert device.auto_sync is True
    assert device.phase1_voltage == [None, 1, 2, 3, 4]


def test_a_failed_batch_sends_nothing_and_restores_the_parameters():
    device = make_device()
    try:
        with device.batch():
            device.phase1_voltage[1] = 1
            raise RuntimeError("stop here")
    except RuntimeError:
        pass
    assert device.port.writes == []
    assert device.phase1_voltage == [None, 5, 5, 5, 5]
    assert device.auto_sync is True


def test_set_default_params_programs_the_device_with_auto_sync_off():
    device = make_device()
    device.auto_sync = False
    device.phase1_voltage = [1] * 4
    device.set_default_params()
    assert device.port.writes[0] == bytes([OP_MENU_BYTE, 91, 128, 0, 0])
    assert device.port.writes[1][:2] == bytes([OP_MENU_BYTE, 92]) and len(device.port.writes) == 2
    assert device.phase1_voltage == [None, 5, 5, 5, 5]
    assert device.auto_sync is False

    device = make_device(firmware_version=22, hardware_version=2)  # No param sync mode to leave
    device.set_default_params()
    assert [w[1] for w in device.port.writes] == [92]


def test_parameters_take_names_and_booleans():
    device = make_device()
    device.trigger_mode[1] = "param sync"
    device.trigger_mode[2] = 2                     # The integer codes still work
    device.burst_duration[1:3] = [0.1, 0.1]        # Custom train times that are burst onsets need bursts
    device.custom_train_target[1] = "bursts"
    device.custom_train_target[2] = 1
    device.is_biphasic[1] = 1
    device.continuous_loop[4] = np.bool_(True)
    assert device.trigger_mode == [None, "Param Sync", "Gated"]
    assert device.custom_train_target == [None, "Bursts", "Bursts", "Pulses", "Pulses"]
    assert device.is_biphasic == [None, True, False, False, False]
    assert device.continuous_loop == [None, False, False, False, True]
    n_writes = len(device.port.writes)
    for name, bad in (("trigger_mode", "Master"), ("trigger_mode", 4), ("trigger_mode", True),
                      ("custom_train_target", "Trains"), ("is_biphasic", 2), ("is_biphasic", "yes"),
                      ("custom_train_id", 1.5), ("custom_train_id", 5)):
        expect_error(getattr(device, name).__setitem__, 1, bad)
    assert len(device.port.writes) == n_writes

    pulse_pal_2 = make_device(hardware_version=2, n_trains=2)
    error = expect_error(pulse_pal_2.trigger_mode.__setitem__, 1, "Param Sync")
    assert "'Normal', 'Toggle', 'Gated'" in str(error), error
    expect_error(pulse_pal_2.trigger_mode.__setitem__, 1, 3)


def test_parameter_lists_are_indexed_by_channel_number():
    device = make_device()
    expect_error(device.phase1_voltage.__setitem__, 0, 5)
    expect_error(device.phase1_voltage.__setitem__, slice(1, 3), [1])
    expect_error(setattr, device, "phase1_voltage", [1, 2])
    expect_error(device.phase1_voltage.append, 1, error=TypeError)
    expect_error(device.trigger_mode.__setitem__, 3, "Normal", error=IndexError)
    assert device.port.writes == []
    device.phase1_voltage = [None, 1, 2, 3, 4]      # Index 0 unused, as the list prints
    assert device.phase1_voltage == [None, 1, 2, 3, 4]


def test_a_single_value_for_a_whole_parameter_is_refused():
    """A single value does not say which channels it is meant for, so the whole list takes one
    value per channel, in every class (see "One way to use all six" in /AGENTS.md)."""
    device = make_device()
    for name, value in (("phase1_voltage", 5), ("phase1_duration", 0.002), ("is_biphasic", True),
                        ("trigger_mode", "Normal"), ("custom_train_target", "Pulses"),
                        ("phase1_voltage", np.float64(5))):
        error = expect_error(setattr, device, name, value)
        assert f"{name}[1] = " in str(error), error
    assert device.port.writes == []
    device.phase1_voltage = np.array([1, 2, 3, 4])
    assert device.phase1_voltage == [None, 1, 2, 3, 4]


def test_times_hold_the_value_the_device_plays():
    """Times are rounded to the device's 50 us timer cycle, and the list holds the rounded time."""
    device = make_device()
    device.phase1_duration[1] = 0.00012     # 2.4 cycles: plays 100 us
    device.pulse_train_delay[2] = 0.000175  # 3.5 cycles: plays 200 us (halfway, to the even cycle)
    device.burst_duration[3] = 1.00004
    assert device.phase1_duration[1] == 0.0001
    assert device.pulse_train_delay[2] == 0.0002
    assert device.burst_duration[3] == 1.00005
    # 0.00012 s plays as the default 0.0001 s, so it sends nothing
    assert device.port.writes[0] == bytes([OP_MENU_BYTE, 74, 11, 2]) + struct.pack("<I", 4)


def test_times_are_at_most_9999_9999_seconds():
    """The longest time the device's joystick menu shows: 199999998 cycles of 50 us."""
    device = make_device()
    device.pulse_train_duration[1] = 9999.9999
    device.pulse_train_delay[1] = 9999.99992     # Rounds to 9999.9999
    assert device.pulse_train_delay[1] == 9999.9999
    assert device.port.writes[0] == bytes([OP_MENU_BYTE, 74, 10, 1]) + struct.pack("<I", 199999998)
    n_writes = len(device.port.writes)
    for bad in (10000, 9999.99995, 3e5):
        error = expect_error(device.pulse_train_duration.__setitem__, 1, bad)
        assert "at most 9999.9999 s" in str(error), error
    expect_error(device.send_custom_pulse_train, 1, [0, 10000], [1, 2])
    expect_error(device.send_custom_waveform, 1, 1, [1] * 10001)  # The last sample would start at 10000 s
    assert len(device.port.writes) == n_writes
    device.send_custom_pulse_train(1, [0, 9999.9999], [1, 2])
    assert device.port.writes[-1][7:15] == struct.pack("<2I", 0, 199999998)

def test_print_shows_every_parameter():
    device = make_device()
    text = repr(device)
    assert "Pulse Pal 3, firmware v22" in text
    for name in (*device.info.output_parameter_names, "trigger_mode", "auto_sync"):
        assert f"{name}: " in text, name


def test_set_fixed_voltage_uses_op_79_per_channel():
    device = make_device()
    device.set_fixed_voltage(4, 2.5)
    device.set_fixed_voltage([1, 3], -10)
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 79, 4]) + struct.pack("<H", volts_to_bits(2.5)),
        bytes([OP_MENU_BYTE, 79, 1]) + struct.pack("<H", 0),
        bytes([OP_MENU_BYTE, 79, 3]) + struct.pack("<H", 0),
    ]


def test_custom_train_uses_op_95_with_a_zero_based_index():
    for train_id in (1, 2, 3, 4):
        device = make_device()
        device.send_custom_pulse_train(train_id, [0, 0.2], [5, -5])
        assert device.port.writes == [
            bytes([OP_MENU_BYTE, 95, train_id - 1])
            + struct.pack("<I", 2)
            + struct.pack("<2I", 0, 4000)
            + struct.pack("<2H", volts_to_bits(5), volts_to_bits(-5))
        ]


def test_custom_train_uses_legacy_ops_on_firmware_v21():
    for train_id in (1, 2):
        device = make_device(firmware_version=21, hardware_version=2, n_trains=2, max_pulses=5000)
        device.send_custom_pulse_train(train_id, [0], [5])
        assert device.port.writes[0][:2] == bytes([OP_MENU_BYTE, 74 + train_id])


def test_custom_train_rejects_bad_ids_and_oversized_trains():
    cases = [
        (4, 10000, 0),      # Train 0 does not exist
        (4, 10000, 5),      # Only 4 trains on this device
        (2, 5000, 3),       # Only 2 trains on a Pulse Pal 2
        (4, 10000, 1.5),    # Not a whole number
        (4, 10000, "1"),    # Not a number
    ]
    for n_trains, max_pulses, train_id in cases:
        device = make_device(n_trains=n_trains, max_pulses=max_pulses)
        expect_error(device.send_custom_pulse_train, train_id, [0], [5])
        assert device.port.writes == []

    device = make_device(max_pulses=3)
    expect_error(device.send_custom_pulse_train, 1, [0, 1, 2, 3], [1, 2, 3, 4])
    assert device.port.writes == []


def test_custom_train_rejects_times_that_do_not_increase():
    """A pulse time that is not later than the one before it freezes the device's
    output for the rest of the train, so the class must not send it. Times must also be
    two cycles apart, so that a trigger channel can detect each pulse."""
    cases = [
        [0, 0.2, 0.2],          # Duplicate
        [0, 0.2, 0.1],          # Decreasing
    ]
    for pulse_times in cases:
        device = make_device()
        error = expect_error(device.send_custom_pulse_train, 1, pulse_times, [1] * len(pulse_times))
        assert "must increase" in str(error)
        assert device.port.writes == []

    device = make_device()
    error = expect_error(device.send_custom_waveform, 1, 0, [1, 2, 3])
    assert "positive multiple of 50 us" in str(error), error
    assert device.port.writes == []


def test_custom_pulse_times_are_multiples_of_the_minimum_pulse():
    """As in the MATLAB class: a custom pulse time or sampling period between two steps of
    DeviceInfo.min_pulse_width_us (100 us on Pulse Pal 2, 50 us on Pulse Pal 3) is refused, not
    rounded."""
    for call in (
        lambda d: d.send_custom_pulse_train(1, [0, 0.00001], [1, 2]),
        lambda d: d.send_custom_pulse_train(1, [0, 0.00005, 0.0001], [1, 2, 3]),   # One 50 us cycle
        lambda d: d.send_custom_pulse_train(1, [0, 0.0001, 0.00025], [1, 2, 3]),
        lambda d: d.send_custom_pulse_train(1, [0.00012], [1]),
        lambda d: d.send_custom_waveform(1, 0.00005, [1, 2, 3]),
        lambda d: d.send_custom_waveform(1, 0.00015, [1, 2, 3]),
    ):
        device = make_device(hardware_version=2, n_trains=2, max_pulses=5000)
        error = expect_error(call, device)
        assert "multiples of 100 us" in str(error), error
        assert device.port.writes == []

    device = make_device(hardware_version=2, n_trains=2, max_pulses=5000)
    device.send_custom_pulse_train(1, np.arange(5) * 0.0001 + 0.0003, [1, 2, 3, 4, 5])  # Float error is fine
    device.send_custom_waveform(custom_train_id=2, sampling_period=0.0002, voltages=[1, 2, 3])
    assert device.port.writes[0][7:27] == struct.pack("<5I", 6, 8, 10, 12, 14)
    assert device.port.writes[1][7:19] == struct.pack("<3I", 0, 4, 8)

    for call in (
        lambda d: d.send_custom_pulse_train(1, [0, 0.00001], [1, 2]),
        lambda d: d.send_custom_pulse_train(1, [0, 0.0001, 0.00012], [1, 2, 3]),
        lambda d: d.send_custom_pulse_train(1, [0.000075], [1]),
        lambda d: d.send_custom_waveform(1, 0.000025, [1, 2, 3]),
    ):
        device = make_device()
        error = expect_error(call, device)
        assert "multiples of 50 us" in str(error), error
        assert device.port.writes == []

    device = make_device()
    device.send_custom_pulse_train(1, np.arange(5) * 0.00005 + 0.00015, [1, 2, 3, 4, 5])  # Float error is fine
    device.send_custom_waveform(custom_train_id=2, sampling_period=0.00005, voltages=[1, 2, 3])
    assert device.port.writes[0][7:27] == struct.pack("<5I", 3, 4, 5, 6, 7)
    assert device.port.writes[1][7:19] == struct.pack("<3I", 0, 1, 2)


def test_waveform_matches_an_equivalent_pulse_train():
    waveform_device = make_device()
    waveform_device.send_custom_waveform(2, 0.001, [1, 2, 3])
    train_device = make_device()
    train_device.send_custom_pulse_train(2, [0, 0.001, 0.002], [1, 2, 3])
    assert waveform_device.port.writes == train_device.port.writes


def test_a_rejected_command_raises():
    """The firmware replies 0 when it rejects a command (see /Firmware/PROTOCOL.md)."""
    device = make_device(ack=0)
    device.port.response = bytearray(bytes([0]) + parameter_message())
    error = expect_error(device.phase1_voltage.__setitem__, 1, 6)
    assert "rejected" in str(error)


def test_sync_to_device_uses_op_92_and_op_73_by_firmware_version():
    device = make_device()
    device.sync_to_device()
    assert device.port.writes[0][:2] == bytes([OP_MENU_BYTE, 92])
    legacy_device = make_device(firmware_version=21, hardware_version=2, n_trains=2, max_pulses=5000)
    legacy_device.sync_to_device()
    assert legacy_device.port.writes[0][:2] == bytes([OP_MENU_BYTE, 73])
    assert len(legacy_device.port.writes[0]) == 2 + 128 + 24 + 16 + 8 + 2


def test_trigger_and_stop_take_channel_numbers():
    """One channel number, or several as a list, tuple or NumPy array: nothing else."""
    device = make_device()
    device.trigger(2)
    device.trigger([1, 3])
    device.trigger((1, 3))
    device.trigger(np.array([1, 3]))
    device.trigger(np.int64(4))
    device.stop()
    device.stop([2])
    device.stop(np.array([1, 4]))
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 77, 0b0010]),
        bytes([OP_MENU_BYTE, 77, 0b0101]),
        bytes([OP_MENU_BYTE, 77, 0b0101]),
        bytes([OP_MENU_BYTE, 77, 0b0101]),
        bytes([OP_MENU_BYTE, 77, 0b1000]),
        bytes([OP_MENU_BYTE, 98, 0b1111]),
        bytes([OP_MENU_BYTE, 98, 0b0010]),
        bytes([OP_MENU_BYTE, 98, 0b1001]),
    ]


def test_trigger_and_stop_refuse_anything_else():
    for call, error in [
        (lambda d: d.trigger(5), PulsePalError),
        (lambda d: d.trigger(0), PulsePalError),
        (lambda d: d.trigger(1.0), PulsePalError),
        (lambda d: d.trigger(True), PulsePalError),
        (lambda d: d.trigger([]), PulsePalError),
        (lambda d: d.trigger("1010"), PulsePalError),
        (lambda d: d.trigger(), TypeError),
        (lambda d: d.trigger(1, 0, 1, 0), TypeError),   # The old flags, one per channel
        (lambda d: d.stop(5), PulsePalError),
        (lambda d: d.stop("1"), PulsePalError),
    ]:
        device = make_device()
        expect_error(call, device, error=error)
        assert device.port.writes == []
    legacy = make_device(firmware_version=21, hardware_version=2, n_trains=2, max_pulses=5000)
    expect_error(legacy.stop, [1])
    legacy.stop()
    assert legacy.port.writes == [bytes([OP_MENU_BYTE, 80])]


def test_values_the_device_cannot_play_raise_before_sending():
    calls = [
        lambda d: d.set_fixed_voltage(1, 12),
        lambda d: d.set_fixed_voltage(1, float("nan")),
        lambda d: d.set_fixed_voltage(5, 1),
        lambda d: d.phase1_voltage.__setitem__(1, 15),
        lambda d: setattr(d, "resting_voltage", [0, 0, -10.5, 0]),
        lambda d: d.phase1_duration.__setitem__(1, 0),
        lambda d: d.phase1_duration.__setitem__(1, 0.00002),   # Rounds to 0 cycles
        lambda d: d.phase2_duration.__setitem__(1, 0),
        lambda d: d.pulse_train_duration.__setitem__(1, 0.00002),
        lambda d: d.pulse_train_delay.__setitem__(1, -0.001),
        lambda d: d.pulse_train_duration.__setitem__(1, float("nan")),
        lambda d: d.inter_pulse_interval.__setitem__(1, 0),
        lambda d: d.is_biphasic.__setitem__(1, 2),
        lambda d: d.custom_train_id.__setitem__(1, 5),
        lambda d: d.custom_train_loop.__setitem__(1, 0.5),
        lambda d: d.trigger_mode.__setitem__(1, 4),
        lambda d: d.trigger_mode.__setitem__(3, 0),
        lambda d: d.send_custom_pulse_train(1, [0, 0.001], [5, 11]),
        lambda d: d.send_custom_waveform(1, 0.001, [0, float("nan")]),
        lambda d: d.phase1_voltage.__setitem__(3, 15),
        lambda d: d.phase1_voltage.__setitem__(3, "5"),
        lambda d: d.inter_pulse_interval.__setitem__(1, 0.00002),
    ]
    for call in calls:
        device = make_device()
        expect_error(call, device, error=(PulsePalError, IndexError))
        assert device.port.writes == []


def test_pulses_and_intervals_last_at_least_the_minimum_pulse():
    """The shortest pulse, as in the MATLAB class and the joystick menu: on Pulse Pal 3 one timer
    cycle (50 us), since its trigger channels latch every edge; on Pulse Pal 2 two (100 us), the
    shortest pulse its trigger channels, which read their inputs once per cycle, detect reliably.
    The limit is in cycles of the device's timer: one 25 us cycle on a 25 us timer. Times of 0
    still mean "off" where they did."""
    device = make_device(hardware_version=2, n_trains=2, max_pulses=5000)
    assert device.info.min_pulse_width_us == 100
    for name in ("phase1_duration", "phase2_duration", "inter_pulse_interval", "pulse_train_duration"):
        getattr(device, name)[1] = 0.0001
        getattr(device, name)[2] = 100 * 1e-6  # Just under 0.0001, but 2 cycles
        assert getattr(device, name)[1:3] == [0.0001, 0.0001], name
        error = expect_error(getattr(device, name).__setitem__, 3, 0.00005)
        assert "at least 0.0001 s (2 cycles of the device's 50 us timer)" in str(error), error
    for name in ("inter_phase_interval", "burst_duration", "inter_burst_interval", "pulse_train_delay"):
        getattr(device, name)[1] = 0
        getattr(device, name)[2] = 0.00005
        assert getattr(device, name)[1:3] == [0, 0.00005], name
    error = expect_error(device.inter_pulse_interval.__setitem__, 1, 0)
    assert "at least 0.0001 s (2 cycles" in str(error), error

    device = make_device()
    assert device.info.min_pulse_width_us == 50
    for name in ("phase1_duration", "phase2_duration", "inter_pulse_interval", "pulse_train_duration"):
        getattr(device, name)[1] = 0.00005
        getattr(device, name)[2] = 50 * 1e-6
        assert getattr(device, name)[1:3] == [0.00005, 0.00005], name
    for name in ("inter_phase_interval", "burst_duration", "inter_burst_interval", "pulse_train_delay"):
        getattr(device, name)[1] = 0
        assert getattr(device, name)[1] == 0, name
    error = expect_error(device.inter_pulse_interval.__setitem__, 1, 0)
    assert "at least 5e-05 s (1 cycle of the device's 50 us timer)" in str(error), error
    device.port.writes.clear()
    device.phase1_duration[3] = 0.00005
    assert device.port.writes == [bytes([OP_MENU_BYTE, 74, 4, 3]) + struct.pack("<I", 1)]

    device = make_device()
    device.info.cycle_period_us = 25
    device.info.cycle_frequency = 40000
    device.info.min_pulse_width_us = 25
    device.phase1_duration[1] = 0.000025  # 1 cycle of 25 us
    device.send_custom_waveform(1, 0.000025, [1, 2, 3])
    assert device.port.writes[0] == bytes([OP_MENU_BYTE, 74, 4, 1]) + struct.pack("<I", 1)
    error = expect_error(device.phase1_duration.__setitem__, 1, 0.00001)
    assert "at least 2.5e-05 s (1 cycle of the device's 25 us timer)" in str(error), error

def test_halfway_values_round_to_even_like_the_matlab_and_cpp_classes():
    """All three classes round a value exactly halfway to the even one, so a script gives the
    same train in each: 125 us (2.5 cycles) is 2 cycles, and +4 V (DAC code 45874.5) is 45874."""
    device = make_device()
    device.pulse_train_delay[1] = 0.000125
    device.phase1_voltage[1] = 4
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 74, 11, 1]) + struct.pack("<I", 2),
        bytes([OP_MENU_BYTE, 74, 2, 1]) + struct.pack("<H", 45874),
    ]


def parameter_message(cycles=200, volt_bits=None, byte_value=1):
    """Build the 178-byte parameter set that op 93 returns."""
    if volt_bits is None:
        volt_bits = volts_to_bits(5)
    return struct.pack(
        "<32I12H26B",
        *([cycles] * 32),
        *([volt_bits] * 12),
        *([byte_value] * 26),
    )


def test_a_refused_value_is_read_back_from_the_device():
    """The firmware resets a value it refuses, so the class reads that parameter back (op 93) on the
    channels it sent, and leaves every other parameter and channel as it was."""
    device = make_device(ack=0)
    device.port.response = bytearray(bytes([0]) + parameter_message(volt_bits=volts_to_bits(2.5), byte_value=1))
    error = expect_error(device.custom_train_id.__setitem__, 2, 3)
    assert "rejected" in str(error)
    assert device.port.writes[1] == bytes([OP_MENU_BYTE, 93])
    assert device.custom_train_id[1:5] == [0, 1, 0, 0]   # Channel 2 from the device
    assert device.phase1_voltage[1:5] == [5] * 4         # Not read back: the device holds 2.5 V in this reply

    # On firmware v21, which has no op 93, the local copy is left as it was
    device = make_device(firmware_version=21, hardware_version=2, n_trains=2, max_pulses=5000, ack=0)
    expect_error(device.custom_train_loop.__setitem__, 2, True)
    assert device.custom_train_loop == [None, False, False, False, False]


def test_status_reads_op_101():
    """status() sends op 101 and reads its 29 bytes: the channels playing, then the counters since the last
    op 101 (rising edges per trigger channel, starts per output channel, comm failures)."""
    device = make_device()
    device.port.response = bytearray([0b0101]) + struct.pack("<7I", 3, 0, 2, 0, 1, 0, 1)
    status = device.status()
    assert device.port.writes == [bytes([OP_MENU_BYTE, 101])] and device.port.reads == [29]
    assert status == pulse_pal.DeviceStatus(playing=[1, 3], trigger_edges=[None, 3, 0],
                                            channel_starts=[None, 2, 0, 1, 0], comm_failures=1), status
    expect_error(make_device(firmware_version=21, hardware_version=2, n_trains=2, max_pulses=5000).status)


def test_a_refused_settings_file_operation_says_what_failed():
    """The device refuses a load of a missing or invalid file and keeps its parameters, so nothing is read
    back; and it refuses a save it could not write, and the deletion of a missing file."""
    for call, text in ((lambda d: d.load_program_from_microsd("MISSING.pps"),
                        "no such file on its microSD card, or it is"),
                       (lambda d: d.save_program_to_microsd("NEW.pps"), "could not save 'NEW.pps'"),
                       (lambda d: d.delete_program_from_microsd("MISSING.pps"), "could not delete 'MISSING.pps'")):
        device = make_device()
        device.phase1_voltage[1] = 2.5
        device.port.writes.clear()
        device.port.ack = 0
        error = expect_error(call, device)
        assert text in str(error), error
        assert len(device.port.writes) == 1 and device.port.writes[0][1] == 90, device.port.writes
        assert device.phase1_voltage[1] == 2.5


def test_pulse_pal_2_refuses_microsd_while_playing():
    """Pulse Pal 2 replies 2 to op 90 while a channel plays, because its microSD card shares the SPI bus with the
    DAC. The error says so, and a refused load reads nothing back."""
    for call, verb in ((lambda d: d.save_program_to_microsd("NEW.pps"), "saved"),
                       (lambda d: d.load_program_from_microsd("NEW.pps"), "loaded"),
                       (lambda d: d.delete_program_from_microsd("NEW.pps"), "deleted")):
        device = make_device(hardware_version=2, n_trains=2, max_pulses=5000)
        device.phase1_voltage[1] = 2.5
        device.port.writes.clear()
        device.port.ack = 2
        error = expect_error(call, device)
        assert "while a channel is playing" in str(error) and f"'NEW.pps' was not {verb}" in str(error), error
        assert len(device.port.writes) == 1 and device.port.writes[0][1] == 90, device.port.writes
        assert device.phase1_voltage[1] == 2.5


def test_sync_from_device_reads_the_message_in_one_read():
    device = make_device()
    device.port.response = bytearray(parameter_message())
    device.sync_from_device()
    assert device.port.writes == [bytes([OP_MENU_BYTE, 93])]
    assert device.port.reads == [178], device.port.reads
    assert device.phase1_duration[1:5] == [0.01] * 4          # 200 cycles at 20 kHz
    assert device.pulse_train_delay[1:5] == [0.01] * 4        # the last time parameter
    assert device.phase1_voltage[1:5] == [5.0] * 4
    assert device.resting_voltage[1:5] == [5.0] * 4           # the last voltage parameter
    assert device.is_biphasic[1:5] == [True] * 4
    assert device.custom_train_target[1:5] == ["Bursts"] * 4
    assert device.link_trigger_channel2[1:5] == [True] * 4    # the last byte array
    assert device.trigger_mode[1:3] == ["Toggle", "Toggle"]
    assert device.continuous_loop[1:5] == [False] * 4         # Not in op 93: left as it was


def test_settings_file_load_does_not_wait_on_current_firmware():
    """Firmware v22 acknowledges op 90 after the load, so no fixed delay is needed."""
    sleeps = []
    original_sleep = pulse_pal.time.sleep
    pulse_pal.time.sleep = lambda seconds: sleeps.append(seconds)
    try:
        device = make_device()
        device.port.response = bytearray(bytes([1]) + parameter_message())
        device.load_program_from_microsd("TEST.pps")
        assert sleeps == [], sleeps
        assert device.port.reads == [1, 178], device.port.reads

        # Firmware v21 does not acknowledge, so the wait is still used there
        sleeps.clear()
        legacy = make_device(firmware_version=21, hardware_version=2, n_trains=2, max_pulses=5000)
        try:
            legacy.load_program_from_microsd("TEST.pps")
        except PulsePalError:
            pass  # v21 has no op 93, so reading the parameters back is unsupported
        assert sleeps == [0.1], sleeps
    finally:
        pulse_pal.time.sleep = original_sleep


def test_settings_files_use_op_90():
    device = make_device()
    device.port.response = bytearray(bytes([1, 1]) + parameter_message() + bytes([1]))
    device.save_program_to_microsd("Protocol1.pps")
    device.load_program_from_microsd("ABCDEFGHIJK.PPS")   # 15 characters
    device.delete_program_from_microsd("Protocol1.pps")
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 90, 1, 13]) + b"Protocol1.pps",
        bytes([OP_MENU_BYTE, 90, 2, 15]) + b"ABCDEFGHIJK.PPS",
        bytes([OP_MENU_BYTE, 93]),
        bytes([OP_MENU_BYTE, 90, 3, 13]) + b"Protocol1.pps",
    ]
    for bad in ("Protocol1", "Protocol1.pps.txt", "ABCDEFGHIJKL.pps", ".pps", "Pr\u00f6tocol.pps", 5):
        device = make_device()
        expect_error(device.save_program_to_microsd, bad)
        assert device.port.writes == []


def test_export_and_import_params():
    """export_params() returns plain lists that json can save, and import_params() sends them in one
    op 92."""
    import json
    device = make_device()
    device.phase1_voltage[2] = 2.5
    device.trigger_mode[1] = "Toggle"
    params = json.loads(json.dumps(device.export_params()))
    assert list(params) == [*device.info.output_parameter_names, "trigger_mode"]
    assert params["phase1_voltage"] == [5, 2.5, 5, 5] and params["trigger_mode"] == ["Toggle", "Normal"]

    other = make_device()
    other.import_params(params)
    assert other.port.writes[0][:2] == bytes([OP_MENU_BYTE, 92])
    assert len(other.port.writes) == 1 and other.port.writes[0][:2] == bytes([OP_MENU_BYTE, 92])
    assert other.export_params() == params
    device.sync_to_device()
    assert other.port.writes[0] == device.port.writes[-1]  # The same parameters as the first device

    # A partial set keeps the other parameters; a bad name or value changes and sends nothing
    other.import_params({"phase1_duration": [0.002] * 4})
    assert other.phase1_duration[1:5] == [0.002] * 4 and other.phase1_voltage[2] == 2.5
    n_writes = len(other.port.writes)
    expect_error(other.import_params, {"phase1_voltage": [1] * 4, "phase2_voltage": [11] * 4})
    assert len(other.port.writes) == n_writes and other.phase1_voltage[1:5] == [5, 2.5, 5, 5]

    # A name this version does not have (one a newer version exported, say) is skipped with a warning
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        other.import_params({"phase1_voltage": [1] * 4, "a_future_parameter": [0] * 4})
    assert "a_future_parameter" in str(caught[0].message)
    assert other.phase1_voltage[1:5] == [1] * 4 and len(other.port.writes) == n_writes + 1


def test_the_defaults_are_the_firmware_defaults():
    """The class programs the defaults the device loads itself (LoadDefaultParameters() in the firmware), so
    the device plays the same program after a comm failure or a format as after connecting: monophasic 5 V
    pulses of 2 cycles (0.1 ms), 20 cycles (1 ms) apart, for 20000 cycles (1 s), on output channels linked
    to no trigger channel, so that a TTL starts nothing until a script links a channel."""
    device = make_device()
    device.set_default_params()
    message = device.port.writes[-1][2:]
    times = struct.unpack("<32I", message[:128])
    assert times == (2,) * 4 + (2,) * 4 + (2,) * 4 + (20,) * 4 + (0,) * 8 + (20000,) * 4 + (0,) * 4, times
    voltages = struct.unpack("<12H", message[128:152])
    assert voltages == (volts_to_bits(5),) * 4 + (volts_to_bits(-5),) * 4 + (32768,) * 4, voltages
    assert message[152:182] == bytes(30), message[152:182]  # Byte parameters, trigger links and modes: all 0
    assert device.link_trigger_channel1 == [None] + [False] * 4


def test_custom_bursts_need_a_burst_duration():
    """A custom train whose times are burst onsets needs bursts that last. Assigned alone (auto_sync on), each
    of the two parameters is checked against the other; in a batch or a sync, together. Nothing is sent when
    one is refused."""
    device = make_device()
    error = expect_error(device.custom_train_target.__setitem__, 2, "Bursts")
    assert "Output channel 2" in str(error) and "burst_duration must be above 0" in str(error), error
    device.burst_duration[2] = 0.05
    device.custom_train_target[2] = "Bursts"
    expect_error(device.burst_duration.__setitem__, 2, 0)
    expect_error(device.burst_duration.__setitem__, 2, 0.00002)   # Rounds to 0 cycles
    assert device.burst_duration[2] == 0.05 and len(device.port.writes) == 2

    device = make_device()
    try:
        with device.batch():
            device.custom_train_target[3] = "Bursts"
        raise AssertionError("the batch was sent")
    except PulsePalError:
        pass
    assert device.port.writes == [] and device.custom_train_target[3] == "Pulses"
    with device.batch():
        device.custom_train_target[3] = "Bursts"
        device.burst_duration[3] = 0.05
    assert len(device.port.writes) == 1
    expect_error(device.import_params, {"burst_duration": [0, 0, 0, 0]})


def test_an_empty_custom_train_is_refused():
    """The device refuses a train with no pulses too: it would have no pulse times to play."""
    for call in (lambda d: d.send_custom_pulse_train(1, [], []),
                 lambda d: d.send_custom_pulse_train(1, np.array([]), np.array([])),
                 lambda d: d.send_custom_waveform(1, 0.001, [])):
        device = make_device()
        error = expect_error(call, device)
        assert "at least one pulse" in str(error), error
        assert device.port.writes == []


def test_custom_trains_convert_as_each_value_alone_would():
    """Custom trains are converted all at once, with NumPy. The result must be what the conversion of each
    value alone gives, halfway values included, whatever holds the values: a list, a tuple, or a NumPy array
    of floats or integers."""
    device = make_device()
    rng = np.random.default_rng(1)
    steps = np.sort(rng.choice(np.arange(0, 200000), size=4000, replace=False)) * 2 + 2  # Multiples of 2 cycles
    times = list(steps * 50e-6)
    volts = list(rng.uniform(-10, 10, size=4000))
    volts[:6] = [4, -4, 10, -10, 0, 9.999847412109375]   # 4 V is code 45874.5: halfway, to the even code
    expected_cycles = [device._custom_time_cycles(t, "t") for t in times]
    expected_bits = [device._volts_to_bits(v) for v in volts]
    for container in (list, tuple, np.array, lambda x: np.array(x, dtype=object)):
        assert list(device._custom_times_to_cycles(container(times), "t")) == expected_cycles, container
        assert list(device._volts_array_to_bits(container(volts), "v")) == expected_bits, container
    assert list(device._custom_times_to_cycles(np.arange(5) * 2, "t")) == [0, 40000, 80000, 120000, 160000]
    device.send_custom_pulse_train(1, times, volts)
    sent = device.port.writes[-1]
    assert sent == (bytes([OP_MENU_BYTE, 95, 0]) + struct.pack("<I", 4000) + struct.pack("<4000I", *expected_cycles)
                    + struct.pack("<4000H", *expected_bits))
    for bad_times, bad_volts, text in (([0, True], [1, 1], "Received True"),
                                       ([0, "0.1"], [1, 1], "Received '0.1'"),
                                       ([0, None], [1, 1], "Received None"),
                                       ([0, -0.1], [1, 1], "Received -0.1"),
                                       ([0, float("inf")], [1, 1], "Received inf"),
                                       ([0, 10000], [1, 1], "at most 9999.9999 s"),
                                       ([0, 0.00012], [1, 1], "Received 0.00012"),
                                       ([0, 0.1], [1, np.bool_(True)], "Received np.True_"),
                                       ([0, 0.1], [1, -10.5], "Received -10.5"),
                                       (np.array([0, 0.1]), np.array([1, np.nan]), "Received nan"),
                                       ([0, 0.1], np.array([True, False]), "Received True")):
        device = make_device()
        error = expect_error(device.send_custom_pulse_train, 1, bad_times, bad_volts)
        assert text in str(error), (text, str(error))
        assert device.port.writes == []


def test_a_reply_that_does_not_arrive_mentions_a_comm_failure():
    """A device showing COMM. FAILURE! reads no command until its joystick is clicked, so every reply times
    out. The error says what to do."""
    class SilentPort(FakePort):
        timeout = 10

        def read(self, n):
            return b""

    device = make_device()
    device.port = SilentPort()
    for call in (lambda d: d.phase1_voltage.__setitem__(1, 1), lambda d: d.sync_from_device()):
        error = expect_error(call, device)
        assert "COMM. FAILURE!" in str(error) and "click its joystick" in str(error), error


class ClosablePort(FakePort):
    """A FakePort that can be closed, and refuses writes once it is, like a real port."""

    def __init__(self, answers_op_103=True):
        super().__init__(answers_op_103=answers_op_103)
        self.is_open = True

    def close(self):
        self.is_open = False

    def write(self, data):
        if not self.is_open:
            raise OSError("port closed")
        return super().write(data)


class LateReplyPort(ClosablePort):
    """A ClosablePort whose device first answers a command that an earlier session left queued on it:
    `late_reply` arrives with the handshake's reply, after the input was discarded."""

    def __init__(self, late_reply):
        super().__init__()
        self.response = bytearray(late_reply) + bytearray([75]) + struct.pack("<I", 22) \
            + PULSE_PAL_3_INFO
        self.sent = len(late_reply) + 5  # The late reply and the handshake's: sent before anything is read

    @property
    def in_waiting(self):
        return self.sent

    def read(self, n):
        self.sent = max(0, self.sent - n)
        return super().read(n)


def test_a_late_reply_to_an_earlier_session_is_skipped():
    """A command an earlier session sent just before it closed can still be waiting on the device, which
    answers it after the input was discarded, and before the handshake. The handshake's reply is the last
    5 bytes the device sends, so the late reply is skipped, also one that looks like a handshake."""
    op_93_reply = bytes([20, 0, 0, 0]) * 8 + bytes(146)  # Starts with 20 cycles: 1 ms
    for late_reply in (op_93_reply, bytes([75, 99, 0, 0, 0]), bytes([1])):
        port = LateReplyPort(late_reply)
        device = connect_to(port)
        assert device.info.firmware_version == 22 and device.info.hardware_version == 3, late_reply[:5]
        assert port.writes[1] == bytes([OP_MENU_BYTE, 94]) and port.sent == 0
        device.close()


def test_connecting_to_other_firmware_says_so():
    """A device running Wave Pal firmware replies 87 ('W') to the handshake, and one running
    Synth Pal firmware 83 ('S'). The class must say so, rather than only that the handshake was
    wrong, and must not send it op 81."""
    for reply, name, client in ((87, "Wave Pal", "WavePalDevice"), (83, "Synth Pal", "SynthPalDevice")):
        port = ClosablePort(answers_op_103=False)  # Wave Pal and Synth Pal ignore op 103
        port.response = bytearray([reply, 1, 0, 0, 0])  # Then firmware v1
        original_serial = pulse_pal.serial.Serial
        pulse_pal.serial.Serial = lambda *args, **kwargs: port
        try:
            error = expect_error(PulsePalDevice, "COM9")
        finally:
            pulse_pal.serial.Serial = original_serial
        assert f"runs {name} firmware (v1)" in str(error), error
        assert client in str(error), error
        assert port.writes == [bytes([OP_MENU_BYTE, 103]), bytes([OP_MENU_BYTE, 72])], port.writes
        assert not port.is_open


def test_a_refused_connection_closes_the_port():
    """Firmware too old for the class, or an error later in the connection, must not leave the
    port open: a second attempt could not open it."""
    handshake_then_version = {
        "old firmware": bytearray([75]) + struct.pack("<I", 19),
        # Current firmware, then a reply to op 94 that ends early
        "an error during setup": bytearray([75]) + struct.pack("<I", 22),
    }
    original_serial = pulse_pal.serial.Serial
    try:
        for case, response in handshake_then_version.items():
            port = ClosablePort()
            port.response = response
            if case == "an error during setup":
                def fail_after_handshake(n, port=port, read=port.read):
                    if len(port.reads) >= 2:
                        raise pulse_pal.serial.SerialException("device removed")
                    return read(n)
                port.read = fail_after_handshake
            pulse_pal.serial.Serial = lambda *args, port=port, **kwargs: port
            try:
                PulsePalDevice("COM9")
                raise AssertionError(f"{case}: connected")
            except (PulsePalError, pulse_pal.serial.SerialException):
                pass
            assert not port.is_open, f"{case}: the port was left open"
            assert bytes([OP_MENU_BYTE, 81]) not in port.writes, case
    finally:
        pulse_pal.serial.Serial = original_serial


class ChunkedPort(FakePort):
    """A FakePort whose reply arrives in separate chunks, as it does from a device
    that pauses between parts of it. Once the chunks are used up, reads return
    the acknowledgement byte, like FakePort."""

    def __init__(self, chunks):
        super().__init__()
        self.chunks = [bytes(chunk) for chunk in chunks]

    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0

    def read(self, n):
        if self.chunks:
            self.reads.append(n)
            return self.chunks.pop(0)
        return super().read(n)


def format_microsd_with_reply(chunks):
    """Run format_microsd() against a device that replies with these chunks."""
    device = make_device()
    device.port = ChunkedPort(chunks)
    original_sleep = pulse_pal.time.sleep
    pulse_pal.input = lambda prompt="": "y"  # Answers the confirmation prompt
    pulse_pal.time.sleep = lambda seconds: None
    try:
        assert device.format_microsd(timeout=1) is True
    finally:
        del pulse_pal.input
        pulse_pal.time.sleep = original_sleep
    return device


def test_format_microsd_reads_the_confirm_byte_sent_after_the_text():
    """Op 97 sends status text ending in '!', then a confirm byte once the device has
    reloaded its defaults. Left unread, that byte would be taken as the reply to the
    next command."""
    device = format_microsd_with_reply([
        b"STATUS: Starting Format...\r\n",
        b"SUCCESS: Card format complete!\r\n",
        b"\x01",
    ])
    # Op 97 with its key, then the default parameters, as after connecting: the device keeps no record of
    # this object's other changes, such as continuous loop mode
    assert device.port.writes[0] == bytes([OP_MENU_BYTE, 97]) + b"WIPE"
    assert device.port.writes[1:] == [bytes([OP_MENU_BYTE, 91, 128, 0, 0]), device.port.writes[2]]
    assert device.port.writes[2][:2] == bytes([OP_MENU_BYTE, 92])
    assert device.port.chunks == [], "the confirm byte was left unread"

    # The confirm byte can also arrive with the text
    device = format_microsd_with_reply([
        b"STATUS: Starting Format...\r\nSUCCESS: Card format complete!\r\n\x01",
    ])
    assert device.port.chunks == []


def test_format_microsd_raises_when_the_device_reports_failure():
    error = expect_error(format_microsd_with_reply, [b"ERROR: Format failed!\r\n", b"\x00"])
    assert "could not format" in str(error)


def test_format_microsd_without_confirmation_does_not_wait_for_input():
    """A script run by an AI agent has no one to answer the prompt."""
    def no_input(prompt=""):
        raise AssertionError("format_microsd(confirm=False) asked for input")

    device = make_device()
    device.port = ChunkedPort([b"SUCCESS: Card format complete!\r\n\x01"])
    pulse_pal.input = no_input
    try:
        assert device.format_microsd(confirm=False) is True
        assert device.port.writes[0] == bytes([OP_MENU_BYTE, 97]) + b"WIPE"
        device = make_device()
        pulse_pal.input = lambda prompt="": "n"
        assert device.format_microsd() is False
        assert device.port.writes == []
    finally:
        del pulse_pal.input


def test_set_screen_saver_uses_op_99():
    device = make_device()
    device.set_screen_saver(True, 300)
    device.set_screen_saver(0, 65535)
    device.set_screen_saver(False)  # The timeout is always sent: 1800 s when left out
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 99, 1]) + struct.pack("<H", 300),
        bytes([OP_MENU_BYTE, 99, 0]) + struct.pack("<H", 65535),
        bytes([OP_MENU_BYTE, 99, 0]) + struct.pack("<H", 1800),
    ]


def test_set_screen_saver_refuses_bad_values_before_sending():
    device = make_device()
    for enabled, timeout in ((2, 600), ("on", 600), (None, 600), (1, 0), (1, 65536), (1, 1.5),
                             (1, float("nan")), (1, True)):
        expect_error(device.set_screen_saver, enabled, timeout)
    assert device.port.writes == []


def test_set_screen_saver_needs_pulse_pal_3_and_firmware_v22():
    device = make_device(hardware_version=2)
    expect_error(device.set_screen_saver, True)
    device.set_screen_saver(False)  # Off is valid on Pulse Pal 2
    assert device.port.writes == [bytes([OP_MENU_BYTE, 99, 0]) + struct.pack("<H", 1800)]
    device = make_device(firmware_version=21)
    expect_error(device.set_screen_saver, False)  # Firmware v21 has no op 99
    assert device.port.writes == []


def test_set_calibration_refuses_bad_values_with_pulse_pal_error():
    device = make_device()
    for channel, offset in ((0, 0), (5, 0), (1, 0.2), (1, float("nan"))):
        expect_error(device.set_calibration, channel, offset)
    device.set_calibration(2, -0.01)
    assert device.port.writes == [bytes([OP_MENU_BYTE, 96, 1]) + struct.pack("<h", round(-0.01 * 65536 / 20))]


def test_the_package_exports_one_error_class():
    import pulsepal
    from pulsepal import synth_pal, wave_pal
    assert pulsepal.PulsePalError is PulsePalError is wave_pal.PulsePalError is synth_pal.PulsePalError
    assert isinstance(pulsepal.__version__, str)


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
