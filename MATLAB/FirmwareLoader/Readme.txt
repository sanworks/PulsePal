**MATLAB Firmware Loader for Pulse Pal**

Usage:
1. Add this folder to the MATLAB path
2. At the MATLAB command prompt, run LoadPulsePalFirmware;

On Linux, Pulse Pal 2's firmware is loaded with bossac, which this folder does not include. On Ubuntu, install it
from a terminal with: sudo apt install bossa-cli

Firmware files in this folder begin with stable versions as of 1 July, 2026, and include more recent updates.
If you need an earlier firmware version, it must be installed manually. Installation instructions are here:
https://sites.google.com/site/pulsepalwiki/upgrading-firmware

Note: If Python is available on your system, a unified firmware loader for all Sanworks devices is provided at https://github.com/sanworks/Sanworks-FirmwareLoader

**Credits**
Note: Firmware updating for Pulse Pal v3 / Teensy 4.1  is powered by tycmd, an open source firmware loader application for Teensy.
More details at: https://github.com/Koromix/tytools

For Pulse Pal v2 / Arduino Due, firmware updating is powered by bossac.
More details at: https://github.com/shumatech/BOSSA
