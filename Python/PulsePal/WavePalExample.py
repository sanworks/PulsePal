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
# snippet, meant to be read alongside the docstrings in pulsepal/wave_pal.py.

import time

import numpy as np

from pulsepal import WavePalDevice

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

# The device stores four waveforms, numbered 1-4, and each output channel
# plays one of them. Voltages are in volts, within the output range. Here:
# waveform 1, a 1 second, 10 Hz sine wave, played by channels 1 and 3, and
# waveform 2, a 20 ms, 2 V square pulse, played by channel 2.
t = np.arange(W.sampling_rate) / W.sampling_rate
W.load_waveform(1, 4 * np.sin(2 * np.pi * 10 * t), [1, 3])
W.load_waveform(2, np.full(int(0.02 * W.sampling_rate), 2.0), 2)

# Trigger from software: one channel number, or several as a list. Channels
# in the same call start on the same sample.
W.trigger([1, 2, 3])
time.sleep(1.5)

# Each channel's waveform is its current_waveform (0 for none). Channel 3
# now plays the square pulse, from its next start.
W.current_waveform[3] = 2

# Channel settings are lists indexed by channel number (index 0 is unused),
# and setting an element programs the device at once.
W.loop_mode[1] = True        # Loop channel 1's waveform...
W.loop_duration[1] = 3       # ...for 3 seconds after each trigger
W.trigger(1)
time.sleep(4)

W.loop_duration[1] = 0       # 0 loops until stopped
W.trigger(1)
time.sleep(1)
W.stop(1)                    # The output returns to 0 V

# TTL triggers. By default, a rising edge on trigger channel 1 plays all four
# channels (those with a waveform). Here output channel 2 is moved to trigger
# channel 2, which is put in gated mode: a TTL plays it for as long as the TTL
# is high. As in Pulse Pal, trigger modes belong to the trigger channels.
W.link_trigger_channel1[2] = False
W.link_trigger_channel2[2] = True
W.trigger_mode[2] = "Gated"
W.loop_mode[2] = True        # With a loop duration of 0: plays while the TTL is high

# Other trigger modes: in "Normal" mode, channels that are playing ignore a
# TTL; "Toggle" stops them, and "Master" restarts them.
W.trigger_mode[1] = "Toggle"

# Param sync: a trigger channel in "Param Sync" mode starts nothing. Waveforms
# loaded (and current_waveform set) while it is in that mode wait for its next
# rising edge, and channels play the old ones until then, so each trial's
# waveform can be loaded during the trial before. Wire the trial-start TTL to
# both trigger channels to start the channels on the same edge.
W.trigger_mode[2] = "Param Sync"
W.load_waveform(1, 2 * np.sin(2 * np.pi * 20 * t))   # Waits for trigger channel 2's next rising edge
W.trigger_mode[2] = "Gated"                          # Leaving param sync mode makes it current at once

# Hold a channel at a fixed voltage until it is triggered or stopped
W.set_fixed_voltage(3, 1.5)

# Playback state, e.g. to check for underruns (blocks of samples the microSD
# card did not deliver in time) after an experiment
print(W.status())

# Closing the port stops playback, as on Pulse Pal. The device keeps its
# settings and waveforms, so TTL triggers still play them.
W.close()
