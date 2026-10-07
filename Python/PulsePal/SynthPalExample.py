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

# Example usage of Synth Pal's Python interface. Synth Pal is alternative
# firmware that makes a Pulse Pal 3 a four channel waveform synthesizer: load
# it from /Firmware/SynthPal first. Each section below is a self-contained
# snippet, meant to be read alongside the docstrings in SynthPal.py.

import time

from SynthPal import SynthPalDevice

# Connect. Replace "COM5" with the device's USB serial port name, which can
# be found with SynthPalDevice.serialportlist(). Connecting stops playback and
# programs the default settings.
S = SynthPalDevice("COM5")
print(f"Synth Pal firmware v{S.info.firmware_version}")

# One frequency applies to all four channels, from 1 Hz to 20 kHz in steps of
# 0.01 Hz. The device plays it exactly, with a whole number of samples per
# cycle (a multiple of 4) at a sampling rate of up to 100 kHz.
S.frequency = 440
print(f"{S.samples_per_cycle} samples per cycle at {S.sampling_rate:.0f} Hz")

# Each output channel has its own waveform, amplitude (peak to peak) and
# resting voltage (the voltage between playbacks, and the waveform's mean).
# Channel settings are lists indexed by channel number (index 0 is unused),
# and setting an element programs the device at once.
S.waveform = ["Sine", "Triangle", "Square", "Sawtooth"]
S.amplitude[1] = 4           # Swings from -2 V to 2 V
S.resting_voltage[2] = 2.5   # Swings from 0 V to 5 V around 2.5 V
S.amplitude[2] = 5

# The waveform must stay within -10 V to 10 V. To raise an amplitude beyond
# what the resting voltage allows, change the resting voltage first.
S.resting_voltage[3] = 0
S.amplitude[3] = 20          # -10 V to 10 V

# Play from software. Channels in the same call start on the same sample, and
# play for their play duration (1 second by default).
S.play([1, 2, 3, 4])
time.sleep(1.5)

S.play_duration[1] = 0.25    # Seconds
S.play(1)
time.sleep(0.5)

S.play_duration[1] = 0       # 0 plays until stopped
S.play(1)
time.sleep(1)
S.frequency = 880            # Takes effect during playback
time.sleep(1)
S.stop(1)                    # The output returns to its resting voltage

# TTL triggers. By default, a rising edge on trigger channel 1 plays all four
# channels. Here channel 4 is moved to trigger channel 2, which is put in
# gated mode: with a play duration of 0, channel 4 plays for as long as the
# TTL is high.
S.link_trigger_channel1[4] = False
S.link_trigger_channel2[4] = True
S.trigger_mode[2] = "Gated"
S.play_duration[4] = 0

# The other trigger mode: "Toggle" starts idle channels and stops playing
# ones. In "Normal" mode, channels that are playing ignore a trigger.
S.trigger_mode[1] = "Toggle"

# Playback state, and the output range the device chose for each channel
print(S.status())

# Closing the port leaves the device as it is, so TTL triggers keep playing
# the channels.
S.close()
