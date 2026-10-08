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

from pulsepal import PulsePalDevice

# Create a new instance of a Pulse Pal object. Replace "COM5" with Pulse
# Pal's USB serial port name, which can be found with
# PulsePalDevice.serialportlist(). Connecting programs the default parameters.
P = PulsePalDevice("COM5")
print(f"Hardware Version: {P.info.hardware_version}")
print(f"Firmware Version: {P.info.firmware_version}")

# Parameters are lists indexed by channel number: [1] is output channel 1,
# and element [0] is not used. Assigning to them programs the device at once.
# Voltages are in volts, and times in seconds.
P.phase1_voltage[1] = 5            # 5 V pulses on output channel 1,
P.phase1_duration[1] = 0.001       # 1 ms long,
P.inter_pulse_interval[1] = 0.049  # 49 ms apart (end of one to start of the next): 20 per second,
P.pulse_train_duration[1] = 2      # for 2 seconds after each trigger

# A slice sets several channels, and a single value sets all four
P.inter_phase_interval[1:5] = [0.0002] * 4
P.resting_voltage = 0

# Biphasic pulses on channel 2: phase 1, an interval at the resting voltage,
# then phase 2
P.is_biphasic[2] = True
P.phase2_voltage[2] = -5

# Trigger modes are set per trigger channel, 1 or 2, by name
P.trigger_mode[1] = "Gated"   # Trigger channel 1: trains play while its TTL is high
P.trigger_mode[2] = "Normal"  # Trigger channel 2: a TTL starts the linked trains

# Each output channel is linked to trigger channel 1, 2, both or neither
P.link_trigger_channel2[3] = True

# Several parameters at once, sent in one command when the block ends
with P.batch():
    P.phase1_voltage[2:5] = [7, 3, 3]
    P.phase1_duration[2:5] = [0.002, 0.0005, 0.0005]

# Print every parameter
print(P)

# Programming a custom pulse train
pulse_times = [0, 0.2, 0.5, 1]  # Pulse onset times, in seconds
voltages = [8, 4, -3.5, -10]    # Pulse voltages, in volts

# Send custom train 2, defined by the lists above. Pulse Pal 2 holds custom
# trains 1-2, and Pulse Pal 3 holds 1-4 (P.info.n_custom_pulse_trains).
P.send_custom_pulse_train(2, pulse_times, voltages)

# Program output channel 1 to play custom train 2
P.custom_train_id[1] = 2

# Programming a custom waveform. This is a convenient shorthand for a
# custom pulse train with evenly spaced, adjoining pulses.
voltages = [math.sin(i / 10) * 10 for i in range(1000)]  # A 20 V peak-to-peak sine wave
pulse_width = 0.001  # The sampling period: 1 kHz sampling
P.send_custom_waveform(1, pulse_width, voltages)

# Program output channel 2 to play custom train 1, with each sample held for
# the sampling period
P.custom_train_id[2] = 1
P.phase1_duration[2] = pulse_width

# Soft-triggering output channels: one channel number, or several as a list
P.trigger([1, 2, 4])  # Trigger channels 1, 2 and 4

# Soft-abort ongoing pulse trains
time.sleep(3)  # Allow pulse trains to play for a while
P.stop()  # Stop pulse trains on all output channels

# Set a fixed voltage on an output channel, until it is triggered
P.set_fixed_voltage(4, 2)  # Set output channel 4 to +2 V

time.sleep(3)

# Disconnect Pulse Pal. It stops all output channels.
P.close()
