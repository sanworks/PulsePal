# Pulse Pal Python interface

Python interface for the [Pulse Pal](https://sites.google.com/site/pulsepalwiki/)
open source pulse train generator. API documentation is published at
https://sanworks.github.io/PulsePal/Python/.

The `pulsepal` package holds three classes, one for each firmware a Pulse Pal
can run:

| Class | Device | Example |
|---|---|---|
| `PulsePalDevice` | Pulse Pal 2 or 3 running Pulse Pal firmware: pulse trains | `PulsePalExample.py` |
| `WavePalDevice` | Pulse Pal 3 running Wave Pal firmware (`/Firmware/WavePal`): sampled waveforms | `WavePalExample.py` |
| `SynthPalDevice` | Pulse Pal 3 running Synth Pal firmware (`/Firmware/SynthPal`): sine, triangle, square and sawtooth waves, and fixed voltages | `SynthPalExample.py` |

```python
from pulsepal import PulsePalDevice

with PulsePalDevice("COM3") as P:      # Replace COM3 with Pulse Pal's port
    P.phase1_voltage[1] = 5            # Volts, output channel 1
    P.phase1_duration[1] = 0.001       # Seconds
    P.trigger(1)
```

The three work the same way: settings are lists indexed by channel number
(index 0 is unused), and assigning to one programs the device at once;
`trigger()` and `stop()` take a channel number, or several as a list, tuple or
NumPy array; `set_default_params()` programs the defaults; `print(device)`
shows every setting; and errors raise `pulsepal.PulsePalError`.
`PulsePalDevice.serialportlist()` lists the USB serial ports.

## Installation

Install [uv](https://docs.astral.sh/uv/getting-started/installation/), then open
the terminal and run the following from this directory:

```
uv sync
```

That creates `.venv` with the interface and its dependencies installed, and is
the whole installation on Windows and Linux. The first run downloads a
CPython build for the project (about 20 MB); later runs reuse it.

The download is deliberate. `pulsepal.gui` imports tkinter, which Linux
distributions package separately from Python, so a virtual environment built
from a distribution's interpreter can install cleanly and then fail at
`P.gui()`. uv's CPython builds bundle Tcl/Tk, so `uv sync` alone is enough on a
bare install. `.python-version` and `python-preference` in `pyproject.toml`
control this.

### Linux: serial port permissions

Pulse Pal appears as `/dev/ttyACM0` (or `ttyACM1`, ...) and most distributions
restrict those ports to the `dialout` group. Without this, connecting raises a
permission error:

```
sudo usermod -a -G dialout $USER
```

Log out and back in for the group change to take effect.

## Using PyCharm

Run `uv sync` before opening this folder as a project. PyCharm detects a `.venv`
in the project root and configures it as the interpreter on first open. If the
project was opened before `.venv` existed, PyCharm records the interpreter it
chose then and will not revisit it: set it under Settings > Project > Python
Interpreter > Add Interpreter > Add Local Interpreter > Select existing, at
`.venv/bin/python` (`.venv\Scripts\python.exe` on Windows), and restart the
Python Console so it picks up the change.

## License

This directory is part of the Sanworks PulsePal repository, released under the
GNU General Public License version 3. See LICENSE.txt in the repository root.
