"""
Python interface for the [Pulse Pal](https://sites.google.com/site/pulsepalwiki/)
open source pulse train generator.

Pulse Pal delivers precisely timed voltage pulse trains on four analog
output channels, and can be triggered by TTL logic on two trigger
channels or in software. This module configures and triggers the device
over its USB serial port.

Everything is accessed through `PulsePalDevice`. Import it, connect to
the device's serial port, set parameters, and trigger, e.g.

```python
from pulsepal import PulsePalDevice

with PulsePalDevice("COM3") as P:
    P.phase1_voltage[1] = 5            # volts, output channel 1
    P.phase1_duration[1] = 0.001       # seconds: 1 ms pulses,
    P.inter_pulse_interval[1] = 0.049  # 49 ms apart (end of one to start of the next),
    P.pulse_train_duration[1] = 2      # for 2 seconds
    P.trigger(1)
```

## Parameters

Each parameter is a list on the device object, indexed by channel
number: index 0 is unused and holds `None`, and indices 1 to 4 hold the
values for output channels 1-4. `PulsePalDevice.trigger_mode` is indexed
the same way by trigger channel, 1 or 2. Assigning an element, a slice
or the whole list programs the device at once. The whole list takes one
value per channel: a single value raises an error, because it does not
say which channels it is meant for.

```python
P.phase1_voltage[2] = 7                  # channel 2 only
P.inter_pulse_interval[1:5] = [0.2] * 4  # all four channels
P.phase1_voltage = [5, 5, 5, 5]          # all four channels
P.trigger_mode[2] = "Toggle"             # trigger channel 2
```

To change several parameters in one command, assign them in a
`PulsePalDevice.batch` block: they are sent together when it ends.

```python
with P.batch():
    P.is_biphasic[1] = True
    P.phase2_voltage[1] = -5
    P.inter_phase_interval[1] = 0.0005
```

`PulsePalDevice.auto_sync` set to `False` does the same for as long as
it stays off, until `PulsePalDevice.sync_to_device` is called. That is
how the next trial's parameters wait for a TTL in param sync mode (see
`PulsePalDevice.trigger_mode`).

## Units and values

Voltages are in volts in the range [-10, 10]. Times are in seconds, from
0 to `MAX_TIME` (9999.9999 s), and are rounded to the nearest cycle of
the device's hardware timer, 50 us (see `DeviceInfo.cycle_period_us`);
the parameter then holds the time the device plays, so 0.00012 reads
back as 0.0001. A time exactly halfway between two cycles rounds to the
even one, as in the MATLAB and C++ classes, and voltages round to the
nearest DAC code the same way. On/off parameters are `True` or `False`,
and modes are names, such as `"Gated"`; names are not case sensitive,
and the integer codes of older Pulse Pal software are accepted too.

A value the device cannot play raises `pulsepal.PulsePalError` before
anything is sent: a voltage outside [-10, 10], a time that is negative,
longer than `MAX_TIME` or not a number, or an unknown name. So does a
pulse phase, inter-pulse interval or pulse train duration shorter than
`DeviceInfo.min_pulse_width_us` (100 us), so that a Pulse Pal's trigger
channels can detect the shortest pulse its output channels play. Custom
pulse times are not rounded: each must be a multiple of
`DeviceInfo.min_pulse_width_us`.

## Saving parameters

`PulsePalDevice.export_params` returns every parameter as a dict, to
store with your data or in a file, and `PulsePalDevice.import_params`
programs the device with one. The device's microSD card holds settings
files too, which its joystick menu can load:
`PulsePalDevice.save_settings_file`.

## Further reading

- Parameter guide:
  https://sites.google.com/site/pulsepalwiki/parameter-guide
- Serial interface and general documentation:
  https://sites.google.com/site/pulsepalwiki/
- `pulsepal.wave_pal`: alternative firmware that makes a Pulse Pal 3 a
  four channel waveform player, and its class, `pulsepal.WavePalDevice`.
- `pulsepal.synth_pal`: alternative firmware that makes a Pulse Pal 3 a
  four channel waveform synthesizer, and its class,
  `pulsepal.SynthPalDevice`.

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

from dataclasses import dataclass
import math
import numbers
import struct
import time

import numpy as np
import serial

from . import _common
from ._common import ChannelSettings, PulsePalError, to_bool, to_name

__all__ = ["PulsePalDevice", "DeviceInfo", "TRIGGER_MODES", "CUSTOM_TRAIN_TARGETS", "MAX_TIME"]
__docformat__ = "google"

MAX_TIME = 9999.9999
"""The longest time parameter or custom pulse time, in seconds: the most
the device's joystick menu shows and edits (four digits before the
point)."""

TRIGGER_MODES = ("Normal", "Toggle", "Gated", "Param Sync")
"""Names of the trigger modes, in order of their code on the device (0-3).
"Param Sync" is on Pulse Pal 3 only. See `PulsePalDevice.trigger_mode`."""

CUSTOM_TRAIN_TARGETS = ("Pulses", "Bursts")
"""Names of the custom train targets, in order of their code on the device.
See `PulsePalDevice.custom_train_target`."""

# Output parameters by parameter code (see "Parameter codes" in /Firmware/PROTOCOL.md), with
# the kind of value each takes. The codes are fixed: installed clients and the firmware use them.
# "pulse_time" is a time of at least _MIN_PULSE_CYCLES.
_OUTPUT_PARAMETERS = {
    1: ("is_biphasic", "bool"),
    2: ("phase1_voltage", "volts"),
    3: ("phase2_voltage", "volts"),
    4: ("phase1_duration", "pulse_time"),
    5: ("inter_phase_interval", "time"),
    6: ("phase2_duration", "pulse_time"),
    7: ("inter_pulse_interval", "pulse_time"),
    8: ("burst_duration", "time"),
    9: ("inter_burst_interval", "time"),
    10: ("pulse_train_duration", "pulse_time"),
    11: ("pulse_train_delay", "time"),
    12: ("link_trigger_channel1", "bool"),
    13: ("link_trigger_channel2", "bool"),
    14: ("custom_train_id", "train_id"),
    15: ("custom_train_target", "target"),
    16: ("custom_train_loop", "bool"),
    17: ("resting_voltage", "volts"),
    18: ("continuous_loop", "bool"),
}
_OUTPUT_PARAMETER_CODES = {name: code for code, (name, _) in _OUTPUT_PARAMETERS.items()}
_TRIGGER_MODE_CODE = 128  # The one trigger channel parameter
_CONTINUOUS_LOOP_CODE = 18  # Not in ops 73 and 93, or settings files

# The parameters that ops 73, 92 and 93 carry in each block, in order
_TIME_PARAMETERS = ("phase1_duration", "inter_phase_interval", "phase2_duration", "inter_pulse_interval",
                    "burst_duration", "inter_burst_interval", "pulse_train_duration", "pulse_train_delay")
_VOLTAGE_PARAMETERS = ("phase1_voltage", "phase2_voltage", "resting_voltage")

_DEFAULT_OUTPUT_VALUES = {
    "is_biphasic": False,
    "phase1_voltage": 5.0,
    "phase2_voltage": -5.0,
    "phase1_duration": 0.001,
    "inter_phase_interval": 0.001,
    "phase2_duration": 0.001,
    "inter_pulse_interval": 0.01,
    "burst_duration": 0.0,
    "inter_burst_interval": 0.0,
    "pulse_train_duration": 1.0,
    "pulse_train_delay": 0.0,
    "link_trigger_channel1": True,
    "link_trigger_channel2": False,
    "custom_train_id": 0,
    "custom_train_target": "Pulses",
    "custom_train_loop": False,
    "resting_voltage": 0.0,
    "continuous_loop": False,
}


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

    output_parameter_names: tuple = tuple(name for name, _ in _OUTPUT_PARAMETERS.values())
    """Output parameter names, ordered by parameter code.

    Each is the name of a parameter list on `PulsePalDevice`, and valid
    as the `param_name` argument of `PulsePalDevice.set_output_param`.
    """

    trigger_parameter_names: tuple = ("trigger_mode",)
    """Trigger parameter names, accepted by
    `PulsePalDevice.set_trigger_param`."""

    trigger_modes: tuple = TRIGGER_MODES
    """Names of the trigger modes the connected device has, accepted by
    `PulsePalDevice.trigger_mode`. Pulse Pal 2 has no "Param Sync"."""

    custom_train_targets: tuple = CUSTOM_TRAIN_TARGETS
    """Names accepted by `PulsePalDevice.custom_train_target`."""

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

    min_pulse_width_us: float = None
    """Shortest pulse phase, inter-pulse interval and pulse train duration,
    and shortest time between custom pulses, in microseconds.

    Two timer cycles: a trigger channel reads its input once per cycle, so
    a pulse must last two cycles to be detected reliably. Custom pulse
    times are multiples of it.
    """

    max_time: float = MAX_TIME
    """Longest time parameter or custom pulse time, in seconds. See
    `MAX_TIME`."""


def _channel_setting(name, doc):
    """A property for one of PulsePalDevice's parameter lists."""

    def get(self):
        return getattr(self, "_" + name)

    def set_(self, values):
        getattr(self, "_" + name)._assign(values)

    return property(get, set_, doc=doc)


