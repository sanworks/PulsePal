# Working on this repository

Pulse Pal is an open source pulse train generator: a device that plays precisely timed
voltage pulse trains on four analog output channels, started by software or by two trigger
inputs. This repository holds its firmware, its client libraries and its hardware design
files. Two hardware versions are supported: Pulse Pal 2 (Arduino Due) and Pulse Pal 3
(Teensy 4.1). The current firmware is v22.

## Layout

| Path | Contents |
|---|---|
| `/Firmware/PulsePal3/` | Current firmware, for **both** Pulse Pal 2 and Pulse Pal 3. Read its `AGENTS.md` before changing it |
| `/Firmware/PROTOCOL.md` | The USB serial protocol: op codes, parameter codes, replies, settings file layout |
| `/Firmware/WavePal/` | Wave Pal: alternative firmware that makes a Pulse Pal 3 a four channel waveform player. Its own `AGENTS.md` and `PROTOCOL.md` |
| `/Firmware/tools/` | `build_check.py`: compiles both hardware versions (or Wave Pal, with `--sketch wavepal`), and compares compiled functions between git revisions |
| `/Firmware/Old/` | Archived firmware, no longer developed |
| `/Python/PulsePal/` | Python class, GUI and offline tests. `WavePal.py` is the Python class for Wave Pal |
| `/MATLAB/@PulsePalDevice/` | MATLAB class. `/MATLAB/Legacy/` holds the older function-based interface |
| `/MATLAB/@WavePalDevice/` | MATLAB class for Wave Pal. `/MATLAB/tests/testWavePalDevice.m` tests it on a device |
| `/c++/` | C++ class (serial port via libserialport) and offline tests. `/c++/legacy/` holds the previous version, which also supports Pulse Pal 1 |
| `/CAD/`, `/Drivers/` | Hardware design files and USB drivers |

## The protocol ties everything together

The firmware and four clients (Python class, MATLAB class, MATLAB legacy functions, C++
class) all encode the same serial protocol, documented in `/Firmware/PROTOCOL.md`. A change
on one side usually needs a matching change on the others.

Op codes, parameter codes, message layouts and the settings file layout are **fixed**,
because installed copies of the clients rely on them. Add new op codes instead of changing
existing ones.

Wave Pal has a protocol of its own, in `/Firmware/WavePal/PROTOCOL.md`, with the same framing
byte. Its clients are `/Python/PulsePal/WavePal.py` and `/MATLAB/@WavePalDevice/`.

Past bugs came from this coupling, so check both sides:

- A Python parameter code was derived from a list position, so inserting a name shifted
  every code after it.
- Custom trains were sent with an op code calculated from the train number, which became a
  different command for trains 3 and 4.
- The last byte of op 97's reply arrived after the clients had stopped reading, and was
  taken as the reply to the next command.

## Checks you can run without a device

```bash
# Compile the firmware for both hardware versions (needs arduino-cli; see build_check.py)
python Firmware/tools/build_check.py

# Compile, and compare every compiled function with the last commit
python Firmware/tools/build_check.py --compare HEAD

# Python client: check the bytes it sends, using a fake serial port.
# uv creates the environment (numpy, pyserial) on first use; see /Python/PulsePal/README.md
cd Python/PulsePal && uv run python tests/test_protocol.py

# Wave Pal: compile its firmware, and check the bytes its Python class sends
python Firmware/tools/build_check.py --sketch wavepal
cd Python/PulsePal && uv run python tests/test_wavepal_protocol.py

# C++ class: the same check, and a byte-for-byte comparison with the Python class's op 92 and
# op 73 messages. Needs CMake and a C++ compiler; see /c++/README.md
cmake -S c++ -B c++/build && cmake --build c++/build --config Release
ctest --test-dir c++/build -C Release --output-on-failure
```

Everything else needs a device: pulse timing, the trigger inputs, the screen, the joystick
menu and the microSD card. `/Python/PulsePal/tests/benchmark_serial.py` measures USB latency
and throughput on a connected device; run it before and after changing serial code. On a device
running Wave Pal, `/Python/PulsePal/tests/wavepal_hardware_test.py` checks transfers and every
sample played, and `/MATLAB/tests/testWavePalDevice.m` does the same for the MATLAB class.

When you finish a change, state plainly what was and was not tested, and give the user a
short list of things to check on the device. `/Firmware/PulsePal3/AGENTS.md` lists the
usual checks.

## Style

- Match the surrounding code. The firmware is long-lived and written in a consistent style,
  and a large reformat makes support calls harder for the people who maintain it.
- Keep existing names for variables, functions, op codes and parameters.
- Comments explain why, and record the traps: interrupt context, fixed numbers, hardware
  quirks.
- Python follows PEP 8; MATLAB follows the guidelines in `CONTRIBUTING.md`.
- Work on the `develop` branch. `master` holds the latest stable release.
- Commit messages list the changes per component, e.g. `-Firmware: Fixed X -Python: Added Y`.
