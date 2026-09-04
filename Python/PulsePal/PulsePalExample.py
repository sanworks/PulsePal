"""
----------------------------------------------------------------------------

This file is part of the Sanworks PulsePal repository
Copyright (C) Sanworks LLC, Rochester, New York, USA

----------------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed WITHOUT ANY WARRANTY and without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
"""

# Example usage of Pulse Pal's Python interface. Each section below is a
# self-contained snippet, meant to be read alongside the API docs.

import math
import time

from PulsePal import PulsePalDevice

# Create a new instance of a Pulse Pal object. Replace "COM5" with Pulse
# Pal's USB serial port name, which can be found with
# PulsePalDevice.serialportlist().
P = PulsePalDevice("COM5")
print(f"Hardware Version: {P.info.hardware_version}")
print(f"Firmware Version: {P.info.firmware_version}")

# Examples of programming individual output channel parameters

# Program output channel 1 to use biphasic pulses
P.set_output_param("is_biphasic", 1, 1)

# Program ch1 to use 10V for phase 1 of biphasic pulses
P.set_output_param("phase1_voltage", 1, 10)

# Params can be specified by their parameter code instead of their name
P.set_output_param(3, 1, -10)

# Example for a 32-bit time parameter
P.set_output_param("phase1_duration", 1, 0.001)

# Programming a trigger channel parameter
P.set_trigger_param("trigger_mode", 1, 2)  # Ch1 to pulse gated mode
P.set_trigger_param("trigger_mode", 2, 0)  # Ch2 to normal mode

# Programming *all* parameters to match the P object's fields.
# Parameter arrays are 5 elements long. Use [1] for output channel 1,
# i.e. is_biphasic[0] is not used.
P.is_biphasic[1] = 0
P.phase1_voltage[2] = 7  # Set output channel 2 to use 7V pulses

# Set all output channels to use inter-phase interval = 0.2s
P.inter_phase_interval[1:5] = [0.2] * 4

# Set channel 1's resting voltage (between pulses) to zero
P.resting_voltage[1] = 0

P.sync_to_device()

# Programming a custom pulse train
pulse_times = [0, 0.2, 0.5, 1]  # An array of pulse times in seconds
voltages = [8, 4, -3.5, -10]  # An array of pulse voltages in volts

# Send custom train 2 (of 2 possible), defined by the arrays above
P.send_custom_pulse_train(2, pulse_times, voltages)

# Program output channel 1 to use custom train 2
P.set_output_param("custom_train_id", 1, 2)

# Programming a custom waveform. This is a convenient shorthand for a
# custom pulse train with evenly spaced, confluent pulses.
voltages = list(range(0, 1000))
for i in voltages:
    # Set 1,000 voltages to create a 20V peak-to-peak sine waveform
    voltages[i] = math.sin(voltages[i] / float(10)) * 10
pulse_width = 0.001  # Set the sampling period for 1kHz sampling
P.send_custom_waveform(1, pulse_width, voltages)

# Program output channel 2 to use custom train 1
P.set_output_param("custom_train_id", 2, 1)

# Set the correct pulse width for the waveform on ch2
P.set_output_param("phase1_duration", 2, pulse_width)

# Soft-triggering output channels
P.trigger([1, 2, 4])  # Trigger channels 1, 2 and 4

# Soft-abort ongoing pulse trains
time.sleep(3)  # Allow pulse trains to play for a while
P.stop()  # Stop pulse trains on all output channels

# Set a fixed voltage on an output channel
P.set_voltage(4, 2)  # Set output channel 4 to +2V, persistently

time.sleep(3)

# Disconnect Pulse Pal
del P