class PulsePalDevice:
    """A class to control a Pulse Pal device on a USB serial port.

    Creating an instance opens the serial port, exchanges a handshake
    with the device, verifies that its firmware is supported, reads the
    device properties into `PulsePalDevice.info`, and programs the
    device with the default parameters (see
    `PulsePalDevice.set_default_params`).

    ```python
    from pulsepal import PulsePalDevice

    P = PulsePalDevice("COM3")
    P.phase1_voltage[1] = 5
    P.trigger(1)
    P.close()
    ```

    Here, replace "COM3" with Pulse Pal's USB serial port name.
    To view a list of available ports, use
    `PulsePalDevice.serialportlist()`.
    Pulse Pal's port may not be visible if it is connected to another
    instance of PulsePalDevice or an external application. Use
    `PulsePalDevice.serialportlist("all")` to view all ports.
    If you see multiple available ports, disconnect Pulse Pal's USB plug
    and run serialportlist() again. Notice which port disappears from the
    list.

    PulsePalDevice is also a context manager, which closes the connection on
    exit even if an error is raised:

    ```python
    with PulsePalDevice("COM3") as P:
        P.trigger(1)
    ```

    The attributes below named after Pulse Pal parameters are lists
    indexed by channel number, with index 0 unused (see "Parameters"
    above). `print(P)` shows them all, and `PulsePalDevice.export_params`
    returns them as a dict.
    """

    port: "serial.Serial"
    """The open `serial.Serial` port connected to the device."""

    info: DeviceInfo
    """Properties of the connected device. See `DeviceInfo`."""

    is_biphasic = _channel_setting("is_biphasic", """Pulse shape per channel: `False` for monophasic, `True` for biphasic.

    Monophasic pulses use only the phase 1 parameters. Biphasic pulses
    follow phase 1 with `PulsePalDevice.inter_phase_interval` and then
    phase 2.
    """)

    phase1_voltage = _channel_setting("phase1_voltage", """Voltage of the first phase of each pulse, in volts [-10, 10].""")

    phase2_voltage = _channel_setting("phase2_voltage", """Voltage of the second phase of each pulse, in volts [-10, 10].

    Used only when `PulsePalDevice.is_biphasic` is `True` for the channel.
    """)

    resting_voltage = _channel_setting("resting_voltage", """Voltage held between pulses, in volts [-10, 10].

    A new resting voltage reaches an idle channel's output at once. A
    channel playing a pulse train keeps playing it, and moves to the new
    resting voltage at its next transition to rest.
    """)

    phase1_duration = _channel_setting("phase1_duration", """Duration of the first phase of each pulse, in seconds.

    At least `DeviceInfo.min_pulse_width_us`.
    """)

    inter_phase_interval = _channel_setting("inter_phase_interval", """Interval between the two phases of a biphasic pulse, in seconds.

    The channel rests at `PulsePalDevice.resting_voltage` during the
    interval. Used only when `PulsePalDevice.is_biphasic` is `True`.
    """)

    phase2_duration = _channel_setting("phase2_duration", """Duration of the second phase of each pulse, in seconds.

    Used only when `PulsePalDevice.is_biphasic` is `True` for the channel.
    At least `DeviceInfo.min_pulse_width_us`.
    """)

    inter_pulse_interval = _channel_setting("inter_pulse_interval", """Interval from the end of one pulse to the start of the next, in
    seconds. At least `DeviceInfo.min_pulse_width_us`.

    It is not the pulse period: 1 ms pulses 49 ms apart play 20 pulses per
    second.
    """)

    burst_duration = _channel_setting("burst_duration", """Duration of each burst of pulses, in seconds.

    Set to `0` to disable bursts, so that pulses continue for the whole
    pulse train. A pulse starts only if it ends before the burst does:
    its first phase, or for a biphasic pulse the whole pulse, so that
    the end of a burst never cuts off a second phase.
    """)

    inter_burst_interval = _channel_setting("inter_burst_interval", """Interval between bursts of pulses, in seconds.

    The channel rests at `PulsePalDevice.resting_voltage` between
    bursts. Ignored when `PulsePalDevice.burst_duration` is `0`.
    """)

    pulse_train_duration = _channel_setting("pulse_train_duration", """Total duration of the pulse train, in seconds.

    At least `DeviceInfo.min_pulse_width_us`. The end of the train cuts
    short a monophasic pulse still playing. A biphasic pulse starts only
    if it can end by the end of the train, so that it keeps its second
    phase.
    """)

    pulse_train_delay = _channel_setting("pulse_train_delay", """Delay from the trigger to the onset of the pulse train, in
    seconds.""")

    link_trigger_channel1 = _channel_setting("link_trigger_channel1", """Whether each output channel is triggered by trigger channel 1.

    `True` links the output channel to trigger channel 1, `False` unlinks
    it.
    """)

    link_trigger_channel2 = _channel_setting("link_trigger_channel2", """Whether each output channel is triggered by trigger channel 2.

    `True` links the output channel to trigger channel 2, `False` unlinks
    it.
    """)

    custom_train_id = _channel_setting("custom_train_id", """Custom pulse train played by each output channel.

    `0` plays the parametrically defined train. `1` or higher plays the
    matching custom train, previously loaded with
    `PulsePalDevice.send_custom_pulse_train` or
    `PulsePalDevice.send_custom_waveform`: 1-4 on Pulse Pal 3, 1-2 on
    Pulse Pal 2 (`DeviceInfo.n_custom_pulse_trains`).
    """)

    custom_train_target = _channel_setting("custom_train_target", """What the times of a custom train mark.

    `"Pulses"` if each time is the onset of a pulse, `"Bursts"` if each
    time is the onset of a burst of pulses.
    """)

    custom_train_loop = _channel_setting("custom_train_loop", """Whether a custom train repeats.

    `True` loops the custom train until
    `PulsePalDevice.pulse_train_duration` has elapsed, `False` plays it
    once.
    """)

    continuous_loop = _channel_setting("continuous_loop", """Whether each output channel plays its pulse train until stopped.

    `False` plays the pulse train once per trigger, for
    `PulsePalDevice.pulse_train_duration`. `True` plays it until the
    channel is stopped, ignoring `PulsePalDevice.pulse_train_duration`.
    Requires firmware v22 or newer. The device does not report it, so
    `PulsePalDevice.sync_from_device` leaves it as it is.
    """)

    trigger_mode = _channel_setting("trigger_mode", """Response of each trigger channel to an incoming TTL pulse.

    Indexed by trigger channel number, 1 or 2 (index 0 is unused). The
    modes, also listed in `DeviceInfo.trigger_modes`, are:

    - `"Normal"`: a rising edge starts the pulse trains of the linked
      output channels, and edges during a train are ignored.
    - `"Toggle"`: as `"Normal"`, but a rising edge during a train stops
      it.
    - `"Gated"`: the trains play only while the trigger TTL is high.
    - `"Param Sync"` (Pulse Pal 3 only): a TTL rising edge starts and
      stops nothing. It loads the parameters most recently sent by
      `PulsePalDevice.sync_to_device` (or at the end of a
      `PulsePalDevice.batch` block). This is how the next trial's
      parameters are sent during the current trial and applied the
      instant it starts.

    The integer codes 0-3 are accepted too.

    An output channel that is idle at a param sync edge takes its new
    parameters in the 50 us timer cycle the edge is detected. One that is
    playing a pulse train finishes that train on the parameters it
    started with, and takes the new ones the moment it ends, so a train
    that runs past the end of a trial keeps one shape throughout and the
    next trigger plays a whole train with the new parameters. A channel
    in continuous loop mode has no train end, so it keeps its parameters
    until something stops it.

    While either trigger channel is in param sync mode, **only
    `PulsePalDevice.sync_to_device` is held back**: with
    `PulsePalDevice.auto_sync` on, every assignment still programs the
    device immediately, as do `PulsePalDevice.set_output_param` and
    `PulsePalDevice.set_trigger_param`. So leaving param sync mode means
    assigning `trigger_mode` with `auto_sync` on; a trigger mode sent by
    `PulsePalDevice.sync_to_device` does not take effect until a sync
    edge arrives.

    A param sync channel's links to output channels are ignored. To
    start a pulse train on the same edge, wire the TTL to the other
    trigger channel as well: both edges arrive in the same timer cycle,
    and the parameters are loaded first.

    Connecting a new `PulsePalDevice`, and
    `PulsePalDevice.set_default_params`, take both trigger channels out
    of param sync mode, so that the default parameters reach the device
    instead of waiting for a TTL.

    ```python
    P.trigger_mode[2] = "Param Sync"   # sent at once
    with P.batch():
        P.phase1_voltage[1] = 2.5
        P.phase1_duration[1] = 0.002
    # Stored by the device: the next rising edge on trigger channel 2 applies it
    ```
    """)

    _CURRENT_FIRMWARE_VERSION = 22
    _OLDEST_FIRMWARE_SUPPORTED = 21

    _OP_MENU_BYTE = _common.OP_MENU_BYTE
    _HANDSHAKE_OPCODE = _common.OP_HANDSHAKE
    _HANDSHAKE_RESPONSE = 75  # 'K'
    _DAC_BITMAX = 65535
    _PARAM_MESSAGE_BYTES = 178  # Length of the parameter set sent by op 93
    # Phase 1 and 2 durations, inter-pulse interval and train duration last at least this many
    # timer cycles, as in the MATLAB and C++ classes and the joystick menu. A trigger channel reads
    # its input once per cycle, so a one cycle pulse from another Pulse Pal could fall between two
    # reads.
    _MIN_PULSE_CYCLES = 2

    _ENDIANNESS = "<"
    _STRUCT_FORMATS = {
        "uint8": "B",
        "int8": "b",
        "uint16": "H",
        "int16": "h",
        "uint32": "I",
        "int32": "i",
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
            baud_rate: Serial baud rate. USB serial ignores it.
            timeout: Serial read timeout, in seconds.

        Raises:
            PulsePalError: If the device does not return the expected
                handshake (for example, it runs Wave Pal or Synth Pal
                firmware), or its firmware is older than v21, or its
                firmware is newer than this module supports. The port is
                closed again before any error is raised.
            serial.SerialException: If the serial port cannot be opened.
        """
        self._closed = True
        self._gui = None
        self._auto_sync = True
        self.info = DeviceInfo()
        self._create_settings()
        self.port = serial.Serial(
            port_name,
            baud_rate,
            timeout=timeout,
            rtscts=True,
        )
        self._closed = False
        try:
            self._start_session(port_name)
        except BaseException:
            # Otherwise the port stays open until the object is garbage
            # collected, and a second attempt, or WavePalDevice, cannot open
            # it. The device is in an unknown state, so it is not sent the
            # disconnect op.
            self.close(send_disconnect=False)
            raise

    def _create_settings(self):
        """Create the parameter lists, holding the default values (nothing is sent)."""
        for code, (name, _) in _OUTPUT_PARAMETERS.items():
            setattr(self, "_" + name, ChannelSettings(
                name, [_DEFAULT_OUTPUT_VALUES[name]] * 4, self, f"_apply_{name}"))
        self._trigger_mode = ChannelSettings(
            "trigger_mode", ["Normal"] * 2, self, "_apply_trigger_mode")

    def _start_session(self, port_name):
        """Handshake, check the firmware, and program the defaults."""
        self._write_serial(
            (self._OP_MENU_BYTE, self._HANDSHAKE_OPCODE),
            "uint8",
        )
        handshake = self._read_serial(1, "uint8")
        if handshake != self._HANDSHAKE_RESPONSE:
            if handshake in _common.FIRMWARE_BY_HANDSHAKE_REPLY:
                version = self._read_serial(1, "uint32")
                raise _common.other_firmware_error(handshake, version, port_name, "Pulse Pal")
            raise PulsePalError(
                "Incorrect handshake returned. Expected "
                f"{self._HANDSHAKE_RESPONSE}, received {handshake}."
            )

        firmware_version = self._read_serial(1, "uint32")
        if firmware_version < self._OLDEST_FIRMWARE_SUPPORTED:
            raise PulsePalError(
                "Old firmware detected, v"
                f"{firmware_version}. v{self._OLDEST_FIRMWARE_SUPPORTED} or "
                "newer is required."
            )
        if firmware_version > self._CURRENT_FIRMWARE_VERSION:
            raise PulsePalError(
                "Future firmware detected, v"
                f"{firmware_version}. Please update the pulsepal package or "
                f"load firmware v{self._CURRENT_FIRMWARE_VERSION}."
            )
        if firmware_version < self._CURRENT_FIRMWARE_VERSION:
            print(
                "Old firmware detected, v"
                f"{firmware_version}. This firmware is supported. Update to v"
                f"{self._CURRENT_FIRMWARE_VERSION} is available."
            )
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
        self.info.min_pulse_width_us = (
            self._MIN_PULSE_CYCLES * self.info.cycle_period_us
        )
        if not self._has_param_sync():
            self.info.trigger_modes = TRIGGER_MODES[:3]

        # Client name op + "PYTHON" in ASCII.
        self._write_serial(
            (self._OP_MENU_BYTE, 89, 80, 89, 84, 72, 79, 78),
            "uint8",
        )

        self.set_default_params()

    @staticmethod
    def serialportlist(ports_to_list="available"):
        """Return the names of the USB serial ports on this computer.

        Called on the class, without connecting to a device, to find the
        port name to pass to `PulsePalDevice`:

        ```python
        from pulsepal import PulsePalDevice

        ports = PulsePalDevice.serialportlist()
        P = PulsePalDevice(ports[0])
        ```

        Args:
            ports_to_list: `available` to list only the ports that are
                not already in use, or `all` to list every USB serial
                port. Not case sensitive.

        Returns:
            Sorted list of port names, such as `["COM3", "COM7"]` on
            Windows or `["/dev/ttyACM0"]` on Linux.

        Raises:
            PulsePalError: If `ports_to_list` is not `available` or
                `all`.
        """
        return _common.serialportlist(ports_to_list)

    # ------------------------------------------------------------------
    # Programming the device
    # ------------------------------------------------------------------

    @property
    def auto_sync(self):
        """Whether assigning a parameter programs the device at once.

        `True` (the default): each assignment, such as
        `P.phase1_voltage[1] = 5`, programs the device at once, also while
        a trigger channel is in param sync mode.

        `False`: assignments change only this object's copy of the
        parameters, checking each value, and
        `PulsePalDevice.sync_to_device` sends all of them in one command.
        Turning it back on does not send changes made meanwhile: call
        `PulsePalDevice.sync_to_device` first. A `PulsePalDevice.batch`
        block does both for you.
        """
        return self._auto_sync

    @auto_sync.setter
    def auto_sync(self, value):
        self._auto_sync = to_bool(value, "auto_sync")

    def batch(self):
        """Change several parameters, and send them in one command.

        Returns a context manager. In its block, assignments change only
        this object's copy of the parameters (as with
        `PulsePalDevice.auto_sync` off), and when the block ends,
        `PulsePalDevice.sync_to_device` sends all of them at once. If the
        block raises an error, nothing is sent, and the parameters go
        back to what they were.

        ```python
        with P.batch():
            P.phase1_voltage = [5, 5, 2.5, 2.5]
            P.phase1_duration[3] = 0.002
        ```

        In param sync mode, the device stores the parameters for the next
        rising edge on the param sync channel (see
        `PulsePalDevice.trigger_mode`).
        """
        return _common.batch(self)

    def sync_to_device(self):
        """Program the device with this object's copy of all parameters.

        Sends every output and trigger parameter to the device in a single
        command. Use it after assignments made with
        `PulsePalDevice.auto_sync` off; with it on, they have already
        reached the device.

        ```python
        P.auto_sync = False
        P.phase1_voltage[1:5] = [5] * 4
        P.sync_to_device()
        ```

        On Pulse Pal 3, if either trigger channel is in param sync mode
        (`PulsePalDevice.trigger_mode` `"Param Sync"`), the device stores
        the parameters instead of programming them, and loads them on the
        next rising edge of that channel. The device still checks every
        value. This is the only method whose effect is deferred that way.

        Raises:
            PulsePalError: If a value is out of range for its parameter
                (nothing is sent), or the device does not acknowledge the
                command.
        """
        self._check_sync()
        self._send_sync()

    def sync_from_device(self):
        """Read all parameters from the device into this object's copy.

        Overwrites the parameter lists with the program currently on the
        device, e.g. after it has been reprogrammed from its thumb
        joystick. `PulsePalDevice.continuous_loop` is left as it is: the
        device does not report it.

        Requires firmware v22 or newer.

        Raises:
            PulsePalError: If the connected firmware is older than v22,
                or the device does not return the full parameter set.
        """
        self._require_firmware(22, "sync_from_device()")
        for name, values in self._read_device_params().items():
            getattr(self, name)._store(values)

    def set_default_params(self):
        """Program the device with the default parameters.

        The defaults are monophasic +5 V pulses of 1 ms, 10 ms apart, for
        1 second, resting at 0 V, on all four output channels, linked to
        trigger channel 1, with both trigger channels in normal mode.
        They are sent at once, also while `PulsePalDevice.auto_sync` is
        off. The constructor calls it.
        """
        for name, value in _DEFAULT_OUTPUT_VALUES.items():
            getattr(self, name)._store([value] * 4)
        self._trigger_mode._store(["Normal"] * 2)
        if self._has_param_sync():
            # A device left in param sync mode would store the sync below instead of running it,
            # leaving the device on its old program until a TTL arrived. A trigger mode sent on
            # its own is not deferred that way, so both trigger channels leave param sync mode
            # first. See PulsePalDevice.trigger_mode.
            self._send_trigger_modes(["Normal"] * 2, "set_default_params()")
        self._send_sync()

    def export_params(self):
        """Return every parameter, as a dict of plain lists.

        Keyed by parameter name (`DeviceInfo.output_parameter_names`, then
        `"trigger_mode"`), with one value per channel and no unused index
        0. It holds only numbers, booleans and names, so it can be saved
        with `json` and logged with your data, to record exactly what the
        device played. `PulsePalDevice.import_params` programs it again.

        ```python
        import json
        with open("trial_params.json", "w") as f:
            json.dump(P.export_params(), f)
        ```
        """
        names = [*self.info.output_parameter_names, "trigger_mode"]
        return {name: list(getattr(self, name))[1:] for name in names}

    def import_params(self, params):
        """Program the device with parameters exported by `PulsePalDevice.export_params`.

        All of them are checked first, and then sent in one command, as
        at the end of a `PulsePalDevice.batch` block: in param sync mode,
        the device stores them for the next sync edge. Parameters missing
        from `params` keep their values.

        Args:
            params: A dict of parameter name to one value per channel (a
                list, tuple or NumPy array), as `export_params` returns.

        Raises:
            PulsePalError: If a name is not a parameter, or a value is
                invalid. Nothing is sent, and the parameters keep the
                values they had.
        """
        names = {*self.info.output_parameter_names, "trigger_mode"}
        unknown = [name for name in params if name not in names]
        if unknown:
            raise PulsePalError(
                f"import_params(): unknown parameter(s) {', '.join(map(repr, unknown))}. Valid names are "
                f"{', '.join(self.info.output_parameter_names)} and trigger_mode."
            )
        with self.batch():
            for name, values in params.items():
                setattr(self, name, values)

    def set_output_param(self, param_name, channel, value):
        """Program an output channel parameter on the device at once.

        This is the same as assigning to the parameter's list with
        `PulsePalDevice.auto_sync` on, but takes effect at once whether
        auto_sync is on or not. The local copy is updated to match.

        ```python
        P.set_output_param("is_biphasic", 1, True)
        P.set_output_param("phase1_voltage", 1, 10)
        P.set_output_param("phase1_voltage", [1, 2, 3, 4], [5, 5, 5, 3])
        P.set_output_param("phase1_duration", [2, 4], 0.002)
        ```

        Setting all four channels in one call programs them with a single
        command (firmware v22 or newer). Otherwise, each listed channel is
        programmed with its own command. Channels that are not listed are
        not changed.

        Args:
            param_name: Parameter name, as listed in
                `DeviceInfo.output_parameter_names`, or its integer
                parameter code.
            channel: Output channel number, 1-4, or several as a list,
                tuple or NumPy array of distinct channel numbers.
            value: Value to set, as for the parameter's list. If
                `channel` lists several channels, either one value for
                all of them, or a list with one value per listed channel,
                in the same order.

        Raises:
            PulsePalError: If the parameter name is not recognized, the
                channels or number of values are invalid, a value is out
                of range for the parameter (nothing is sent), or the device
                does not acknowledge the command. When the device refuses a
                value, the local copy of the parameter is first read back
                from the device, which resets a value it refuses.
        """
        code = self._get_output_param_code(param_name)
        name = _OUTPUT_PARAMETERS[code][0]
        channels = _common.channel_numbers(channel)
        if len(set(channels)) != len(channels):
            raise PulsePalError(f"set_output_param(): channels must be distinct. Received {channel!r}.")
        values = list(value) if isinstance(value, (list, tuple, np.ndarray)) else [value]
        if len(values) == 1:
            values = values * len(channels)
        if len(values) != len(channels):
            raise PulsePalError(
                f"{len(values)} values were given for {len(channels)} "
                "channels. Give one value, or one value per channel."
            )
        values = [self._normalize_output_value(code, v, ch) for ch, v in zip(channels, values)]
        settings = getattr(self, name)
        new_values = list(settings)[1:]
        for ch, v in zip(channels, values):
            new_values[ch - 1] = v
        if sorted(channels) == [1, 2, 3, 4] and self.info.firmware_version > 21:
            self._send_output_param(code, new_values, "set_output_param()")
        else:
            for ch in channels:
                self._send_output_param_to_channel(code, ch, new_values[ch - 1], "set_output_param()")
        settings._store(new_values)

    def set_trigger_param(self, param_name, channel, value):
        """Program a trigger channel parameter on the device at once.

        This is the same as assigning to `PulsePalDevice.trigger_mode`
        with `PulsePalDevice.auto_sync` on, but takes effect at once
        whether auto_sync is on or not, also in param sync mode. The local
        copy is updated to match.

        ```python
        P.set_trigger_param("trigger_mode", 1, "Gated")
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
                channel or value is out of range (nothing is sent), or the
                device does not acknowledge the command.
        """
        self._get_trigger_param_code(param_name)
        channel = _common.channel_numbers(channel, n_channels=2, kind="Trigger")
        if len(channel) != 1:
            raise PulsePalError("set_trigger_param() takes one trigger channel, 1 or 2.")
        channel = channel[0]
        mode = self._normalize_trigger_mode(value)
        self._write_serial(
            (self._OP_MENU_BYTE, 74, _TRIGGER_MODE_CODE, channel, TRIGGER_MODES.index(mode)),
            "uint8",
        )
        self._read_ack("set_trigger_param()", on_refusal=self._refresh_trigger_mode)
        modes = list(self._trigger_mode)[1:]
        modes[channel - 1] = mode
        self._trigger_mode._store(modes)

    def set_fixed_voltage(self, channels, voltage):
        """Set output channels to a fixed voltage.

        Each channel holds the voltage until it is set again or until a
        pulse train is triggered on it.

        ```python
        P.set_fixed_voltage(4, 2.5)
        P.set_fixed_voltage([1, 3], -1)
        ```

        Args:
            channels: Output channel number, 1-4, or several as a list,
                tuple or NumPy array.
            voltage: Voltage to set on each of them, in volts [-10, 10].

        Raises:
            PulsePalError: If a channel number is not 1-4, the voltage is
                outside [-10, 10] (nothing is sent), or the device does not
                acknowledge the command.
        """
        channels = _common.channel_numbers(channels)
        voltage_bits = self._volts_to_bits(voltage, "voltage")
        for channel in channels:
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
                `channel` is not 1-4, `voltage_offset` is outside
                [-0.1, 0.1], or the device does not acknowledge the
                command.
        """
        if self.info.hardware_version < 3:
            raise PulsePalError(
                "set_calibration() requires hardware v3 or newer."
            )
        if not (isinstance(channel, numbers.Integral) and channel in (1, 2, 3, 4)):
            raise PulsePalError("channel must be 1, 2, 3 or 4")
        if not (isinstance(voltage_offset, numbers.Real) and -0.1 <= voltage_offset <= 0.1):  # Also refuses NaN
            raise PulsePalError(
                "voltage_offset for zero code calibration must be in range "
                "[-0.1, 0.1]"
            )
        # To the nearest DAC code, halfway values to the even one, as the MATLAB class rounds it
        voltage_bits = int(round(voltage_offset * (1 / (20 / 65536))))
        self._write_serial(
            (self._OP_MENU_BYTE, 96, channel - 1),
            "uint8",
            voltage_bits,
            "int16",
        )
        self._read_ack("set_calibration()")

    def set_screen_saver(self, enabled, timeout=1800):
        """Switch the device's screen saver on or off, and set its timeout.

        With the screen saver on, the device dims its screen once it has
        been left alone for `timeout` seconds: no command from the
        computer, no rising edge on a trigger channel, and no joystick
        click or push. The next of these brings the screen back. Both
        settings are stored in the device's EEPROM and kept through power
        cycles; the screen saver can also be switched on and off from the
        device's joystick menu. A new device has it on, with 1800 s.

        The device saves the settings once no channel is playing. Saving
        pauses its timer, usually for about 20 us but, once in about 2000
        changes, for tens of milliseconds, which would delay a trigger.
        Change the settings before an experiment rather than during one.

        Requires firmware v22 or newer. Only Pulse Pal 3 has a screen
        saver: Pulse Pal 2 accepts `enabled=False` only.

        ```python
        P.set_screen_saver(True, 300)  # dim after 5 minutes
        P.set_screen_saver(False)
        ```

        Args:
            enabled: True (or 1) to switch the screen saver on, False (or
                0) to switch it off.
            timeout: Seconds without activity before the screen dims, a
                whole number from 1 to 65535. It is sent with every call,
                so leaving it out sets 1800 s.

        Raises:
            PulsePalError: If the firmware is older than v22, the
                screen saver is switched on with hardware older than v3,
                a value is out of range, or the device does not
                acknowledge the command.
        """
        self._require_firmware(22, "set_screen_saver()")
        enabled = to_bool(enabled, "enabled")
        if not (
            isinstance(timeout, numbers.Real)
            and not isinstance(timeout, bool)
            and math.isfinite(timeout)
            and timeout == int(timeout)
            and 1 <= timeout <= 65535
        ):
            raise PulsePalError(
                "timeout must be a whole number of seconds from 1 to 65535"
            )
        if enabled and (self.info.hardware_version or 2) < 3:
            raise PulsePalError(
                "The screen saver requires hardware v3 or newer: Pulse Pal 2 "
                "accepts set_screen_saver(False) only."
            )
        self._write_serial(
            (self._OP_MENU_BYTE, 99, int(enabled)),
            "uint8",
            int(timeout),
            "uint16",
        )
        self._read_ack("set_screen_saver()")

    # ------------------------------------------------------------------
    # Custom trains, playback and settings files
    # ------------------------------------------------------------------

    def send_custom_pulse_train(
        self,
        custom_train_id,
        pulse_times,
        pulse_voltages,
    ):
        """Load a custom pulse train onto the device.

        A custom pulse train is an arbitrary list of pulse onset times
        and voltages, replacing the parametric pulse voltage and onset
        timing. Set `PulsePalDevice.custom_train_id` on an output channel
        to play the train there. Each pulse takes the channel's own phase
        durations; a biphasic pulse's second phase is its voltage with the
        sign reversed.

        ```python
        P.send_custom_pulse_train(
            2, [0, 0.2, 0.5, 1], [8, 4, -3.5, -10]
        )
        P.custom_train_id[1] = 2
        ```

        Args:
            custom_train_id: Custom train to load, from 1 to
                `DeviceInfo.n_custom_pulse_trains` (2 on Pulse Pal 2,
                4 on Pulse Pal 3).
            pulse_times: Pulse onset times, in seconds, relative to the
                start of the train, increasing. Each is a multiple of
                `DeviceInfo.min_pulse_width_us` (100 us), from 0 to
                `MAX_TIME`. Accepts a list, tuple or NumPy array.
            pulse_voltages: Voltage of each pulse, in volts [-10, 10].
                Must be the same length as `pulse_times`.

        Raises:
            PulsePalError: If `custom_train_id` is out of range,
                `pulse_times` and `pulse_voltages` differ in length,
                there are more pulses than
                `DeviceInfo.max_custom_pulses`, a pulse time is not a
                multiple of `DeviceInfo.min_pulse_width_us`, is out of
                range or is not later than the one before it, or the
                device does not acknowledge the command.
        """
        pulse_times = self._as_list(pulse_times)
        pulse_voltages = self._as_list(pulse_voltages)
        if len(pulse_times) != len(pulse_voltages):
            raise PulsePalError(
                "pulse_times and pulse_voltages must be the same length."
            )

        pulse_times_cycles = [
            self._custom_time_cycles(pulse_time, "pulse_times")
            for pulse_time in pulse_times
        ]
        pulse_voltage_bits = [
            self._volts_to_bits(voltage, "pulse_voltages")
            for voltage in pulse_voltages
        ]

        self._send_custom_train(
            custom_train_id,
            pulse_times_cycles,
            pulse_voltage_bits,
            "send_custom_pulse_train()",
        )

    def send_custom_waveform(
        self,
        custom_train_id,
        pulse_width,
        pulse_voltages,
    ):
        """Load an arbitrary waveform onto the device.

        A convenience shorthand for
        `PulsePalDevice.send_custom_pulse_train` with evenly spaced,
        adjoining pulses, so that `pulse_voltages` is played as a
        waveform sampled every `pulse_width` seconds.

        Set the channel's `PulsePalDevice.phase1_duration` to
        `pulse_width` as well, so that each sample is held for the
        sampling period.

        ```python
        import math

        samples = [math.sin(i / 10.0) * 10 for i in range(1000)]
        P.send_custom_waveform(1, 0.001, samples)   # 1 kHz
        P.custom_train_id[2] = 1
        P.phase1_duration[2] = 0.001
        ```

        Args:
            custom_train_id: Custom train to load, from 1 to
                `DeviceInfo.n_custom_pulse_trains` (2 on Pulse Pal 2,
                4 on Pulse Pal 3).
            pulse_width: Sampling period, in seconds: a multiple of
                `DeviceInfo.min_pulse_width_us` (100 us). Each voltage is
                held for this long.
            pulse_voltages: Waveform samples, in volts [-10, 10].
                Accepts a list, tuple or NumPy array.

        Raises:
            PulsePalError: If `custom_train_id` is out of range, there
                are more samples than `DeviceInfo.max_custom_pulses`,
                `pulse_width` is not a positive multiple of
                `DeviceInfo.min_pulse_width_us`, the last sample would
                start after `MAX_TIME`, or the device does not
                acknowledge the command.
        """
        pulse_voltages = self._as_list(pulse_voltages)
        pulse_width_cycles = self._custom_time_cycles(pulse_width, "pulse_width")
        pulse_times = [pulse_width_cycles * i for i in range(len(pulse_voltages))]
        pulse_voltage_bits = [
            self._volts_to_bits(voltage, "pulse_voltages")
            for voltage in pulse_voltages
        ]

        self._send_custom_train(
            custom_train_id,
            pulse_times,
            pulse_voltage_bits,
            "send_custom_waveform()",
        )

    def trigger(self, channels):
        """Trigger output channels in software.

        The channels start their pulse trains together, in the same timer
        cycle. A channel that is already playing a pulse train ignores
        the trigger, and `PulsePalDevice.stop` cancels a trigger that has
        not started its channel yet.

        ```python
        P.trigger(3)          # one channel
        P.trigger([1, 4])     # several: a list, tuple or NumPy array
        ```

        Args:
            channels: Output channel number, 1-4, or several as a list,
                tuple or NumPy array.

        Raises:
            PulsePalError: If a channel number is not 1-4, or no channel
                is given. Nothing is triggered.
        """
        self._write_serial((self._OP_MENU_BYTE, 77, _common.channel_bits(channels)), "uint8")

    def stop(self, channels=None):
        """Stop pulse trains currently playing on the device.

        The stopped channels return to their
        `PulsePalDevice.resting_voltage`. A soft trigger that has not
        started its channel yet is cancelled too.

        ```python
        P.stop()           # all output channels
        P.stop([1, 3, 4])  # channels 1, 3 and 4
        ```

        Args:
            channels: Output channel number, 1-4, or several as a list,
                tuple or NumPy array. `None`, the default, stops all
                channels. Stopping some channels requires firmware v22
                or newer.

        Raises:
            PulsePalError: If a channel number is not 1-4, or channels are
                given and the firmware is older than v22.
        """
        if self.info.firmware_version < 22:
            if channels is not None:
                raise PulsePalError("stop() cannot address individual channels before firmware v22")
            self._write_serial((self._OP_MENU_BYTE, 80), "uint8")  # Stops all channels
            return
        bits = 0x0F if channels is None else _common.channel_bits(channels)
        self._write_serial((self._OP_MENU_BYTE, 98, bits), "uint8")

    def save_settings_file(self, file_name):
        """Save the parameters to a settings file on the device's microSD card.

        A settings file holds a complete Pulse Pal program, which can be
        loaded later with `PulsePalDevice.load_settings_file` or from the
        device's joystick menu. A file of the same name is replaced. To
        keep parameters on this computer instead, see
        `PulsePalDevice.export_params`.

        ```python
        P.save_settings_file("MyProtocol.pps")
        ```

        Args:
            file_name: At most 15 ASCII characters, ending in `.pps`.

        Raises:
            PulsePalError: If the file name is invalid, or the device does
                not acknowledge the command.
        """
        self._settings_file_op(file_name, 1, "save_settings_file()")

    def load_settings_file(self, file_name):
        """Load a settings file from the device's microSD card.

        The device plays the program in the file, and the parameters are
        read back into this object (see `PulsePalDevice.sync_from_device`).

        Args:
            file_name: The file's name, ending in `.pps`, as saved by
                `PulsePalDevice.save_settings_file` or the joystick menu.

        Raises:
            PulsePalError: If the file name is invalid, or the device could
                not load the file. A load that fails leaves the device on
                its own default parameters (not the ones
                `set_default_params` sets), and they are read back into
                this object before the error is raised.
        """
        self._settings_file_op(file_name, 2, "load_settings_file()")

    def delete_settings_file(self, file_name):
        """Delete a settings file from the device's microSD card.

        Args:
            file_name: The file's name, ending in `.pps`.

        Raises:
            PulsePalError: If the file name is invalid, or the device does
                not acknowledge the command.
        """
        self._settings_file_op(file_name, 3, "delete_settings_file()")

    def _settings_file_op(self, file_name, op_byte, context):
        """Save (op_byte 1), load (2) or delete (3) a settings file, with op 90."""
        valid = (
            isinstance(file_name, str)
            and all(32 <= ord(character) <= 126 for character in file_name)  # Printable ASCII
            and file_name.lower().endswith(".pps")
            and 4 < len(file_name) <= 15
        )
        if not valid:
            raise PulsePalError(
                f"{context}: the file name must be 1 to 11 ASCII characters followed by .pps, "
                f"e.g. 'Protocol1.pps'. Received {file_name!r}."
            )
        filename_bytes = file_name.encode("ascii")
        self._write_serial(
            (self._OP_MENU_BYTE, 90, op_byte, len(filename_bytes)),
            "uint8",
            list(filename_bytes),
            "uint8",
        )
        if self.info.firmware_version > 21:
            # Sent after the file operation has finished. A refused load leaves the device on
            # its default parameters, so the local copy is read back first.
            self._read_ack(context, on_refusal=self.sync_from_device if op_byte == 2 else None)
        elif op_byte == 2:
            time.sleep(0.1)  # Firmware v21 does not acknowledge, so allow time for the load
        if op_byte == 2:
            self.sync_from_device()

    def format_microsd(self, *, confirm=True, timeout=30):
        """Format the device's microSD card.

        Erases every settings file stored on the device and resets its
        parameters to the defaults. By default, asks at the console for
        confirmation first; a script that runs on its own, or an AI agent,
        passes `confirm=False`.

        Requires Pulse Pal hardware v3 or newer.

        Args:
            confirm: If `True`, ask at the console before anything is
                erased. If `False`, format at once.
            timeout: Seconds to wait for the device to report that
                formatting has finished.

        Returns:
            `True` once the card has been formatted, or `False` if the
            user declines the confirmation prompt.

        Raises:
            PulsePalError: If the connected hardware is older than v3, if
                the device reports that formatting failed, or if it does
                not report a result within `timeout` seconds.
        """
        if self.info.hardware_version < 3:
            raise PulsePalError(
                "format_microsd() requires hardware v3 or newer."
            )

        if to_bool(confirm, "confirm"):
            print("*** Pulse Pal microSD Formatter ***")
            print("This will format Pulse Pal's microSD card,")
            print("erase all settings files on the device")
            print("and reset all parameters to defaults.")
            reply = input("Do you want to continue (y/n)")
            if reply.strip().lower() != "y":
                print("Choice confirmed - microSD Card NOT formatted.")
                return False

        self._write_serial((self._OP_MENU_BYTE, 97), "uint8")

        # The device replies with lines of status text, the last of which
        # contains "!", and then a confirm byte (1 if the card was formatted,
        # 0 if not). The confirm byte is sent after the device has reloaded
        # its default parameters, so it can arrive well after the text. It
        # must be read here: left in the buffer, it would be taken as the
        # reply to the next command, and every reply after that would be
        # read one byte late.
        start = time.time()
        message = bytearray()
        flag_index = -1
        line_end = -1

        while time.time() - start < timeout:
            n_waiting = self.bytes_available()
            if n_waiting:
                message.extend(self.port.read(n_waiting))
                flag_index = message.find(b"!")
                if flag_index >= 0:
                    line_end = message.find(b"\n", flag_index)
                if line_end >= 0 and len(message) > line_end + 1:
                    break
            time.sleep(0.01)
        if line_end < 0 or len(message) <= line_end + 1:
            raise PulsePalError(
                "Pulse Pal did not report the result of formatting "
                f"its microSD card within {timeout} s."
            )
        confirm_byte = message[line_end + 1]

        text = bytes(message[:flag_index]).decode(
            "ascii", errors="replace"
        ).rstrip()
        if text:
            print(text)

        # The device has loaded its own defaults, which are this class's
        for name, value in _DEFAULT_OUTPUT_VALUES.items():
            getattr(self, name)._store([value] * 4)
        self._trigger_mode._store(["Normal"] * 2)
        if confirm_byte != 1:
            raise PulsePalError(
                "Pulse Pal could not format its microSD card."
            )
        return True

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
            The `pulsepal.gui.PulsePalGUI` instance driving the window.

        Raises:
            ValueError: If the theme name is not recognized.
        """
        gui = getattr(self, "_gui", None)
        if gui is not None and not gui.is_closed:
            if theme is not None:
                gui.set_theme(theme)
            gui.focus()
            return gui

        from .gui import PulsePalGUI

        gui = PulsePalGUI(self, theme=theme)
        self._gui = gui
        gui.start(block=block)
        return gui

    # ------------------------------------------------------------------
    # Connection
    # ------------------------------------------------------------------

    def close(self, send_disconnect=True):
        """Close the connection to the device, and the GUI if open.

        The device stops all output channels when the client disconnects,
        and shows its own name on its screen again. Safe to call more
        than once; later calls do nothing. Called automatically when
        leaving a `with` block and when the object is garbage collected,
        so pulse trains also stop when the last reference to the object
        goes, e.g. when a function that created it returns.

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
        self._closed = True
        try:
            if send_disconnect and self.port and self.port.is_open:
                self._write_serial((self._OP_MENU_BYTE, 81), "uint8")
        except Exception:
            pass  # The port may already be gone, e.g. the cable was unplugged
        finally:
            if self.port and self.port.is_open:
                self.port.close()

    def bytes_available(self):
        """Return the number of bytes waiting in the serial read buffer.

        Returns:
            Count of bytes that can be read without blocking.
        """
        return self.port.in_waiting

    def __enter__(self):
        """Enter a `with` block, returning the connected device."""
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        """Disconnect and close the port when leaving a `with` block.

        Returns:
            `False`, so any exception raised in the block propagates.
        """
        self.close()
        return False

    def __del__(self):
        """Disconnect and close the port when the object is collected."""
        try:
            self.close()
        except Exception:
            # Destructors should not raise; the serial object may already be
            # gone during interpreter shutdown.
            pass

    def __repr__(self):
        """Describe the device and its parameters, e.g. with `print(P)`."""
        port = getattr(getattr(self, "port", None), "port", None)
        lines = [
            f"PulsePalDevice on {port} (Pulse Pal {self.info.hardware_version}, "
            f"firmware v{self.info.firmware_version})",
            f"auto_sync: {self._auto_sync}",
        ]
        for name in self.info.output_parameter_names:
            lines.append(f"{name}: {list(getattr(self, name))}")
        lines.append(f"trigger_mode: {list(self._trigger_mode)}")
        return "\n".join(lines)

    # ------------------------------------------------------------------
    # Internals: parameters
    # ------------------------------------------------------------------

    def _has_param_sync(self):
        """Param sync mode is on Pulse Pal 3, with firmware v22 or newer."""
        return (self.info.hardware_version or 2) > 2 and (self.info.firmware_version or 0) > 21

    def _get_output_param_code(self, param_name):
        """Resolve an output parameter name or code to its code."""
        if isinstance(param_name, str):
            if param_name not in _OUTPUT_PARAMETER_CODES:
                raise PulsePalError(f"Unknown output parameter: {param_name}.")
            return _OUTPUT_PARAMETER_CODES[param_name]
        if isinstance(param_name, numbers.Integral) and param_name in _OUTPUT_PARAMETERS:
            return int(param_name)
        raise PulsePalError(f"Unknown output parameter: {param_name!r}.")

    def _get_trigger_param_code(self, param_name):
        """Resolve a trigger parameter name or code to its code."""
        if param_name in ("trigger_mode", _TRIGGER_MODE_CODE) and not isinstance(param_name, bool):
            return _TRIGGER_MODE_CODE
        raise PulsePalError(f"Unknown trigger parameter: {param_name!r}.")

    def _normalize_output_value(self, code, value, channel):
        """Check a value for an output parameter, and return it as the parameter's list holds it:
        a time as the device plays it, rounded to its timer cycle.

        Raises PulsePalError, before anything is sent, for a value the device cannot play. The
        device refuses most of them too, but it also resets them (see validateOutputParams() in
        /Firmware/PulsePal3/USBOps.ino), so the local copy would describe a different program.
        """
        name, kind = _OUTPUT_PARAMETERS[code]
        label = f"{name} on channel {channel}"
        if kind == "bool":
            value = to_bool(value, label)
            if code == _CONTINUOUS_LOOP_CODE and value and self.info.firmware_version < 22:
                raise PulsePalError("continuous_loop requires firmware v22 or newer.")
            return value
        if kind == "volts":
            self._volts_to_bits(value, label)
            return float(value)
        if kind in ("time", "pulse_time"):
            cycles = self._seconds_to_cycles(value, label)
            # Checked in whole cycles, as the device receives it: 100 * 1e-6 is just under 0.0001,
            # but is exactly 2 cycles of 50 us
            if kind == "pulse_time" and cycles < self._MIN_PULSE_CYCLES:
                raise PulsePalError(
                    f"{label} must be at least {self._min_pulse_seconds()} s "
                    f"({self._MIN_PULSE_CYCLES} cycles of the device's {self.info.cycle_period_us} us "
                    f"timer), so that trigger channels can detect the pulses. Received {value!r}."
                )
            return self._cycles_to_seconds(cycles)  # The time the device plays
        if kind == "train_id":
            self._check_whole_number(value, label, 0, self.info.n_custom_pulse_trains)
            return int(value)
        # "target"
        return to_name(value, CUSTOM_TRAIN_TARGETS, label, codes={"Pulses": 0, "Bursts": 1})

    def _normalize_trigger_mode(self, value):
        modes = self.info.trigger_modes
        return to_name(value, modes, "trigger_mode", codes={mode: TRIGGER_MODES.index(mode) for mode in modes})

    def _encode_output_param(self, code, values):
        """Convert output parameter values to device units.

        Returns the converted values and the datatype the device reads for this
        parameter code: DAC codes for voltages, hardware timer cycles for times,
        and bytes for everything else.
        """
        name, kind = _OUTPUT_PARAMETERS[code]
        if kind == "volts":
            return [self._volts_to_bits(v, name) for v in values], "uint16"
        if kind in ("time", "pulse_time"):
            return [self._seconds_to_cycles(v, name) for v in values], "uint32"
        if kind == "target":
            return [CUSTOM_TRAIN_TARGETS.index(v) for v in values], "uint8"
        return [int(v) for v in values], "uint8"

    def _apply_output_param(self, code, values):
        """Check new values for all four channels of an output parameter, and with auto_sync on,
        program them. Returns them as the parameter's list holds them."""
        values = [self._normalize_output_value(code, v, ch) for ch, v in enumerate(values, start=1)]
        if self._auto_sync:
            if self.info.firmware_version > 21:
                self._send_output_param(code, values, f"setting {_OUTPUT_PARAMETERS[code][0]}")
            else:
                for ch in range(1, 5):
                    self._send_output_param_to_channel(code, ch, values[ch - 1],
                                                       f"setting {_OUTPUT_PARAMETERS[code][0]}")
        return values

    def _send_output_param(self, code, values, context):
        """Program one output parameter on all four channels with one command (op 91)."""
        data, datatype = self._encode_output_param(code, values)
        self._write_serial((self._OP_MENU_BYTE, 91, code), "uint8", data, datatype)
        self._read_ack(context, on_refusal=lambda: self._refresh_output_param(code, (1, 2, 3, 4)))

    def _send_output_param_to_channel(self, code, channel, value, context):
        """Program one output parameter on one channel (op 74)."""
        if code == _CONTINUOUS_LOOP_CODE and self.info.firmware_version < 22:
            return  # Firmware v21 has no parameter 18, and _normalize_output_value() lets only False through
        data, datatype = self._encode_output_param(code, [value])
        self._write_serial((self._OP_MENU_BYTE, 74, code, channel), "uint8", data, datatype)
        self._read_ack(context, on_refusal=lambda: self._refresh_output_param(code, (channel,)))

    def _apply_trigger_mode(self, values):
        modes = [self._normalize_trigger_mode(v) for v in values]
        if self._auto_sync:
            self._send_trigger_modes(modes, "setting trigger_mode")
        return modes

    def _send_trigger_modes(self, modes, context):
        """Program both trigger channels' modes: op 91, or op 74 per channel on firmware v21."""
        codes = [TRIGGER_MODES.index(mode) for mode in modes]
        if self.info.firmware_version > 21:
            self._write_serial((self._OP_MENU_BYTE, 91, _TRIGGER_MODE_CODE, *codes), "uint8")
            self._read_ack(context, on_refusal=self._refresh_trigger_mode)
        else:
            for channel, mode_code in enumerate(codes, start=1):
                self._write_serial((self._OP_MENU_BYTE, 74, _TRIGGER_MODE_CODE, channel, mode_code), "uint8")
                self._read_ack(context)

    def _snapshot(self):
        """All parameters, for batch() to restore if its block fails."""
        names = [*self.info.output_parameter_names, "trigger_mode"]
        return {name: list(getattr(self, name))[1:] for name in names}

    def _restore(self, snapshot):
        for name, values in snapshot.items():
            getattr(self, name)._store(values)

    def _check_sync(self):
        """Check the whole local copy before sync_to_device() sends it."""
        for code, (name, _) in _OUTPUT_PARAMETERS.items():
            values = getattr(self, name)
            for ch in range(1, 5):
                self._normalize_output_value(code, values[ch], ch)
        for ch in (1, 2):
            self._normalize_trigger_mode(self._trigger_mode[ch])

    def _send_sync(self):
        """Send every parameter in one command: op 92 on firmware v22 or newer, op 73 on v21."""
        if self.info.firmware_version > 21:
            self._sync_all_params()
        else:
            self._sync_all_params_legacy()
        self._read_ack("sync_to_device()", on_refusal=self._refresh_after_refused_sync)

    def _device_bytes(self, name):
        """The byte values of a byte parameter for channels 1-4, as the device reads them."""
        values = list(getattr(self, name))[1:]
        if name == "custom_train_target":
            return [CUSTOM_TRAIN_TARGETS.index(v) for v in values]
        return [int(v) for v in values]

    def _sync_all_params(self):
        """Send all parameters using the packed sync op (firmware v22+).

        Values are grouped by width so that the whole program travels as
        one uint32 block, one uint16 block and one uint8 block. See op 92
        in /Firmware/PROTOCOL.md.
        """
        time_values = []
        for name in _TIME_PARAMETERS:
            time_values.extend(self._seconds_to_cycles(v) for v in list(getattr(self, name))[1:])
        voltage_values = []
        for name in _VOLTAGE_PARAMETERS:
            voltage_values.extend(self._volts_to_bits(v) for v in list(getattr(self, name))[1:])
        single_byte_values = []
        for name in ("is_biphasic", "custom_train_id", "custom_train_target", "custom_train_loop",
                     "continuous_loop", "link_trigger_channel1", "link_trigger_channel2"):
            single_byte_values.extend(self._device_bytes(name))
        single_byte_values.extend(TRIGGER_MODES.index(mode) for mode in list(self._trigger_mode)[1:])

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
        channel by channel as the older firmware expects. Continuous
        loop mode is not part of it.
        """
        program_values_32 = []
        program_values_16 = []
        program_values_8 = []
        for ch in range(1, 5):
            program_values_32.extend(self._seconds_to_cycles(getattr(self, name)[ch]) for name in _TIME_PARAMETERS)
        for ch in range(1, 5):
            program_values_16.extend(self._volts_to_bits(getattr(self, name)[ch]) for name in _VOLTAGE_PARAMETERS)
        byte_values = {name: self._device_bytes(name) for name in
                       ("is_biphasic", "custom_train_id", "custom_train_target", "custom_train_loop")}
        for ch in range(4):
            program_values_8.extend(byte_values[name][ch] for name in byte_values)
        links = self._device_bytes("link_trigger_channel1") + self._device_bytes("link_trigger_channel2")
        modes = [TRIGGER_MODES.index(mode) for mode in list(self._trigger_mode)[1:]]

        self._write_serial(
            (self._OP_MENU_BYTE, 73),
            "uint8",
            program_values_32,
            "uint32",
            program_values_16,
            "uint16",
            program_values_8 + links + modes,
            "uint8",
        )

    def _read_device_params(self):
        """Read the device's parameters (op 93).

        Returns a dict of parameter values for channels 1-4 (trigger
        channels 1-2 for trigger_mode), as the parameter lists hold them.
        Continuous loop mode is not part of op 93, so `continuous_loop` is
        not included.
        """
        self._write_serial((self._OP_MENU_BYTE, 93), "uint8")
        # The device sends the whole parameter set as one message, so read it in one go and
        # unpack it here. See "Op codes", op 93, in /Firmware/PROTOCOL.md.
        message = self._read_raw(self._PARAM_MESSAGE_BYTES)
        values = struct.unpack(f"{self._ENDIANNESS}32I12H26B", message)
        params = {}
        for index, name in enumerate(_TIME_PARAMETERS):
            params[name] = [self._cycles_to_seconds(x) for x in values[index * 4:index * 4 + 4]]
        for index, name in enumerate(_VOLTAGE_PARAMETERS):
            params[name] = [self._bits_to_volts(x) for x in values[32 + index * 4:32 + index * 4 + 4]]
        for index, name in enumerate(("is_biphasic", "custom_train_id", "custom_train_target",
                                      "custom_train_loop", "link_trigger_channel1", "link_trigger_channel2")):
            codes = values[44 + index * 4:44 + index * 4 + 4]
            if name == "custom_train_id":
                params[name] = list(codes)
            elif name == "custom_train_target":
                params[name] = [CUSTOM_TRAIN_TARGETS[min(code, 1)] for code in codes]
            else:
                params[name] = [bool(code) for code in codes]
        params["trigger_mode"] = [TRIGGER_MODES[min(code, 3)] for code in values[68:70]]
        return params

    def _refresh_output_param(self, code, channels):
        """Read an output parameter back from the device into the local copy, for these channels.

        Called after the device refused a value for them, which it resets. The parameter's other
        channels, and other parameters, keep their local values, which may hold edits not yet synced.
        """
        name = _OUTPUT_PARAMETERS[code][0]
        if self.info.firmware_version < 22 or code == _CONTINUOUS_LOOP_CODE:
            return  # No op 93 to read it with, or not in op 93
        device_values = self._read_device_params()[name]
        settings = getattr(self, name)
        local_values = list(settings)[1:]
        for ch in channels:
            local_values[ch - 1] = device_values[ch - 1]
        settings._store(local_values)

    def _refresh_trigger_mode(self):
        """Read the trigger modes back from the device, after the device refused a new one."""
        if self.info.firmware_version > 21:
            self._trigger_mode._store(self._read_device_params()["trigger_mode"])

    def _refresh_after_refused_sync(self):
        """Read the parameters back after the device refused a sync_to_device() set.

        The device programs the set with the refused values reset, so the local copy is read back.
        In param sync mode the device stores the set instead, and op 93 would return the parameters
        playing now, so the local copy is left as it is.
        """
        if self.info.firmware_version > 21 and "Param Sync" not in list(self._trigger_mode)[1:]:
            self.sync_from_device()

    def _check_whole_number(self, value, name, low, high):
        """Raise PulsePalError unless value is a whole number from low to high."""
        valid = (
            isinstance(value, numbers.Real)
            and not isinstance(value, (bool, np.bool_))
            and math.isfinite(value)
            and value == int(value)
            and low <= value <= high
        )
        if not valid:
            raise PulsePalError(
                f"{name} must be a whole number from {low} to {high}. Received {value!r}."
            )

    def _min_pulse_seconds(self):
        """The shortest pulse phase, interval or train, in seconds. See _MIN_PULSE_CYCLES."""
        return self._cycles_to_seconds(self._MIN_PULSE_CYCLES)

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

    # ------------------------------------------------------------------
    # Internals: custom trains
    # ------------------------------------------------------------------

    def _send_custom_train(self, custom_train_id, pulse_times_cycles,
                           pulse_voltage_bits, context):
        """Send a custom pulse train, already converted to device units.

        Uses op 95 on firmware v22 or newer, and the legacy ops 75 and 76
        (trains 1 and 2 only) on firmware v21.
        """
        n_trains = self.info.n_custom_pulse_trains
        valid_id = (
            isinstance(custom_train_id, numbers.Integral)
            and not isinstance(custom_train_id, (bool, np.bool_))
            and 1 <= custom_train_id <= n_trains
        )
        if not valid_id:
            raise PulsePalError(
                f"{context}: custom_train_id must be an integer from 1 to "
                f"{n_trains}. Received {custom_train_id!r}."
            )
        train_id = int(custom_train_id)
        n_pulses = len(pulse_times_cycles)
        if n_pulses > self.info.max_custom_pulses:
            raise PulsePalError(
                f"{context}: {n_pulses} pulses were given. Pulse Pal can "
                f"store up to {self.info.max_custom_pulses} pulses per "
                "custom train."
            )
        # The device plays each pulse until the next one's time, so a time
        # that is not later than the one before it would freeze the output
        # for the rest of the train. Times are multiples of _MIN_PULSE_CYCLES
        # (see _custom_time_cycles()), so increasing times are far enough
        # apart for a trigger channel to detect each pulse.
        for i in range(1, n_pulses):
            if (pulse_times_cycles[i] - pulse_times_cycles[i - 1]
                    < self._MIN_PULSE_CYCLES):
                raise PulsePalError(
                    f"{context}: pulse times must increase, by at least "
                    f"{self._min_pulse_seconds()} s ({self._MIN_PULSE_CYCLES} "
                    f"cycles of the device's {self.info.cycle_period_us} us "
                    f"timer). Pulse {i + 1} is at "
                    f"{self._cycles_to_seconds(pulse_times_cycles[i])} s, "
                    f"and pulse {i} is at "
                    f"{self._cycles_to_seconds(pulse_times_cycles[i - 1])} s."
                )
        if n_pulses and pulse_times_cycles[-1] > self._max_time_cycles():
            raise PulsePalError(
                f"{context}: the last pulse starts at {self._cycles_to_seconds(pulse_times_cycles[-1])} s. "
                f"Pulse times must be at most {self.info.max_time} s."
            )

        if self.info.firmware_version > 21:
            header = (self._OP_MENU_BYTE, 95, train_id - 1)
        else:
            header = (self._OP_MENU_BYTE, 74 + train_id)
        self._write_serial(
            header,
            "uint8",
            n_pulses,
            "uint32",
            pulse_times_cycles,
            "uint32",
            pulse_voltage_bits,
            "uint16",
        )
        self._read_ack(context)

    # ------------------------------------------------------------------
    # Internals: serial port and units
    # ------------------------------------------------------------------

    def _write_serial(self, *args):
        """Write one or more data/type pairs to the serial port, in one write."""
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
                f"Wrote {bytes_written} byte(s), expected to write "
                f"{len(payload)} byte(s)."
            )

    def _read_raw(self, n_bytes):
        """Read exactly n_bytes from the serial port."""
        message_bytes = self.port.read(n_bytes)
        if len(message_bytes) < n_bytes:
            raise PulsePalError(
                f"Serial port timed out. "
                f"{len(message_bytes)} byte(s) read. "
                f"Expected {n_bytes} byte(s)."
            )
        return message_bytes

    def _read_serial(self, n_values, datatype):
        """Read values from the serial port and unpack them with struct."""
        fmt = self._STRUCT_FORMATS[datatype]
        n_values = int(n_values)
        n_bytes = n_values * struct.calcsize(fmt)
        message_bytes = self._read_raw(n_bytes)

        values = struct.unpack(
            f"{self._ENDIANNESS}{n_values}{fmt}",
            message_bytes,
        )
        if n_values == 1:
            return values[0]
        return list(values)

    def _read_ack(self, context, on_refusal=None):
        """Read a one-byte acknowledgement from the device.

        The device replies 1 if it executed the command, or 0 if it rejected
        the command because a value was out of range. On a 0, on_refusal()
        is called before the error is raised: the device resets a value it
        refuses, so callers use it to read the value back into the local copy.
        """
        try:
            acknowledgement = self._read_serial(1, "uint8")
        except PulsePalError as exc:
            raise PulsePalError(
                "Pulse Pal did not return an acknowledgement byte "
                f"after {context}."
            ) from exc
        if acknowledgement != 1:
            refusal = PulsePalError(
                f"Pulse Pal rejected the command sent by {context}. "
                "This usually means that a channel number, parameter code or "
                "value was out of range for the connected device."
            )
            if on_refusal is not None:
                try:
                    on_refusal()
                except PulsePalError as exc:
                    raise refusal from exc
            raise refusal

    def _pack_values(self, values, datatype):
        """Pack scalar, list/tuple, or NumPy array values into bytes."""
        fmt = self._STRUCT_FORMATS[datatype]
        values_list = self._as_list(values)
        min_value, max_value = self._TYPE_RANGES[datatype]
        normalized = []
        for value in values_list:
            value = int(value)
            if not min_value <= value <= max_value:
                raise PulsePalError(
                    f"Value {value} is out of range for {datatype} "
                    f"({min_value} to {max_value})."
                )
            normalized.append(value)
        return struct.pack(f"{self._ENDIANNESS}{len(normalized)}{fmt}", *normalized)

    @staticmethod
    def _as_list(values):
        """Return values as a flat list, wrapping scalars in one."""
        if isinstance(values, np.ndarray):
            return values.ravel().tolist()
        if isinstance(values, (bytes, bytearray, str)):
            return [values]
        if isinstance(values, numbers.Number):
            return [values]
        try:
            return list(values)
        except TypeError:
            return [values]

    def _volts_to_bits(self, value, name="voltage"):
        """Convert -10 V to +10 V to the corresponding DAC code.

        Rounds to the nearest code, halves to even (round()), as the MATLAB and C++ classes do.
        Raises PulsePalError for a voltage outside [-10, 10] or not a number: clamping it would
        play a different voltage.
        """
        try:
            volts = float(value)
        except (TypeError, ValueError):
            volts = float("nan")
        if isinstance(value, (bool, np.bool_, str)) or not -10 <= volts <= 10:  # Also refuses NaN
            raise PulsePalError(f"{name} must be in [-10, 10] V. Received {value!r}.")
        return int(round((volts + 10) / 20 * self._DAC_BITMAX))

    def _bits_to_volts(self, value):
        """Convert a DAC code to volts, snapping to a clean value within 1 LSB."""
        raw_volts = (float(value) / self._DAC_BITMAX * 20) - 10
        lsb_volts = 20.0 / self._DAC_BITMAX
        # The nearest 3 decimal number (e.g. 5.000, 4.255), if the code is within 1 LSB of it
        clean_volts = round(raw_volts, 3)
        if abs(raw_volts - clean_volts) <= lsb_volts:
            return clean_volts
        return round(raw_volts, 4)

    def _seconds_to_cycles(self, value, name="time"):
        """Convert seconds to the corresponding refresh-cycle count.

        Rounds to the nearest cycle, halves to even (round()), as the MATLAB and C++ classes do:
        125 us, 2.5 cycles, is 2 cycles in all three. Raises PulsePalError for a time that is
        negative, not a number, or longer than DeviceInfo.max_time.
        """
        try:
            seconds = float(value)
        except (TypeError, ValueError):
            seconds = float("nan")
        if isinstance(value, (bool, np.bool_, str)) or not (math.isfinite(seconds) and seconds >= 0):
            raise PulsePalError(f"{name} must be a time of 0 s or more. Received {value!r}.")
        cycles = int(round(seconds * float(self.info.cycle_frequency)))
        if cycles > self._max_time_cycles():
            raise PulsePalError(
                f"{name} must be at most {self.info.max_time} s, the longest time the device's screen "
                f"shows. Received {value!r}."
            )
        return cycles

    def _max_time_cycles(self):
        """DeviceInfo.max_time in timer cycles: 199999998 on a 50 us timer."""
        return int(round(self.info.max_time * float(self.info.cycle_frequency)))

    def _custom_time_cycles(self, value, name):
        """Convert a custom pulse time or sampling period to timer cycles.

        It must be a whole number of DeviceInfo.min_pulse_width_us (100 us), to the nearest
        microsecond, as in the MATLAB class. A time between two steps is refused, not rounded
        as the time parameters are, so that a train plays as it was written.
        """
        self._seconds_to_cycles(value, name)  # Checks that it is a time the device can play
        microseconds = int(round(float(value) * 1e6))
        step_us = int(self.info.min_pulse_width_us)
        if microseconds % step_us:
            raise PulsePalError(
                f"{name} must be multiples of {step_us} us ({step_us / 1e6:g} s). Received {value!r}."
            )
        return microseconds // int(self.info.cycle_period_us)

    def _cycles_to_seconds(self, value):
        """Convert hardware timer cycle counts to seconds."""
        return float(value) / float(self.info.cycle_frequency)


def _apply_method(code):
    def apply(self, values):
        return self._apply_output_param(code, values)
    return apply


# One _apply_<name> method per output parameter, which its ChannelSettings list calls
for _code, (_name, _kind) in _OUTPUT_PARAMETERS.items():
    setattr(PulsePalDevice, f"_apply_{_name}", _apply_method(_code))
del _code, _name, _kind
