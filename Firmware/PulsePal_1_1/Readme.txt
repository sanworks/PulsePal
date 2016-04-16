Firmware Upload Instructions

The Pulse Pal firmware can be uploaded to Pulse Pal 1 device using Leaflabs Maple software.


1. Download Maple from:
http://docs.leaflabs.com/docs.leaflabs.com/index.html
and install it on your system. 

On Windows Vista, 7, 8 and 10, you will need to install the driver from here:
https://github.com/rogerclarkmelbourne/Arduino_STM32/tree/master/drivers/win

2. Launch Maple

3. Select: File > Open and choose /PulsePal/Firmware/PulsePal_X_X/PulsePal_X_X.pde

4. Select Tools > Board > Leaflabs Maple r3+ To Flash

5. Select Tools > Port > (whichever port appears when you connect Pulse Pal, and disappears when it is disconnected)

6. Press "Upload" - the right-pointing arrow in the square button, in the first toolbar above the code

If you get stuck at "Searching for DFU Device..."DFU Device not found", try plugging in Pulse Pal the moment it says "Searching for DFU Device". 

If all goes well, after a few seconds you should see some text in the black console window that says "Verify successful".

If you have trouble uploading the firmware, please post in the Sanworks support forum:
https://sanworks.io/forum/forumdisplay.php?fid=3

These instructions are also available on the Pulse Pal Wiki at:
https://sites.google.com/site/pulsepalwiki/updating-firmware