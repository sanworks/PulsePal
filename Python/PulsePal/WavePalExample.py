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

# Example usage of Wave Pal's Python interface. Wave Pal is alternative
# firmware that makes a Pulse Pal 3 a four channel waveform player: load it
# from /Firmware/WavePal first. Each section below is a self-contained
# snippet, meant to be read alongside the docstrings in WavePal.py.

import time

import numpy as np

from WavePal import WavePalDevice

# Connect. Replace "COM5" with the device's USB serial port name, which can
# be found with WavePalDevice.serialportlist(). Connecting stops playback and
# programs the default settings.
W = WavePalDevice("COM5")
print(f"Wave Pal firmware v{W.info.firmware_version}, "
      f"up to {W.info.max_samples} samples per waveform")

# The sampling rate applies to all four channels, up to 100 kHz
W.sampling_rate = 50000

# The output range applies to all four channels. The smallest range that fits
# the waveforms gives the finest voltage steps.
W.output_range = "-5V:5V"

# Each output channel has one waveform. Voltages are in volts, within the
# output range. Here: a 1 second, 10 Hz sine wave on channel 1, and a 20 ms,
# 2 V square pulse on channel 2.
t = np.arange(W.sampling_rate) / W.sampling_rate
W.load_waveform(1, 4 * np.sin(2 * np.pi * 10 * t))
W.load_waveform(2, np.full(int(0.02 * W.sampling_rate), 2.0))

# Play from software. Channels in the same call start on the same sample.
W.play([1, 2])
time.sleep(1.5)

# Channel settings are lists indexed by channel number (index 0 is unused),
# and setting an element programs the device at once.
W.loop_mode[1] = True        # Loop channel 1's waveform...
W.loop_duration[1] = 3       # ...for 3 seconds after each trigger
W.play(1)
time.sleep(4)

W.loop_duration[1] = 0       # 0 loops until stopped
W.play(1)
time.sleep(1)
W.stop(1)                    # The output returns to 0 V

# TTL triggers. By default, a rising edge on trigger channel 1 plays all four
# channels (those with a waveform). Here channel 2 is moved to trigger
# channel 2, where a TTL plays it for as long as the TTL is high.
W.link_trigger_channel1[2] = False
W.link_trigger_channel2[2] = True
W.trigger_mode[2] = "Gated"
W.loop_mode[2] = True        # With a loop duration of 0: plays while the TTL is high

# Other trigger modes: "Normal" ignores triggers while the waveform plays,
# "Master" restarts it, and "Toggle" stops it.
W.trigger_mode[1] = "Toggle"

# Hold a channel at a fixed voltage until it is triggered or stopped
W.set_fixed_voltage(3, 1.5)

# Playback state, e.g. to check for underruns (blocks of samples the microSD
# card did not deliver in time) after an experiment
print(W.status())

# Closing the port leaves the device as it is, so TTL triggers keep playing
# the loaded waveforms.
W.close()
