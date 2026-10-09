"""
Python interface for Wave Pal, a waveform player for Pulse Pal 3.

Wave Pal is alternative firmware for Pulse Pal 3 hardware. It stores one
sampled waveform per output channel on the device's microSD card, and
plays it when the channel is triggered: by a TTL pulse on a trigger
channel, from software, or from the thumb joystick. Waveforms can be up
to a million samples long, and are played at up to 100 kHz.

Everything is accessed through `WavePalDevice`. Import it, connect to
the device's serial port, load waveforms, and trigger, e.g.

```python
import numpy as np
from pulsepal import WavePalDevice

with WavePalDevice("COM3") as W:
    W.sampling_rate = 50000                         # Hz, all channels
    t = np.arange(50000) / 50000                    # 1 second
    W.load_waveform(1, 5 * np.sin(2 * np.pi * 10 * t))
    W.trigger(1)
```

The device needs Wave Pal firmware, which is in
[/Firmware/WavePal](https://github.com/sanworks/PulsePal/tree/develop/Firmware/WavePal) in the
Pulse Pal repository. Its USB protocol is documented in
[PROTOCOL.md](https://github.com/sanworks/PulsePal/blob/develop/Firmware/WavePal/PROTOCOL.md).

## Channel settings

Settings that apply to one output channel, such as
`WavePalDevice.loop_mode`, are lists indexed by channel number: index 0
is unused and holds `None`, and indices 1 to 4 hold the settings of
output channels 1-4, as in `pulsepal.PulsePalDevice`. Setting an element
or a slice programs the device at once. Assigning a whole list sets all
four channels, and takes one value per channel: a single value raises an
error, because it does not say which channels it is meant for.

```python
W.loop_mode[2] = True          # channel 2 only
W.loop_duration[1:5] = [1, 2, 3, 4]
W.loop_mode = [False] * 4      # all four channels
```

`WavePalDevice.trigger_mode` is indexed the same way by trigger channel
number, 1 or 2, as in Pulse Pal.

## Units

Voltages are in volts, within `WavePalDevice.output_range`. Times are in
seconds, and the sampling rate is in Hz.

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

__all__ = ["WavePalDevice", "DeviceInfo", "DeviceStatus", "OUTPUT_RANGES", "TRIGGER_MODES"]
__docformat__ = "google"

OUTPUT_RANGES = {
    "0V:5V": (0.0, 5.0),
    "0V:10V": (0.0, 10.0),
    "-5V:5V": (-5.0, 5.0),
    "-10V:10V": (-10.0, 10.0),
}
"""Output ranges in order of their range index on the device, with their
limits in volts. See "Output ranges" in /Firmware/WavePal/PROTOCOL.md."""

TRIGGER_MODES = ("Normal", "Toggle", "Gated", "Master")
"""Names of the trigger modes. See `WavePalDevice.trigger_mode`."""

# Each trigger mode's code on the device. The first three are Pulse Pal's codes; 3 is Pulse Pal's
# param sync mode, which Wave Pal does not have.
_TRIGGER_MODE_CODES = {"Normal": 0, "Toggle": 1, "Gated": 2, "Master": 4}


@dataclass
class DeviceInfo:
    """Properties of the connected Wave Pal.

    Populated when `WavePalDevice` connects, and available as
    `WavePalDevice.info`.
    """

    firmware_version: int = None
    """Wave Pal firmware version running on the device."""

    hardware_version: int = None
    """Pulse Pal hardware version, e.g. `3`."""

    n_channels: int = None
    """Number of output channels."""

    max_samples: int = None
    """Maximum number of samples in one waveform."""

    max_sampling_rate: int = None
    """Highest sampling rate, in Hz."""

    buffer_samples: int = None
    """Samples per playback buffer.

    The first `buffer_samples` samples of each waveform are kept in the
    device's RAM, so that playback starts at once. A waveform no longer
    than this plays without reading the microSD card.
    """

    sample_clock_hz: int = None
    """Clock that the sample rate is divided from, in Hz.

    See `WavePalDevice.actual_sampling_rate`.
    """

    output_ranges: tuple = tuple(OUTPUT_RANGES)
    """Names of the output ranges, accepted by
    `WavePalDevice.output_range`."""

    trigger_modes: tuple = TRIGGER_MODES
    """Names of the trigger modes, accepted by
    `WavePalDevice.trigger_mode`."""


@dataclass
class DeviceStatus:
    """A snapshot of the device's playback state, from
    `WavePalDevice.status`."""

    playing: list
    """Numbers of the output channels playing a waveform, e.g. `[1, 3]`."""

    samples_loaded: list
    """Samples in each channel's waveform, `0` if it has none.

    Indexed by channel number, with index 0 unused. This is what the
    device holds, which can include waveforms loaded by an earlier
    connection.
    """

    underruns: list
    """Underruns on each channel since the device started, indexed by
    channel number.

    An underrun is a block of samples that was not read from the microSD
    card by the time it was due. The output then holds its last value
    until the block arrives. See "Storage and buffering" in the
    [Wave Pal protocol](https://github.com/sanworks/PulsePal/blob/develop/Firmware/WavePal/PROTOCOL.md#storage-and-buffering).
    """

    longest_interrupt_us: float
    """Longest run of the device's playback interrupt since the previous
    call to `WavePalDevice.status`, in microseconds.

    It must stay below the sample period, `1e6 / sampling_rate`.
    """


class WavePalDevice:
    """A class to control a Wave Pal on a USB serial port.

    Creating an instance opens the serial port, checks that the device
    runs Wave Pal firmware, reads its properties into
    `WavePalDevice.info`, shows "PYTHON Connected" on the device's
    screen, stops any playback and programs the default settings (see
    `WavePalDevice.set_default_params`).

    ```python
    from pulsepal import WavePalDevice

    W = WavePalDevice("COM3")
    W.load_waveform(1, [0, 1, 2, 3, 4, 5, 0])
    W.trigger(1)
    W.close()
    ```

    Replace "COM3" with the device's USB serial port name, which
    `WavePalDevice.serialportlist` lists. `WavePalDevice` is also a
    context manager, which closes the port on exit.

    Closing the connection stops playback, as it does on Pulse Pal, and
    puts the device's own name back on its screen. The device keeps its
    settings and waveforms, so TTL triggers still play them.
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
    _OP_SET_SAMPLING_RATE = ord("S")
    _OP_SET_OUTPUT_RANGE = ord("R")
    _OP_LOAD_WAVEFORM = ord("L")
    _OP_TRIGGER = ord("P")
    _OP_STOP = ord("X")
    _OP_SET_FIXED_VOLTAGE = ord("!")
    _OP_SET_LOOP_MODE = ord("O")
    _OP_SET_LOOP_DURATION = ord("D")
    _OP_SET_TRIGGER_MODE = ord("T")
    _OP_SET_TRIGGER_LINKS = ord("I")
    _OP_GET_STATUS = ord("G")
    _OP_GET_PLAYBACK_CHECKSUMS = ord("Z")

    _HANDSHAKE_REPLY = 87  # 'W'
    _HARDWARE_INFO_FORMAT = "<BBIIII"
    _STATUS_FORMAT = "<B4I4II"
    _DAC_BITMAX = 65535
    _UINT32_MAX = 2**32 - 1
    _ALL_CHANNELS = 0x0F

    def __init__(self, port_name, *, timeout=10):
        """Open a connection to a Wave Pal.

        Args:
            port_name: USB serial port of the device, such as `COM3` on
                Windows or `/dev/ttyACM0` on Linux.
            timeout: Serial read timeout, in seconds. Loading a long
                waveform onto a slow microSD card can take a few seconds.

        Firmware newer than this module knows is used with a warning: new
        firmware only adds commands. Update the package to use what is new.

        Raises:
            PulsePalError: If the device does not reply to the handshake, or
                runs other firmware.
            serial.SerialException: If the serial port cannot be opened.
        """
        self._closed = True
        self.info = DeviceInfo()
        self._sampling_rate = None
        self._output_range = None
        self._waveforms = [None] * 5
        self._loop_mode = ChannelSettings(
            "loop_mode", [False] * 4, self, "_apply_loop_mode")
        self._loop_duration = ChannelSettings(
            "loop_duration", [0.0] * 4, self, "_apply_loop_duration")
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
            # only once the device has identified itself as a Wave Pal
            self._close(send_disconnect=self.info.firmware_version is not None)
            raise

    @staticmethod
    def serialportlist(ports_to_list="available"):
        """Return the names of the USB serial ports on this computer.

        Called on the class, without connecting to a device, to find the
        port name to pass to `WavePalDevice`.

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

        The defaults are a 10 kHz sampling rate, the -10 V to 10 V output
        range, loop mode off with loop durations of 0, both trigger
        channels in normal mode, and all output channels linked to trigger
        channel 1 and not to trigger channel 2. They match the settings
        the device starts with.

        Loaded waveforms are kept, and loaded again if the output range
        changes (see `WavePalDevice.output_range`).

        Raises:
            PulsePalError: If a loaded waveform does not fit the default
                output range, -10 V to 10 V.
        """
        self.sampling_rate = 10000
        self.output_range = "-10V:10V"
        self.loop_mode = [False] * 4
        self.loop_duration = [0] * 4
        self.trigger_mode = ["Normal"] * 2
        self._set_trigger_links([True] * 4, [False] * 4)

    @property
    def sampling_rate(self):
        """Sampling rate of all output channels, in Hz.

        A whole number from 1 to `DeviceInfo.max_sampling_rate`. It can be
        changed during playback. The rate played can differ slightly from
        the rate set: see `WavePalDevice.actual_sampling_rate`.
        """
        return self._sampling_rate

    @sampling_rate.setter
    def sampling_rate(self, rate):
        try:
            rate_hz = int(rate)
            is_whole = rate_hz == rate and not isinstance(rate, (bool, str))
        except (TypeError, ValueError, OverflowError):
            is_whole = False
        if not is_whole or not 1 <= rate_hz <= self.info.max_sampling_rate:
            raise PulsePalError(
                "sampling_rate must be a whole number of Hz from 1 to "
                f"{self.info.max_sampling_rate}. Received {rate!r}."
            )
        # Loop durations are sent in samples, so check that they still fit
        # before anything is changed
        loop_samples = None
        if self._sampling_rate is not None:
            loop_samples = self._durations_to_samples(
                self._loop_duration[1:], rate_hz)
        self._write_command(self._OP_SET_SAMPLING_RATE,
                            struct.pack("<I", rate_hz))
        self._read_ack("setting sampling_rate")
        self._sampling_rate = rate_hz
        if loop_samples is not None:
            self._send_loop_duration_samples(loop_samples)

    @property
    def actual_sampling_rate(self):
        """The sampling rate the device plays, in Hz.

        The device divides `DeviceInfo.sample_clock_hz` (24 MHz) by a
        whole number, so the rate played is the nearest one of those to
        `WavePalDevice.sampling_rate`. For example, 44100 Hz plays at
        44117.6 Hz. Rates that divide 24 MHz exactly, such as 10 kHz,
        25 kHz or 100 kHz, play exactly.
        """
        return self._actual_rate(self._sampling_rate)

    @property
    def output_range(self):
        """Voltage range of all output channels.

        One of `DeviceInfo.output_ranges`: `"0V:5V"`, `"0V:10V"`,
        `"-5V:5V"` or `"-10V:10V"`. The smallest range that fits the
        waveforms gives the finest voltage steps.

        Changing the range stops playback, sets all outputs to 0 V, and
        loads the waveforms again, encoded for the new range. It raises
        an error, and changes nothing, if a loaded waveform does not fit
        the new range.
        """
        return self._output_range

    @output_range.setter
    def output_range(self, range_name):
        name = self._range_name(range_name)
        low, high = OUTPUT_RANGES[name]
        for channel in range(1, 5):
            waveform = self._waveforms[channel]
            if waveform is not None and (
                    waveform.min() < low or waveform.max() > high):
                raise PulsePalError(
                    f"The waveform on channel {channel} spans "
                    f"{waveform.min():g} V to {waveform.max():g} V, which "
                    f"does not fit the {name} range. Load a new waveform "
                    "first, or choose a wider range."
                )
        range_index = list(OUTPUT_RANGES).index(name)
        self._write_command(self._OP_SET_OUTPUT_RANGE, bytes([range_index]))
        self._read_ack("setting output_range")
        changed = name != self._output_range
        self._output_range = name
        if changed:
            # The device unloads the waveforms when the range changes,
            # because their samples encode voltages in the old range
            waveforms = self._waveforms
            self._waveforms = [None] * 5
            for channel in range(1, 5):
                if waveforms[channel] is not None:
                    self.load_waveform(channel, waveforms[channel])

    @property
    def loop_mode(self):
        """Whether each output channel loops its waveform.

        Indexed by channel number (see "Channel settings" above). `True`
        repeats the waveform until `WavePalDevice.loop_duration` has
        elapsed, or until stopped if the loop duration is 0. `False` plays
        it once. A change applies to playback in progress.
        """
        return self._loop_mode

    @loop_mode.setter
    def loop_mode(self, values):
        self._loop_mode._assign(values)

    @property
    def loop_duration(self):
        """How long each output channel loops its waveform, in seconds.

        Indexed by channel number (see "Channel settings" above). Applies
        in loop mode only, and counts from the trigger. The channel stops
        when it has elapsed, part way through the waveform if need be. `0`
        loops until the channel is stopped. Durations are rounded to a
        whole number of samples, and a nonzero duration lasts at least
        one sample.
        """
        return self._loop_duration

    @loop_duration.setter
    def loop_duration(self, values):
        self._loop_duration._assign(values)

    @property
    def trigger_mode(self):
        """How each trigger channel acts on the output channels linked to it.

        Indexed by trigger channel number, 1 or 2 (index 0 is unused), as
        in Pulse Pal. A TTL edge on a trigger channel acts on the output
        channels linked to it (`WavePalDevice.link_trigger_channel1`,
        `WavePalDevice.link_trigger_channel2`):

        - `"Normal"`: a rising edge starts the linked channels' waveforms.
          Channels that are playing ignore it.
        - `"Toggle"`: a rising edge starts the linked channels, or stops
          those that are playing.
        - `"Gated"`: a rising edge starts the linked channels, and a
          falling edge stops them, unless the other trigger channel is also
          gated, linked to them, and still high. With loop mode on and a
          loop duration of 0, a waveform plays for exactly as long as the
          TTL is high.
        - `"Master"`: a rising edge starts the linked channels, and
          restarts those that are playing from their first sample.

        The first three are Pulse Pal's trigger modes. Names are not case
        sensitive.
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
        both settings in its EEPROM, shared with Pulse Pal and Synth Pal
        firmware, and saves them once no channel is playing. A new device
        starts with the screen saver on and 1800 s.

        Args:
            enabled: `True` to switch the screen saver on, `False` to
                switch it off.
            timeout: Seconds without activity before the screen dims, 1 to
                65535. It is sent with every call, so leaving it out sets
                1800 s.

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

    def export_params(self):
        """Return every setting, as a dict of plain values and lists.

        Keyed by setting name: `"sampling_rate"` and `"output_range"`, then
        the channel settings, each with one value per channel and no
        unused index 0. It holds only numbers, booleans and names, so it
        can be saved with `json` and logged with your data. The waveforms
        are not part of it: `WavePalDevice.waveforms` holds them.

        ```python
        import json
        with open("session_settings.json", "w") as f:
            json.dump(W.export_params(), f)
        ```
        """
        params = {"sampling_rate": self._sampling_rate, "output_range": self._output_range}
        for name in ("loop_mode", "loop_duration", "trigger_mode", "link_trigger_channel1",
                     "link_trigger_channel2"):
            params[name] = list(getattr(self, name))[1:]
        return params

    @property
    def waveforms(self):
        """The waveforms loaded with `WavePalDevice.load_waveform`.

        A five element list indexed by channel number, with index 0
        unused. Each element is a read-only NumPy array of voltages, or
        `None` if this object has not loaded a waveform on that channel.
        `WavePalDevice.status` shows what the device itself holds.
        """
        return list(self._waveforms)

    # ------------------------------------------------------------------
    # Waveforms and playback
    # ------------------------------------------------------------------

    def load_waveform(self, channel, waveform):
        """Load a waveform onto an output channel.

        The samples are written to the device's microSD card, and the
        first `DeviceInfo.buffer_samples` of them are also kept in its RAM.
        Loading stops the channel if it is playing. Other channels keep
        playing.

        ```python
        t = np.arange(10000) / W.sampling_rate
        W.load_waveform(2, 3 * np.sin(2 * np.pi * 100 * t))
        ```

        Args:
            channel: Output channel number, 1-4.
            waveform: Voltages of the samples, played at
                `WavePalDevice.sampling_rate`. A list, tuple or NumPy
                array of 1 to `DeviceInfo.max_samples` values, all within
                `WavePalDevice.output_range`.

        Raises:
            PulsePalError: If the channel or waveform is invalid, or the
                device could not store the waveform. The channel is then
                left without a waveform.
        """
        channel = self._channel_number(channel)
        samples = np.array(waveform, dtype=float).ravel()
        if not 1 <= samples.size <= self.info.max_samples:
            raise PulsePalError(
                f"A waveform must have 1 to {self.info.max_samples} samples. "
                f"Received {samples.size}."
            )
        codes = self._volts_to_codes(samples)
        self._waveforms[channel] = None  # The device unloads it first
        self._write_command(
            self._OP_LOAD_WAVEFORM,
            struct.pack("<BI", channel, samples.size) + codes.tobytes(),
        )
        self._read_ack("load_waveform()")
        samples.flags.writeable = False
        self._waveforms[channel] = samples

    def trigger(self, channels):
        """Trigger output channels in software.

        Idle channels start their waveforms from the first sample,
        together. Channels that are playing ignore it, as Pulse Pal's
        channels ignore a soft trigger while they play a pulse train.
        Channels without a waveform are ignored.

        ```python
        W.trigger(1)
        W.trigger([2, 4])     # a list, tuple or NumPy array
        ```

        Args:
            channels: Output channel number 1-4, or several as a list,
                tuple or NumPy array.
        """
        self._write_command(self._OP_TRIGGER, bytes([_common.channel_bits(channels)]))

    def stop(self, channels=None):
        """Stop playback. The stopped channels output 0 V.

        Args:
            channels: Output channel number 1-4, or several as a list,
                tuple or NumPy array. `None` stops all channels.
        """
        bits = self._ALL_CHANNELS if channels is None else _common.channel_bits(channels)
        self._write_command(self._OP_STOP, bytes([bits]))

    def set_fixed_voltage(self, channels, voltage):
        """Set output channels to a fixed voltage.

        Stops playback on those channels. The voltage holds until the
        channel is triggered or stopped.

        Args:
            channels: Output channel number 1-4, or several as a list,
                tuple or NumPy array.
            voltage: Voltage, within `WavePalDevice.output_range`.

        Raises:
            PulsePalError: If the voltage is outside the output range, or
                the device rejects the command.
        """
        bits = _common.channel_bits(channels)
        code = int(self._volts_to_codes([voltage])[0])
        self._write_command(self._OP_SET_FIXED_VOLTAGE,
                            struct.pack("<BH", bits, code))
        self._read_ack("set_fixed_voltage()")

    def status(self):
        """Read the device's playback state.

        Returns:
            A `DeviceStatus`, with the channels that are playing, the
            number of samples each channel holds, underrun counts, and
            the longest playback interrupt.
        """
        self._write_command(self._OP_GET_STATUS)
        values = struct.unpack(
            self._STATUS_FORMAT,
            self._read_raw(struct.calcsize(self._STATUS_FORMAT)),
        )
        playing_bits = values[0]
        return DeviceStatus(
            playing=[ch for ch in range(1, 5)
                     if playing_bits & (1 << (ch - 1))],
            samples_loaded=[None, *values[1:5]],
            underruns=[None, *values[5:9]],
            longest_interrupt_us=values[9] / 1000,
        )

    def _playback_checksums(self):
        """For testing: what each channel played since it last started.

        Returns two lists indexed by channel number, with index 0 unused:
        the number of samples played, and the sum of their DAC codes
        modulo 2**32. `tests/wavepal_hardware_test.py` compares them with
        the waveforms it loaded, to check every sample the device played.
        """
        self._write_command(self._OP_GET_PLAYBACK_CHECKSUMS)
        values = struct.unpack("<8I", self._read_raw(32))
        return [None, *values[:4]], [None, *values[4:]]

    # ------------------------------------------------------------------
    # Connection
    # ------------------------------------------------------------------

    def close(self):
        """Close the connection to the device.

        The device stops playback on all channels, and shows its own name
        on its screen again, in place of "PYTHON Connected". It keeps its
        settings and waveforms, so TTL triggers still play them. Safe to
        call more than once. Called automatically when leaving a `with`
        block and when the object is garbage collected, so playback also
        stops when the last reference to the object goes.

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
        """Describe the device and its settings, e.g. with `print(W)`."""
        port = getattr(getattr(self, "port", None), "port", None)
        loaded = ", ".join(
            f"{ch}: {'none' if w is None else f'{w.size} samples'}"
            for ch, w in enumerate(self._waveforms) if ch > 0
        )
        return (
            f"WavePalDevice on {port} (Wave Pal firmware "
            f"v{self.info.firmware_version})\n"
            f"sampling_rate: {self._sampling_rate} Hz\n"
            f"output_range: {self._output_range}\n"
            f"loop_mode: {list(self._loop_mode)}\n"
            f"loop_duration: {list(self._loop_duration)}\n"
            f"trigger_mode: {list(self._trigger_mode)}\n"
            f"link_trigger_channel1: {list(self._link_trigger_channel1)}\n"
            f"link_trigger_channel2: {list(self._link_trigger_channel2)}\n"
            f"waveforms: {loaded}"
        )

    # ------------------------------------------------------------------
    # Internals
    # ------------------------------------------------------------------

    def _handshake(self):
        """Check that the device runs a supported Wave Pal firmware."""
        self._write_command(self._OP_HANDSHAKE)
        handshake = _common.read_handshake_reply(self.port)  # Skips a reply to an earlier session's command
        if handshake is None:
            raise PulsePalError(
                f"No reply from the device on {self.port.port}. Is it a "
                "Pulse Pal 3 running Wave Pal firmware?"
            )
        reply, version = handshake
        if reply != self._HANDSHAKE_REPLY:
            if reply in _common.FIRMWARE_BY_HANDSHAKE_REPLY:
                raise _common.other_firmware_error(reply, version, self.port.port, "Wave Pal")
            raise PulsePalError(
                "Incorrect handshake returned. Expected "
                f"{self._HANDSHAKE_REPLY}, received {reply}."
            )
        if version > self._CURRENT_FIRMWARE_VERSION:
            # New firmware only adds commands (see PROTOCOL.md), so this class still works with it
            warnings.warn(
                f"Wave Pal firmware v{version} is newer than this pulsepal package knows "
                f"(v{self._CURRENT_FIRMWARE_VERSION}). It works with it, but update the package to use what is "
                "new.", stacklevel=3)
        self.info.firmware_version = version

    def _read_hardware_info(self):
        self._write_command(self._OP_HARDWARE_INFO)
        (
            self.info.hardware_version,
            self.info.n_channels,
            self.info.max_samples,
            self.info.max_sampling_rate,
            self.info.buffer_samples,
            self.info.sample_clock_hz,
        ) = struct.unpack(
            self._HARDWARE_INFO_FORMAT,
            self._read_raw(struct.calcsize(self._HARDWARE_INFO_FORMAT)),
        )

    def _apply_loop_mode(self, values):
        modes = [to_bool(value, "loop_mode") for value in values]
        self._write_command(self._OP_SET_LOOP_MODE, bytes(map(int, modes)))
        self._read_ack("setting loop_mode")
        return modes

    def _apply_loop_duration(self, values):
        durations = []
        for value in values:
            if isinstance(value, (bool, np.bool_)) or not isinstance(value, numbers.Real):
                raise PulsePalError(
                    f"loop_duration must be in seconds. Received {value!r}."
                )
            duration = float(value)
            if not math.isfinite(duration) or duration < 0:
                raise PulsePalError(
                    "loop_duration must be 0 (loop until stopped) or a "
                    f"positive number of seconds. Received {value!r}."
                )
            durations.append(duration)
        samples = self._durations_to_samples(durations, self._sampling_rate)
        self._send_loop_duration_samples(samples)
        return durations

    def _send_loop_duration_samples(self, samples):
        self._write_command(self._OP_SET_LOOP_DURATION,
                            struct.pack("<4I", *samples))
        self._read_ack("setting loop_duration")

    def _durations_to_samples(self, durations, rate_hz):
        """Convert loop durations in seconds to samples at a sampling rate."""
        actual_rate = self._actual_rate(rate_hz)
        samples = []
        for duration in durations:
            n = round(duration * actual_rate)
            if duration > 0:
                n = max(n, 1)  # 0 samples would mean "loop until stopped"
            if n > self._UINT32_MAX:
                raise PulsePalError(
                    f"A loop_duration of {duration} s is too long at "
                    f"{rate_hz} Hz. The longest is "
                    f"{self._UINT32_MAX / actual_rate:.0f} s."
                )
            samples.append(n)
        return samples

    def _apply_trigger_mode(self, values):
        names = [to_name(value, TRIGGER_MODES, "trigger mode") for value in values]
        self._write_command(
            self._OP_SET_TRIGGER_MODE,
            bytes(_TRIGGER_MODE_CODES[name] for name in names),
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
        self._write_command(self._OP_SET_TRIGGER_LINKS,
                            bytes([*map(int, links1), *map(int, links2)]))
        self._read_ack("setting the trigger channel links")

    def _actual_rate(self, rate_hz):
        clock = self.info.sample_clock_hz
        return clock / round(clock / rate_hz)

    def _range_name(self, range_name):
        """Return the canonical name of an output range."""
        if isinstance(range_name, str):
            for name in OUTPUT_RANGES:
                if name.lower() == range_name.replace(" ", "").lower():
                    return name
        raise PulsePalError(
            f"Unknown output range: {range_name!r}. Valid ranges are "
            f"{', '.join(OUTPUT_RANGES)}."
        )

    def _volts_to_codes(self, volts):
        """Convert voltages to DAC codes in the current output range."""
        low, high = OUTPUT_RANGES[self._output_range]
        volts = np.asarray(volts, dtype=float)
        if not np.all(np.isfinite(volts)):
            raise PulsePalError("Voltages must be finite numbers.")
        if volts.min() < low or volts.max() > high:
            raise PulsePalError(
                f"Voltages must be within the output range, {low:g} V to "
                f"{high:g} V. Received {volts.min():g} V to "
                f"{volts.max():g} V. Change output_range to use a wider "
                "range."
            )
        codes = np.round((volts - low) / (high - low) * self._DAC_BITMAX)
        return codes.astype("<u2")

    @staticmethod
    def _channel_number(channel):
        if (
            not isinstance(channel, numbers.Integral)
            or isinstance(channel, (bool, np.bool_))
            or not 1 <= channel <= 4
        ):
            raise PulsePalError(
                f"Output channels are numbered 1-4. Received {channel!r}."
            )
        return int(channel)

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
                f"Wave Pal did not confirm {context}."
            ) from exc
        if reply != 1:
            raise PulsePalError(
                f"Wave Pal rejected {context}. A value was out of range, "
                "or a waveform could not be written to the microSD card."
            )
