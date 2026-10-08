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
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from pulsepal import pulse_pal  # noqa: E402
from pulsepal import PulsePalDevice, PulsePalError  # noqa: E402

OP_MENU_BYTE = 213


class FakePort:
    """Records written bytes and read sizes, and replies to reads.

    Bytes placed in `response` are returned first; after that, reads return the
    acknowledgement byte.
    """

    def __init__(self, ack=1):
        self.port = "FAKE"
        self.writes = []
        self.reads = []
        self.ack = ack
        self.response = bytearray()

    def write(self, data):
        self.writes.append(bytes(data))
        return len(data)

    def read(self, n):
        self.reads.append(n)
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


def expect_error(function, *args, error=PulsePalError):
    try:
        function(*args)
    except error as exc:
        return exc
    raise AssertionError(f"{function} did not raise {error.__name__}")


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
    for code, name in enumerate(expected, start=1):
        assert device._get_output_param_code(name) == code
        assert device._get_output_param_code(code) == code
    assert device._get_trigger_param_code("trigger_mode") == 128


def test_connection_programs_the_defaults():
    """Connecting takes both trigger channels out of param sync mode (Pulse Pal 3), then sends the
    default parameters in one op 92."""
    port = ClosablePort()
    port.response = bytearray([75]) + struct.pack("<I", 22) + struct.pack("<BIBI", 3, 50, 4, 10000)
    original_serial = pulse_pal.serial.Serial
    pulse_pal.serial.Serial = lambda *args, **kwargs: port
    try:
        device = PulsePalDevice("COM9")
    finally:
        pulse_pal.serial.Serial = original_serial
    assert port.writes[:4] == [
        bytes([OP_MENU_BYTE, 72]),
        bytes([OP_MENU_BYTE, 94]),
        bytes([OP_MENU_BYTE, 89]) + b"PYTHON",
        bytes([OP_MENU_BYTE, 91, 128, 0, 0]),
    ], port.writes
    assert port.writes[4][:2] == bytes([OP_MENU_BYTE, 92]) and len(port.writes) == 5
    assert device.info.hardware_version == 3 and device.info.min_pulse_width_us == 100
    assert device.info.trigger_modes == ("Normal", "Toggle", "Gated", "Param Sync")
    assert device.is_biphasic == [None, False, False, False, False]
    assert device.phase1_voltage == [None, 5, 5, 5, 5]
    assert device.custom_train_target == [None] + ["Pulses"] * 4
    assert device.trigger_mode == [None, "Normal", "Normal"]
    device.close()
    assert port.writes[-1] == bytes([OP_MENU_BYTE, 81]) and not port.is_open


def test_assigning_a_parameter_programs_the_device():
    """With auto_sync on (the default), an assignment sends the parameter for all four channels in one
    op 91, whether it is to one element, a slice or the whole list."""
    device = make_device()
    device.phase1_voltage[2] = 7
    device.inter_pulse_interval[1:5] = [0.2] * 4
    device.phase1_duration = 0.002                 # One value for every channel
    device.is_biphasic[3] = True
    device.trigger_mode[2] = "Toggle"
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 91, 2]) + struct.pack("<4H", volts_to_bits(5), volts_to_bits(7),
                                                   volts_to_bits(5), volts_to_bits(5)),
        bytes([OP_MENU_BYTE, 91, 7]) + struct.pack("<4I", *[4000] * 4),
        bytes([OP_MENU_BYTE, 91, 4]) + struct.pack("<4I", *[40] * 4),
        bytes([OP_MENU_BYTE, 91, 1, 0, 0, 1, 0]),
        bytes([OP_MENU_BYTE, 91, 128, 0, 1]),
    ]
    assert device.phase1_voltage == [None, 5, 7, 5, 5]
    assert device.phase1_duration == [None, 0.002, 0.002, 0.002, 0.002]


def test_assigning_on_firmware_v21_uses_op_74_per_channel():
    device = make_device(firmware_version=21, hardware_version=2, n_trains=2, max_pulses=5000)
    device.resting_voltage[1] = 1
    device.trigger_mode[1] = "Gated"
    assert [w[:4] for w in device.port.writes] == [
        bytes([OP_MENU_BYTE, 74, 17, channel]) for channel in (1, 2, 3, 4)
    ] + [bytes([OP_MENU_BYTE, 74, 128, 1]), bytes([OP_MENU_BYTE, 74, 128, 2])]
    expect_error(device.continuous_loop.__setitem__, 1, True)  # Firmware v22 or newer


