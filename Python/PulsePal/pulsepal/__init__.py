"""
Python interface for [Pulse Pal](https://sites.google.com/site/pulsepalwiki/),
an open source pulse train generator, and for Wave Pal and Synth Pal, two
alternative firmwares for Pulse Pal 3.

```python
from pulsepal import PulsePalDevice

with PulsePalDevice("COM3") as P:   # Replace COM3 with Pulse Pal's port
    P.phase1_voltage[1] = 5
    P.trigger(1)
```

| Device firmware | Class | Module |
|---|---|---|
| Pulse Pal: pulse trains | `PulsePalDevice` | `pulsepal.pulse_pal` |
| Wave Pal: sampled waveforms, streamed from the microSD card | `WavePalDevice` | `pulsepal.wave_pal` |
| Synth Pal: sine, triangle, square and sawtooth waves, and fixed voltages | `SynthPalDevice` | `pulsepal.synth_pal` |

The three classes work the same way. Settings are lists indexed by
channel number (index 0 is unused), and assigning to one programs the
device at once; `trigger()` starts channels and `stop()` stops them;
`set_default_params()` programs the defaults; `print(device)` shows every
setting; and they raise `PulsePalError` when something fails. Each class
names the firmware a device runs when it is not its own.

`PulsePalDevice.serialportlist()` lists the USB serial ports, to find the
device's.

This file is part of the Sanworks PulsePal repository, released under the
GNU General Public License version 3.
"""

from ._common import PulsePalError
from .pulse_pal import PulsePalDevice
from .synth_pal import SynthPalDevice
from .wave_pal import WavePalDevice

__version__ = "3.0.0"
"""Version of this package."""

__all__ = ["PulsePalDevice", "WavePalDevice", "SynthPalDevice", "PulsePalError", "__version__"]
__docformat__ = "google"
