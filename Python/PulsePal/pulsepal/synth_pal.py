"""
Python interface for Synth Pal, a waveform synthesizer for Pulse Pal 3.

Synth Pal is alternative firmware for Pulse Pal 3 hardware. Each output
channel plays a sine, triangle, square or sawtooth wave, or steps to a
fixed voltage, when it is triggered: by a TTL pulse on a trigger channel,
from software, or from the thumb joystick. Each channel has its own
waveform, peak to peak voltage, mean voltage, resting voltage, play
duration, and on and off ramps, and one frequency, 1 Hz to 20 kHz in steps
of 0.01 Hz, applies to all four.

Everything is accessed through `SynthPalDevice`. Import it, connect to
the device's serial port, set the waveforms, and trigger, e.g.

```python
from pulsepal import SynthPalDevice

with SynthPalDevice("COM3") as S:
    S.frequency = 440                  # Hz, all channels
    S.waveform[1] = "Sine"
    S.peak_to_peak[1] = 4              # volts: swings from -2 V to 2 V
    S.play_duration[1] = 0.5           # seconds
    S.trigger(1)
```

The device needs Synth Pal firmware, which is in
[/Firmware/SynthPal](https://github.com/sanworks/PulsePal/tree/develop/Firmware/SynthPal) in the
Pulse Pal repository. Its USB protocol is documented in
[PROTOCOL.md](https://github.com/sanworks/PulsePal/blob/develop/Firmware/SynthPal/PROTOCOL.md).

## Channel settings

Settings that apply to one output channel, such as
`SynthPalDevice.peak_to_peak`, are lists indexed by channel number: index
0 is unused and holds `None`, and indices 1 to 4 hold the settings of
output channels 1-4, as in `pulsepal.PulsePalDevice`. Setting an element
or a slice programs the device at once. Assigning a whole list sets all
four channels, and takes one value per channel: a single value raises an
error, because it does not say which channels it is meant for.

```python
S.peak_to_peak[2] = 5              # channel 2 only
S.play_duration[1:5] = [1, 2, 3, 4]
S.waveform = ["Square"] * 4        # all four channels
```

`SynthPalDevice.trigger_mode` is indexed the same way by trigger
channel number, 1 or 2.

To set several of one channel's settings at once, use
`SynthPalDevice.configure`:

```python
S.configure(2, waveform="Triangle", peak_to_peak=20, mean_voltage=0)
```

## Units and levels

Voltages are in volts, times in seconds, and frequencies in Hz. A
periodic waveform swings `peak_to_peak / 2` above and below its
`mean_voltage`, and must stay within -10 V to 10 V:
`abs(mean_voltage) + peak_to_peak / 2 <= 10`. A `"Fixed Voltage"`
channel steps to its `fixed_voltage`, -10 V to 10 V, for its play
duration. Between playbacks, a channel outputs its `resting_voltage`,
and its `on_ramp_duration` and `off_ramp_duration` fade it in from there
and back to it (see "Ramps" below).

## Ramps

A channel's on ramp follows each trigger, and fades its waveform in from
the resting voltage: its peak to peak voltage rises linearly from 0 to
its full value, and its mean from the resting voltage to the mean voltage
(a fixed voltage ramps from the resting voltage to its voltage). The play
duration follows at full amplitude, then the off ramp fades back to the
resting voltage. The off ramp also follows a stop: `stop()`, a toggle or
gated trigger, or the joystick. So the ramps lengthen playback: from a
trigger to rest takes `on_ramp_duration + play_duration +
off_ramp_duration`. During its off ramp, a channel counts as stopping: a
trigger fades it in again from where it is, without restarting its
waveform's cycle, and plays its play duration again. A channel stopped
during its on ramp fades out from where it is, at the off ramp's rate.
The output never jumps.

## Param sync

`"Param Sync"`, Pulse Pal's param sync trigger mode, lets the settings of
the next trial be sent during the current one, and applied the instant it
starts. Change settings in a `SynthPalDevice.batch` block: they are kept
here, and sent all at once when it ends. While a trigger channel is in
param sync mode, the device stores the whole set, and that channel's next
rising edge loads it.

```python
S.trigger_mode[2] = "Param Sync"   # sent at once
with S.batch():
    S.frequency = 880
    S.peak_to_peak[1] = 2
# Stored for trigger channel 2's next rising edge
```

See `SynthPalDevice.trigger_mode` for what the edge does.

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
import warnings

import numpy as np
import serial

from . import _common
from ._common import ChannelSettings, PulsePalError, to_bool, to_name

__all__ = ["SynthPalDevice", "DeviceInfo", "DeviceStatus", "WAVEFORMS", "TRIGGER_MODES"]
__docformat__ = "google"

WAVEFORMS = ("Sine", "Triangle", "Square", "Sawtooth", "Fixed Voltage")
"""Names of the waveforms, in order of their code on the device."""

FIXED_VOLTAGE = "Fixed Voltage"

TRIGGER_MODES = ("Normal", "Toggle", "Gated", "Param Sync")
"""Names of the trigger modes, in order of their code on the device. These
are Pulse Pal's trigger modes, with the same codes."""

# The output ranges the device chooses from, in order of their index on the
# device, with their limits in volts. See "Output ranges" in
# /Firmware/SynthPal/PROTOCOL.md.
OUTPUT_RANGES = {
    "0V:5V": (0.0, 5.0),
    "0V:10V": (0.0, 10.0),
    "-5V:5V": (-5.0, 5.0),
    "-10V:10V": (-10.0, 10.0),
}

_MAX_VOLTAGE_UV = 10_000_000  # Every output voltage stays within +/-10 V

