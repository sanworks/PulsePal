This folder contains the Pulse Pal firmware. To load it onto a device you can either compile it
yourself with Arduino (https://www.arduino.cc/en/software/#ide), or use a firmware loader that
needs no Arduino setup.

Loading firmware without Arduino:

1. With Python: the firmware loader for all Sanworks devices, including Pulse Pal, at
   https://github.com/sanworks/Sanworks-FirmwareLoader
2. With MATLAB: the firmware loading tool in /MATLAB/FirmwareLoader/

Compiling it yourself: the board support and libraries must be installed first. They are listed
at the top of PulsePal3/PulsePal3.ino, and in the "Building" section of PulsePal3/AGENTS.md.

Folders and files:

/PulsePal3
The current firmware. It compiles for both Pulse Pal 2 (Arduino Due) and Pulse Pal 3 (Teensy 4.1),
selected with the HARDWARE_VERSION macro at the top of PulsePal3.ino. AGENTS.md in that folder has
the code map, build instructions and the rules to follow when changing the firmware.

/WavePal
Wave Pal: alternative firmware for Pulse Pal 3 (Teensy 4.1) that makes it a four channel waveform
player. Each output channel plays a waveform of up to 1 million samples, stored on the microSD card,
when it is triggered by TTL, by software or from the joystick. It is controlled with the Python class
in /Python/PulsePal/WavePal.py. PROTOCOL.md and AGENTS.md in that folder describe its USB protocol and
code.

/Old
Archived firmware for earlier hardware and releases, kept for reference and no longer developed.

/tools
build_check.py compiles the firmware for both hardware versions. It can also compare the compiled
code with another git revision, function by function, to confirm that an edit did not change
behavior.

PROTOCOL.md
The USB serial protocol between the device and its Python, MATLAB and C++ clients: op codes,
parameter codes, replies and the settings file layout.
