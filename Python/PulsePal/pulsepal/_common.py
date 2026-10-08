"""
Code shared by the Pulse Pal, Wave Pal and Synth Pal classes.

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

import contextlib
import numbers
import weakref

import numpy as np
import serial
import serial.tools.list_ports

OP_MENU_BYTE = 213  # The first byte of every command, for all three firmwares
OP_HANDSHAKE = 72

# The firmwares that run on Pulse Pal hardware, by their reply to the handshake
# (op 72): (name, client class). Each client names the firmware it finds when
# it is not its own.
FIRMWARE_BY_HANDSHAKE_REPLY = {
    75: ("Pulse Pal", "pulsepal.PulsePalDevice"),   # 'K'
    87: ("Wave Pal", "pulsepal.WavePalDevice"),     # 'W'
    83: ("Synth Pal", "pulsepal.SynthPalDevice"),   # 'S'
}


class PulsePalError(Exception):
    """Raised when communication with the device, or a setting, fails.

    All three device classes (`pulsepal.PulsePalDevice`,
    `pulsepal.WavePalDevice` and `pulsepal.SynthPalDevice`) raise it, for
    serial reads that time out, short serial writes, commands the device
    rejects, and values it cannot play, which are refused before anything
    is sent.
    """


def other_firmware_error(handshake_reply, version, port_name, expected):
    """The error for a device that replied to the handshake as another
    firmware does, or None if the reply is not one of the three."""
    if handshake_reply not in FIRMWARE_BY_HANDSHAKE_REPLY:
        return None
    name, client = FIRMWARE_BY_HANDSHAKE_REPLY[handshake_reply]
    expected_name = {"Pulse Pal": "PulsePal3", "Wave Pal": "WavePal",
                     "Synth Pal": "SynthPal"}[expected]
    return PulsePalError(
        f"The device on {port_name} runs {name} firmware (v{version}), not "
        f"{expected} firmware. Load {expected} firmware onto it "
        f"(/Firmware/{expected_name}), or connect with {client}."
    )


def serialportlist(ports_to_list="available"):
    """Return the names of the USB serial ports on this computer.

    See `pulsepal.PulsePalDevice.serialportlist`.
    """
    mode = ports_to_list.lower() if isinstance(ports_to_list, str) else None
    if mode not in ("available", "all"):
        raise PulsePalError(
            f"Unknown port list type: {ports_to_list!r}. Use 'available' or "
            "'all'."
        )
    port_names = []
    for port_info in serial.tools.list_ports.comports():
        is_usb = port_info.vid is not None or "USB" in (
            port_info.hwid or ""
        ).upper()
        if not is_usb:
            continue
        if mode == "available" and not _port_is_free(port_info.device):
            continue
        port_names.append(port_info.device)
    return sorted(port_names)


def _port_is_free(port_name):
    """Return True if the port is not already open in another program."""
    port = serial.Serial()
    port.port = port_name
    # Leaving the control lines low avoids resetting boards that reset on DTR
    # while the port is probed.
    port.dtr = False
    port.rts = False
    try:
        port.open()
    except (serial.SerialException, OSError):
        return False
    port.close()
    return True


def is_iterable(value):
    try:
        iter(value)
    except TypeError:
        return False
    return True


def to_bool(value, name):
    """Return True or False for a bool, a NumPy bool, or 1 or 0."""
    if isinstance(value, (bool, np.bool_)):
        return bool(value)
    if isinstance(value, numbers.Integral) and value in (0, 1):
        return bool(value)
    raise PulsePalError(f"{name} values must be True or False. Received {value!r}.")


def to_name(value, names, setting, codes=None):
    """Return the canonical spelling of a name, matched without regard to case.

    `names` lists the valid names. If `codes` is given (a dict of name ->
    code), the matching integer codes are accepted too, as Pulse Pal's
    clients used them before names.
    """
    if isinstance(value, str):
        for name in names:
            if name.lower() == value.lower():
                return name
    elif codes is not None and isinstance(value, numbers.Integral) and not isinstance(value, (bool, np.bool_)):
        for name in names:
            if codes[name] == value:
                return name
    raise PulsePalError(
        f"Unknown {setting}: {value!r}. Valid values are "
        f"{', '.join(repr(name) for name in names)}."
    )


def channel_numbers(channels, n_channels=4, kind="Output"):
    """Return channel numbers as a list of ints.

    `channels` is one channel number, or several as a list, tuple or NumPy
    array. NumPy integers are accepted; bools and empty lists are not.
    """
    if isinstance(channels, np.ndarray):
        values = channels.ravel().tolist()
    elif isinstance(channels, (list, tuple)):
        values = list(channels)
    else:
        values = [channels]
    if not values or not all(
        isinstance(ch, numbers.Integral) and not isinstance(ch, (bool, np.bool_)) and 1 <= ch <= n_channels
        for ch in values
    ):
        example = "1 or [2, 4]" if n_channels == 4 else "1 or [1, 2]"
        raise PulsePalError(
            f"{kind} channels are numbered 1-{n_channels}: give one, or several as a list, tuple or NumPy array, "
            f"e.g. {example}. Received {channels!r}."
        )
    return [int(ch) for ch in values]


def channel_bits(channels):
    """One bit per output channel, channel 1 in bit 0, as ops that take several channels expect."""
    bits = 0
    for channel in channel_numbers(channels):
        bits |= 1 << (channel - 1)
    return bits


class ChannelSettings(list):
    """One setting per channel, indexed by channel number.

    Index 0 is unused and holds `None`, so `settings[2]` belongs to
    channel 2. Assigning to an element or a slice sets the device's
    setting (or, with `auto_sync` off, this object's copy of it); if the
    device refuses the new values, the list is left unchanged. The list
    always holds one element per channel and index 0, so methods that
    would change its length raise `TypeError`. `list(settings)` or
    `copy.copy` gives a plain list, detached from the device.
    """

    def __init__(self, name, values, device, apply_method):
        super().__init__([None, *values])
        self._name = name
        self._n = len(values)
        # A weak reference, so that the device and its settings do not form a
        # reference cycle: deleting the device then closes its port at once
        self._device = weakref.ref(device)
        self._apply_method = apply_method

    def __setitem__(self, index, value):
        values = list(self)
        values[index] = value
        if len(values) != self._n + 1:
            raise PulsePalError(
                f"{self._name} holds one value per channel, at indices "
                f"1-{self._n}. A slice assignment must keep its length."
            )
        if values[0] is not None:
            raise PulsePalError(
                f"{self._name}[0] is unused: channels are numbered 1-{self._n}."
            )
        self._set_all(values[1:])

    def _assign(self, values):
        """Set all channels from a single value, one value per channel, or a
        list with index 0 unused."""
        if isinstance(values, (str, bytes)) or not is_iterable(values):
            values = [values] * self._n
        else:
            values = list(values)
            if len(values) == self._n + 1 and values[0] is None:
                values = values[1:]
            elif len(values) != self._n:
                raise PulsePalError(
                    f"{self._name} needs one value for all channels, or one "
                    f"value per channel 1-{self._n}. Received {len(values)} "
                    "values."
                )
        self._set_all(values)

    def _set_all(self, values):
        device = self._device()
        if device is None:
            raise PulsePalError(f"The device that owns {self._name} is gone.")
        normalized = getattr(device, self._apply_method)(values)
        self._store(normalized)

    def _store(self, values):
        """Replace the values without sending them, e.g. with values read from the device."""
        super().__setitem__(slice(1, self._n + 1), list(values))

    def __reduce__(self):
        return (list, (list(self),))

    def _refuse(self, *args, **kwargs):
        raise TypeError(
            f"{self._name} holds exactly one value per channel. Set its "
            "elements instead."
        )

    append = extend = insert = pop = remove = clear = _refuse
    sort = reverse = __delitem__ = __iadd__ = __imul__ = _refuse


@contextlib.contextmanager
def batch(device):
    """The body of `batch()`, for the classes with `auto_sync`.

    Turns auto_sync off for the block, and sends every setting with one
    sync_to_device() at its end. If the block raises, or the settings fail
    sync_to_device()'s checks, nothing is sent and the settings go back to
    what they were, so that they still describe the device. The device
    class provides _snapshot(), _restore(snapshot), _check_sync() and
    _send_sync().
    """
    previous = device.auto_sync
    snapshot = device._snapshot()
    device.auto_sync = False
    try:
        yield device
        device._check_sync()
    except BaseException:
        device._restore(snapshot)
        device.auto_sync = previous
        raise
    device.auto_sync = previous
    device._send_sync()