# The settings sync_to_device() sends, in the class's order
_CHANNEL_SETTINGS = ("waveform", "peak_to_peak", "fixed_voltage", "mean_voltage", "resting_voltage",
                     "play_duration", "on_ramp_duration", "off_ramp_duration", "trigger_mode",
                     "link_trigger_channel1", "link_trigger_channel2")


def _samples_per_cycle(centihz):
    """Samples in one cycle at a frequency, as the device works it out (see SynthPalDevice.samples_per_cycle)."""
    return 4 * (2_500_000 // centihz)


class _Rejected(PulsePalError):
    """The device replied 0: it rejected a command, and changed nothing."""


def _is_valid_level(waveform, amplitude_uv, mean_uv):
    """Whether the device accepts an output channel's waveform, amplitude and mean voltage together, as
    isValidOutputLevel() in /Firmware/SynthPal/Settings.ino checks them. On the device, a channel has one
    amplitude: a fixed voltage's voltage, or a periodic waveform's peak to peak voltage."""
    if waveform == FIXED_VOLTAGE:
        return abs(amplitude_uv) <= _MAX_VOLTAGE_UV
    return 0 <= amplitude_uv and 2 * abs(mean_uv) + amplitude_uv <= 2 * _MAX_VOLTAGE_UV


def _microvolts(volts):
    return round(volts * 1e6)


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
    then `"0V:10V"` or `"-5V:5V"` (153 uV), then `"-10V:10V"` (305 uV).
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


class SynthPalDevice:
    """A class to control a Synth Pal on a USB serial port.

    Creating an instance opens the serial port, checks that the device
    runs Synth Pal firmware, reads its properties into
    `SynthPalDevice.info`, shows "PYTHON Connected" on the device's
    screen, stops any playback and programs the default settings (see
    `SynthPalDevice.set_default_params`).

    ```python
    from pulsepal import SynthPalDevice

    S = SynthPalDevice("COM3")
    S.waveform[1] = "Triangle"
    S.trigger(1)
    S.close()
    ```

    Replace "COM3" with the device's USB serial port name, which
    `SynthPalDevice.serialportlist` lists. `SynthPalDevice` is also a
    context manager, which closes the port on exit.

    Closing the connection stops playback, as it does on Pulse Pal (each
    channel over its off ramp), and puts the device's own name back on its
    screen. The device keeps its settings, so TTL triggers still play the
    channels.
    """

    port: "serial.Serial"
    """The open `serial.Serial` port connected to the device."""

    info: DeviceInfo
    """Properties of the connected device. See `DeviceInfo`."""

    _CURRENT_FIRMWARE_VERSION = 1
    _BAUD_RATE = 12000000  # USB serial ignores the baud rate

    _OP_MENU_BYTE = _common.OP_MENU_BYTE
    _OP_HANDSHAKE = _common.OP_HANDSHAKE
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
    _OP_SET_ALL_SETTINGS = ord("U")
    _OP_GET_ALL_SETTINGS = ord("R")
    # Every setting, as ops 85 ('U') and 82 ('R') carry them: frequency (centiHz), waveform codes, amplitudes,
    # mean and resting voltages (uV), play, on ramp and off ramp durations (us), links, trigger modes
    _SETTINGS_FORMAT = "<I4B4i4i4i4I4I4I8B2B"
    _OP_TRIGGER = ord("P")
    _OP_STOP = ord("X")
    _OP_GET_STATUS = ord("G")
    _OP_GET_PLAYBACK_CHECKSUMS = ord("Z")

    _HANDSHAKE_REPLY = 83  # 'S'
    _HARDWARE_INFO_FORMAT = "<BBIIIII"
    _STATUS_FORMAT = "<BI4BII"
    _ALL_CHANNELS = 0x0F

    def __init__(self, port_name, *, timeout=10):
        """Open a connection to a Synth Pal.

        Args:
            port_name: USB serial port of the device, such as `COM3` on
                Windows or `/dev/ttyACM0` on Linux.
            timeout: Serial read timeout, in seconds.

        Firmware newer than this module knows is used with a warning: new
        firmware only adds commands. Update the package to use what is new.

        Raises:
            PulsePalError: If the device does not reply to the handshake, or
                runs other firmware.
            serial.SerialException: If the serial port cannot be opened.
        """
        self._closed = True
        self._auto_sync = True
        self.info = DeviceInfo()
        self._frequency = None
        self._samples_per_cycle = None
        # What the device holds for each output channel's waveform, amplitude (in microvolts: a periodic
        # waveform's peak to peak voltage, or a fixed voltage) and mean voltage (in microvolts), as far as this
        # object has programmed them. _send_levels() changes them in an order the device accepts.
        self._device_waveform = ["Sine"] * 4
        self._device_amplitude_uv = [5_000_000] * 4
        self._device_mean_uv = [0] * 4
        self._waveform = ChannelSettings(
            "waveform", ["Sine"] * 4, self, "_apply_waveform")
        self._peak_to_peak = ChannelSettings(
            "peak_to_peak", [5.0] * 4, self, "_apply_peak_to_peak")
        self._fixed_voltage = ChannelSettings(
            "fixed_voltage", [5.0] * 4, self, "_apply_fixed_voltage")
        self._mean_voltage = ChannelSettings(
            "mean_voltage", [0.0] * 4, self, "_apply_mean_voltage")
        self._resting_voltage = ChannelSettings(
            "resting_voltage", [0.0] * 4, self, "_apply_resting_voltage")
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
            self._BAUD_RATE,
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
            self.set_default_params()
        except BaseException:
            # Op 81 means something else to other devices, so it is sent
            # only once the device has identified itself as a Synth Pal
            self._close(send_disconnect=self.info.firmware_version is not None)
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
            PulsePalError: If `ports_to_list` is not `available` or `all`.
        """
        return _common.serialportlist(ports_to_list)

    # ------------------------------------------------------------------
    # Settings
    # ------------------------------------------------------------------

    def set_default_params(self):
        """Program the default settings on the device.

        The defaults are a frequency of 100 Hz, and on every output
        channel a sine wave of 5 V peak to peak around a mean voltage of
        0 V, resting at 0 V, played for 1 second with no ramps, and a
        fixed voltage of 5 V for when the waveform is "Fixed Voltage".
        Both trigger channels are in normal mode, and all output channels
        are linked to trigger channel 1 and not to trigger channel 2. They
        match the settings the device starts with. They are sent at once,
        also while `SynthPalDevice.auto_sync` is off.
        """
        auto_sync = self._auto_sync
        self._auto_sync = True
        try:
            self.frequency = 100
            # In this order, each is valid whatever the device holds: a mean of 0 V goes with any
            # amplitude, 5 V is then a valid amplitude for any waveform, and a sine wave is then valid
            self._send_levels_in_order((("M", [0] * 4), ("A", [5_000_000] * 4), ("W", ["Sine"] * 4)),
                                       force=True)
            for settings, value in ((self._waveform, "Sine"), (self._peak_to_peak, 5.0),
                                    (self._fixed_voltage, 5.0), (self._mean_voltage, 0.0)):
                settings._store([value] * 4)
            self.resting_voltage = [0] * 4
            self.play_duration = [1] * 4
            self.on_ramp_duration = [0] * 4
            self.off_ramp_duration = [0] * 4
            self.trigger_mode = ["Normal"] * 2
            self._set_trigger_links([True] * 4, [False] * 4)
        finally:
            self._auto_sync = auto_sync

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
        if isinstance(value, (bool, np.bool_)) or not isinstance(value, numbers.Real) \
                or not math.isfinite(value):
            raise PulsePalError(
                f"frequency must be a number of Hz. Received {value!r}.")
        centihz = round(value * 100)
        low = round(self.info.min_frequency * 100)
        high = round(self.info.max_frequency * 100)
        if not low <= centihz <= high:
            raise PulsePalError(
                f"frequency must be {self.info.min_frequency:g} to "
                f"{self.info.max_frequency:g} Hz. Received {value!r}."
            )
        if self._auto_sync:
            self._write_command(self._OP_SET_FREQUENCY,
                                struct.pack("<I", centihz))
            confirmed, samples_per_cycle = struct.unpack(
                "<BI", self._read_raw(5))
            if confirmed != 1:
                raise PulsePalError(
                    f"Synth Pal rejected the frequency {value!r} Hz.")
        else:
            samples_per_cycle = _samples_per_cycle(centihz)
        self._frequency = centihz / 100
        self._samples_per_cycle = samples_per_cycle

    @property
    def auto_sync(self):
        """Whether assigning a setting programs the device at once.

        `True` (the default): each assignment, such as
        `S.peak_to_peak[1] = 2`, programs the device at once, as the
        device's own op for that setting does, also while a trigger
        channel is in param sync mode.

        `False`: assignments change only this object's copy of the settings,
        checking each value; `SynthPalDevice.sync_to_device` then sends all
        of them at once. Use it to store the next trial's settings for a
        param sync edge (see `SynthPalDevice.trigger_mode`), or to change
        several settings in one command. Turning it back on does not send
        changes made meanwhile: call `SynthPalDevice.sync_to_device` first.
        A `SynthPalDevice.batch` block does both for you. `trigger`,
        `stop`, `status` and `set_default_params` act at once either way.
        """
        return self._auto_sync

    @auto_sync.setter
    def auto_sync(self, value):
        self._auto_sync = to_bool(value, "auto_sync")

    def batch(self):
        """Change several settings, and send them in one command.

        Returns a context manager. In its block, assignments change only
        this object's copy of the settings (as with
        `SynthPalDevice.auto_sync` off), and when the block ends,
        `SynthPalDevice.sync_to_device` sends all of them at once. If the
        block raises an error, nothing is sent, and the settings go back
        to what they were.

        ```python
        with S.batch():
            S.frequency = 880
            S.waveform = ["Triangle"] * 4
            S.peak_to_peak = [2, 4, 6, 8]
        ```

        In param sync mode, the device stores the settings for the next
        rising edge on the param sync channel (see
        `SynthPalDevice.trigger_mode`).
        """
        return _common.batch(self)

    def sync_from_device(self):
        """Read every setting from the device into this object.

        Use it after settings were changed with the device's joystick, for
        example. The device holds one amplitude per channel: a Fixed
        Voltage channel's goes to `SynthPalDevice.fixed_voltage`, and any
        other channel's to `SynthPalDevice.peak_to_peak`. This object keeps
        the other one, reduced if need be to suit the mean voltage read
        back. In param sync mode, it reads the settings the device plays
        now, not a set waiting for the next edge.

        Raises:
            PulsePalError: If the device does not reply.
        """
        self._write_command(self._OP_GET_ALL_SETTINGS)
        values = struct.unpack(self._SETTINGS_FORMAT, self._read_raw(struct.calcsize(self._SETTINGS_FORMAT)))
        centihz = values[0]
        waveforms = [WAVEFORMS[code] for code in values[1:5]]
        amplitudes, means = list(values[5:9]), list(values[9:13])
        peak_to_peak, fixed_voltage = list(self._peak_to_peak)[1:], list(self._fixed_voltage)[1:]
        for i, (name, amplitude, mean) in enumerate(zip(waveforms, amplitudes, means)):
            if name == FIXED_VOLTAGE:
                fixed_voltage[i] = amplitude / 1e6
                # This object's own peak to peak voltage must still suit the mean voltage (see _check_levels())
                peak_to_peak[i] = min(peak_to_peak[i], (2 * _MAX_VOLTAGE_UV - 2 * abs(mean)) / 1e6)
            else:
                peak_to_peak[i] = amplitude / 1e6
        self._frequency = centihz / 100
        self._samples_per_cycle = _samples_per_cycle(centihz)
        self._waveform._store(waveforms)
        self._peak_to_peak._store(peak_to_peak)
        self._fixed_voltage._store(fixed_voltage)
        self._mean_voltage._store([mean / 1e6 for mean in means])
        self._resting_voltage._store([rest / 1e6 for rest in values[13:17]])
        self._play_duration._store([d / 1e6 for d in values[17:21]])
        self._on_ramp_duration._store([d / 1e6 for d in values[21:25]])
        self._off_ramp_duration._store([d / 1e6 for d in values[25:29]])
        self._link_trigger_channel1._store([bool(link) for link in values[29:33]])
        self._link_trigger_channel2._store([bool(link) for link in values[33:37]])
        self._trigger_mode._store([TRIGGER_MODES[code] for code in values[37:39]])
        self._device_waveform, self._device_amplitude_uv, self._device_mean_uv = waveforms, amplitudes, means

    def export_params(self):
        """Return every setting, as a dict of plain values and lists.

        Keyed by setting name: `"frequency"`, then the channel settings,
        each with one value per channel and no unused index 0. It holds
        only numbers, booleans and names, so it can be saved with `json`
        and logged with your data, to record exactly what the device
        played.

        ```python
        import json
        with open("trial_settings.json", "w") as f:
            json.dump(S.export_params(), f)
        ```
        """
        params = {"frequency": self._frequency}
        params.update({name: list(getattr(self, name))[1:] for name in _CHANNEL_SETTINGS})
        return params

    def sync_to_device(self):
        """Send every setting to the device in one command.

        While a trigger channel is in param sync mode, the device stores
        the set, replacing any set stored before, and that channel's next
        rising edge loads it (see `SynthPalDevice.trigger_mode`). Otherwise
        the device applies it at once. The settings sent are this object's:
        `frequency`, and for each channel `waveform`, `peak_to_peak` or
        `fixed_voltage`, `mean_voltage`, `resting_voltage`,
        `play_duration`, `on_ramp_duration`, `off_ramp_duration`,
        `link_trigger_channel1` and `link_trigger_channel2`, and
        `trigger_mode`. They read as sent, including while a set waits for
        its edge.

        Raises:
            PulsePalError: If the device rejects the set.
        """
        self._check_sync()
        self._send_sync()

    def configure(self, channels, *, waveform=None, peak_to_peak=None, fixed_voltage=None,
                  mean_voltage=None, resting_voltage=None, play_duration=None,
                  on_ramp_duration=None, off_ramp_duration=None):
        """Set several of the output channels' settings at once.

        Settings left out keep their values. The new settings are checked
        together, so their order does not matter: for example, a channel
        whose sine wave swings 2 V around a mean voltage of 9 V can go
        straight to 20 V peak to peak around 0 V. With
        `SynthPalDevice.auto_sync` on, the class programs the device in an
        order it accepts at every step.

        ```python
        S.configure(1, waveform="Sine", peak_to_peak=20, mean_voltage=0)
        S.configure([2, 3], waveform="Fixed Voltage", fixed_voltage=-2.5,
                    play_duration=0.1)
        ```

        Args:
            channels: Output channel number 1-4, or several as a list,
                tuple or NumPy array.
            waveform, peak_to_peak, fixed_voltage, mean_voltage,
                resting_voltage, play_duration, on_ramp_duration,
                off_ramp_duration: New values, as for the settings of the
                same names: one value for every channel given, or a list
                with one value per channel given, in the same order.

        Raises:
            PulsePalError: If a value is out of range, or the settings do
                not go together (nothing is sent), or the device rejects
                them.
        """
        channels = _common.channel_numbers(channels)
        if len(set(channels)) != len(channels):
            raise PulsePalError(f"configure(): channels must be distinct. Received {channels!r}.")
        requested = {"waveform": waveform, "peak_to_peak": peak_to_peak, "fixed_voltage": fixed_voltage,
                     "mean_voltage": mean_voltage, "resting_voltage": resting_voltage,
                     "play_duration": play_duration, "on_ramp_duration": on_ramp_duration,
                     "off_ramp_duration": off_ramp_duration}
        new_values = {}
        for name, value in requested.items():
            if value is None:
                continue
            values = list(value) if isinstance(value, (list, tuple, np.ndarray)) else [value]
            if len(values) == 1:
                values = values * len(channels)
            if len(values) != len(channels):
                raise PulsePalError(
                    f"configure(): {name} has {len(values)} values for {len(channels)} channels. Give one "
                    "value, or one value per channel."
                )
            settings = list(getattr(self, name))[1:]
            for channel, item in zip(channels, values):
                settings[channel - 1] = item
            new_values[name] = self._normalize(name, settings)
        levels = {name: new_values.get(name, list(getattr(self, name))[1:])
                  for name in ("waveform", "peak_to_peak", "fixed_voltage", "mean_voltage")}
        self._check_levels(levels["peak_to_peak"], levels["mean_voltage"], "configure")
        if self._auto_sync:
            self._send_levels(*self._device_levels(**levels))
        for name in levels:
            if name in new_values:
                getattr(self, name)._store(new_values[name])
        ops = {"resting_voltage": self._OP_SET_RESTING_VOLTAGE, "play_duration": self._OP_SET_PLAY_DURATION,
               "on_ramp_duration": self._OP_SET_ON_RAMP_DURATION,
               "off_ramp_duration": self._OP_SET_OFF_RAMP_DURATION}
        for name, op in ops.items():
            if name in new_values:
                if self._auto_sync:
                    self._send_setting(name, op, new_values[name])
                getattr(self, name)._store(new_values[name])

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

        - `"Sine"` and `"Triangle"`: start at the mean voltage, rising.
        - `"Square"`: high for the first half of each cycle, then low.
        - `"Sawtooth"`: rises from its lowest voltage to its highest, then
          falls back at the end of the cycle.
        - `"Fixed Voltage"`: steps to the channel's
          `SynthPalDevice.fixed_voltage` for the play duration, and
          returns to the resting voltage. It is not periodic, so the
          frequency does not change it.

        The periodic waveforms swing `SynthPalDevice.peak_to_peak` around
        `SynthPalDevice.mean_voltage`. Names are not case sensitive. A
        change applies to playback in progress.
        """
        return self._waveform

    @waveform.setter
    def waveform(self, values):
        self._waveform._assign(values)

    @property
    def peak_to_peak(self):
        """The peak to peak voltage of each output channel's waveform, in volts.

        Indexed by channel number (see "Channel settings" above). 0 to
        20 V: the waveform swings `peak_to_peak / 2` above and below the
        channel's `SynthPalDevice.mean_voltage`, so it must stay within
        -10 V to 10 V: `abs(mean_voltage) + peak_to_peak / 2 <= 10`. To
        raise it beyond what the mean voltage allows, change the mean
        voltage first, or set both with `SynthPalDevice.configure`.

        A sine wave of 4 V peak to peak swings from -2 V to 2 V around a
        mean voltage of 0 V: it is 2 sin(2 pi f t). A `"Fixed Voltage"`
        channel keeps its peak to peak voltage for when it plays a
        periodic waveform again.

        Set to the nearest microvolt. A change applies to playback in
        progress.
        """
        return self._peak_to_peak

    @peak_to_peak.setter
    def peak_to_peak(self, values):
        self._peak_to_peak._assign(values)

    @property
    def fixed_voltage(self):
        """The voltage each output channel steps to when its waveform is
        `"Fixed Voltage"`, in volts.

        Indexed by channel number (see "Channel settings" above). -10 to
        10 V, with any resting voltage and mean voltage. A channel playing
        another waveform keeps its fixed voltage for when its waveform is
        `"Fixed Voltage"` again.

        Set to the nearest microvolt. A change applies to playback in
        progress.
        """
        return self._fixed_voltage

    @fixed_voltage.setter
    def fixed_voltage(self, values):
        self._fixed_voltage._assign(values)

    @property
    def mean_voltage(self):
        """The mean voltage of each output channel's waveform, in volts.

        Indexed by channel number (see "Channel settings" above). -10 to
        10 V. A periodic waveform swings around it at full amplitude,
        and must stay within -10 V to 10 V with the `peak_to_peak`
        voltage: `abs(mean_voltage) + peak_to_peak / 2 <= 10`. It may
        differ from the `resting_voltage`, which the channel outputs
        between playbacks: the ramps move the mean between the two (see
        "Ramps" above). A `"Fixed Voltage"` ignores it. Set to the nearest
        microvolt. A change applies to playback in progress.
        """
        return self._mean_voltage

    @mean_voltage.setter
    def mean_voltage(self, values):
        self._mean_voltage._assign(values)

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
        ramp. Over it, the peak to peak voltage rises linearly from 0 V to
        `peak_to_peak`, and the mean from the `resting_voltage` to the
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
        ramp. Over it, the peak to peak voltage falls linearly to 0 V, and
        the mean to the `resting_voltage`, after the play duration or a
        stop (see "Ramps" above). Counted in samples, as `play_duration`
        is. A change applies to playback in progress.
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
        - `"Param Sync"`: a rising edge starts and stops nothing (the
          channel's links are ignored). It loads the settings most recently
          sent by `SynthPalDevice.sync_to_device` (or at the end of a
          `SynthPalDevice.batch` block), if any. This is how the next
          trial's settings are sent during the current trial and applied
          the instant it starts.

        At a param sync edge, the frequency and both trigger modes change
        at once: they are shared by all channels. An output channel at its
        resting voltage takes its new settings at once. One that is playing
        (off ramp included) finishes on the settings it started with, at
        the new frequency, and takes the new ones the moment it reaches its
        resting voltage, so the next trigger plays them. A trigger mode
        sent by `SynthPalDevice.sync_to_device` applies from the next
        edge.

        Only `SynthPalDevice.sync_to_device` is held back for the edge:
        with `SynthPalDevice.auto_sync` on, every assignment programs the
        device at once, also in param sync mode. So leaving param sync mode
        means assigning `trigger_mode` with `auto_sync` on. When no trigger
        channel is left in the mode, the device discards a stored set.

        To start channels on the same edge, wire the TTL to the other
        trigger channel too: the settings load first, so the channels it
        starts play the new settings. Connecting, and `set_default_params`,
        take both trigger channels out of param sync mode.

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
            PulsePalError: If a value is out of range.
        """
        state = to_bool(enabled, "enabled")
        if isinstance(timeout, (bool, np.bool_)) \
                or not isinstance(timeout, numbers.Integral) \
                or not 1 <= timeout <= 65535:
            raise PulsePalError(
                "timeout must be a whole number of seconds from 1 to 65535. "
                f"Received {timeout!r}."
            )
        self._write_command(self._OP_SET_SCREEN_SAVER,
                            struct.pack("<BH", int(state), int(timeout)))
        self._read_ack("set_screen_saver()")

    # ------------------------------------------------------------------
    # Playback
    # ------------------------------------------------------------------

    def trigger(self, channels):
        """Trigger output channels in software.

        Channels that are idle start from the beginning of their waveform
        cycle, together. Channels that are playing ignore it, as they
        ignore a trigger channel in normal mode.

        ```python
        S.trigger(1)
        S.trigger([2, 4])     # a list, tuple or NumPy array
        ```

        Args:
            channels: Output channel number 1-4, or several as a list,
                tuple or NumPy array.
        """
        self._write_command(self._OP_TRIGGER, bytes([_common.channel_bits(channels)]))

    def stop(self, channels=None):
        """Stop playback. The stopped channels return to their resting
        voltage, over their off ramps.

        Args:
            channels: Output channel number 1-4, or several as a list,
                tuple or NumPy array. `None` stops all channels.
        """
        bits = self._ALL_CHANNELS if channels is None else _common.channel_bits(channels)
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

    def close(self):
        """Close the connection to the device.

        The device stops playback on all channels, each over its off ramp,
        and shows its own name on its screen again, in place of "PYTHON
        Connected". It keeps its settings, so TTL triggers still play the
        channels. Safe to call more than once. Called automatically when
        leaving a `with` block and when the object is garbage collected,
        so playback also stops when the last reference to the object
        goes.

        """
        self._close(send_disconnect=True)

    def _close(self, send_disconnect):
        """Close the connection. send_disconnect: tell the device first (op 81). False when the device may not
        run this firmware: op 81 means something else to other devices."""
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
        """Describe the device and its settings, e.g. with `print(S)`."""
        port = getattr(getattr(self, "port", None), "port", None)
        lines = [
            f"SynthPalDevice on {port} (Synth Pal firmware v{self.info.firmware_version})",
            f"frequency: {self._frequency} Hz ({self._samples_per_cycle} samples per cycle)",
            f"auto_sync: {self._auto_sync}",
        ]
        lines += [f"{name}: {list(getattr(self, name))}" for name in _CHANNEL_SETTINGS]
        return "\n".join(lines)

    # ------------------------------------------------------------------
    # Internals: settings
    # ------------------------------------------------------------------

    def _handshake(self):
        """Check that the device runs a supported Synth Pal firmware."""
        self._write_command(self._OP_HANDSHAKE)
        handshake = _common.read_handshake_reply(self.port)  # Skips a reply to an earlier session's command
        if handshake is None:
            raise PulsePalError(
                f"No reply from the device on {self.port.port}. Is it a "
                "Pulse Pal 3 running Synth Pal firmware?"
            )
        reply, version = handshake
        if reply != self._HANDSHAKE_REPLY:
            if reply in _common.FIRMWARE_BY_HANDSHAKE_REPLY:
                raise _common.other_firmware_error(reply, version, self.port.port, "Synth Pal")
            raise PulsePalError(
                "Incorrect handshake returned. Expected "
                f"{self._HANDSHAKE_REPLY}, received {reply}."
            )
        if version > self._CURRENT_FIRMWARE_VERSION:
            # New firmware only adds commands (see PROTOCOL.md), so this class still works with it
            warnings.warn(
                f"Synth Pal firmware v{version} is newer than this pulsepal package knows "
                f"(v{self._CURRENT_FIRMWARE_VERSION}). It works with it, but update the package to use what is "
                "new.", stacklevel=3)
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

    def _normalize(self, name, values):
        """Check new values for all four channels of a setting, and return them as its list holds them."""
        if name == "waveform":
            return [to_name(value, WAVEFORMS, "waveform") for value in values]
        if name == "peak_to_peak":
            return [self._to_volts(value, name, 0, 20, " peak to peak") for value in values]
        if name in ("fixed_voltage", "mean_voltage", "resting_voltage"):
            return [self._to_volts(value, name, -10, 10) for value in values]
        zero_meaning = "0 (play until stopped)" if name == "play_duration" else "0 (no ramp)"
        return [self._to_seconds(value, name, zero_meaning) for value in values]

    def _check_levels(self, peak_to_peak, mean_voltage, setting):
        """Check that each channel's periodic waveform stays within -10 V to 10 V, as the device does.

        Checked on every channel, also one playing a fixed voltage, so that a change of waveform always
        suits the levels it finds. `setting` is the one being changed, for the advice in the message.
        """
        for channel, (pp, mean) in enumerate(zip(peak_to_peak, mean_voltage), start=1):
            pp_uv, mean_uv = _microvolts(pp), _microvolts(mean)
            if 2 * abs(mean_uv) + pp_uv > 2 * _MAX_VOLTAGE_UV:
                advice = {
                    "peak_to_peak": "Change mean_voltage first, set both with configure(), or choose a "
                                    "smaller peak_to_peak.",
                    "mean_voltage": "Change peak_to_peak first, set both with configure(), or choose a "
                                    "smaller mean_voltage.",
                }.get(setting, "Change peak_to_peak or mean_voltage.")
                raise PulsePalError(
                    f"On channel {channel}, a mean voltage of {mean:g} V and a peak to peak voltage of "
                    f"{pp:g} V would reach {(abs(mean_uv) + pp_uv / 2) / 1e6:g} V. The waveform must "
                    f"stay within -10 V to 10 V. {advice}"
                )

    def _device_levels(self, waveform, peak_to_peak, fixed_voltage, mean_voltage):
        """What the device holds for these settings: waveforms, amplitudes in microvolts (a fixed voltage, or a
        periodic waveform's peak to peak voltage) and mean voltages in microvolts."""
        amplitudes = [_microvolts(fixed if name == FIXED_VOLTAGE else pp)
                      for name, pp, fixed in zip(waveform, peak_to_peak, fixed_voltage)]
        return list(waveform), amplitudes, [_microvolts(mean) for mean in mean_voltage]

    def _apply_levels(self, name, values):
        """Apply new values of waveform, peak_to_peak, fixed_voltage or mean_voltage, for all four channels."""
        values = self._normalize(name, values)
        levels = {level: list(getattr(self, level))[1:]
                  for level in ("waveform", "peak_to_peak", "fixed_voltage", "mean_voltage")}
        levels[name] = values
        self._check_levels(levels["peak_to_peak"], levels["mean_voltage"], name)
        if self._auto_sync:
            self._send_levels(*self._device_levels(**levels))
        return values

    def _apply_waveform(self, values):
        return self._apply_levels("waveform", values)

    def _apply_peak_to_peak(self, values):
        return self._apply_levels("peak_to_peak", values)

    def _apply_fixed_voltage(self, values):
        return self._apply_levels("fixed_voltage", values)

    def _apply_mean_voltage(self, values):
        return self._apply_levels("mean_voltage", values)

    def _send_levels(self, waveforms, amplitudes_uv, means_uv):
        """Program the device's waveforms, amplitudes and mean voltages, in an order it accepts at every step.

        The device checks each op against what it holds for the other two (see _is_valid_level()), and one
        order does not suit every change: a sine wave of 20 V peak to peak cannot become a fixed voltage of
        -5 V by either op first. So each channel first takes an amplitude that suits both its current and
        its new waveform and mean voltage: its current amplitude if it can, else its new one, else 0 V, which
        suits any. Then the waveforms, the mean voltages and the new amplitudes follow. If the device
        refuses a step, which means this object's record of what it holds is out of date (e.g. a param sync
        edge loaded a stored set), the change is made from 0 V, which works from any state.
        """
        current = (self._device_waveform, self._device_amplitude_uv, self._device_mean_uv)
        if (waveforms, amplitudes_uv, means_uv) == tuple(map(list, current)):
            return
        first_amplitudes = []
        for w0, a0, m0, w1, a1, m1 in zip(*current, waveforms, amplitudes_uv, means_uv):
            for candidate in (a0, a1, 0):
                if all(_is_valid_level(w, candidate, m) for w, m in ((w0, m0), (w1, m0), (w1, m1))):
                    first_amplitudes.append(candidate)
                    break
        steps = (("A", first_amplitudes), ("W", waveforms), ("M", means_uv), ("A", amplitudes_uv))
        try:
            self._send_levels_in_order(steps)
        except _Rejected:
            self._send_levels_in_order((("A", [0] * 4), ("W", waveforms), ("M", means_uv),
                                        ("A", amplitudes_uv)), force=True)

    def _send_levels_in_order(self, steps, force=False):
        """Send ops 'A' (amplitudes, uV), 'W' (waveform names) and 'M' (mean voltages, uV), in order, and keep
        the record of what the device holds. Unless `force`, an op that would not change what the record says
        the device holds is skipped."""
        for op, values in steps:
            if op == "W":
                if values == self._device_waveform and not force:
                    continue
                self._write_command(self._OP_SET_WAVEFORM, bytes(WAVEFORMS.index(name) for name in values))
                self._read_ack("setting waveform")
                self._device_waveform = list(values)
            elif op == "A":
                if values == self._device_amplitude_uv and not force:
                    continue
                self._write_command(self._OP_SET_AMPLITUDE, struct.pack("<4i", *values))
                self._read_ack("setting the amplitude")
                self._device_amplitude_uv = list(values)
            else:
                if values == self._device_mean_uv and not force:
                    continue
                self._write_command(self._OP_SET_MEAN_VOLTAGE, struct.pack("<4i", *values))
                self._read_ack("setting mean_voltage")
                self._device_mean_uv = list(values)

    def _apply_resting_voltage(self, values):
        # Any resting voltage within -10 V to 10 V goes with any waveform
        values = self._normalize("resting_voltage", values)
        if self._auto_sync:
            self._send_setting("resting_voltage", self._OP_SET_RESTING_VOLTAGE, values)
        return values

    def _apply_play_duration(self, values):
        return self._apply_duration("play_duration", self._OP_SET_PLAY_DURATION, values)

    def _apply_on_ramp_duration(self, values):
        return self._apply_duration("on_ramp_duration", self._OP_SET_ON_RAMP_DURATION, values)

    def _apply_off_ramp_duration(self, values):
        return self._apply_duration("off_ramp_duration", self._OP_SET_OFF_RAMP_DURATION, values)

    def _apply_duration(self, name, op, values):
        values = self._normalize(name, values)
        if self._auto_sync:
            self._send_setting(name, op, values)
        return values

    def _send_setting(self, name, op, values):
        """Send the resting voltages (int32 microvolts) or a duration (uint32 microseconds), for all four
        channels."""
        if name == "resting_voltage":
            data = struct.pack("<4i", *(_microvolts(v) for v in values))
        else:
            data = struct.pack("<4I", *(round(d * 1e6) for d in values))
        self._write_command(op, data)
        self._read_ack(f"setting {name}")

    def _apply_trigger_mode(self, values):
        names = [to_name(value, TRIGGER_MODES, "trigger mode") for value in values]
        if self._auto_sync:
            self._write_command(
                self._OP_SET_TRIGGER_MODE,
                bytes(TRIGGER_MODES.index(name) for name in names),
            )
            self._read_ack("setting trigger_mode")
        return names

    # One op programs both trigger channels' links, so each list is sent
    # with the other's current values
    def _apply_trigger_channel1_links(self, values):
        links1 = [to_bool(v, "link_trigger_channel1") for v in values]
        self._send_trigger_links(links1, self._link_trigger_channel2[1:])
        return links1

    def _apply_trigger_channel2_links(self, values):
        links2 = [to_bool(v, "link_trigger_channel2") for v in values]
        self._send_trigger_links(self._link_trigger_channel1[1:], links2)
        return links2

    def _set_trigger_links(self, links1, links2):
        """Program both trigger channels' links with one command."""
        self._send_trigger_links(links1, links2)
        self._link_trigger_channel1._store(links1)
        self._link_trigger_channel2._store(links2)

    def _send_trigger_links(self, links1, links2):
        if self._auto_sync:
            self._write_command(self._OP_SET_TRIGGER_LINKS,
                                bytes([*map(int, links1), *map(int, links2)]))
            self._read_ack("setting the trigger channel links")

    def _snapshot(self):
        """All settings, for batch() to restore if its block fails."""
        settings = {name: list(getattr(self, name))[1:] for name in _CHANNEL_SETTINGS}
        return settings, self._frequency, self._samples_per_cycle

    def _restore(self, snapshot):
        settings, self._frequency, self._samples_per_cycle = snapshot
        for name, values in settings.items():
            getattr(self, name)._store(values)

    def _check_sync(self):
        """Check the whole set before sync_to_device() sends it."""
        self._check_levels(self._peak_to_peak[1:], self._mean_voltage[1:], "sync")

    def _send_sync(self):
        """Send every setting with op 85 ('U'). The device checks the whole set, so no order matters."""
        waveforms, amplitudes, means = self._device_levels(
            self._waveform[1:], self._peak_to_peak[1:], self._fixed_voltage[1:], self._mean_voltage[1:])
        links = [int(link) for link in (*self._link_trigger_channel1[1:],
                                        *self._link_trigger_channel2[1:])]
        payload = struct.pack(
            self._SETTINGS_FORMAT,
            round(self._frequency * 100),
            *(WAVEFORMS.index(name) for name in waveforms),
            *amplitudes, *means, *(_microvolts(v) for v in self._resting_voltage[1:]),
            *(round(d * 1e6) for d in self._play_duration[1:]),
            *(round(d * 1e6) for d in self._on_ramp_duration[1:]),
            *(round(d * 1e6) for d in self._off_ramp_duration[1:]),
            *links,
            *(TRIGGER_MODES.index(name) for name in self._trigger_mode[1:]),
        )
        self._write_command(self._OP_SET_ALL_SETTINGS, payload)
        self._read_ack("sync_to_device()")
        # Applied now, or, in param sync mode, at the next edge. Either way these are the levels the next
        # assignments build on; if a stored set has not loaded yet, _send_levels() finds out and recovers.
        self._device_waveform, self._device_amplitude_uv, self._device_mean_uv = waveforms, amplitudes, means

    @staticmethod
    def _to_volts(value, name, low, high, note=""):
        if isinstance(value, (bool, np.bool_)) or not isinstance(value, numbers.Real) \
                or not math.isfinite(value) or not low <= value <= high:
            raise PulsePalError(
                f"{name} values must be numbers of volts from {low} to "
                f"{high}{note}. Received {value!r}."
            )
        return float(value)

    def _to_seconds(self, value, name, zero_meaning):
        if isinstance(value, (bool, np.bool_)) or not isinstance(value, numbers.Real):
            raise PulsePalError(
                f"{name} must be in seconds. Received {value!r}."
            )
        duration = float(value)
        if not math.isfinite(duration) or not \
                0 <= duration <= self.info.max_play_duration:
            raise PulsePalError(
                f"{name} must be {zero_meaning} or a positive number "
                f"of seconds up to {self.info.max_play_duration:g}. "
                f"Received {value!r}."
            )
        return duration

    # ------------------------------------------------------------------
    # Internals: serial port
    # ------------------------------------------------------------------

    def _write_command(self, op_code, data=b""):
        """Send one command, with its framing byte, in a single write."""
        message = bytes([self._OP_MENU_BYTE, op_code]) + data
        bytes_written = self.port.write(message)
        if bytes_written != len(message):
            raise PulsePalError(
                f"Wrote {bytes_written} byte(s), expected to write "
                f"{len(message)} byte(s)."
            )

    def _read_raw(self, n_bytes):
        """Read exactly n_bytes from the serial port."""
        message = self.port.read(n_bytes)
        if len(message) < n_bytes:
            raise PulsePalError(
                f"Serial port timed out. {len(message)} byte(s) read. "
                f"Expected {n_bytes} byte(s)."
            )
        return message

    def _read_ack(self, context):
        """Read a one-byte confirmation: 1 if the device executed the
        command, 0 if it rejected it."""
        try:
            reply = self._read_raw(1)[0]
        except PulsePalError as exc:
            raise PulsePalError(
                f"Synth Pal did not confirm {context}."
            ) from exc
        if reply != 1:
            raise _Rejected(
                f"Synth Pal rejected {context}. A value was out of range."
            )
