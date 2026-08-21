"""
Python interface for the [Pulse Pal](https://sites.google.com/site/pulsepalwiki/)
open source pulse train generator.

Pulse Pal delivers precisely timed voltage pulse trains on four analog
output channels, and can be triggered by TTL logic on two trigger
channels or in software. This module configures and triggers the device
over its USB serial port.

Everything is accessed through `PulsePalDevice`. Import it, connect to
the device's serial port, program parameters, and trigger:

```python
from PulsePal import PulsePalDevice

with PulsePalDevice("COM3") as P:       # /dev/ttyACM0 on Linux
    P.set_output_param("phase1_voltage", 1, 5)
    P.set_output_param("phase1_duration", 1, 0.001)
    P.set_output_param("pulse_train_duration", 1, 2)
    P.trigger(1)
```

## Parameter arrays

Output parameters are exposed as plain Python lists on the device
object, one list per parameter. Each list has five elements so that the
list index matches the Pulse Pal channel number: index 0 is unused and
holds `nan`, and indices 1 to 4 hold the values for output channels
1-4. `PulsePalDevice.trigger_mode` follows the same convention with
three elements, for trigger channels 1 and 2.

```python
P.phase1_voltage[2] = 7                  # channel 2 only
P.inter_pulse_interval[1:5] = [0.2] * 4  # all four channels
P.sync_to_device()             # push the edits to the device
```

Editing these lists changes only the local copy. Call
`PulsePalDevice.sync_to_device` to program the device, or use
`PulsePalDevice.set_output_param` and
`PulsePalDevice.set_trigger_param`, which program a single parameter
immediately and keep the local copy in step.

## Units

Voltages are in volts in the range [-10, 10]. Times are in seconds, and
are rounded to the nearest cycle of the device's hardware timer (see
`DeviceInfo.cycle_frequency`). Enumerated parameters are integers, and
their meanings are given with each attribute below.

## Further reading

- Parameter guide:
  https://sites.google.com/site/pulsepalwiki/parameter-guide
- Serial interface and general documentation:
  https://sites.google.com/site/pulsepalwiki/

## License

This file is part of the Sanworks PulsePal repository.
Copyright (C) Sanworks LLC, Rochester, New York, USA

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed WITHOUT ANY WARRANTY and without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
"""

from decimal import Decimal
from dataclasses import dataclass
import numbers
import struct
import time

import numpy as np
import serial

__all__ = ["PulsePalDevice", "DeviceInfo", "PulsePalError"]
__docformat__ = "google"


class PulsePalError(Exception):
    """Raised when Pulse Pal communication or configuration fails.

    This covers serial reads that time out, short serial writes, missing
    acknowledgement bytes, unknown parameter names, values that do not
    fit the datatype expected by the device, and operations that the
    connected firmware or hardware revision does not support.
    """


@dataclass
class DeviceInfo:
    """Properties of the connected Pulse Pal device.

    An instance is created for each connection and populated during the
    handshake in `PulsePalDevice.__init__`. It is available as
    `PulsePalDevice.info`. Devices running firmware v21 report only a
    firmware version, so the remaining fields are filled in with the
    known values for Pulse Pal hardware v2.

    ```python
    print(P.info.firmware_version)
    ```
    """

    output_parameter_names: list = None
    """Output parameter names, ordered by parameter code.

    The position of a name in this list, plus 1, is the parameter code
    the device expects. Any name here is valid as the `param_name`
    argument of `PulsePalDevice.set_output_param`, and is also the name
    of the matching parameter array attribute on `PulsePalDevice`.
    """

    trigger_parameter_names: list = None
    """Trigger parameter names, accepted by
    `PulsePalDevice.set_trigger_param`."""

    firmware_version: int = None
    """Firmware version running on the connected device."""

    hardware_version: int = None
    """Hardware revision of the connected device, e.g. `2` or `3`.

    Reported by the device on firmware v22 and newer; assumed to be `2`
    on older firmware.
    """

    max_custom_pulses: int = None
    """Maximum number of pulses in a single custom pulse train."""

    n_custom_pulse_trains: int = None
    """Number of custom pulse trains the device can store."""

    cycle_frequency: float = None
    """Update frequency of the device's hardware timer, in Hz.

    All time parameters are rounded to a whole number of these cycles,
    so this sets the timing resolution of the device.
    """

    cycle_period_us: float = None
    """Update period of the device's hardware timer, in microseconds."""


