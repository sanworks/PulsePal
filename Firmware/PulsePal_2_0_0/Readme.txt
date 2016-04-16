Firmware Upload Instructions

The Pulse Pal firmware can be uploaded to the Pulse Pal 2 device using Arduino software.
These instructions walk you through uploading with Arduino 1.6.8.

1. Download Arduino from:
https://www.arduino.cc/en/Main/Software
and install it on your system

2. Launch Arduino

3. Select: Tools > Board > Boards Manager

4. Under "Arduino SAM Boards (32-bits ARM Cortex-M3) choose "Install"

5. Close the boards Manager.

6. Select: File > Open and choose /PulsePal/Firmware/PulsePal_2_X_X/PulsePal2_X_X.ino

7. Select Tools > Board > Arduino Due (Native USB Port)

8. Select Tools > Port > (whichever port appears when you connect Pulse Pal, and disappears when it is disconnected)

9. Press "Upload" - the right-pointing arrow below in the first toolbar above the code

If all goes well, after a few seconds you should see some orange text in the black console window that says "Verify successful.... CPU reset.".

If you have trouble uploading the firmware, please post in the Sanworks support forum:
https://sanworks.io/forum/forumdisplay.php?fid=3

These instructions are also available on the Pulse Pal Wiki at:
https://sites.google.com/site/pulsepalwiki/updating-firmware