def test_auto_sync_off_keeps_assignments_until_sync_to_device():
    device = make_device()
    device.auto_sync = False
    device.phase1_voltage[1] = 2.5
    device.custom_train_target[2] = "Bursts"
    device.trigger_mode = "Gated"
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
    device.phase1_voltage = 1
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


def test_print_shows_every_parameter():
    device = make_device()
    text = repr(device)
    assert "Pulse Pal 3, firmware v22" in text
    for name in (*device.info.output_parameter_names, "trigger_mode", "auto_sync"):
        assert f"{name}: " in text, name


def test_set_output_param_one_channel_uses_op_74():
    device = make_device()
    device.set_output_param("phase1_voltage", 2, 5)
    device.set_output_param("phase1_duration", 3, 0.001)
    device.set_output_param("is_biphasic", 1, True)
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 74, 2, 2]) + struct.pack("<H", volts_to_bits(5)),
        bytes([OP_MENU_BYTE, 74, 4, 3]) + struct.pack("<I", 20),
        bytes([OP_MENU_BYTE, 74, 1, 1, 1]),
    ]
    assert device.phase1_voltage[2] == 5
    assert device.phase1_duration[3] == 0.001
    assert device.is_biphasic[1] is True


def test_set_output_param_all_channels_uses_op_91_even_with_auto_sync_off():
    device = make_device()
    device.auto_sync = False
    device.set_output_param("phase1_voltage", (1, 2, 3, 4), [-10, 0, 5, 10])
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 91, 2])
        + struct.pack("<4H", 0, volts_to_bits(0), volts_to_bits(5), 65535)
    ]
    assert device.phase1_voltage[1:5] == [-10, 0, 5, 10]


def test_set_output_param_channel_subset_uses_op_74_per_channel():
    device = make_device()
    device.set_output_param("pulse_train_duration", np.array([2, 4]), 1)
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 74, 10, 2]) + struct.pack("<I", 20000),
        bytes([OP_MENU_BYTE, 74, 10, 4]) + struct.pack("<I", 20000),
    ]


def test_set_output_param_rejects_bad_channels_and_value_counts():
    for channel, value in [([1, 1], 5), ([1, 5], 5), ([], 5), ([1, 2, 3, 4], [1, 2])]:
        device = make_device()
        expect_error(device.set_output_param, "phase1_voltage", channel, value)
        assert device.port.writes == []
    expect_error(make_device().set_output_param, "playback_mode", 1, 1)  # Renamed continuous_loop


def test_set_trigger_param_uses_op_74_with_code_128():
    device = make_device()
    device.set_trigger_param("trigger_mode", 2, "Gated")
    device.set_trigger_param("trigger_mode", 1, 1)
    assert device.port.writes == [bytes([OP_MENU_BYTE, 74, 128, 2, 2]), bytes([OP_MENU_BYTE, 74, 128, 1, 1])]
    assert device.trigger_mode == [None, "Toggle", "Gated"]


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
        [0, 0.00001],           # Distinct, but both round to cycle 0
        [0, 0.00005, 0.0001],   # One 50 us cycle apart
        [0, 0.0001, 0.00013],   # The last two round to 1 cycle apart
    ]
    for pulse_times in cases:
        device = make_device()
        error = expect_error(device.send_custom_pulse_train, 1, pulse_times, [1] * len(pulse_times))
        assert "must increase" in str(error)
        assert device.port.writes == []

    device = make_device()
    error = expect_error(device.send_custom_waveform, 1, 0.00005, [1, 2, 3])  # One 50 us cycle
    assert "must increase" in str(error)
    assert device.port.writes == []

    device = make_device()
    device.send_custom_pulse_train(1, [0, 0.0001, 0.00025], [1, 2, 3])  # 2 and 3 cycles apart
    device.send_custom_waveform(2, 0.0001, [1, 2, 3])
    assert len(device.port.writes) == 2


def test_waveform_matches_an_equivalent_pulse_train():
    waveform_device = make_device()
    waveform_device.send_custom_waveform(2, 0.001, [1, 2, 3])
    train_device = make_device()
    train_device.send_custom_pulse_train(2, [0, 0.001, 0.002], [1, 2, 3])
    assert waveform_device.port.writes == train_device.port.writes


