"""
Python interface for Synth Pal, a waveform synthesizer for Pulse Pal 3.

Synth Pal is alternative firmware for Pulse Pal 3 hardware. Each output
channel plays a sine, triangle, square or sawtooth wave, or steps to a
fixed voltage, when it is triggered: by a TTL pulse on a trigger channel,
from software, or from the thumb joystick. Each channel has its own
waveform, amplitude, mean voltage, resting voltage, play duration, and
on and off ramps, and one frequency, 1 Hz to 20 kHz in steps of 0.01 Hz,
applies to all four.

Everything is accessed through `SynthPalDevice`. Import it, connect to
the device's serial port, set the waveforms, and trigger, e.g.

```python
from SynthPal import SynthPalDevice

with SynthPalDevice("COM3") as S:
    S.frequency = 440                  # Hz, all channels
    S.waveform[1] = "Sine"
    S.amplitude[1] = 4                 # volts peak to peak
    S.play_duration[1] = 0.5           # seconds
    S.play(1)
```

The device needs Synth Pal firmware, which is in
[/Firmware/SynthPal](https://github.com/sanworks/PulsePal/tree/develop/Firmware/SynthPal) in the
Pulse Pal repository. Its USB protocol is documented in
[PROTOCOL.md](https://github.com/sanworks/PulsePal/blob/develop/Firmware/SynthPal/PROTOCOL.md).

## Channel settings

Settings that apply to one output channel, such as
`SynthPalDevice.amplitude`, are lists indexed by channel number: index 0
is unused and holds `None`, and indices 1 to 4 hold the settings of
output channels 1-4, as in `PulsePal.PulsePalDevice`. Setting an element
or a slice programs the device at once. Assigning a whole list sets all
four channels, and a single value sets them all to that value:

```python
S.amplitude[2] = 5                 # channel 2 only
S.play_duration[1:5] = [1, 2, 3, 4]
S.waveform = "Square"              # all four channels
```

`SynthPalDevice.trigger_mode` is indexed the same way by trigger
channel number, 1 or 2.

## Units

Voltages are in volts, times in seconds, and frequencies in Hz. A
channel's waveform swings `amplitude / 2` above and below its
`mean_voltage`, and must stay within -10 V to 10 V. A `"Fixed Voltage"`
channel steps to its `amplitude`, a voltage from -10 V to 10 V, for its
play duration. Between playbacks, a channel outputs its
`resting_voltage`, and its `on_ramp_duration` and `off_ramp_duration`
fade it in from there and back to it (see "Ramps" below).

## Ramps

A channel's on ramp follows each trigger, and fades its waveform in from
the resting voltage: its amplitude rises linearly from 0 to its full
amplitude, and its mean from the resting voltage to the mean voltage (a
fixed voltage ramps from the resting voltage to its voltage). The play
duration follows at full amplitude, then the off ramp fades back to the
resting voltage. The off ramp also follows a stop: `stop()`, a toggle or
gated trigger, or the joystick. So the ramps lengthen playback: from a
trigger to rest takes `on_ramp_duration + play_duration +
off_ramp_duration`. During its off ramp, a channel counts as stopping: a
trigger fades it in again from where it is, without restarting its
waveform's cycle, and plays its play duration again. A channel stopped
during its on ramp fades out from where it is, at the off ramp's rate.
The output never jumps.

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
import weakref

import numpy as np
import serial
import serial.tools.list_ports

__all__ = ["SynthPalDevice", "DeviceInfo", "DeviceStatus", "SynthPalError"]
__docformat__ = "google"

# Waveforms in order of their code on the device
WAVEFORMS = ("Sine", "Triangle", "Square", "Sawtooth", "Fixed Voltage")
FIXED_VOLTAGE = "Fixed Voltage"  # Its amplitude is a voltage, not peak to peak

# Trigger modes in order of their code on the device. These are Pulse Pal's
# trigger modes, with the same codes.
TRIGGER_MODES = ("Normal", "Toggle", "Gated")

# The output ranges the device chooses from, in order of their index on the
# device, with their limits in volts. See "Output ranges" in
# /Firmware/SynthPal/PROTOCOL.md.
OUTPUT_RANGES = {
    "0V:5V": (0.0, 5.0),
    "0V:10V": (0.0, 10.0),
    "-5V:5V": (-5.0, 5.0),
    "-10V:10V": (-10.0, 10.0),
}


class SynthPalError(Exception):
    """Raised when Synth Pal communication or configuration fails.

    This covers serial reads that time out, short serial writes,
    commands the device rejects, and values that are out of range, such
    as a frequency the device cannot play or a waveform that would go
    beyond -10 V to 10 V.
    """


@dataclass
class DeviceInfo:
    """Properties of the connected Synth Pal.

    Populated when `SynthPalDevice` connects, and available as
    `SynthPalDevice.info`.
    """

    firmware_version: int = None
    """Synth Pal firmware version running on the device."""

    hardware_version: int = None
    """Pulse Pal hardware version, e.g. `3`."""

    n_channels: int = None
    """Number of output channels."""

    min_frequency: float = None
    """Lowest frequency, in Hz."""

    max_frequency: float = None
    """Highest frequency, in Hz."""

    max_sampling_rate: int = None
    """Highest sampling rate, in Hz. See `SynthPalDevice.sampling_rate`."""

    timer_clock_hz: int = None
    """Clock that the sample clock counts, in Hz."""

    max_play_duration: float = None
    """Longest play duration, in seconds. See
    `SynthPalDevice.play_duration`."""

    waveforms: tuple = WAVEFORMS
    """Names of the waveforms, accepted by `SynthPalDevice.waveform`."""

    trigger_modes: tuple = TRIGGER_MODES
    """Names of the trigger modes, accepted by
    `SynthPalDevice.trigger_mode`."""


@dataclass
class DeviceStatus:
    """A snapshot of the device's playback state, from
    `SynthPalDevice.status`."""

    playing: list
    """Numbers of the output channels that are playing, e.g. `[1, 3]`.

    A channel in its off ramp counts as playing until it reaches its
    resting voltage."""

    samples_per_cycle: int
    """Samples in one cycle of the waveform. See
    `SynthPalDevice.samples_per_cycle`."""

    output_ranges: list
    """The output range the device chose for each channel, indexed by
    channel number, with index 0 unused, e.g. `"-5V:5V"`.

    Each channel uses the range with the finest voltage steps that holds
    its whole waveform and its resting voltage: `"0V:5V"` (76 uV steps),
    then `"0V:10V"` or
    `"-5V:5V"` (153 uV), then `"-10V:10V"` (305 uV).
    """

    longest_interrupt_us: float
    """Longest run of the device's sample clock interrupt since the
    previous call to `SynthPalDevice.status`, in microseconds.

    It must stay below the sample period, `1e6 / sampling_rate`.
    """

    late_updates: int
    """Output updates since the previous call to `SynthPalDevice.status`
    that may have come later than their fixed time after a sample clock
    tick. It is normally 0; a change of frequency during playback can
    make one update late. See "Timing" in the
    [Synth Pal protocol](https://github.com/sanworks/PulsePal/blob/develop/Firmware/SynthPal/PROTOCOL.md#timing).
    """


class ChannelSettings(list):
    """One setting per channel, indexed by channel number.

    Index 0 is unused and holds `None`, so `settings[2]` belongs to
    channel 2. Setting an element or a slice programs the device at once;
    if the device refuses the new values, the list is left unchanged.
    The list always holds one element per channel and index 0, so methods
    that would change its length raise `TypeError`. `list(settings)` or
    `copy.copy` gives a plain list, detached from the device.
    """

    def __init__(self, name, values, device, apply_method):
        super().__init__([None, *values])
        self._name = name
        self._n = len(values)
        # A weak reference, so that the device and its settings do not form
        # a reference cycle: deleting the device then closes its port at once
        self._device = weakref.ref(device)
        self._apply_method = apply_method

    def __setitem__(self, index, value):
        values = list(self)
        values[index] = value
        if len(values) != self._n + 1:
            raise SynthPalError(
                f"{self._name} holds one value per channel, at indices "
                f"1-{self._n}. A slice assignment must keep its length."
            )
        if values[0] is not None:
            raise SynthPalError(
                f"{self._name}[0] is unused: channels are numbered "
                f"1-{self._n}."
            )
        self._set_all(values[1:])

    def _assign(self, values):
        """Set all channels from a single value, one value per channel, or
        a list with index 0 unused."""
        if isinstance(values, (str, bytes)) or not _is_iterable(values):
            values = [values] * self._n
        else:
            values = list(values)
            if len(values) == self._n + 1:
                values = values[1:]
            elif len(values) != self._n:
                raise SynthPalError(
                    f"{self._name} needs one value for all channels, or "
                    f"one value per channel 1-{self._n}. Received "
                    f"{len(values)} values."
                )
        self._set_all(values)

    def _set_all(self, values):
        device = self._device()
        if device is None:
            raise SynthPalError(f"The device that owns {self._name} is gone.")
        normalized = getattr(device, self._apply_method)(values)
        super().__setitem__(slice(0, self._n + 1), [None, *normalized])

    def __reduce__(self):
        return (list, (list(self),))

    def _refuse(self, *args, **kwargs):
        raise TypeError(
            f"{self._name} holds exactly one value per channel. "
            "Set its elements instead."
        )

    append = extend = insert = pop = remove = clear = _refuse
    sort = reverse = __delitem__ = __iadd__ = __imul__ = _refuse


def _is_iterable(value):
    try:
        iter(value)
    except TypeError:
        return False
    return True


class SynthPalDevice:
    """A class to control a Synth Pal on a USB serial port.

    Creating an instance opens the serial port, checks that the device
    runs Synth Pal firmware, reads its properties into
    `SynthPalDevice.info`, shows "PYTHON Connected" on the device's
    screen, stops any playback and programs the default settings (see
    `SynthPalDevice.set_defaults`).

    ```python
    from SynthPal import SynthPalDevice

    S = SynthPalDevice("COM3")
    S.waveform[1] = "Triangle"
    S.play(1)
    S.close()
    ```

    Replace "COM3" with the device's USB serial port name, which
    `SynthPalDevice.serialportlist` lists. `SynthPalDevice` is also a
    context manager, which closes the port on exit.

    Closing the connection puts the device's own name back on its
    screen, and leaves everything else as it is: playback continues, and
    TTL triggers keep playing the channels.
    """

    port: "serial.Serial"
    """The open `serial.Serial` port connected to the device."""

    info: DeviceInfo
    """Properties of the connected device. See `DeviceInfo`."""

    _CURRENT_FIRMWARE_VERSION = 1

    _OP_MENU_BYTE = 213
    _OP_HANDSHAKE = 72
    _OP_DISCONNECT = 81
    _OP_SET_CLIENT_NAME = 89
    _OP_SET_SCREEN_SAVER = 99
    _OP_HARDWARE_INFO = ord("N")
    _OP_SET_FREQUENCY = ord("F")
    _OP_SET_WAVEFORM = ord("W")
    _OP_SET_AMPLITUDE = ord("A")
    _OP_SET_RESTING_VOLTAGE = ord("V")
    _OP_SET_MEAN_VOLTAGE = ord("M")
    _OP_SET_PLAY_DURATION = ord("D")
    _OP_SET_ON_RAMP_DURATION = ord("B")
    _OP_SET_OFF_RAMP_DURATION = ord("E")
    _OP_SET_TRIGGER_LINKS = ord("I")
    _OP_SET_TRIGGER_MODE = ord("T")
    _OP_PLAY = ord("P")
    _OP_STOP = ord("X")
    _OP_GET_STATUS = ord("G")
    _OP_GET_PLAYBACK_CHECKSUMS = ord("Z")

    _SYNTH_PAL_HANDSHAKE_REPLY = 83  # 'S'
    _PULSE_PAL_HANDSHAKE_REPLY = 75  # 'K': the device runs Pulse Pal firmware
    _WAVE_PAL_HANDSHAKE_REPLY = 87  # 'W': the device runs Wave Pal firmware
    _HARDWARE_INFO_FORMAT = "<BBIIIII"
    _STATUS_FORMAT = "<BI4BII"
    _ALL_CHANNELS = 0x0F
    _MAX_VOLTAGE_UV = 10_000_000  # Every output voltage stays within +/-10 V

    def __init__(self, port_name, baud_rate=12000000, timeout=10):
        """Open a connection to a Synth Pal.

        Args:
            port_name: USB serial port of the device, such as `COM3` on
                Windows or `/dev/ttyACM0` on Linux.
            baud_rate: Serial baud rate. USB serial ignores it.
            timeout: Serial read timeout, in seconds.

        Raises:
            SynthPalError: If the device does not reply to the handshake,
                runs other firmware, or runs Synth Pal firmware newer than
                this module supports.
            serial.SerialException: If the serial port cannot be opened.
        """
        self._closed = True
        self.info = DeviceInfo()
        self._frequency = None
        self._samples_per_cycle = None
        # The device's defaults, which set_defaults() programs once connected
        self._amplitude_uv = [None, *[5_000_000] * 4]
        self._resting_uv = [None, *[0] * 4]
        self._mean_uv = [None, *[0] * 4]
        self._waveform = ChannelSettings(
            "waveform", ["Sine"] * 4, self, "_apply_waveform")
        self._amplitude = ChannelSettings(
            "amplitude", [5.0] * 4, self, "_apply_amplitude")
        self._resting_voltage = ChannelSettings(
            "resting_voltage", [0.0] * 4, self, "_apply_resting_voltage")
        self._mean_voltage = ChannelSettings(
            "mean_voltage", [0.0] * 4, self, "_apply_mean_voltage")
        self._play_duration = ChannelSettings(
            "play_duration", [1.0] * 4, self, "_apply_play_duration")
        self._on_ramp_duration = ChannelSettings(
            "on_ramp_duration", [0.0] * 4, self, "_apply_on_ramp_duration")
        self._off_ramp_duration = ChannelSettings(
            "off_ramp_duration", [0.0] * 4, self, "_apply_off_ramp_duration")
        self._trigger_mode = ChannelSettings(
            "trigger_mode", ["Normal"] * 2, self, "_apply_trigger_mode")
        self._link_trigger_channel1 = ChannelSettings(
            "link_trigger_channel1", [True] * 4, self,
            "_apply_trigger_channel1_links")
        self._link_trigger_channel2 = ChannelSettings(
            "link_trigger_channel2", [False] * 4, self,
            "_apply_trigger_channel2_links")

        self.port = serial.Serial(
            port_name,
            baud_rate,
            timeout=timeout,
            rtscts=True,
        )
        self._closed = False
        try:
            # Discard anything left in the buffer by an earlier session
            self.port.reset_input_buffer()
            self._handshake()
            self._read_hardware_info()
            # Client name op + "PYTHON" in ASCII, shown as "PYTHON Connected"
            self._write_command(self._OP_SET_CLIENT_NAME, b"PYTHON")
            self.stop()
            self.set_defaults()
        except BaseException:
            # Op 81 means something else to other devices, so it is sent
            # only once the device has identified itself as a Synth Pal
            self.close(
                send_disconnect=self.info.firmware_version is not None)
            raise

    @staticmethod
    def serialportlist(ports_to_list="available"):
        """Return the names of the USB serial ports on this computer.

        Called on the class, without connecting to a device, to find the
        port name to pass to `SynthPalDevice`.

        Args:
            ports_to_list: `available` to list only the ports that are
                not already in use, or `all` to list every USB serial
                port. Not case sensitive.

        Returns:
            Sorted list of port names, such as `["COM3", "COM7"]`.

        Raises:
            SynthPalError: If `ports_to_list` is not `available` or `all`.
        """
        mode = str(ports_to_list).lower()
        if mode not in ("available", "all"):
            raise SynthPalError(
                f"Unknown port list type: {ports_to_list}. "
                "Use 'available' or 'all'."
            )
        port_names = []
        for port_info in serial.tools.list_ports.comports():
            is_usb = port_info.vid is not None or "USB" in (
                port_info.hwid or ""
            ).upper()
            if not is_usb:
                continue
            if mode == "available" and not SynthPalDevice._port_is_free(
                port_info.device
            ):
                continue
            port_names.append(port_info.device)
        return sorted(port_names)

    @staticmethod
    def _port_is_free(port_name):
        """Return True if the port is not already open in another program."""
        port = serial.Serial()
        port.port = port_name
        # Leaving the control lines low avoids resetting boards that
        # reset on DTR while the port is probed.
        port.dtr = False
        port.rts = False
        try:
            port.open()
        except (serial.SerialException, OSError):
            return False
        port.close()
        return True

    # ------------------------------------------------------------------
    # Settings
    # ------------------------------------------------------------------

    def set_defaults(self):
        """Program the default settings on the device.

        The defaults are a frequency of 100 Hz, and on every output
        channel a sine wave of 5 V peak to peak around a mean voltage of
        0 V, resting at 0 V, played for 1 second with no ramps. Both
        trigger channels are in normal mode, and all output channels are
        linked to trigger channel 1 and not to trigger channel 2. They
        match the settings the device starts with.
        """
        self.frequency = 100
        # In this order, each is valid whatever the device holds: a
        # resting voltage goes with any waveform, a mean of 0 V with any
        # amplitude, 5 V is then a valid amplitude for any waveform, and a
        # sine wave is then valid
        self.resting_voltage = 0
        self.mean_voltage = 0
        self.amplitude = 5
        self.waveform = "Sine"
        self.play_duration = 1
        self.on_ramp_duration = 0
        self.off_ramp_duration = 0
        self.trigger_mode = "Normal"
        self._set_trigger_links([True] * 4, [False] * 4)

    @property
    def frequency(self):
        """Frequency of all output channels, in Hz.

        1 to 20000 Hz, rounded to 0.01 Hz. It can be changed during
        playback: playing channels carry on from the same point in their
        cycle, at the new frequency, and keep the time they have left to
        play. The frequency played is exact: the device's sample clock is
        a whole multiple of it (see `SynthPalDevice.sampling_rate`).
        """
        return self._frequency

    @frequency.setter
    def frequency(self, value):
        if isinstance(value, bool) or not isinstance(value, numbers.Real) \
                or not math.isfinite(value):
            raise SynthPalError(
                f"frequency must be a number of Hz. Received {value!r}.")
        centihz = round(value * 100)
        low = round(self.info.min_frequency * 100)
        high = round(self.info.max_frequency * 100)
        if not low <= centihz <= high:
            raise SynthPalError(
                f"frequency must be {self.info.min_frequency:g} to "
                f"{self.info.max_frequency:g} Hz. Received {value!r}."
            )
        self._write_command(self._OP_SET_FREQUENCY,
                            struct.pack("<I", centihz))
        confirmed, samples_per_cycle = struct.unpack(
            "<BI", self._read_raw(5))
        if confirmed != 1:
            raise SynthPalError(
                f"Synth Pal rejected the frequency {value!r} Hz.")
        self._frequency = centihz / 100
        self._samples_per_cycle = samples_per_cycle

    @property
    def samples_per_cycle(self):
        """Samples in one cycle of the waveform, at `frequency`.

        The largest multiple of 4 whose sampling rate is at most
        `DeviceInfo.max_sampling_rate` (100 kHz), so that a sample falls
        on every edge, peak and trough of every waveform. For example,
        332 at 300 Hz and 4 at 20 kHz.
        """
        return self._samples_per_cycle

    @property
    def sampling_rate(self):
        """The rate at which the device plays samples, in Hz.

        `samples_per_cycle` times `frequency`: 100 kHz or just below it,
        and at least 50 kHz. For example, 100 kHz at 100 Hz, 99.6 kHz at
        300 Hz, and 80 kHz at 20 kHz. The device's timer cannot divide
        every rate exactly, so sample periods differ by up to one tick of
        its 24 MHz clock (42 ns), arranged so that the frequency is exact.
        """
        return self._samples_per_cycle * self._frequency

    @property
    def waveform(self):
        """The waveform of each output channel.

        Indexed by channel number (see "Channel settings" above). Each
        cycle starts at the trigger:

        - `"Sine"` and `"Triangle"`: start at the resting voltage, rising.
        - `"Square"`: high for the first half of each cycle, then low.
        - `"Sawtooth"`: rises from its lowest voltage to its highest, then
          falls back at the end of the cycle.
        - `"Fixed Voltage"`: steps to the channel's `amplitude`, which is
          then a voltage, -10 V to 10 V, for the play duration, and
          returns to the resting voltage. It is not periodic, so the
          frequency does not change it.

        Names are not case sensitive. A change applies to playback in
        progress. The channel's amplitude must suit the new waveform: a
        negative amplitude (a fixed voltage below 0 V) is no amplitude for
        a periodic waveform, and a fixed voltage must stay within -10 V to
        10 V. Set an amplitude that suits both waveforms first, or change
        it after the waveform when it suits the new one.
        """
        return self._waveform

    @waveform.setter
    def waveform(self, values):
        self._waveform._assign(values)

    @property
    def amplitude(self):
        """The amplitude of each output channel, in volts.

        Indexed by channel number (see "Channel settings" above). For the
        periodic waveforms, it is peak to peak, 0 to 20 V: the waveform
        swings `amplitude / 2` above and below the channel's
        `mean_voltage`, so it must stay within -10 V to 10 V:
        `abs(mean_voltage) + amplitude / 2 <= 10`. To raise the
        amplitude beyond what the mean voltage allows, change the mean
        voltage first.

        For a `"Fixed Voltage"` channel, it is the voltage the channel
        steps to, -10 V to 10 V, with any resting voltage. The mean
        voltage does not apply to it.

        Set to the nearest microvolt. A change applies to playback in
        progress.
        """
        return self._amplitude

    @amplitude.setter
    def amplitude(self, values):
        self._amplitude._assign(values)

    @property
    def resting_voltage(self):
        """The resting voltage of each output channel, in volts.

        Indexed by channel number (see "Channel settings" above). -10 to
        10 V, with any waveform. The channel outputs it while idle, and
        its ramps start and end there (see "Ramps" above). Set to the
        nearest microvolt. A change applies to playback in progress.
        """
        return self._resting_voltage

    @resting_voltage.setter
    def resting_voltage(self, values):
        self._resting_voltage._assign(values)

    @property
    def mean_voltage(self):
        """The mean voltage of each output channel's waveform, in volts.

        Indexed by channel number (see "Channel settings" above). -10 to
        10 V. A periodic waveform swings around it at full amplitude,
        and must stay within -10 V to 10 V with the `amplitude`:
        `abs(mean_voltage) + amplitude / 2 <= 10`. It may differ from the
        `resting_voltage`, which the channel outputs between playbacks:
        the ramps move the mean between the two (see "Ramps" above). A
        `"Fixed Voltage"` ignores it. Set to the nearest microvolt. A
        change applies to playback in progress.
        """
        return self._mean_voltage

    @mean_voltage.setter
    def mean_voltage(self, values):
        self._mean_voltage._assign(values)

    @property
    def play_duration(self):
        """How long each output channel plays after a trigger, in seconds.

        Indexed by channel number (see "Channel settings" above). Up to
        `DeviceInfo.max_play_duration` (3600 s). `0` plays until the
        channel is stopped. It counts at full amplitude, after the on
        ramp. The channel stops when the duration has elapsed, part way
        through a cycle if need be, and returns to its resting voltage,
        over its off ramp. Durations are counted in samples, rounded to
        the nearest one, and a nonzero duration lasts at least one sample.
        A change applies to playback in progress.
        """
        return self._play_duration

    @play_duration.setter
    def play_duration(self, values):
        self._play_duration._assign(values)

    @property
    def on_ramp_duration(self):
        """How long each output channel fades in after a trigger, in seconds.

        Indexed by channel number (see "Channel settings" above). Up to
        `DeviceInfo.max_play_duration` (3600 s); `0` (the default) for no
        ramp. Over it, the amplitude rises linearly from 0 V peak to peak
        to the `amplitude`, and the mean from the `resting_voltage` to the
        `mean_voltage`, before the play duration (see "Ramps" above).
        Counted in samples, as `play_duration` is. A change applies to
        playback in progress: a ramp under way keeps the level it has
        reached, and goes on at the new rate.
        """
        return self._on_ramp_duration

    @on_ramp_duration.setter
    def on_ramp_duration(self, values):
        self._on_ramp_duration._assign(values)

    @property
    def off_ramp_duration(self):
        """How long each output channel fades out when it stops, in seconds.

        Indexed by channel number (see "Channel settings" above). Up to
        `DeviceInfo.max_play_duration` (3600 s); `0` (the default) for no
        ramp. Over it, the amplitude falls linearly to 0 V peak to peak,
        and the mean to the `resting_voltage`, after the play duration or
        a stop (see "Ramps" above). Counted in samples, as
        `play_duration` is. A change applies to playback in progress.
        """
        return self._off_ramp_duration

    @off_ramp_duration.setter
    def off_ramp_duration(self, values):
        self._off_ramp_duration._assign(values)

    @property
    def trigger_mode(self):
        """How each trigger channel acts on the output channels linked to it.

        Indexed by trigger channel number, 1 or 2 (index 0 is unused).
        These are Pulse Pal's trigger modes:

        - `"Normal"`: a rising edge starts the linked channels. Channels
          that are playing ignore it.
        - `"Toggle"`: a rising edge starts the linked channels, or stops
          those that are playing.
        - `"Gated"`: a rising edge starts the linked channels, and a
          falling edge stops them, unless the other trigger channel is
          also gated, linked to them, and still high. With a play
          duration of 0, a channel plays for exactly as long as the TTL is
          high.

        Names are not case sensitive.
        """
        return self._trigger_mode

    @trigger_mode.setter
    def trigger_mode(self, values):
        self._trigger_mode._assign(values)

    @property
    def link_trigger_channel1(self):
        """Whether each output channel is triggered by trigger channel 1.

        Indexed by channel number (see "Channel settings" above).
        """
        return self._link_trigger_channel1

    @link_trigger_channel1.setter
    def link_trigger_channel1(self, values):
        self._link_trigger_channel1._assign(values)

    @property
    def link_trigger_channel2(self):
        """Whether each output channel is triggered by trigger channel 2.

        Indexed by channel number (see "Channel settings" above).
        """
        return self._link_trigger_channel2

    @link_trigger_channel2.setter
    def link_trigger_channel2(self, values):
        self._link_trigger_channel2._assign(values)

    def set_screen_saver(self, enabled, timeout=1800):
        """Switch the device's screen saver on or off, and set its timeout.

        With the screen saver on, the device dims its screen once it has
        been left alone for `timeout` seconds: no command from a computer,
        no rising edge on a trigger channel, and no joystick click or
        push. The next of these brings the screen back. The device keeps
        both settings in its EEPROM, shared with Pulse Pal firmware, and
        saves them once no channel is playing. A new device starts with
        the screen saver on and 1800 s.

        Args:
            enabled: `True` to switch the screen saver on, `False` to
                switch it off.
            timeout: Seconds without activity before the screen dims, 1 to
                65535.

        Raises:
            SynthPalError: If a value is out of range.
        """
        state = self._to_bool(enabled, "enabled")
        if isinstance(timeout, bool) \
                or not isinstance(timeout, numbers.Integral) \
                or not 1 <= timeout <= 65535:
            raise SynthPalError(
                "timeout must be a whole number of seconds from 1 to 65535. "
                f"Received {timeout!r}."
            )
        self._write_command(self._OP_SET_SCREEN_SAVER,
                            struct.pack("<BH", int(state), int(timeout)))
        self._read_ack("set_screen_saver()")

    # ------------------------------------------------------------------
    # Playback
    # ------------------------------------------------------------------

    def play(self, channels):
        """Trigger output channels in software.

        Channels that are idle start from the beginning of their waveform
        cycle, together. Channels that are playing ignore it, as they
        ignore a trigger channel in normal mode.

        ```python
        S.play(1)
        S.play([2, 4])
        ```

        Args:
            channels: Output channel number 1-4, or a list of them.
        """
        bits = self._channel_bits(channels)
        self._write_command(self._OP_PLAY, bytes([bits]))

    def stop(self, channels=None):
        """Stop playback. The stopped channels return to their resting
        voltage.

        Args:
            channels: Output channel number 1-4, or a list of them.
                `None` stops all channels.
        """
        if channels is None:
            bits = self._ALL_CHANNELS
        else:
            bits = self._channel_bits(channels)
        self._write_command(self._OP_STOP, bytes([bits]))

    def status(self):
        """Read the device's playback state.

        Returns:
            A `DeviceStatus`, with the channels that are playing, the
            samples per cycle, each channel's output range, and timing
            figures.
        """
        self._write_command(self._OP_GET_STATUS)
        values = struct.unpack(
            self._STATUS_FORMAT,
            self._read_raw(struct.calcsize(self._STATUS_FORMAT)),
        )
        playing_bits = values[0]
        range_names = list(OUTPUT_RANGES)
        return DeviceStatus(
            playing=[ch for ch in range(1, 5)
                     if playing_bits & (1 << (ch - 1))],
            samples_per_cycle=values[1],
            output_ranges=[None, *(range_names[i] if i < len(range_names)
                                   else None for i in values[2:6])],
            longest_interrupt_us=values[6] / 1000,
            late_updates=values[7],
        )

    def _playback_checksums(self):
        """For testing: what each channel played since it last started.

        Returns two lists indexed by channel number, with index 0 unused:
        the number of samples played, and the sum of their DAC codes
        modulo 2**32. `tests/synthpal_hardware_test.py` compares them with
        the samples it expects, to check what the device played.
        """
        self._write_command(self._OP_GET_PLAYBACK_CHECKSUMS)
        values = struct.unpack("<8I", self._read_raw(32))
        return [None, *values[:4]], [None, *values[4:]]

    # ------------------------------------------------------------------
    # Connection
    # ------------------------------------------------------------------

    def close(self, send_disconnect=True):
        """Close the connection to the device.

        The device shows its own name on its screen again, in place of
        "PYTHON Connected". It keeps its settings, and playback in
        progress continues. Safe to call more than once. Called
        automatically when leaving a `with` block and when the object is
        garbage collected.

        Args:
            send_disconnect: If `True`, tell the device that the client
                is disconnecting before closing the port. Set to `False`
                when the device may not be a Synth Pal.
        """
        if getattr(self, "_closed", True):
            return
        self._closed = True
        try:
            if send_disconnect and self.port and self.port.is_open:
                self._write_command(self._OP_DISCONNECT)
        except Exception:
            pass  # The port may already be gone, e.g. the cable was unplugged
        finally:
            if self.port and self.port.is_open:
                self.port.close()

    def bytes_available(self):
        """Return the number of bytes waiting in the serial read buffer."""
        return self.port.in_waiting

    def __enter__(self):
        """Enter a `with` block, returning the connected device."""
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        """Close the port when leaving a `with` block.

        Returns:
            `False`, so any exception raised in the block propagates.
        """
        self.close()
        return False

    def __del__(self):
        """Close the port when the object is collected."""
        try:
            self.close()
        except Exception:
            # Destructors should not raise; the serial object may already be
            # gone during interpreter shutdown.
            pass

    def __repr__(self):
        """Describe the device and its settings."""
        port = getattr(getattr(self, "port", None), "port", None)
        return (
            f"SynthPalDevice on {port} (Synth Pal firmware "
            f"v{self.info.firmware_version})\n"
            f"frequency: {self._frequency} Hz ({self._samples_per_cycle} "
            "samples per cycle)\n"
            f"waveform: {list(self._waveform)}\n"
            f"amplitude: {list(self._amplitude)}\n"
            f"resting_voltage: {list(self._resting_voltage)}\n"
            f"mean_voltage: {list(self._mean_voltage)}\n"
            f"play_duration: {list(self._play_duration)}\n"
            f"on_ramp_duration: {list(self._on_ramp_duration)}\n"
            f"off_ramp_duration: {list(self._off_ramp_duration)}\n"
            f"trigger_mode: {list(self._trigger_mode)}\n"
            f"link_trigger_channel1: {list(self._link_trigger_channel1)}\n"
            f"link_trigger_channel2: {list(self._link_trigger_channel2)}"
        )

    # ------------------------------------------------------------------
    # Internals
    # ------------------------------------------------------------------

    def _handshake(self):
        """Check that the device runs a supported Synth Pal firmware."""
        self._write_command(self._OP_HANDSHAKE)
        try:
            reply = self._read_raw(1)[0]
        except SynthPalError as exc:
            raise SynthPalError(
                f"No reply from the device on {self.port.port}. Is it a "
                "Pulse Pal 3 running Synth Pal firmware?"
            ) from exc
        other_firmware = {
            self._PULSE_PAL_HANDSHAKE_REPLY: ("Pulse Pal",
                                              "PulsePal.PulsePalDevice"),
            self._WAVE_PAL_HANDSHAKE_REPLY: ("Wave Pal",
                                             "WavePal.WavePalDevice"),
        }
        if reply in other_firmware:
            name, client = other_firmware[reply]
            version = struct.unpack("<I", self._read_raw(4))[0]
            raise SynthPalError(
                f"The device on {self.port.port} runs {name} firmware "
                f"(v{version}). Load Synth Pal firmware onto it "
                f"(/Firmware/SynthPal), or connect with {client}."
            )
        if reply != self._SYNTH_PAL_HANDSHAKE_REPLY:
            raise SynthPalError(
                "Incorrect handshake returned. Expected "
                f"{self._SYNTH_PAL_HANDSHAKE_REPLY}, received {reply}."
            )
        version = struct.unpack("<I", self._read_raw(4))[0]
        if version > self._CURRENT_FIRMWARE_VERSION:
            raise SynthPalError(
                f"Future firmware detected, v{version}. Please update "
                "SynthPal.py or load Synth Pal firmware "
                f"v{self._CURRENT_FIRMWARE_VERSION}."
            )
        self.info.firmware_version = version

    def _read_hardware_info(self):
        self._write_command(self._OP_HARDWARE_INFO)
        (
            self.info.hardware_version,
            self.info.n_channels,
            min_centihz,
            max_centihz,
            self.info.max_sampling_rate,
            self.info.timer_clock_hz,
            max_duration_us,
        ) = struct.unpack(
            self._HARDWARE_INFO_FORMAT,
            self._read_raw(struct.calcsize(self._HARDWARE_INFO_FORMAT)),
        )
        self.info.min_frequency = min_centihz / 100
        self.info.max_frequency = max_centihz / 100
        self.info.max_play_duration = max_duration_us / 1e6

    def _apply_waveform(self, values):
        names = []
        for value in values:
            matches = [name for name in WAVEFORMS
                       if isinstance(value, str)
                       and name.lower() == value.lower()]
            if not matches:
                raise SynthPalError(
                    f"Unknown waveform: {value!r}. Valid waveforms are "
                    f"{', '.join(WAVEFORMS)}."
                )
            names.append(matches[0])
        self._check_output_levels(names, self._amplitude_uv[1:],
                                  self._mean_uv[1:], "waveform")
        self._write_command(
            self._OP_SET_WAVEFORM,
            bytes(WAVEFORMS.index(name) for name in names),
        )
        self._read_ack("setting waveform")
        return names

    def _apply_amplitude(self, values):
        volts = []
        for channel, value in enumerate(values, start=1):
            if self._waveform[channel] == FIXED_VOLTAGE:
                volts.append(self._to_volts(
                    value, "amplitude", -10, 10,
                    " on a Fixed Voltage channel: the voltage it steps to"))
            else:
                volts.append(self._to_volts(
                    value, "amplitude", 0, 20,
                    f" peak to peak on a {self._waveform[channel]} channel "
                    f"(channel {channel}). Only a Fixed Voltage can be "
                    "negative"))
        microvolts = [round(v * 1e6) for v in volts]
        self._check_output_levels(self._waveform[1:], microvolts,
                                  self._mean_uv[1:], "amplitude")
        self._write_command(self._OP_SET_AMPLITUDE,
                            struct.pack("<4i", *microvolts))
        self._read_ack("setting amplitude")
        self._amplitude_uv[1:] = microvolts
        return volts

    def _apply_resting_voltage(self, values):
        # Any resting voltage within -10 V to 10 V goes with any waveform
        volts = [self._to_volts(value, "resting_voltage", -10, 10)
                 for value in values]
        microvolts = [round(v * 1e6) for v in volts]
        self._write_command(self._OP_SET_RESTING_VOLTAGE,
                            struct.pack("<4i", *microvolts))
        self._read_ack("setting resting_voltage")
        self._resting_uv[1:] = microvolts
        return volts

    def _apply_mean_voltage(self, values):
        volts = [self._to_volts(value, "mean_voltage", -10, 10)
                 for value in values]
        microvolts = [round(v * 1e6) for v in volts]
        self._check_output_levels(self._waveform[1:], self._amplitude_uv[1:],
                                  microvolts, "mean_voltage")
        self._write_command(self._OP_SET_MEAN_VOLTAGE,
                            struct.pack("<4i", *microvolts))
        self._read_ack("setting mean_voltage")
        self._mean_uv[1:] = microvolts
        return volts

    def _check_output_levels(self, waveforms, amplitudes_uv, means_uv,
                             setting):
        """Check that each channel's levels suit its waveform, and keep
        its output within -10 V to 10 V, as the device does. `setting` is
        the one being changed, for the advice in the message."""
        for channel, (waveform, amplitude, mean) in enumerate(
                zip(waveforms, amplitudes_uv, means_uv), start=1):
            if waveform == FIXED_VOLTAGE:
                if abs(amplitude) > self._MAX_VOLTAGE_UV:
                    raise SynthPalError(
                        f"On channel {channel}, a Fixed Voltage of "
                        f"{amplitude / 1e6:g} V is beyond -10 V to 10 V. "
                        "Its amplitude is the voltage it steps to."
                        + (" Change amplitude first." if setting ==
                           "waveform" else "")
                    )
            elif amplitude < 0:
                raise SynthPalError(
                    f"On channel {channel}, an amplitude of "
                    f"{amplitude / 1e6:g} V is negative, which only a Fixed "
                    f"Voltage can be: a {waveform} wave's amplitude is peak "
                    "to peak, 0 to 20 V."
                    + (" Change amplitude first." if setting ==
                       "waveform" else "")
                )
            elif 2 * abs(mean) + amplitude > 2 * self._MAX_VOLTAGE_UV:
                other = {"amplitude": "mean_voltage",
                         "mean_voltage": "amplitude",
                         "waveform": "amplitude or mean_voltage"}[setting]
                raise SynthPalError(
                    f"On channel {channel}, a mean voltage of "
                    f"{mean / 1e6:g} V and an amplitude of "
                    f"{amplitude / 1e6:g} V peak to peak would reach "
                    f"{(abs(mean) + amplitude / 2) / 1e6:g} V. The "
                    "waveform must stay within -10 V to 10 V. Change "
                    f"{other} first"
                    + (f", or choose a smaller {setting}."
                       if setting != "waveform" else ".")
                )

    def _apply_play_duration(self, values):
        return self._apply_duration(values, "play_duration",
                                    "0 (play until stopped)",
                                    self._OP_SET_PLAY_DURATION)

    def _apply_on_ramp_duration(self, values):
        return self._apply_duration(values, "on_ramp_duration",
                                    "0 (no ramp)",
                                    self._OP_SET_ON_RAMP_DURATION)

    def _apply_off_ramp_duration(self, values):
        return self._apply_duration(values, "off_ramp_duration",
                                    "0 (no ramp)",
                                    self._OP_SET_OFF_RAMP_DURATION)

    def _apply_duration(self, values, name, zero_meaning, op):
        durations = []
        for value in values:
            if isinstance(value, bool) or not isinstance(value, numbers.Real):
                raise SynthPalError(
                    f"{name} must be in seconds. Received {value!r}."
                )
            duration = float(value)
            if not math.isfinite(duration) or not \
                    0 <= duration <= self.info.max_play_duration:
                raise SynthPalError(
                    f"{name} must be {zero_meaning} or a positive number "
                    f"of seconds up to {self.info.max_play_duration:g}. "
                    f"Received {value!r}."
                )
            durations.append(duration)
        microseconds = [round(d * 1e6) for d in durations]
        self._write_command(op, struct.pack("<4I", *microseconds))
        self._read_ack(f"setting {name}")
        return durations

    def _apply_trigger_mode(self, values):
        names = []
        for value in values:
            matches = [mode for mode in TRIGGER_MODES
                       if isinstance(value, str)
                       and mode.lower() == value.lower()]
            if not matches:
                raise SynthPalError(
                    f"Unknown trigger mode: {value!r}. Valid modes are "
                    f"{', '.join(TRIGGER_MODES)}."
                )
            names.append(matches[0])
        self._write_command(
            self._OP_SET_TRIGGER_MODE,
            bytes(TRIGGER_MODES.index(name) for name in names),
        )
        self._read_ack("setting trigger_mode")
        return names

    # One op programs both trigger channels' links, so each list is sent
    # with the other's current values
    def _apply_trigger_channel1_links(self, values):
        links1 = [self._to_bool(v, "link_trigger_channel1") for v in values]
        self._send_trigger_links(links1, self._link_trigger_channel2[1:])
        return links1

    def _apply_trigger_channel2_links(self, values):
        links2 = [self._to_bool(v, "link_trigger_channel2") for v in values]
        self._send_trigger_links(self._link_trigger_channel1[1:], links2)
        return links2

    def _set_trigger_links(self, links1, links2):
        """Program both trigger channels' links with one command."""
        self._send_trigger_links(links1, links2)
        # Bypasses ChannelSettings.__setitem__, which would send them again
        list.__setitem__(self._link_trigger_channel1, slice(1, 5), links1)
        list.__setitem__(self._link_trigger_channel2, slice(1, 5), links2)

    def _send_trigger_links(self, links1, links2):
        self._write_command(self._OP_SET_TRIGGER_LINKS,
                            bytes([*map(int, links1), *map(int, links2)]))
        self._read_ack("setting the trigger channel links")

    @staticmethod
    def _to_volts(value, name, low, high, note=""):
        if isinstance(value, bool) or not isinstance(value, numbers.Real) \
                or not math.isfinite(value) or not low <= value <= high:
            raise SynthPalError(
                f"{name} values must be numbers of volts from {low} to "
                f"{high}{note}. Received {value!r}."
            )
        return float(value)

    @staticmethod
    def _to_bool(value, name):
        if isinstance(value, (bool, np.bool_)):
            return bool(value)
        if isinstance(value, numbers.Integral) and value in (0, 1):
            return bool(value)
        raise SynthPalError(f"{name} values must be True or False. "
                            f"Received {value!r}.")

    @staticmethod
    def _channel_number(channel):
        if (
            not isinstance(channel, numbers.Integral)
            or isinstance(channel, bool)
            or not 1 <= channel <= 4
        ):
            raise SynthPalError(
                f"Output channels are numbered 1-4. Received {channel!r}."
            )
        return int(channel)

    def _channel_bits(self, channels):
        """Convert a channel number, or a list of them, to channel bits."""
        if isinstance(channels, numbers.Integral):
            channels = [channels]
        channels = list(channels)
        if not channels:
            raise SynthPalError("No output channels were given.")
        bits = 0
        for channel in channels:
            bits |= 1 << (self._channel_number(channel) - 1)
        return bits

    def _write_command(self, op_code, data=b""):
        """Send one command, with its framing byte, in a single write."""
        message = bytes([self._OP_MENU_BYTE, op_code]) + data
        bytes_written = self.port.write(message)
        if bytes_written != len(message):
            raise SynthPalError(
                f"Wrote {bytes_written} byte(s), expected to write "
                f"{len(message)} byte(s)."
            )

    def _read_raw(self, n_bytes):
        """Read exactly n_bytes from the serial port."""
        message = self.port.read(n_bytes)
        if len(message) < n_bytes:
            raise SynthPalError(
                f"Serial port timed out. {len(message)} byte(s) read. "
                f"Expected {n_bytes} byte(s)."
            )
        return message

    def _read_ack(self, context):
        """Read a one-byte confirmation: 1 if the device executed the
        command, 0 if it rejected it."""
        try:
            reply = self._read_raw(1)[0]
        except SynthPalError as exc:
            raise SynthPalError(
                f"Synth Pal did not confirm {context}."
            ) from exc
        if reply != 1:
            raise SynthPalError(
                f"Synth Pal rejected {context}. A value was out of range."
            )
