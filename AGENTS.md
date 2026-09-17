# Working on this repository

Pulse Pal is an open source pulse train generator: a device that plays precisely timed
voltage pulse trains on four analog output channels. This repository holds its firmware,
its client libraries, and its hardware design files.

## Layout

| Path | Contents |
|---|---|
| `/Firmware/PulsePal3/` | Current firmware. Builds for **both** Pulse Pal 2 and Pulse Pal 3. Read `AGENTS.md` there before changing it |
| `/Firmware/Old/` | Archived firmware, no longer developed |
| `/Firmware/PROTOCOL.md` | The USB serial protocol: op codes, parameter codes, settings file layout |
| `/Firmware/tools/` | `build_check.py`, which compiles both hardware versions and compares compiled functions between git revisions |
| `/Python/PulsePal/` | Python class, GUI and offline tests |
| `/MATLAB/@PulsePalDevice/` | MATLAB class. `/MATLAB/Legacy/` holds the older function-based interface |
| `/c++/` | C++ class |
| `/CAD/`, `/Drivers/` | Hardware design files and USB drivers |

## The protocol ties everything together

The firmware and four clients (Python, MATLAB class, MATLAB legacy functions, C++) all
encode the same serial protocol. A change on one side usually needs a matching change on
the others, and `/Firmware/PROTOCOL.md` is the reference.

Op codes, parameter codes and the settings file layout are **fixed**: installed copies of
the clients rely on them. Add new op codes instead of changing existing ones.

Recent bugs came from this coupling, so it is worth double-checking:

- A parameter code was derived from a list position in the Python class, and inserting a
  name shifted every code after it.
- Custom trains were sent with an op code calculated from the train number, which silently
  became a different command for trains 3 and 4.

## Checks you can run without a device

```bash
# Compile the firmware for both hardware versions
python Firmware/tools/build_check.py

# Compile, and compare every compiled function with the last commit
python Firmware/tools/build_check.py --compare HEAD

# Python client: check the bytes it sends, using a fake serial port
cd Python/PulsePal && python tests/test_protocol.py
```

Everything else needs hardware: pulse timing, the screen, the joystick menu, the microSD
card and the trigger inputs. `Python/PulsePal/tests/benchmark_serial.py` measures USB latency
and throughput against a connected device, before and after a change. State plainly what was and was not tested, and give the user
a short list of things to check on the device.

## Style

- Match the surrounding code. It is long-lived firmware written in a consistent style, and
  a big reformat makes support calls harder for the people who maintain it.
- Keep existing names for variables, functions, op codes and parameters.
- Comments explain why, and record the traps: interrupt context, fixed numbers, hardware
  quirks.
- Python follows PEP 8; MATLAB follows the guidelines in `CONTRIBUTING.md`.
- Commit messages in this repository list the changes per component, e.g.
  `-Firmware: Fixed X -Python: Added Y`.
