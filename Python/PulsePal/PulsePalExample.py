"""
----------------------------------------------------------------------------

This file is part of the Sanworks PulsePal repository
Copyright (C) Sanworks LLC, Rochester, New York, USA

----------------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed  WITHOUT ANY WARRANTY and without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""

# Example usage of PulsePal's Python Interface

# Import the PulsePal class
from PulsePal import PulsePalDevice  # Import PulsePalObject

# Import math and time (required for examples)
import math
import time

# Initializing PulsePal
P = PulsePalDevice('COM10')  # Create a new instance of a PulsePal object (set this to your USB serial port)
print("Hardware Version: " + str(P.info.hardware_version))  # Print hardware version to the console
print("Firmware Version: " + str(P.info.firmware_version))  # Print firmware version to the console

# Examples of programming individual output channel parameters
P.set_output_param('is_biphasic', 1, 1)  # Program output channel 1 to use biphasic pulses
P.set_output_param('phase1_voltage', 1, 10)  # Program ch1 to use 10V for phase 1 of biphasic pulses
P.set_output_param(3, 1, -10)  # Params can be specified by their parameter code instead of their name
P.set_output_param('phase1_duration', 1, 0.001)  # Example for a 32-bit time parameter

# Programming a trigger channel parameter
P.set_trigger_param('trigger_mode', 1, 2)  # Set trigger channel 1 to pulse gated mode
P.set_trigger_param('trigger_mode', 2, 0)  # Set trigger channel 2 to normal mode

# Programming *all* parameters to match the P object's fields
P.is_biphasic[1] = 0  # parameter arrays are 5 elements long. Use [1] for output channel 1. (i.e. isBiphasic[0] is not used)
P.phase1_voltage[2] = 7  # set output channel 2 to use 7V pulses
P.inter_phase_interval[1:5] = [0.2]*4  # set all output channels to use inter-pulse interval = 0.1s
P.resting_voltage[1] = 0  # set channel 1's resting voltage (between pulses) to zero
P.sync_to_device()

# Programming a custom pulse train
pulseTimes = [0, 0.2, 0.5, 1]  # Create an array of pulse times in seconds
voltages = [8, 4, -3.5, -10]  # Create an array of pulse voltages in volts
P.send_custom_pulse_train(2, pulseTimes, voltages)  # Send custom train 2 defined by these arrays (of 2 possible)
P.set_output_param('custom_train_id', 1, 2)  # Program output channel 1 to use custom train 2

# Programming a custom waveform (convenient shorthand for custom pulse train with evenly spaced, confluent pulses)
voltages = list(range(0, 1000))
for i in voltages:
    voltages[i] = math.sin(voltages[i]/float(10))*10  # Set 1,000 voltages to create a 20V peak-to-peak sine waveform
pulseWidth = 0.001  # Set the sampling period for 1kHz sampling
P.send_custom_waveform(1, pulseWidth, voltages)
P.set_output_param('custom_train_id', 2, 1)  # Program output channel 2 to use custom train 1
P.set_output_param('phase1_duration', 2, pulseWidth)  # Set correct pulse width for the waveform on ch2

# Soft-triggering output channels

P.trigger([1,2,4])  # Trigger channels 1, 2 and 4

# Soft-abort ongoing pulse trains
time.sleep(3)  # Allow pulse trains to play for a while
P.stop()  # Stop pulse trains on all output channels

# Set a channel to loop its pulse train indefinitely the next time it is triggered

P.set_continuous_loop(3, 1)  # Set channel 3 to continuous loop mode
P.set_continuous_loop(4, 0)  # Set channel 4 to normal mode
P.set_continuous_loop(3, 0)  # Set channel 3 back to normal mode

# Set a fixed voltage on an output channel

P.set_voltage(4, 7)  # Set output channel 4 to +7V, persistently

time.sleep(3)

# Disconnect PulsePal
del P
