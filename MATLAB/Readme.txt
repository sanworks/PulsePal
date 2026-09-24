***MATLAB Control Interface for Pulse Pal***

There are two control interfaces in this folder:
1. @PulsePalDevice, a modern object-oriented interface
2. /Legacy/, the original interface.

For new projects, the modern interface is strongly recommended.

To get started:
If using the modern interface
1. Add this folder ('MATLAB') to the MATLAB path.
2. Run P = PulsePalDevice('COM3'); % Replace COM3 with the correct USB serial port name

If using the Legacy interface
2. Add /Legacy/ to the MATLAB path. Subfolders are not necessary.
3. Run 'PulsePal'

***MATLAB Control Interface for Wave Pal***

Wave Pal is alternative firmware that makes a Pulse Pal 3 a four channel waveform player (see /Firmware/WavePal).
To control it:
1. Add this folder ('MATLAB') to the MATLAB path.
2. Run W = WavePalDevice('COM3'); % Replace COM3 with the correct USB serial port name
3. Type 'help WavePalDevice' for an example and a description of its settings.

***MATLAB Firmware Load Tool for Pulse Pal***

To load firmware to the device, add the 'Firmware' folder to the MATLAB path and run: LoadPulsePalFirmware;