def test_a_rejected_command_raises():
    """The firmware replies 0 when it rejects a command (see /Firmware/PROTOCOL.md)."""
    device = make_device(ack=0)
    error = expect_error(device.set_output_param, "phase1_voltage", 1, 5)
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
        lambda d: d.set_output_param("phase1_voltage", 1, 15),
        lambda d: d.set_output_param("resting_voltage", [1, 2, 3, 4], [0, 0, -10.5, 0]),
        lambda d: d.set_output_param("phase1_duration", 1, 0),
        lambda d: d.set_output_param("phase1_duration", 1, 0.00002),   # Rounds to 0 cycles
        lambda d: d.set_output_param("phase1_duration", 1, 0.00005),   # One cycle
        lambda d: d.set_output_param("phase2_duration", 1, 0),
        lambda d: d.set_output_param("pulse_train_duration", 1, 0.00005),
        lambda d: d.set_output_param("pulse_train_delay", 1, -0.001),
        lambda d: d.set_output_param("pulse_train_duration", 1, float("nan")),
        lambda d: d.set_output_param("inter_pulse_interval", 1, 0),
        lambda d: d.set_output_param("is_biphasic", 1, 2),
        lambda d: d.set_output_param("custom_train_id", 1, 5),
        lambda d: d.set_output_param("custom_train_loop", 1, 0.5),
        lambda d: d.set_trigger_param("trigger_mode", 1, 4),
        lambda d: d.set_trigger_param("trigger_mode", 3, 0),
        lambda d: d.send_custom_pulse_train(1, [0, 0.001], [5, 11]),
        lambda d: d.send_custom_waveform(1, 0.001, [0, float("nan")]),
        lambda d: d.phase1_voltage.__setitem__(3, 15),
        lambda d: d.phase1_voltage.__setitem__(3, "5"),
        lambda d: d.inter_pulse_interval.__setitem__(1, 0.00005),
    ]
    for call in calls:
        device = make_device()
        expect_error(call, device)
        assert device.port.writes == []


def test_pulses_and_intervals_last_at_least_two_cycles():
    """Two cycles is the shortest pulse a trigger channel detects reliably, as in the MATLAB
    class and the joystick menu: 100 us on a 50 us timer, 50 us on a 25 us timer. Times of 0
    still mean "off" where they did."""
    device = make_device()
    for name in ("phase1_duration", "phase2_duration", "inter_pulse_interval", "pulse_train_duration"):
        getattr(device, name)[1] = 0.0001
        getattr(device, name)[1] = 100 * 1e-6  # Just under 0.0001, but 2 cycles
    for name in ("inter_phase_interval", "burst_duration", "inter_burst_interval", "pulse_train_delay"):
        getattr(device, name)[1] = 0
        getattr(device, name)[1] = 0.00005
    assert len(device.port.writes) == 16
    error = expect_error(device.inter_pulse_interval.__setitem__, 1, 0)
    assert "at least 0.0001 s (2 cycles" in str(error), error

    device = make_device()
    device.info.cycle_period_us = 25
    device.info.cycle_frequency = 40000
    device.set_output_param("phase1_duration", 1, 0.00005)  # 2 cycles of 25 us
    device.send_custom_waveform(1, 0.00005, [1, 2, 3])
    assert device.port.writes[0] == bytes([OP_MENU_BYTE, 74, 4, 1]) + struct.pack("<I", 2)
    error = expect_error(device.set_output_param, "phase1_duration", 1, 0.000025)
    assert "at least 5e-05 s (2 cycles of the device's 25 us timer)" in str(error), error


