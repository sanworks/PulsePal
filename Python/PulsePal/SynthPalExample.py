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
# snippet, meant to be read alongside the docstrings in pulsepal/synth_pal.py.

import time

from pulsepal import SynthPalDevice

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

# Each output channel has its own waveform, peak to peak voltage, mean
# voltage (the waveform's mean) and resting voltage (the voltage between
# playbacks). Channel settings are lists indexed by channel number (index 0
# is unused), and setting an element programs the device at once.
S.waveform = ["Sine", "Triangle", "Square", "Sawtooth"]
S.peak_to_peak[1] = 4        # Swings from -2 V to 2 V: 2 sin(2 pi f t)
S.mean_voltage[2] = 2.5      # Swings from 0 V to 5 V around 2.5 V...
S.peak_to_peak[2] = 5
S.resting_voltage[2] = -1    # ...and rests at -1 V between playbacks

# The waveform must stay within -10 V to 10 V. configure() sets several of a
# channel's settings at once, in an order the device accepts.
S.configure(3, mean_voltage=0, peak_to_peak=20)  # -10 V to 10 V

# Ramps fade a channel in from its resting voltage after each trigger, and
# back to it when it stops: the peak to peak voltage rises from 0, and the
# mean from the resting voltage to the mean voltage, in straight lines. They
# lengthen playback: channel 2 now plays for 0.1 + 1 + 0.2 seconds.
S.on_ramp_duration[2] = 0.1  # Seconds
S.off_ramp_duration[2] = 0.2

# A "Fixed Voltage" channel steps to its fixed voltage, -10 V to 10 V, for its
# play duration, then returns to its resting voltage.
S.configure(4, waveform="Fixed Voltage", fixed_voltage=-3)  # Steps from 0 V to -3 V when triggered

# Trigger from software: one channel number, or several as a list. Channels
# in the same call start on the same sample, and play for their play duration
# (1 second by default).
S.trigger([1, 2, 3, 4])
time.sleep(1.5)

S.play_duration[1] = 0.25    # Seconds
S.trigger(1)
time.sleep(0.5)

S.play_duration[1] = 0       # 0 plays until stopped
S.trigger(1)
time.sleep(1)
S.frequency = 880            # Takes effect during playback
time.sleep(1)
S.stop(1)                    # The output returns to its resting voltage

# TTL triggers. By default, a rising edge on trigger channel 1 plays all four
# channels. Here channel 4 is moved to trigger channel 2, which is put in
# gated mode: with a play duration of 0, channel 4 holds its fixed voltage
# for as long as the TTL is high.
S.link_trigger_channel1[4] = False
S.link_trigger_channel2[4] = True
S.trigger_mode[2] = "Gated"
S.play_duration[4] = 0

# The other trigger modes: "Toggle" starts idle channels and stops playing
# ones. In "Normal" mode, channels that are playing ignore a trigger.
S.trigger_mode[1] = "Toggle"

# "Param Sync": a rising edge loads the settings most recently sent with
# sync_to_device(), and starts nothing. In a batch block, assignments are kept
# here, and sent all at once when it ends. Here trigger channel 2 becomes the
# param sync channel, and the next trial's frequency and peak to peak voltage
# wait on the device for its next rising edge.
S.trigger_mode[2] = "Param Sync"  # Sent at once
with S.batch():
    S.frequency = 660
    S.peak_to_peak[1] = 2
# Stored: applied at the next edge

# Every setting, and the device's playback state, with the output range it
# chose for each channel
print(S)
print(S.status())

# Closing the port leaves the device as it is, so TTL triggers keep playing
# the channels.
S.close()
