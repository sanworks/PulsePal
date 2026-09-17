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
import importlib.util
import struct
import sys
from decimal import Decimal
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parent.parent / "PulsePal.py"
_spec = importlib.util.spec_from_file_location("PulsePal_under_test", MODULE_PATH)
PulsePal = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(PulsePal)

OP_MENU_BYTE = 213


class FakePort:
    """Records written bytes, and replies to acknowledgement reads."""

    def __init__(self, ack=1):
        self.writes = []
        self.ack = ack

    def write(self, data):
        self.writes.append(bytes(data))
        return len(data)

    def read(self, n):
        return bytes([self.ack]) * n


def make_device(firmware_version=22, n_trains=4, max_pulses=10000, ack=1):
    """Build a PulsePalDevice that talks to a fake port, without connecting."""
    device = PulsePal.PulsePalDevice.__new__(PulsePal.PulsePalDevice)
    device.info = PulsePal.DeviceInfo()
    device.info.firmware_version = firmware_version
    device.info.cycle_frequency = 20000
    device.info.n_custom_pulse_trains = n_trains
    device.info.max_custom_pulses = max_pulses
    device.info.output_parameter_names = list(
        PulsePal.PulsePalDevice._OUTPUT_PARAMETER_NAMES
    )
    device.info.trigger_parameter_names = list(
        PulsePal.PulsePalDevice._TRIGGER_PARAMETER_NAMES
    )
    device._dac_bit_max = Decimal(65535).quantize(Decimal("1.0000"))
    device.port = FakePort(ack=ack)
    device._closed = True  # Stops __del__ from writing to the port
    device.set_default_params()
    return device


def volts_to_bits(volts):
    return round((volts + 10) / 20 * 65535)


def test_parameter_names_match_parameter_codes():
    """The name list and the attribute map must agree, or commands carry the wrong code."""
    device = make_device()
    for code, attribute in PulsePal.PulsePalDevice._OUTPUT_PARAMETER_ATTRS.items():
        assert device._get_output_param_code(attribute) == code
    assert device._get_trigger_param_code("trigger_mode") == 128


def test_set_output_param_one_channel_uses_op_74():
    device = make_device()
    device.set_output_param("phase1_voltage", 2, 5)
    device.set_output_param("phase1_duration", 3, 0.001)
    device.set_output_param("is_biphasic", 1, 1)
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 74, 2, 2]) + struct.pack("<H", volts_to_bits(5)),
        bytes([OP_MENU_BYTE, 74, 4, 3]) + struct.pack("<I", 20),
        bytes([OP_MENU_BYTE, 74, 1, 1, 1]),
    ]
    assert device.phase1_voltage[2] == 5
    assert device.phase1_duration[3] == 0.001


def test_set_output_param_all_channels_uses_op_91():
    device = make_device()
    device.set_output_param("phase1_voltage", [1, 2, 3, 4], [-10, 0, 5, 10])
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 91, 2])
        + struct.pack("<4H", 0, volts_to_bits(0), volts_to_bits(5), 65535)
    ]
    assert device.phase1_voltage[1:5] == [-10, 0, 5, 10]


def test_set_output_param_channel_subset_uses_op_74_per_channel():
    device = make_device()
    device.set_output_param("pulse_train_duration", [2, 4], 1)
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 74, 10, 2]) + struct.pack("<I", 20000),
        bytes([OP_MENU_BYTE, 74, 10, 4]) + struct.pack("<I", 20000),
    ]


def test_set_output_param_all_channels_on_firmware_v21_uses_op_74():
    device = make_device(firmware_version=21, n_trains=2, max_pulses=5000)
    device.set_output_param("resting_voltage", [1, 2, 3, 4], 0)
    assert len(device.port.writes) == 4
    assert all(message[:2] == bytes([OP_MENU_BYTE, 74]) for message in device.port.writes)


def test_set_output_param_rejects_bad_channels_and_value_counts():
    for channel, value in [([1, 1], 5), ([1, 5], 5), ([], 5), ([1, 2, 3, 4], [1, 2])]:
        device = make_device()
        try:
            device.set_output_param("phase1_voltage", channel, value)
            raise AssertionError(f"no error for channel={channel}, value={value}")
        except PulsePal.PulsePalError:
            assert device.port.writes == []


def test_set_trigger_param_uses_op_74_with_code_128():
    device = make_device()
    device.set_trigger_param("trigger_mode", 2, 2)
    assert device.port.writes == [bytes([OP_MENU_BYTE, 74, 128, 2, 2])]


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
        device = make_device(firmware_version=21, n_trains=2, max_pulses=5000)
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
        try:
            device.send_custom_pulse_train(train_id, [0], [5])
            raise AssertionError(f"no error for train id {train_id!r}")
        except PulsePal.PulsePalError:
            assert device.port.writes == []

    device = make_device(max_pulses=3)
    try:
        device.send_custom_pulse_train(1, [0, 1, 2, 3], [1, 2, 3, 4])
        raise AssertionError("no error for a train with too many pulses")
    except PulsePal.PulsePalError:
        assert device.port.writes == []


def test_waveform_matches_an_equivalent_pulse_train():
    waveform_device = make_device()
    waveform_device.send_custom_waveform(2, 0.001, [1, 2, 3])
    train_device = make_device()
    train_device.send_custom_pulse_train(2, [0, 0.001, 0.002], [1, 2, 3])
    assert waveform_device.port.writes == train_device.port.writes


def test_a_rejected_command_raises():
    """The firmware replies 0 when it rejects a command (see /Firmware/PROTOCOL.md)."""
    device = make_device(ack=0)
    try:
        device.set_output_param("phase1_voltage", 1, 5)
        raise AssertionError("a reply of 0 did not raise")
    except PulsePal.PulsePalError as error:
        assert "rejected" in str(error)


def test_sync_to_device_uses_op_92_and_op_73_by_firmware_version():
    device = make_device()
    device.sync_to_device()
    assert device.port.writes[0][:2] == bytes([OP_MENU_BYTE, 92])
    legacy_device = make_device(firmware_version=21, n_trains=2, max_pulses=5000)
    legacy_device.sync_to_device()
    assert legacy_device.port.writes[0][:2] == bytes([OP_MENU_BYTE, 73])


def test_stop_and_trigger_message_bytes():
    device = make_device()
    device.trigger([1, 3])
    device.stop()
    device.stop([2])
    assert device.port.writes == [
        bytes([OP_MENU_BYTE, 77, 0b0101]),
        bytes([OP_MENU_BYTE, 98, 0b1111]),
        bytes([OP_MENU_BYTE, 98, 0b0010]),
    ]


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
            failures.append(f"{test.__name__}: {error}")
    print(f"{len(tests) - len(failures)}/{len(tests)} tests passed")
    for failure in failures:
        print("  FAILED " + failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