def test_halfway_values_round_to_even_like_the_matlab_and_cpp_classes():
    """All three classes round a value exactly halfway to the even one, so a script gives the
    same train in each: 125 us (2.5 cycles) is 2 cycles, and +4 V (DAC code 45874.5) is 45874."""
    device = make_device()
    device.set_output_param("phase1_duration", 1, 0.000125)
    device.set_output_param("phase1_voltage", 1, 4)
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 74, 4, 1]) + struct.pack("<I", 2),
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
    """The firmware resets a value it refuses, so the class reads that parameter back (op 93)
    for the channels it sent, and leaves every other local value as it was."""
    device = make_device(ack=0)
    device.auto_sync = False
    device.phase1_voltage[1] = 7   # A local edit, not yet synced
    device.port.response = bytearray(bytes([0]) + parameter_message(byte_value=1))
    error = expect_error(device.set_output_param, "custom_train_id", 2, 3)
    assert "rejected" in str(error)
    assert device.port.writes[1] == bytes([OP_MENU_BYTE, 93])
    assert device.custom_train_id[1:5] == [0, 1, 0, 0]   # Channel 2 from the device
    assert device.phase1_voltage[1] == 7

    # The same for an assignment, which sends all four channels
    device = make_device(ack=0)
    device.port.response = bytearray(bytes([0]) + parameter_message(byte_value=1))
    expect_error(device.custom_train_loop.__setitem__, 2, True)
    assert device.custom_train_loop == [None, True, True, True, True]


def test_a_refused_settings_load_reads_the_parameters_back():
    """A failed load leaves the device on its own defaults, so the local copy is read back."""
    device = make_device()
    device.port.response = bytearray(bytes([0]) + parameter_message(cycles=2))
    expect_error(device.sd_settings, "MISSING.pps", "load")
    assert device.port.writes[-1] == bytes([OP_MENU_BYTE, 93])
    assert device.phase1_duration[1:5] == [0.0001] * 4


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
        device.sd_settings("TEST.pps", "load")
        assert sleeps == [], sleeps
        assert device.port.reads == [1, 178], device.port.reads

        # Firmware v21 does not acknowledge, so the wait is still used there
        sleeps.clear()
        legacy = make_device(firmware_version=21, hardware_version=2, n_trains=2, max_pulses=5000)
        try:
            legacy.sd_settings("TEST.pps", "load")
        except PulsePalError:
            pass  # v21 has no op 93, so reading the parameters back is unsupported
        assert sleeps == [0.1], sleeps
    finally:
        pulse_pal.time.sleep = original_sleep


class ClosablePort(FakePort):
    """A FakePort that can be closed, and refuses writes once it is, like a real port."""

    def __init__(self):
        super().__init__()
        self.is_open = True

    def close(self):
        self.is_open = False

    def write(self, data):
        if not self.is_open:
            raise OSError("port closed")
        return super().write(data)


def test_connecting_to_other_firmware_says_so():
    """A device running Wave Pal firmware replies 87 ('W') to the handshake, and one running
    Synth Pal firmware 83 ('S'). The class must say so, rather than only that the handshake was
    wrong, and must not send it op 81."""
    for reply, name, client in ((87, "Wave Pal", "WavePalDevice"), (83, "Synth Pal", "SynthPalDevice")):
        port = ClosablePort()
        port.response = bytearray([reply, 1, 0, 0, 0])  # Then firmware v1
        original_serial = pulse_pal.serial.Serial
        pulse_pal.serial.Serial = lambda *args, **kwargs: port
        try:
            error = expect_error(PulsePalDevice, "COM9")
        finally:
            pulse_pal.serial.Serial = original_serial
        assert f"runs {name} firmware (v1)" in str(error), error
        assert client in str(error), error
        assert port.writes == [bytes([OP_MENU_BYTE, 72])], port.writes
        assert not port.is_open


def test_a_refused_connection_closes_the_port():
    """Firmware too old or too new for the class, or an error later in the connection,
    must not leave the port open: a second attempt could not open it."""
    handshake_then_version = {
        "old firmware": bytearray([75]) + struct.pack("<I", 19),
        "future firmware": bytearray([75]) + struct.pack("<I", 99),
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
        device.format_microsd(timeout=1)
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
    assert device.port.writes == [bytes([OP_MENU_BYTE, 97])]
    assert device.port.chunks == [], "the confirm byte was left unread"

    # The confirm byte can also arrive with the text
    device = format_microsd_with_reply([
        b"STATUS: Starting Format...\r\nSUCCESS: Card format complete!\r\n\x01",
    ])
    assert device.port.chunks == []


def test_format_microsd_raises_when_the_device_reports_failure():
    error = expect_error(format_microsd_with_reply, [b"ERROR: Format failed!\r\n", b"\x00"])
    assert "could not format" in str(error)


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
