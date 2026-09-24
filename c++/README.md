# Pulse Pal C++ class

A C++ interface for Pulse Pal 2 and Pulse Pal 3, running firmware v21 or v22, on Windows, macOS and Linux.

```cpp
#include "PulsePal.h"

PulsePal pulsePal;
if (!pulsePal.initialize("COM3")) {  // "/dev/cu.usbmodem14101" on macOS, "/dev/ttyACM0" on Linux
    return 1;
}
pulsePal.setPhase1Voltage(1, 5);       // 5V pulses on output channel 1
pulsePal.setPulseTrainDuration(1, 2);  // for 2 seconds
pulsePal.triggerChannel(1);
pulsePal.end();
```

[PulsePal.h](PulsePal.h) documents every method. [PulsePalExample.cpp](PulsePalExample.cpp) is a longer
example, and the serial protocol is described in [/Firmware/PROTOCOL.md](../Firmware/PROTOCOL.md).

The class uses [libserialport](https://sigrok.org/wiki/Libserialport) for the USB serial port. It is an actively
maintained, cross-platform C library from the sigrok project, packaged for Homebrew, Debian/Ubuntu, MSYS2 and
vcpkg, and licensed LGPL-3.0-or-later. The previous version of this class, built on openFrameworks' `ofSerial`, is
in [legacy/](legacy/). Use it for Pulse Pal 1.

## Building

You need CMake 3.14 or newer and a C++11 compiler. The build makes a static library, `pulsepal`, the example
program `PulsePalExample`, and the offline tests `test_protocol`.

### Windows 11

Install Visual Studio 2022 with the "Desktop development with C++" workload, which includes CMake and the Windows
SDK. Then, in a Developer PowerShell, from this folder:

```bash
cmake -S . -B build
cmake --build build --config Release
build\Release\PulsePalExample.exe COM3
```

The first `cmake` downloads libserialport 0.1.2 from sigrok.org, checks its SHA-256 hash, and builds it with the
project, so there is nothing else to install. (Or open this folder in Visual Studio with File > Open > Folder.)

Pulse Pal's port is listed in Device Manager under "Ports (COM & LPT)".

### macOS

```bash
brew install cmake pkg-config libserialport
cmake -S . -B build
cmake --build build
./build/PulsePalExample /dev/cu.usbmodem14101
```

Pulse Pal appears as `/dev/cu.usbmodem*`. List the candidates with `ls /dev/cu.usbmodem*`. Use the `cu.` device,
not `tty.`.

### Ubuntu

```bash
sudo apt install build-essential cmake pkg-config libserialport-dev
cmake -S . -B build
cmake --build build
./build/PulsePalExample /dev/ttyACM0
```

Pulse Pal appears as `/dev/ttyACM*`. To open it without `sudo`, add yourself to the `dialout` group once, then log
out and back in:

```bash
sudo usermod -aG dialout $USER
```

## Using the class in your project

With CMake, add this folder and link to `pulsepal`:

```cmake
add_subdirectory(path/to/PulsePal/c++ pulsepal)
target_link_libraries(your_app PRIVATE pulsepal)
```

Without CMake, compile `PulsePal.cpp` and `SerialPort.cpp` with your program, and link to libserialport. Only
`SerialPort.cpp` includes `libserialport.h`.

## Errors

Methods that send a command return `true` if it was sent and, for commands the device confirms, the device
confirmed it. They return `false`, and print the reason to `std::cerr`, if:

- the device is not connected,
- an argument is out of range for the connected device, in which case nothing is sent,
- or the device rejected the command, or did not reply within 2 seconds.

A device that stops replying is usually showing **COMM. FAILURE!** on its screen, after a command was cut short (a
faulty cable or hub, or a program that stopped mid-command). Click its joystick, and call `initialize()` again.

## Pulse Pal 3

- Custom pulse trains 1-4, with up to 10,000 pulses each (Pulse Pal 2: trains 1-2, 5,000 pulses).
- Param sync trigger mode: `setTriggerMode(channel, 3)`. While a trigger channel is in this mode,
  `syncAllParams()` stores the parameters on the device, and the next rising edge on that trigger channel applies
  them. This is how the next trial's parameters can be sent during the current trial. Only `syncAllParams()` is
  held back: every other method takes effect at once. See "Param sync mode" in
  [/Firmware/PROTOCOL.md](../Firmware/PROTOCOL.md).
- `getHardwareVersion()` returns 2 or 3.

## Changes from the legacy class

The methods and fields are the same as in [legacy/PulsePal.h](legacy/PulsePal.h), plus `getHardwareVersion()`.
These changed:

- Pulse Pal 1, and firmware older than v21, are not supported. Use the legacy class.
- Methods that sent a command now return `bool` instead of `void` (see [Errors](#errors)). `initialize()` also
  returns `bool`.
- `initialize()` now programs the device with the default parameters, as the MATLAB and Python classes do, so
  that the device and `currentOutputParams` agree.
- Out-of-range values are rejected, instead of silently limited. Voltages are -10 to 10 V. Times are 0 to 3600 s,
  except phase durations, the inter-pulse interval and the pulse train duration, whose minimum is 0.0001 s. These
  are the MATLAB class's limits.
- Times are rounded to the nearest 50 µs timer cycle, and voltages to the nearest DAC code, as in the Python
  class. The legacy class truncated times, so e.g. a 0.0007 s phase was sent as 13 cycles (0.00065 s) instead of 14.
- The device's confirm bytes are read. The legacy class left them unread.
- Fixed: `setInterPulseInterval()` stored its value in `interPhaseInterval`; `currentInputParams` (the trigger
  modes) was uninitialized, so `syncAllParams()` could send any trigger mode; `setPulseTrainDelay()` could not set
  a delay of 0.
- `stdafx.h` and the Visual Studio `_tmain` entry point are no longer needed.

## Tests

`tests/test_protocol.cpp` checks the bytes the class sends, using a fake serial port, so it needs no Pulse Pal. It
compares the parameter messages (ops 92 and 73) with those sent by the Python class, byte for byte. Run it after
building:

```bash
ctest --test-dir build -C Release --output-on-failure
```

(`--test-dir` needs CMake 3.20. On older versions, run `ctest -C Release` from the `build` folder.)

Everything else needs a device. `PulsePalExample` calls every method on a connected Pulse Pal, and plays pulse
trains of up to ±10 V on all four output channels, so disconnect anything attached to the outputs first.
