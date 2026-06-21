'''
----------------------------------------------------------------------------

This file is part of the Sanworks Pulse Pal repository
Copyright (C) 2016 Sanworks LLC, Sound Beach, New York, USA

----------------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed  WITHOUT ANY WARRANTY and without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
'''

# Exmaple usage of PulsePal's Python API fields and functions

# Initializing PulsePal
from PulsePal import PulsePalObject # Import PulsePalObject
myPulsePal = PulsePalObject() # Create a new instance of a PulsePal object
myPulsePal.connect('COM3') # Connect to PulsePal on port COM3 (open port, handshake and receive firmware version)
print(myPulsePal.firmwareVersion) # Print firmware version to the console

# Examples of programming individual output channel parameters
myPulsePal.programOutputChannelParam('isBiphasic', 1, 1) # Program output channel 1 to use biphasic pulses
myPulsePal.programOutputChannelParam('phase1Voltage', 1, 10) # Program channel 1 to use 10V for phase 1 of its biphasic pulses
myPulsePal.programOutputChannelParam(3, 1, -10) # Parameters can be specified by their parameter code instead of their name
myPulsePal.programOutputChannelParam('phase1Duration', 1, 0.001) # Example for a 32-bit time parameter

# Programming a trigger channel parameter
myPulsePal.programTriggerChannelParam('triggerMode', 1, 2) # Set trigger channel 1 to pulse gated mode
myPulsePal.programTriggerChannelParam('triggerMode', 2, 0) # Set trigger channel 2 to normal mode

# Programming *all* parameters to match the myPulsePal object's fields
myPulsePal.isBiphasic[1] = 0 # parameter arrays are 5 elements long. Use [1] for output channel 1. (i.e. isBiphasic[0] is not used)
myPulsePal.phase1Voltage[2] = 7 # set output channel 2 to use 7V pulses
myPulsePal.interPulseInterval[1:5] = [0.2]*4 # set all output channels to use inter-pulse interval = 0.1s
myPulsePal.restingVoltage[1] = 0 # set channel 1's resting voltage (between pulses) to zero
myPulsePal.syncAllParams()

# Programming a custom pulse train
pulseTimes = [0, 0.2, 0.5, 1] # Create an array of pulse times in seconds
voltages = [8,4,-3.5,-10] # Create an array of pulse voltages in volts
myPulsePal.sendCustomPulseTrain(2, pulseTimes, voltages) # Send these arrays to PulsePal, defined as custom train 2 (of 2 possible)
myPulsePal.programOutputChannelParam('customTrainID', 1, 2) # Program output channel 1 to use custom train 2

# Programming a custom waveform (convenient shorthand for custom pulse train with evenly spaced, confluent pulses)
import math
voltages = range(0,1000)
for i in voltages:
    voltages[i] = math.sin(voltages[i]/float(10))*10 # Set 1,000 voltages to create a 20V peak-to-peak sine waveform
pulseWidth = 0.001 # Set the sampling period for 1kHz sampling
myPulsePal.sendCustomWaveform(1, pulseWidth, voltages)
myPulsePal.programOutputChannelParam('customTrainID', 2, 1) # Program output channel 2 to use custom train 1
myPulsePal.programOutputChannelParam('phase1Duration', 2, pulseWidth) # Program output channel 2 to use correct pulse width for the waveform

# Soft-triggering output channels

myPulsePal.triggerOutputChannels(1, 1, 0, 1) # Soft-trigger channels 1, 2 and 4

# Soft-abort ongoing pulse trains
import time
time.sleep(0.5) # Allow pulse trains to play for a while
myPulsePal.abortPulseTrains() # Abort pulse trains on all output channels

# Set a channel to loop its pulse train indefinitely the next time it is triggered

myPulsePal.setContinuousLoop(3, 1) # Set channel 3 to continuous mode
myPulsePal.setContinuousLoop(4, 0) # Set channel 4 to normal mode

# Set a fixed voltage on an output channel

myPulsePal.setFixedVoltage(4,7) # Set output channel 4 to +7V, persistently

# Write text to Pulse Pal oLED display
time.sleep(2)
import time
myPulsePal.setDisplay("PYTHON Connected", "Beware of fangs!") # Writes "Python Connected" on line 1 and "Beware of fangs" on line 2
time.sleep(2)
myPulsePal.setDisplay("PYTHON Connected", " Click for menu")
time.sleep(2)

# Disconnect PulsePal
myPulsePal.disconnect() # Sends a termination byte and closes the serial port. PulsePal stores current params to its EEPROM.
