This is the Matlab API for Pulse Pal

To get started:
1. Add this folder ('MATLAB') to the MATLAB path. Subfolders not necessary.
2. Run 'PulsePal'. 


If you are on Windows XP, or if you have trouble starting Pulse Pal, determine which serial port Pulse Pal is on and initialize as:
PulsePal('mySerialPort') where mySerialPort is a string (e.g. COM3 on windows, /dev/TTYACM0 on linux). 