class PulsePalDevice:
    """A class to control a Pulse Pal device on a USB serial port.

    Creating an instance opens the serial port, exchanges a handshake
    with the device, verifies that its firmware is supported, reads the
    device properties into `PulsePalDevice.info`, and programs the
    device with the default parameters.

    ```python
    from PulsePal import PulsePalDevice

    P = PulsePalDevice("COM3")
    P.set_output_param("phase1_voltage", 1, 5)
    P.trigger(1)
    P.close()
    ```

    The class is also a context manager, which closes the connection on
    exit even if an error is raised:

    ```python
    with PulsePalDevice("COM3") as P:
        P.trigger(1)
    ```

    The attributes below named after Pulse Pal parameters are the local
    copy of the device's program. Each is a five element list indexed by
    channel number, with index 0 unused. Assigning to them does not
    reach the device until `PulsePalDevice.sync_to_device` is called;
    `PulsePalDevice.set_output_param` programs one parameter right away.
    """

    port: "serial.Serial"
    """The open `serial.Serial` port connected to the device."""

    info: DeviceInfo
    """Properties of the connected device. See `DeviceInfo`."""

    is_biphasic: list
    """Pulse shape per channel: `0` for monophasic, `1` for biphasic.

    Monophasic pulses use only the phase 1 parameters. Biphasic pulses
    follow phase 1 with `PulsePalDevice.inter_phase_interval` and then
    phase 2.
    """

    phase1_voltage: list
    """Voltage of the first phase of each pulse, in volts [-10, 10]."""

    phase2_voltage: list
    """Voltage of the second phase of each pulse, in volts [-10, 10].

    Used only when `PulsePalDevice.is_biphasic` is `1` for the channel.
    """

    resting_voltage: list
    """Voltage held between pulses, in volts [-10, 10]."""

    phase1_duration: list
    """Duration of the first phase of each pulse, in seconds."""

    inter_phase_interval: list
    """Interval between the two phases of a biphasic pulse, in seconds.

    The channel rests at `PulsePalDevice.resting_voltage` during the
    interval. Used only when `PulsePalDevice.is_biphasic` is `1`.
    """

    phase2_duration: list
    """Duration of the second phase of each pulse, in seconds.

    Used only when `PulsePalDevice.is_biphasic` is `1` for the channel.
    """

    inter_pulse_interval: list
    """Interval from the end of one pulse to the onset of the next, in
    seconds."""

    burst_duration: list
    """Duration of each burst of pulses, in seconds.

    Set to `0` to disable bursts, so that pulses continue for the whole
    pulse train.
    """

    inter_burst_interval: list
    """Interval between bursts of pulses, in seconds.

    The channel rests at `PulsePalDevice.resting_voltage` between
    bursts. Ignored when `PulsePalDevice.burst_duration` is `0`.
    """

    pulse_train_duration: list
    """Total duration of the pulse train, in seconds."""

    pulse_train_delay: list
    """Delay from the trigger to the onset of the pulse train, in
    seconds."""

    link_trigger_channel1: list
    """Whether each output channel is linked to trigger channel 1.

    `1` links the output channel to trigger channel 1, `0` unlinks it.
    """

    link_trigger_channel2: list
    """Whether each output channel is linked to trigger channel 2.

    `1` links the output channel to trigger channel 2, `0` unlinks it.
    """

    custom_train_id: list
    """Custom pulse train played by each output channel.

    `0` plays the parametrically defined train. `1` or higher plays the
    matching custom train, previously loaded with
    `PulsePalDevice.send_custom_pulse_train` or
    `PulsePalDevice.send_custom_waveform`.
    """

    custom_train_target: list
    """What the timestamps of a custom train mark.

    `0` if each timestamp is the onset of a pulse, `1` if each timestamp
    is the onset of a burst of pulses.
    """

    custom_train_loop: list
    """Whether a custom train repeats.

    `1` loops the custom train until
    `PulsePalDevice.pulse_train_duration` has elapsed, `0` plays it
    once.
    """

    trigger_mode: list
    """Response of each trigger channel to an incoming TTL pulse.

    Three element list indexed by trigger channel, with index 0 unused.
    Elements 1 and 2 control the respective channels on the device.
    Their values can be:

    - `0` (normal): a TTL rising edge starts the pulse train, and edges
      during the train are ignored.
    - `1` (toggle): same as 0 but a TTL rising edge during the train stops it.
    - `2` (pulse gated): the train runs only while the trigger TTL is high.
    """

    _CURRENT_FIRMWARE_VERSION = 22

    _OP_MENU_BYTE = 213
    _HANDSHAKE_OPCODE = 72
    _HANDSHAKE_RESPONSE = 75
    _DAC_BITMAX = 65535
    _OLDEST_FIRMWARE_SUPPORTED = 21

    _OUTPUT_PARAMETER_NAMES = (
        "is_biphasic",
        "phase1_voltage",
        "phase2_voltage",
        "phase1_duration",
        "inter_phase_interval",
        "phase2_duration",
        "inter_pulse_interval",
        "burst_duration",
        "inter_burst_interval",
        "pulse_train_duration",
        "pulse_train_delay",
        "link_trigger_channel1",
        "link_trigger_channel2",
        "custom_train_id",
        "custom_train_target",
        "custom_train_loop",
        "resting_voltage",
    )
    _TRIGGER_PARAMETER_NAMES = ("trigger_mode",)

    _OUTPUT_PARAMETER_ATTRS = {
        1: "is_biphasic",
        2: "phase1_voltage",
        3: "phase2_voltage",
        4: "phase1_duration",
        5: "inter_phase_interval",
        6: "phase2_duration",
        7: "inter_pulse_interval",
        8: "burst_duration",
        9: "inter_burst_interval",
        10: "pulse_train_duration",
        11: "pulse_train_delay",
        12: "link_trigger_channel1",
        13: "link_trigger_channel2",
        14: "custom_train_id",
        15: "custom_train_target",
        16: "custom_train_loop",
        17: "resting_voltage",
    }
    _ENDIANNESS = "<"
    _STRUCT_FORMATS = {
        "uint8": "B",
        "int8": "b",
        "char": "c",
        "uint16": "H",
        "int16": "h",
        "uint32": "I",
        "int32": "i",
        "single": "f",
        "double": "d",
    }
    _TYPE_RANGES = {
        "uint8": (0, 2**8 - 1),
        "int8": (-(2**7), 2**7 - 1),
        "uint16": (0, 2**16 - 1),
        "int16": (-(2**15), 2**15 - 1),
        "uint32": (0, 2**32 - 1),
        "int32": (-(2**31), 2**31 - 1),
    }

    def __init__(self, port_name, baud_rate=12000000, timeout=10):
        """Open a connection to a Pulse Pal device.

        Opens the serial port, exchanges the handshake, verifies the
        firmware version, reads the device properties into
        `PulsePalDevice.info`, and programs the device with the default
        parameters.

        Args:
            port_name: USB serial port for the Pulse Pal device, such as
                `COM3` on Windows or `/dev/ttyACM0` on Linux.
            baud_rate: Serial baud rate.
            timeout: Serial read timeout, in seconds.

        Raises:
            PulsePalError: If the device does not return the expected
                handshake, or its firmware is older than v21, or its
                firmware is newer than this module supports.
            serial.SerialException: If the serial port cannot be opened.
        """
        self.info = DeviceInfo()
        self._gui = None
        self.port = serial.Serial(
            port_name,
            baud_rate,
            timeout=timeout,
            rtscts=True,
        )
        self._closed = False
        self._dac_bit_max = self._to_decimal(0)
        self.info.firmware_version = None
        self.info.hardware_version = None
        self.info.output_parameter_names = list(self._OUTPUT_PARAMETER_NAMES)
        self.info.trigger_parameter_names = list(self._TRIGGER_PARAMETER_NAMES)

        self._write_serial(
            (self._OP_MENU_BYTE, self._HANDSHAKE_OPCODE),
            "uint8",
        )
        handshake = self._read_serial(1, "uint8")
        if handshake != self._HANDSHAKE_RESPONSE:
            self.close(send_disconnect=False)
            raise PulsePalError(
                "Error: incorrect handshake returned. Expected "
                f"{self._HANDSHAKE_RESPONSE}, received {handshake}."
            )

        firmware_version = self._read_serial(1, "uint32")
        if firmware_version < self._OLDEST_FIRMWARE_SUPPORTED:
            raise PulsePalError(
                "Error: Old firmware detected, v"
                f"{firmware_version}. v{self._OLDEST_FIRMWARE_SUPPORTED} or "
                "newer is required."
            )
        if firmware_version > self._CURRENT_FIRMWARE_VERSION:
            raise PulsePalError(
                "Error: Future firmware detected, v"
                f"{firmware_version}. Please update PulsePal.py or downgrade "
                f"firmware to v{self._CURRENT_FIRMWARE_VERSION}."
            )
        if firmware_version < self._CURRENT_FIRMWARE_VERSION:
            print(
                "Old firmware detected, v"
                f"{firmware_version}. This firmware is supported. Update to v"
                f"{self._CURRENT_FIRMWARE_VERSION} is available."
            )
        self._dac_bit_max = self._to_decimal(self._DAC_BITMAX)
        self.info.firmware_version = firmware_version

        if self.info.firmware_version > 21:
            self._write_serial((self._OP_MENU_BYTE, 94), "uint8")
            self.info.hardware_version = self._read_serial(1, "uint8")
            self.info.cycle_period_us = self._read_serial(1, "uint32")
            self.info.cycle_frequency = 1 / (
                self.info.cycle_period_us / 1000000
            )
            self.info.n_custom_pulse_trains = self._read_serial(1, "uint8")
            self.info.max_custom_pulses = self._read_serial(1, "uint32")
        else:
            self.info.hardware_version = 2
            self.info.cycle_period_us = 50
            self.info.cycle_frequency = 20000
            self.info.n_custom_pulse_trains = 2
            self.info.max_custom_pulses = 5000

        # Client name op + "PYTHON" in ASCII.
        self._write_serial(
            (self._OP_MENU_BYTE, 89, 80, 89, 84, 72, 79, 78),
            "uint8",
        )

        self.set_default_params()
        self.sync_to_device()

    def set_default_params(self):
        """Reset the local copy of all parameters to their defaults.

        The defaults are a 1 ms, +5 V monophasic pulse every 10 ms for
        1 second, on all four output channels, linked to trigger channel
        1 in normal trigger mode.

        This updates only the local copy. Call
        `PulsePalDevice.sync_to_device` to program the device with them.
        """
        nan = float("nan")
        self.is_biphasic = [nan, 0, 0, 0, 0]
        self.phase1_voltage = [nan, 5, 5, 5, 5]
        self.phase2_voltage = [nan, -5, -5, -5, -5]
        self.resting_voltage = [nan, 0, 0, 0, 0]
        self.phase1_duration = [nan, 0.001, 0.001, 0.001, 0.001]
        self.inter_phase_interval = [nan, 0.001, 0.001, 0.001, 0.001]
        self.phase2_duration = [nan, 0.001, 0.001, 0.001, 0.001]
        self.inter_pulse_interval = [nan, 0.01, 0.01, 0.01, 0.01]
        self.burst_duration = [nan, 0, 0, 0, 0]
        self.inter_burst_interval = [nan, 0, 0, 0, 0]
        self.pulse_train_duration = [nan, 1, 1, 1, 1]
        self.pulse_train_delay = [nan, 0, 0, 0, 0]
        self.link_trigger_channel1 = [nan, 1, 1, 1, 1]
        self.link_trigger_channel2 = [nan, 0, 0, 0, 0]
        self.custom_train_id = [nan, 0, 0, 0, 0]
        self.custom_train_target = [nan, 0, 0, 0, 0]
        self.custom_train_loop = [nan, 0, 0, 0, 0]
        self.trigger_mode = [nan, 0, 0]

    def set_voltage(self, channel, voltage):
        """Set an output channel to a fixed voltage.

        The channel holds the voltage until it is set again or until a
        pulse train is triggered on it.

        Args:
            channel: Output channel number, 1-4.
            voltage: Voltage to set, in volts [-10, 10].

        Raises:
            PulsePalError: If the device does not acknowledge the
                command.
        """
        voltage_bits = self._volts_to_bits(voltage)
        self._write_serial(
            (self._OP_MENU_BYTE, 79, channel),
            "uint8",
            voltage_bits,
            "uint16",
        )
        self._read_ack("set_fixed_voltage()")

    def set_calibration(self, channel, voltage_offset):
        """Calibrate the zero code of an output channel.

        The offset is added to every voltage the channel produces, to
        correct for DAC offset error. It is stored in the device's
        EEPROM and reloaded on boot, so it only needs to be set once.

        Requires Pulse Pal hardware v3 or newer.

        Args:
            channel: Output channel number, 1-4.
            voltage_offset: Offset to apply, in volts [-0.1, 0.1].

        Raises:
            PulsePalError: If the connected hardware is older than v3,
                or the device does not acknowledge the command.
            ValueError: If `channel` is not 1-4, or `voltage_offset` is
                outside [-0.1, 0.1].
        """
        if self.info.hardware_version < 3:
            raise PulsePalError(
                "set_calibration() requires hardware v3 or newer."
            )
        if channel not in (1, 2, 3, 4):
            raise ValueError("channel must be 1, 2, 3 or 4")
        if voltage_offset < -0.1 or voltage_offset > 0.1:
            raise ValueError(
                "voltage_offset for zero code calibration must be in range "
                "[-0.1, 0.1]"
            )
        voltage_bits = voltage_offset * (1 / (20 / 65536))
        self._write_serial(
            (self._OP_MENU_BYTE, 96, channel - 1),
            "uint8",
            voltage_bits,
            "int16",
        )
        self._read_ack("set_calibration()")

    def set_output_param(self, param_name, channel, value):
        """Program a single output channel parameter on the device.

        The local copy of the parameter is updated to match, so a later
        `PulsePalDevice.sync_to_device` will not undo the change.

        ```python
        P.set_output_param("is_biphasic", 1, 1)
        P.set_output_param("phase1_voltage", 1, 10)
        P.set_output_param(3, 1, -10)   # same, by param code
        ```

        Args:
            param_name: Parameter name, as listed in
                `DeviceInfo.output_parameter_names`, or its integer
                parameter code.
            channel: Output channel number, 1-4.
            value: Value to set. Units are volts for voltage parameters,
                seconds for time parameters, and integers for enumerated
                parameters. See the attributes of `PulsePalDevice` for
                the meaning of each parameter.

        Raises:
            PulsePalError: If the parameter name is not recognized, the
                value does not fit the datatype the device expects, or
                the device does not acknowledge the command.
        """
        original_value = value
        param_code = self._get_output_param_code(param_name)

        if param_code in (2, 3, 17):
            value = self._volts_to_bits(value)
            self._write_serial(
                (self._OP_MENU_BYTE, 74, param_code, channel),
                "uint8",
                value,
                "uint16",
            )
        elif 4 <= param_code <= 11:
            self._write_serial(
                (self._OP_MENU_BYTE, 74, param_code, channel),
                "uint8",
                self._seconds_to_cycles(value),
                "uint32",
            )
        else:
            self._write_serial(
                (self._OP_MENU_BYTE, 74, param_code, channel, value),
                "uint8",
            )

        self._read_ack("program_output_channel_param()")
        self._set_output_param_value(param_code, channel, original_value)

    def set_trigger_param(self, param_name, channel, value):
        """Program a single trigger channel parameter on the device.

        The local copy of the parameter is updated to match, so a later
        `PulsePalDevice.sync_to_device` will not undo the change.

        ```python
        P.set_trigger_param("trigger_mode", 1, 2)  # pulse gated
        ```

        Args:
            param_name: Parameter name, as listed in
                `DeviceInfo.trigger_parameter_names`, or its integer
                parameter code.
            channel: Trigger channel number, 1-2.
            value: Value to set. See `PulsePalDevice.trigger_mode` for
                the trigger modes.

        Raises:
            PulsePalError: If the parameter name is not recognized, the
                value does not fit the datatype the device expects, or
                the device does not acknowledge the command.
        """
        original_value = value
        param_code = self._get_trigger_param_code(param_name)

        self._write_serial(
            (self._OP_MENU_BYTE, 74, param_code, channel, value),
            "uint8",
        )
        self._read_ack("program_trigger_channel_param()")

        if param_code in (1, 128):
            self.trigger_mode[channel] = original_value

    def sync_to_device(self):
        """Program the device with the local copy of all parameters.

        Call this after assigning to the parameter array attributes, to
        send every output and trigger parameter to the device in a
        single transaction.

        ```python
        P.phase1_voltage[1:5] = [5] * 4
        P.sync_to_device()
        ```

        Raises:
            PulsePalError: If the device does not acknowledge the
                command.
        """
        # _sync_all_params() for firmware v22+ uses the newer packed sync
        # opcode (92). _sync_all_params_legacy() uses the less efficient
        # legacy packed sync opcode (73).
        if self.info.firmware_version > 21:
            self._sync_all_params()
        else:
            self._sync_all_params_legacy()
        self._read_ack("sync_to_device()")

    def sync_from_device(self):
        """Read all parameters from the device into the local copy.

        Overwrites every parameter array attribute with the program
        currently stored on the device. Useful after the device has been
        reprogrammed from its thumb joystick.

        Requires firmware v22 or newer.

        Raises:
            PulsePalError: If the connected firmware is older than v22,
                or the device does not return the full parameter set.
        """
        self._require_firmware(22, "sync_from_device()")
        self._write_serial((self._OP_MENU_BYTE, 93), "uint8")
        for attr_name in (
                "phase1_duration",
                "inter_phase_interval",
                "phase2_duration",
                "inter_pulse_interval",
                "burst_duration",
                "inter_burst_interval",
                "pulse_train_duration",
                "pulse_train_delay",
        ):
            setattr(
                self,
                attr_name,
                [float("nan")]
                + [
                    self._cycles_to_seconds(x)
                    for x in self._read_serial(4, "uint32")
                ],
            )

        for attr_name in (
                "phase1_voltage",
                "phase2_voltage",
                "resting_voltage",
        ):
            setattr(
                self,
                attr_name,
                [float("nan")]
                + [
                    self._bits_to_volts(x)
                    for x in self._read_serial(4, "uint16")
                ],
            )

        for attr_name in (
                "is_biphasic",
                "custom_train_id",
                "custom_train_target",
                "custom_train_loop",
                "link_trigger_channel1",
                "link_trigger_channel2",
        ):
            setattr(
                self,
                attr_name,
                [float("nan")] + self._read_serial(4, "uint8"),
            )
        self.trigger_mode = [float("nan")] + self._read_serial(2, "uint8")

    def send_custom_pulse_train(
        self,
        custom_train_id,
        pulse_times,
        pulse_voltages,
    ):
        """Load a custom pulse train onto the device.

        A custom pulse train is an arbitrary list of pulse onset times
        and voltages, replacing the parametric pulse voltage and onset timing.
        Set `PulsePalDevice.custom_train_id` on an output channel to play
        the train there.

        ```python
        P.send_custom_pulse_train(
            2, [0, 0.2, 0.5, 1], [8, 4, -3.5, -10]
        )
        P.set_output_param("custom_train_id", 1, 2)
        ```

        Args:
            custom_train_id: Custom train to load, 1-2. See
                `DeviceInfo.n_custom_pulse_trains`.
            pulse_times: Pulse onset times, in seconds, relative to the
                start of the train. Accepts a list, tuple or NumPy
                array.
            pulse_voltages: Voltage of each pulse, in volts [-10, 10].
                Must be the same length as `pulse_times`.

        Raises:
            PulsePalError: If `pulse_times` and `pulse_voltages` differ
                in length, or the device does not acknowledge the
                command.
        """
        pulse_times = self._as_list(pulse_times)
        pulse_voltages = self._as_list(pulse_voltages)
        n_pulses = len(pulse_times)
        if n_pulses != len(pulse_voltages):
            raise PulsePalError(
                "pulse_times and pulse_voltages must be the same length."
            )

        pulse_times_cycles = [
            self._seconds_to_cycles(pulse_time)
            for pulse_time in pulse_times
        ]
        pulse_voltage_bits = [
            self._volts_to_bits(voltage)
            for voltage in pulse_voltages
        ]

        op_code = int(custom_train_id) + 74
        self._write_serial(
            (self._OP_MENU_BYTE, op_code),
            "uint8",
            n_pulses,
            "uint32",
            pulse_times_cycles,
            "uint32",
            pulse_voltage_bits,
            "uint16",
        )
        self._read_ack("send_custom_pulse_train()")

    def send_custom_waveform(
        self,
        custom_train_id,
        pulse_width,
        pulse_voltages,
    ):
        """Load an arbitrary waveform onto the device.

        A convenience shorthand for
        `PulsePalDevice.send_custom_pulse_train` with evenly spaced,
        confluent pulses, so that `pulse_voltages` is played as a
        waveform sampled every `pulse_width` seconds.

        Set the channel's `PulsePalDevice.phase1_duration` to
        `pulse_width` as well, so that each sample is held for the
        sampling period.

        ```python
        import math

        samples = [math.sin(i / 10.0) * 10 for i in range(1000)]
        P.send_custom_waveform(1, 0.001, samples)   # 1 kHz
        P.set_output_param("custom_train_id", 2, 1)
        P.set_output_param("phase1_duration", 2, 0.001)
        ```

        Args:
            custom_train_id: Custom train to load, 1-2. See
                `DeviceInfo.n_custom_pulse_trains`.
            pulse_width: Sampling period, in seconds. Each voltage is
                held for this long.
            pulse_voltages: Waveform samples, in volts [-10, 10].
                Accepts a list, tuple or NumPy array.

        Raises:
            PulsePalError: If the device does not acknowledge the
                command.
        """
        pulse_voltages = self._as_list(pulse_voltages)
        n_pulses = len(pulse_voltages)
        pulse_width_cycles = self._seconds_to_cycles(pulse_width)
        pulse_times = [pulse_width_cycles * i for i in range(n_pulses)]
        pulse_voltage_bits = [
            self._volts_to_bits(voltage)
            for voltage in pulse_voltages
        ]

        op_code = int(custom_train_id) + 74
        self._write_serial(
            (self._OP_MENU_BYTE, op_code),
            "uint8",
            n_pulses,
            "uint32",
            pulse_times,
            "uint32",
            pulse_voltage_bits,
            "uint16",
        )
        self._read_ack("send_custom_waveform()")

    def set_continuous_loop(self, channel, state):
        """Set the continuous loop state of an output channel.

        In continuous loop mode the channel repeats its pulse train
        indefinitely the next time it is triggered, until the mode is
        cleared or `PulsePalDevice.stop` is called.

        Args:
            channel: Output channel number, 1-4.
            state: `1` for continuous loop, `0` for normal mode.
        """
        self._write_serial(
            (self._OP_MENU_BYTE, 82, channel, state),
            "uint8",
        )

    def trigger(
        self,
        channel1=None,
        channel2=None,
        channel3=None,
        channel4=None,
    ):
        """Trigger output channels in software.

        Triggered channels start their pulse trains together. Three
        calling conventions are accepted:

        ```python
        P.trigger(1, 0, 1, 0)   # one flag per channel
        P.trigger(3)            # a single channel number
        P.trigger([1, 4])       # several channel numbers
        ```

        Channel numbers outside 1-4 are ignored.

        Args:
            channel1: `1` to trigger channel 1, otherwise `0`; or, when
                it is the only argument given, a single channel number
                or a list of channel numbers to trigger.
            channel2: `1` to trigger channel 2, otherwise `0`.
            channel3: `1` to trigger channel 3, otherwise `0`.
            channel4: `1` to trigger channel 4, otherwise `0`.
        """
        trigger_byte = 0

        # Options 2 & 3: Only one argument was provided
        if channel2 is None and channel3 is None and channel4 is None:
            # Option 2: Single integer
            if isinstance(channel1, int):
                channels_to_trigger = [channel1]
            # Option 3: List/Tuple of integers
            elif isinstance(channel1, (list, tuple, set)):
                channels_to_trigger = channel1
            else:
                channels_to_trigger = []

            # Use bitwise shifts to calculate the trigger byte
            # (ch1=bit0, ch2=bit1, etc.)
            for ch in channels_to_trigger:
                if 1 <= ch <= 4:
                    trigger_byte |= (1 << (ch - 1))

        # Option 1: Original input scheme (logicals for each channel)
        else:
            # Fallback to 0 if an argument was omitted via kwargs
            c1 = channel1 if channel1 is not None else 0
            c2 = channel2 if channel2 is not None else 0
            c3 = channel3 if channel3 is not None else 0
            c4 = channel4 if channel4 is not None else 0

            trigger_byte = (
                (1 * c1)
                + (2 * c2)
                + (4 * c3)
                + (8 * c4)
            )

        self._write_serial((self._OP_MENU_BYTE, 77, trigger_byte), "uint8")

    def sd_settings(self, settings_file_name, op):
        """Save, load, or delete a settings file on the microSD card.

        A settings file holds a complete Pulse Pal program, so that it
        can be recalled later from software or from the device's front
        panel. Loading a file also refreshes the local copy of the
        parameters, via `PulsePalDevice.sync_from_device`.

        ```python
        P.sd_settings("MyProtocol.pps", "save")
        ```

        Args:
            settings_file_name: Settings file name, at most 15 ASCII
                characters including the required `.pps` extension.
            op: `"save"`, `"load"`, or `"delete"`.

        Raises:
            PulsePalError: If the file name has no `.pps` extension or
                is too long, `op` is not one of the three operations, or
                the device does not acknowledge the command.
        """
        if ".pps" not in settings_file_name:
            raise PulsePalError(
                "Error: The file name must have a valid .pps extension."
            )
        op_byte_by_name = {"save": 1, "load": 2, "delete": 3}
        try:
            op_byte = op_byte_by_name[str(op).lower()]
        except KeyError as exc:
            raise PulsePalError(
                "File op must be: 'save', 'load' or 'delete'."
            ) from exc

        filename_bytes = settings_file_name.encode("ascii")
        if len(filename_bytes) > 15:
            raise PulsePalError("settings_file_name is too long.")
        self._write_serial(
            (self._OP_MENU_BYTE, 90, op_byte, len(filename_bytes)),
            "uint8",
            list(filename_bytes),
            "uint8",
        )
        if self.info.firmware_version > 21:
            self._read_ack("sd_settings()")
        if op_byte == 2:
            time.sleep(0.1)
            self.sync_from_device()

    def stop(self):
        """Stop all pulse trains currently playing on the device.

        Every output channel returns to its
        `PulsePalDevice.resting_voltage`.
        """
        self._write_serial((self._OP_MENU_BYTE, 80), "uint8")

    def format_microsd(self, timeout=30):
        """Format the device's microSD card.

        Erases every settings file stored on the device and resets its
        parameters to the defaults. The user is prompted at the console
        to confirm before anything is erased.

        Requires Pulse Pal hardware v3 or newer.

        Args:
            timeout: Seconds to wait for the device to report that
                formatting has finished.

        Returns:
            `None` once the card has been formatted, or an empty string
            if the user declines the confirmation prompt.

        Raises:
            PulsePalError: If the connected hardware is older than v3.
        """
        if self.info.hardware_version < 3:
            raise PulsePalError(
                "format_microsd() requires hardware v3 or newer."
            )

        print("*** Pulse Pal microSD Formatter ***")
        print("This will format Pulse Pal's microSD card,")
        print("erase all settings files on the device")
        print("and reset all parameters to defaults.")

        reply = input("Do you want to continue (y/n)")

        if reply.strip().lower() != "y":
            print("Choice confirmed - microSD Card NOT formatted.")
            return ""

        self._write_serial((self._OP_MENU_BYTE, 97), "uint8")

        start = time.time()
        message = bytearray()

        while time.time() - start < timeout:
            n_waiting = self.bytes_available()
            if n_waiting:
                message.extend(self.port.read(n_waiting))
                if ord("!") in message:
                    break
            time.sleep(0.01)
        raw_message = bytes(message)
        flag_index = raw_message.find(b"!")
        if flag_index >= 0:
            displayed_message = raw_message[:flag_index]
        else:
            displayed_message = raw_message

        text = displayed_message.decode("ascii", errors="replace").rstrip()
        if text:
            print(text)

        self.set_default_params()
        return None

    def gui(self, block=None, theme=None):
        """Open the Pulse Pal parameter GUI, or focus an open one.

        The GUI edits its own copy of the parameters, and loads them to
        the device when its 'Load to Device' button is clicked. The
        window closes automatically when the device is closed or
        deleted.

        Calling this while the GUI is already open focuses the existing
        window rather than opening a second one.

        Args:
            block: If `True`, the call returns when the GUI is closed. If
                `False`, the call returns immediately, and the host
                application must run the Tk event loop. If `None`, the
                GUI blocks only when the host does not already provide a
                Tk event loop, e.g. when launched from a script.
            theme: `"light"` or `"dark"` to select the color theme, or
                `None` to match the desktop theme. Passing a theme to an
                already-open GUI recolors it in place.

        Returns:
            The `PulsePalGUI.PulsePalGUI` instance driving the window.

        Raises:
            ValueError: If the theme name is not recognized.
        """
        gui = getattr(self, "_gui", None)
        if gui is not None and not gui.is_closed:
            if theme is not None:
                gui.set_theme(theme)
            gui.focus()
            return gui

        try:
            from .PulsePalGUI import PulsePalGUI
        except ImportError:
            from PulsePalGUI import PulsePalGUI

        gui = PulsePalGUI(self, theme=theme)
        self._gui = gui
        gui.start(block=block)
        return gui

    def close(self, send_disconnect=True):
        """Close the connection to the device, and the GUI if open.

        Safe to call more than once; later calls do nothing. Called
        automatically when leaving a `with` block and when the object is
        garbage collected.

        Args:
            send_disconnect: If `True`, tell the device that the client
                is disconnecting before closing the port. Set to `False`
                when the device is in an unknown state, such as after a
                failed handshake.
        """
        gui = getattr(self, "_gui", None)
        self._gui = None
        if gui is not None:
            try:
                gui.close()
            except Exception:
                # Cleanup must not raise; Tk may already be torn down
                pass

        if getattr(self, "_closed", True):
            return

        try:
            if send_disconnect and self.port and self.port.is_open:
                self._write_serial((self._OP_MENU_BYTE, 81), "uint8")
        finally:
            if self.port and self.port.is_open:
                self.port.close()
            self._closed = True

    def bytes_available(self):
        """Return the number of bytes waiting in the serial read buffer.

        Returns:
            Count of bytes that can be read without blocking.
        """
        return self.port.in_waiting

    def _get_output_param_code(self, param_name):
        """Resolve an output parameter name or code to its code."""
        if isinstance(param_name, str):
            try:
                return self.info.output_parameter_names.index(param_name) + 1
            except ValueError as exc:
                raise PulsePalError(
                    f"Unknown output parameter: {param_name}."
                ) from exc
        return int(param_name)

    def _get_trigger_param_code(self, param_name):
        """Resolve a trigger parameter name or code to its code."""
        if isinstance(param_name, str):
            try:
                index = self.info.trigger_parameter_names.index(param_name)
                return index + 128
            except ValueError as exc:
                raise PulsePalError(
                    f"Unknown trigger parameter: {param_name}."
                ) from exc
        return int(param_name)

    def _write_serial(self, *args):
        """Write one or more data/type pairs to the serial port."""
        if len(args) % 2 != 0:
            raise PulsePalError(
                "Serial writes require data/type argument pairs."
            )

        payload = bytearray()
        for i in range(0, len(args), 2):
            payload.extend(self._pack_values(args[i], args[i + 1]))

        bytes_written = self.port.write(bytes(payload))
        if bytes_written != len(payload):
            raise PulsePalError(
                f"Error: wrote {bytes_written} byte(s), expected to write "
                f"{len(payload)} byte(s)."
            )

    def _read_serial(self, n_values, datatype):
        """Read values from the serial port and unpack them with struct."""
        datatype = self._normalize_datatype(datatype)
        fmt = self._STRUCT_FORMATS[datatype]
        n_values = int(n_values)
        n_bytes = n_values * struct.calcsize(fmt)
        message_bytes = self.port.read(n_bytes)
        if len(message_bytes) < n_bytes:
            raise PulsePalError(
                f"Error: serial port timed out. "
                f"{len(message_bytes)} byte(s) read. "
                f"Expected {n_bytes} byte(s)."
            )

        values = struct.unpack(
            f"{self._ENDIANNESS}{n_values}{fmt}",
            message_bytes,
        )
        if n_values == 1:
            return values[0]
        return list(values)

    def _read_ack(self, context):
        """Read a one-byte acknowledgement from the device."""
        try:
            self._read_serial(1, "uint8")
        except PulsePalError as exc:
            raise PulsePalError(
                "Error: Pulse Pal did not return an acknowledgement byte "
                f"after a call to {context}."
            ) from exc

    def _pack_values(self, values, datatype):
        """Pack scalar, list/tuple, or NumPy array values into bytes."""
        datatype = self._normalize_datatype(datatype)
        fmt = self._STRUCT_FORMATS[datatype]
        values_list = self._as_list(values)

        if datatype == "char":
            values_list = self._normalize_char_values(values_list)
        elif datatype in self._TYPE_RANGES:
            values_list = self._normalize_int_values(values_list, datatype)
        else:
            values_list = [float(value) for value in values_list]

        return struct.pack(
            f"{self._ENDIANNESS}{len(values_list)}{fmt}",
            *values_list,
        )

    def _normalize_datatype(self, datatype):
        """Return the datatype name, rejecting unsupported types."""
        datatype = str(datatype)
        if datatype not in self._STRUCT_FORMATS:
            raise PulsePalError(
                f"Error: {datatype} is not a data type supported by "
                "PulsePalObject."
            )
        return datatype

    def _normalize_char_values(self, values):
        """Coerce str, int and bytes values to single ASCII bytes."""
        normalized = []
        for value in values:
            if isinstance(value, str):
                value = value.encode("ascii")
            if isinstance(value, int):
                value = bytes((value,))
            if not isinstance(value, (bytes, bytearray)) or len(value) != 1:
                raise PulsePalError(
                    "char values must be one-byte bytes, chars, or "
                    "integers."
                )
            normalized.append(bytes(value))
        return normalized

    def _normalize_int_values(self, values, datatype):
        """Coerce values to ints, rejecting any out of range."""
        min_value, max_value = self._TYPE_RANGES[datatype]
        normalized = []
        for value in values:
            value = int(value)
            if not min_value <= value <= max_value:
                raise PulsePalError(
                    f"Value {value} is out of range for {datatype} "
                    f"({min_value} to {max_value})."
                )
            normalized.append(value)
        return normalized

    def _as_list(self, values):
        """Return values as a flat list, wrapping scalars in one."""
        if isinstance(values, np.ndarray):
            return values.ravel().tolist()
        if isinstance(values, (bytes, bytearray, str)):
            return [values]
        if isinstance(values, numbers.Number) or isinstance(values, Decimal):
            return [values]
        try:
            return list(values)
        except TypeError:
            return [values]

    def _set_output_param_value(self, param_code, channel, original_value):
        """Store a programmed value in its local parameter array."""
        attr_name = self._OUTPUT_PARAMETER_ATTRS.get(param_code)
        if attr_name is None:
            return
        values = getattr(self, attr_name)
        values[channel] = original_value

    def _to_decimal(self, value):
        """Convert a value to a Decimal with PulsePal precision."""
        return Decimal(value).quantize(Decimal("1.0000"))

    def _volts_to_bits(self, value):
        """Convert -10 V to +10 V to the corresponding DAC bit value."""
        normalized = (float(value) + 10) / 20
        bit_max = int(self._dac_bit_max)
        return int(min(max(round(normalized * bit_max), 0), bit_max))

    def _bits_to_volts(self, value):
        """Convert a DAC code to volts, snapping clean values within 1 LSB."""
        bit_max = int(self._dac_bit_max)
        raw_volts = (float(value) / bit_max * 20) - 10

        # Calculate the voltage of 1 bit
        lsb_volts = 20.0 / bit_max

        # Find the nearest clean 3-decimal number (e.g. 5.000, 4.255)
        clean_volts = round(raw_volts, 3)

        # If the raw voltage is within 1 bit of the clean voltage, snap to it
        if abs(raw_volts - clean_volts) <= lsb_volts:
            return clean_volts

        # Otherwise, return the standard 4-decimal reading
        return round(raw_volts, 4)

    def _seconds_to_cycles(self, value):
        """Convert seconds to the corresponding refresh-cycle count."""
        return int(round(float(value) * float(self.info.cycle_frequency)))

    def _cycles_to_seconds(self, value):
        """Convert hardware timer cycle counts to seconds."""
        return float(value) / float(self.info.cycle_frequency)

    def _require_firmware(self, minimum_version, context):
        """Raise unless the device firmware is new enough for an op."""
        if (
            self.info.firmware_version is None
            or self.info.firmware_version < minimum_version
        ):
            raise PulsePalError(
                f"{context} requires firmware v{minimum_version} or newer. "
                f"Detected firmware is v{self.info.firmware_version}."
            )

    def _sync_all_params(self):
        """Send all parameters using the packed sync op (firmware v22+).

        Values are grouped by width so that the whole program travels as
        one uint32 block, one uint16 block and one uint8 block.
        """
        time_values = []
        for attr_name in (
            "phase1_duration",
            "inter_phase_interval",
            "phase2_duration",
            "inter_pulse_interval",
            "burst_duration",
            "inter_burst_interval",
            "pulse_train_duration",
            "pulse_train_delay",
        ):
            time_values.extend(
                self._seconds_to_cycles(getattr(self, attr_name)[channel])
                for channel in range(1, 5)
            )

        voltage_values = []
        for attr_name in (
            "phase1_voltage",
            "phase2_voltage",
            "resting_voltage",
        ):
            voltage_values.extend(
                self._volts_to_bits(getattr(self, attr_name)[channel])
                for channel in range(1, 5)
            )

        single_byte_values = []
        for attr_name in (
            "is_biphasic",
            "custom_train_id",
            "custom_train_target",
            "custom_train_loop",
        ):
            single_byte_values.extend(
                int(getattr(self, attr_name)[channel])
                for channel in range(1, 5)
            )
        single_byte_values.extend(
            int(self.link_trigger_channel1[channel])
            for channel in range(1, 5)
        )
        single_byte_values.extend(
            int(self.link_trigger_channel2[channel])
            for channel in range(1, 5)
        )
        single_byte_values.extend(
            int(value) for value in self.trigger_mode[1:3]
        )

        self._write_serial(
            (self._OP_MENU_BYTE, 92),
            "uint8",
            time_values,
            "uint32",
            voltage_values,
            "uint16",
            single_byte_values,
            "uint8",
        )

    def _sync_all_params_legacy(self):
        """Send all parameters using the legacy sync op (firmware v21).

        Equivalent to `_sync_all_params`, but lays the program out
        channel by channel as the older firmware expects.
        """
        program_values_16 = []
        program_values_32 = []
        program_values_8 = [0] * 16

        for channel in range(1, 5):
            program_values_32.extend(
                [
                    self._seconds_to_cycles(
                        self.phase1_duration[channel]),
                    self._seconds_to_cycles(
                        self.inter_phase_interval[channel]),
                    self._seconds_to_cycles(
                        self.phase2_duration[channel]),
                    self._seconds_to_cycles(
                        self.inter_pulse_interval[channel]),
                    self._seconds_to_cycles(
                        self.burst_duration[channel]),
                    self._seconds_to_cycles(
                        self.inter_burst_interval[channel]),
                    self._seconds_to_cycles(
                        self.pulse_train_duration[channel]),
                    self._seconds_to_cycles(
                        self.pulse_train_delay[channel]),
                ]
            )

        for channel in range(1, 5):
            program_values_16.extend(
                [
                    self._volts_to_bits(self.phase1_voltage[channel]),
                    self._volts_to_bits(self.phase2_voltage[channel]),
                    self._volts_to_bits(self.resting_voltage[channel]),
                ]
            )

        position = 0
        for channel in range(1, 5):
            program_values_8[position] = self.is_biphasic[channel]
            position += 1
            program_values_8[position] = self.custom_train_id[channel]
            position += 1
            program_values_8[position] = self.custom_train_target[channel]
            position += 1
            program_values_8[position] = self.custom_train_loop[channel]
            position += 1

        program_values_tl = [0] * 8
        position = 0
        for channel in range(1, 5):
            program_values_tl[position] = self.link_trigger_channel1[channel]
            position += 1
        for channel in range(1, 5):
            program_values_tl[position] = self.link_trigger_channel2[channel]
            position += 1

        self._write_serial(
            (self._OP_MENU_BYTE, 73),
            "uint8",
            program_values_32,
            "uint32",
            program_values_16,
            "uint16",
            program_values_8,
            "uint8",
            program_values_tl,
            "uint8",
            self.trigger_mode[1:3],
            "uint8",
        )

    def __enter__(self):
        """Enter a `with` block, returning the connected device."""
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        """Disconnect and close the port when leaving a `with` block.

        Returns:
            `False`, so any exception raised in the block propagates.
        """
        try:
            self._write_serial((self._OP_MENU_BYTE, 81), "uint8")
        except Exception:
            pass
        self.close()
        return False

    def __del__(self):
        """Disconnect and close the port when the object is collected."""
        try:
            self._write_serial((self._OP_MENU_BYTE, 81), "uint8")
        except Exception:
            pass

        try:
            self.close()
        except Exception:
            # Destructors should not raise; the serial object may already be
            # gone during interpreter shutdown.
            pass
