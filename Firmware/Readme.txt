Note: This folder contains firmware files that must be compiled and loaded to the device with Arduino.
https://www.arduino.cc/en/software/#ide
Support for boards and dependencies must be manually installed, per the code comments.

If you would prefer to skip that setup and load the firmware without using Arduino, there are two options:

1. If Python is available on your system, a unified firmware loader for all Sanworks devices including Pulse Pal is provided at https://github.com/sanworks/Sanworks-FirmwareLoader

2. If MATLAB is available on your system, you can use the firmware loading tool in /PulsePal/MATLAB/FirmwareLoader/

Folders:

/PulsePal3
The current firmware. It compiles for both Pulse Pal 2 (Arduino Due) and Pulse Pal 3 (Teensy 4.1),
selected with the HARDWARE_VERSION macro at the top of PulsePal3.ino. See AGENTS.md in that folder
for the code map, build instructions and the rules to follow when modifying the firmware.

/Old
Archived firmware for earlier hardware and earlier releases. These are kept for reference and are no
longer developed.

/tools
build_check.py compiles the firmware for both hardware versions, and can compare the compiled code
with another git revision function by function, to confirm that an edit did not change behavior.

PROTOCOL.md
The USB serial protocol used by the Python, MATLAB and C++ interfaces: op codes, parameter codes,
the settings file layout, and the meaning of the confirm byte returned by the device.
