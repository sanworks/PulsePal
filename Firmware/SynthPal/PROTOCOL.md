# Synth Pal serial protocol

Synth Pal is alternative firmware for Pulse Pal 3 hardware. Each output channel plays a sine,
triangle, square or sawtooth wave, or steps to a fixed voltage, when it is triggered, with its own
amplitude, mean voltage, resting voltage, play duration, and on and off ramps, at one frequency
shared by all four channels. This page is the reference for its USB serial protocol, as of Synth
Pal firmware v1.

| Client | Location |
|---|---|
| Python class | `pulsepal.SynthPalDevice`, in `/Python/PulsePal/pulsepal/synth_pal.py` |
| MATLAB class | `/MATLAB/@SynthPalDevice/SynthPalDevice.m` |

On the device, `processUSBCommands()` in `/Firmware/SynthPal/USBOps.ino` executes commands, and
the `OpCode` enum in `/Firmware/SynthPal/SynthPal.ino` defines the codes.

**The numbers on this page are fixed** once a client has been released: installed clients use
these op codes and message layouts. New functionality gets a new op code.

## Message format

Every command from the PC is:

```
213, op code, op-specific data
```

This is the Pulse Pal framing: bytes that do not start with 213 are ignored. Ops 72, 81, 89 and
99 are Pulse Pal's ops of the same numbers; the others are ASCII letters, as in Wave Pal.

- Multi-byte values are little-endian.
- Frequencies are in hundredths of a Hz (centiHz), voltages in microvolts, and durations in
  microseconds.
- Ops that set an output channel setting take one value per output channel, for channels 1-4.
  Ops that take several channels at once use one bit per channel (bit 0 = channel 1); bits 4-7
  are ignored.

## Connecting

The Python and MATLAB classes connect in this order:

1. Op 72 (handshake). A Synth Pal replies `83` ('S'). A Pulse Pal replies `75` ('K') and a Wave
   Pal `87` ('W') to the same op, so each client can tell which firmware a device runs, and say
   so. The Pulse Pal and Wave Pal clients do the same for a Synth Pal. A firmware version newer
   than the client knows is used, with a warning: new firmware only adds ops. As for Pulse Pal
   (see its [connection steps](../PROTOCOL.md#connecting)), the clients discard the bytes waiting
   before op 72, then read its reply until the device has been quiet for 50 ms and take the last
   5 bytes, which skips a reply to a command an earlier session left queued on the device.
2. Op 78 ('N'): hardware properties and limits.
3. Op 89: the client's name, "PYTHON" or "MATLAB", shown as "PYTHON Connected".
4. Op 88 ('X') with all four channel bits, then the default settings: ops 70, 77, 65, 87, 86,
   68, 66, 69, 84 and 73. In this order each is valid whatever the device holds (see
   [Levels](#levels)): a mean voltage of 0 V (op 77) goes with any amplitude, the default
   amplitude of 5 V (op 65) then goes with any waveform, a sine wave (op 87) then goes with both,
   and a resting voltage (op 86) goes with any waveform. These ops apply at once also on a device left in
   [param sync mode](#param-sync-trigger-mode-3), and op 84 takes both trigger channels out of
   it, which discards a stored set.

When they close, they send op 81, which stops playback and puts "Synth Pal v3.0" back on the
screen.

## Confirm bytes

Ops marked "1 / 0" reply with one byte: 1 if the command was executed, 0 if it was rejected
because a value was out of range. A rejected command changes nothing. Its data is still read,
so that it is not taken for the next command.

## Op codes

| Op | Char | Name | Data after the op code | Reply |
|---|---|---|---|---|
| 72 | `H` | Handshake | none | `83` ('S'), then firmware version (uint32) |
| 78 | `N` | Hardware info | none | Hardware version (uint8), number of output channels (uint8), lowest frequency in centiHz (uint32), highest frequency in centiHz (uint32), highest sampling rate in Hz (uint32), sample clock's timer clock in Hz (uint32), longest play duration in µs (uint32) |
| 70 | `F` | Set frequency | Frequency in centiHz (uint32), 100 to 2000000 (1 Hz to 20 kHz) | 1 / 0, then samples per cycle (uint32). Always 5 bytes: after a 0, the samples per cycle of the frequency in use |
| 87 | `W` | Set waveforms | 4 bytes, one per output channel, see [Waveforms](#waveforms) | 1 / 0 |
| 65 | `A` | Set amplitudes | 4 int32, one per output channel, in µV: peak to peak, 0 to 20000000, or for a fixed voltage the voltage, -10000000 to 10000000. See [Levels](#levels) | 1 / 0 |
| 86 | `V` | Set resting voltages | 4 int32, one per output channel: in µV, -10000000 to 10000000. See [Levels](#levels) | 1 / 0 |
| 77 | `M` | Set mean voltages | 4 int32, one per output channel: in µV, -10000000 to 10000000. See [Levels](#levels) | 1 / 0 |
| 68 | `D` | Set play durations | 4 uint32, one per output channel: in µs, 0 to 3600000000. 0 plays until stopped. See [Ramps](#ramps) | 1 / 0 |
| 66 | `B` | Set on ramp durations (at the Beginning) | 4 uint32, one per output channel: in µs, 0 to 3600000000. 0 for no ramp. See [Ramps](#ramps) | 1 / 0 |
| 69 | `E` | Set off ramp durations (at the End) | 4 uint32, one per output channel: in µs, 0 to 3600000000. 0 for no ramp. See [Ramps](#ramps) | 1 / 0 |
| 84 | `T` | Set trigger modes | 2 bytes, one per trigger channel, see [Triggers](#triggers) | 1 / 0 |
| 85 | `U` | Set all settings: applied at once, or stored for a param sync edge (see [Param sync](#param-sync-trigger-mode-3)) | 114 bytes: frequency in centiHz (uint32), then for channels 1-4: waveforms (4 bytes), amplitudes, mean voltages and resting voltages (4 int32 each), play durations, on ramp durations and off ramp durations (4 uint32 each); then trigger links (8 bytes, as op 73) and trigger modes (2 bytes, as op 84). Each value as in the op that sets it alone | 1 / 0 |
| 82 | `R` | Get all settings: those the device plays now | none | 114 bytes, in op 85's layout. See [Reading the settings back](#reading-the-settings-back) |
| 73 | `I` | Set trigger links | 8 bytes: trigger channel 1's links to output channels 1-4, then trigger channel 2's. Each is 1 (linked) or 0 | 1 / 0 |
| 80 | `P` | Soft trigger: starts idle channels | Channel bits (uint8) | none |
| 88 | `X` | Stop | Channel bits (uint8) | none |
| 71 | `G` | Get status | none | Playing channel bits (uint8: a channel in its off ramp counts as playing, until it reaches its resting voltage), samples per cycle (uint32), the output range of channels 1-4 (4 uint8, see [Output ranges](#output-ranges)), longest sample clock interrupt since the previous op 71, in nanoseconds (uint32), late output updates since the previous op 71 (uint32, see [Timing](#timing)) |
| 89 | `Y` | Set the client name | 6 characters, shown on the top screen as "NAME Connected" | none |
| 81 | `Q` | Disconnect: stop all channels, and show the device's own name on the top screen again | none | none |
| 99 | `c` | Set the screen saver | State (uint8: 0 off, 1 on), timeout in seconds (uint16, 1-65535) | 1 / 0 |
| 90 | `Z` | Playback checksums (testing) | none | For channels 1-4: samples played since the channel last started (4 uint32), then the sum of their DAC codes, modulo 2^32 (4 uint32) |

Ops 72, 81, 89 and 99 are Pulse Pal's ops of the same numbers. Like Pulse Pal's op 81, Synth
Pal's stops every channel, as op 88 does (over their off ramps), so that closing a client stops
its outputs on all three firmwares. The settings stay: TTL triggers still play the channels after
the client closes. Op 99 works as in Pulse Pal firmware (see its
[screen saver](../PROTOCOL.md#screen-saver) notes), with the settings at the same EEPROM
addresses, so they carry over when a device changes firmware.

### Reading the settings back

Op 82 returns every setting in op 85's layout: the settings the device plays now, including
changes made with the joystick menu. The Python and MATLAB classes read it in
`sync_from_device()` / `syncFromDevice()`. A set that op 85 stored for a param sync edge is not
part of it until the edge loads it. After the edge, an output channel still finishing on its old
settings (see [Param sync](#param-sync-trigger-mode-3)) is reported with them until it takes the
new ones; the frequency and the trigger modes are the new ones at once.

The device holds one amplitude per channel (see [Levels](#levels)). The classes keep a channel's
peak to peak voltage and its fixed voltage apart, so they put a Fixed Voltage channel's amplitude
in its fixed voltage, and any other channel's in its peak to peak voltage. They keep their own
value of the other one, reduced if need be to suit the mean voltage read back.

Op 90 is for testing, like Wave Pal's op 90: `/Python/PulsePal/tests/synthpal_hardware_test.py`
and `/MATLAB/tests/testSynthPalDevice.m` use it to check the samples a device played. While a
channel plays, its count includes its next sample, which is computed one sample period before it
reaches the output. Once the channel has stopped, the count and the sum are of the samples that
reached the output, however it stopped: at the end of its play duration, or by op 88, a toggle or
gated trigger edge, or the menu. A frequency change during playback rescales the count to the new
sampling rate (it keeps the time left to play), so after one the count is no longer exact.

Every setting takes effect at once, also during playback. A new frequency keeps each playing
channel's place in its cycle, the time it has left to play and the level its ramp has reached. A
new play duration counts from the end of the channel's on ramp: a channel that has already played
longer stops on its next sample (and starts its off ramp). A new ramp duration applies to a ramp
under way from the level it has reached.

## Frequency and sampling

One frequency applies to all four output channels. Each cycle of a waveform is a fixed number of
samples, `samplesPerCycle`: the largest multiple of 4 whose sampling rate (`samplesPerCycle`
times the frequency) is at most 100 kHz. Because it is a whole number, every cycle is rendered
the same way, and because it is a multiple of 4, a sample falls on every vertical edge (square,
sawtooth), peak and trough (sine, triangle). Op 70 replies with it.

| Frequency | Samples per cycle | Sampling rate |
|---|---|---|
| 1 Hz | 100000 | 100 kHz |
| 100 Hz | 1000 | 100 kHz |
| 300 Hz | 332 | 99.6 kHz |
| 500 Hz | 200 | 100 kHz |
| 12.6 kHz | 4 | 50.4 kHz |
| 20 kHz | 4 | 80 kHz |

The sample clock counts a 24 MHz clock (op 78 reports it), so a sample period is a whole number
of its ticks (41.7 ns). Where the exact period is not, periods alternate between two lengths a
tick apart, so that every sample comes within one tick of its exact time. The frequency played
is exact, to the accuracy of the device's crystal.

Play durations are counted in samples: a duration is rounded to the nearest sample at the
sampling rate in use, and a nonzero duration lasts at least one sample.

## Waveforms

| Code | Name | One cycle, from the trigger |
|---|---|---|
| 0 | Sine | Starts at the mean voltage, rising. Peak at a quarter cycle, trough at three quarters |
| 1 | Triangle | Starts at the mean voltage, rising. Peak at a quarter cycle, trough at three quarters |
| 2 | Square | High for the first half cycle, low for the second |
| 3 | Sawtooth | Rises in equal steps from its lowest voltage, at the first sample, to its highest, at the last, then falls back |
| 4 | Fixed Voltage | Not periodic: steps to the amplitude, a voltage, and holds it for the play duration |

A channel without an off ramp that stops, at the end of its play duration or otherwise, returns
to its resting voltage on the next sample, part way through a cycle if need be.

A fixed voltage plays like the other waveforms: it starts on the sample clock, as they do (about
8 µs after a trigger when no channel plays), lasts its play duration in samples, and stops and
responds to the trigger modes in the same way. Its output is written to the DAC once, as it
starts, and once more as it stops. The frequency does not change it, except through the sampling
rate that its play duration is counted in.

## Levels

A channel's waveform swings half its amplitude above and below its mean voltage, which is the
waveform's mean. The whole waveform must stay within -10 V to 10 V:

```
2 * |mean voltage| + amplitude <= 20 V
```

The channel outputs its resting voltage while it is idle, anywhere within -10 V to 10 V, with
any waveform. The ramps lead from it to the waveform and back (see [Ramps](#ramps)); without
them, the output steps from the resting voltage to the waveform at a trigger, and back when it
stops.

A fixed voltage (waveform 4) reads the amplitude differently: the amplitude is the voltage the
output steps to, -10 V to 10 V, and may be negative. The mean voltage does not apply to it. Only
a fixed voltage takes a negative amplitude.

Op 65 checks the new amplitudes against the waveforms and mean voltages the device holds, op 77
the new mean voltages against its waveforms and amplitudes, and op 87 the new waveforms against
its amplitudes and mean voltages. Each replies 0 if any channel would break these rules. So to
raise an amplitude beyond what a channel's mean voltage allows, send the mean voltage first; and
to change a waveform, send an amplitude that suits both the old waveform and the new one first
(an amplitude of 0 goes with every waveform and mean voltage), or change the amplitude after the
waveform. For example, from a fixed voltage of -5 V to a sine wave of 4 V peak to peak: op 65
with 4 V, then op 87.

The Python and MATLAB classes keep a periodic waveform's peak to peak voltage and a fixed voltage
as two settings (`peak_to_peak` and `fixed_voltage`, `peakToPeak` and `fixedVoltage`), and send
whichever the channel's waveform uses as its amplitude. They work out an order of ops 65, 87 and
77 that the device accepts at each step: each channel first takes an amplitude that suits its
current and its new waveform and mean voltage (its current amplitude, else its new one, else 0 V,
which suits any), then the waveforms, the mean voltages and the new amplitudes follow. If the
device refuses a step, the classes start again from an amplitude of 0 V on every channel.

At full amplitude, each sample is the mean voltage's DAC code plus an offset rounded half away
from zero, so samples the same distance above and below the mean voltage are the same number of
codes from it: when the mean voltage falls exactly on a DAC code (as 0 V does in the bipolar
ranges), the mean of any whole number of cycles is exactly that code. A fixed voltage is rounded to its
nearest DAC code, as the resting voltage is. The top of a range is one DAC step above the DAC's
highest code, so a waveform that reaches it stops one step short.

### Output ranges

The DAC is an AD5754R with its internal 2.5 V reference, and each output channel has its own
output range. The device chooses it from the channel's levels: the first range in this order
that holds the whole waveform and the resting voltage, which gives the finest steps (the ramps
stay between the two). For a fixed voltage, the range holds the fixed voltage and the resting
voltage.

| Index | Range | Step |
|---|---|---|
| 0 | 0 V to 5 V | 76 µV |
| 1 | 0 V to 10 V | 153 µV |
| 2 | -5 V to 5 V | 153 µV |
| 3 | -10 V to 10 V | 305 µV |

Op 71 reports each channel's range. A change of range on a playing channel writes the range and
the channel's current sample together, so the output shows its old code in the new range only
for a fraction of a microsecond. The zero code calibration that Pulse Pal firmware stores in
EEPROM (Pulse Pal op 96) is applied in the -10 V to 10 V range, the range it was measured in.

## Ramps

Each channel can fade in after a trigger, and fade out when it stops. At an envelope e, from 0 to
1, the output is the resting voltage plus e times (the waveform at full amplitude minus the
resting voltage): the amplitude scales with e, and the waveform's mean moves in a straight line
from the resting voltage to the mean voltage. A fixed voltage ramps in a straight line from the
resting voltage to its voltage.

- **On ramp** (op 66), N samples: the envelope rises linearly, 0, 1/N, ... (N-1)/N. The first
  sample after the trigger is the resting voltage.
- **Play duration** (op 68), at full amplitude. It counts from the end of the on ramp.
- **Off ramp** (op 69), M samples: the envelope falls linearly, 1, (M-1)/M, ... 1/M, and the
  next sample is the resting voltage. It starts at the end of the play duration, or when the
  channel is stopped: op 88, a toggle or gated trigger edge, the menu, or a comm failure.

So the ramps lengthen playback: from a trigger to the resting voltage takes the on ramp, the play
duration and the off ramp, each rounded to whole samples as play durations are (see
[Frequency and sampling](#frequency-and-sampling)). With a play duration of 0, the channel plays
at full amplitude until it is stopped. A ramp of 0 (the default) means no ramp: the output steps.

The envelope never jumps:

- A channel stopped during its on ramp falls from the level it has reached, at the off ramp's
  rate (taking that fraction of the off ramp).
- A channel in its off ramp counts as stopping. A trigger (or op 80) starts it again: it rises
  from the level it has reached, at the on ramp's rate, without starting its waveform's cycle
  again, then plays its play duration again and its off ramp. With no on ramp, it returns to
  full amplitude at once.
- A change of a ramp's duration (op 66 or 69) applies to that ramp from the level it has reached.

## Triggers

Each trigger channel has a trigger mode (op 84). These are Pulse Pal's trigger modes, with the
same codes. An edge on a trigger channel acts on the output channels linked to it (op 73):

| Mode | Name | Rising edge | Falling edge |
|---|---|---|---|
| 0 | Normal | Starts idle channels. Channels that are playing ignore it | Nothing |
| 1 | Toggle | Starts idle channels, and stops channels that are playing | Nothing |
| 2 | Gated | Starts idle channels. Channels that are playing ignore it | Stops the channels, unless the other trigger channel is also gated, linked to them, and still high |
| 3 | Param Sync | Loads the settings op 85 stored, if any. Starts and stops nothing: its links are ignored | Nothing |

A channel in its off ramp counts as idle here: a rising edge starts it again (see
[Ramps](#ramps)). With a play duration of 0, a channel in gated mode plays for exactly as long as
the TTL is high, plus its off ramp.
A soft trigger (op 80) starts idle channels, and channels that are playing ignore it, as in Pulse
Pal firmware. Op 88 stops channels.

### Param sync (trigger mode 3)

Param sync mode, as in Pulse Pal 3 firmware (see its
[param sync](../PROTOCOL.md#param-sync-mode-pulse-pal-3-trigger-mode-3) notes), lets the settings
of the next trial be sent during the current one, and applied the instant it starts.

**Storing a set.** Op 85 carries every setting at once. While either trigger channel is in param
sync mode, the device stores the set instead of applying it. The confirm byte reports whether
every value was in range, and every channel's levels suit its waveform (see [Levels](#levels)):
the set is checked whole, so its settings may be in any order. A rejected set is not stored, and
a later op 85 replaces a stored set. Outside param sync mode, op 85 applies the set at once.

**Only op 85 is delayed.** The ops that set one setting (70, 87, 65, 77, 86, 68, 66, 69, 84, 73)
apply at once, in param sync mode as in any other. The Python and MATLAB classes send op 85 from
`sync_to_device()` / `syncToDevice()`, with their `auto_sync` / `autoSync` switch off, and the
Python class at the end of a `batch()` block.

**At the edge.** A rising edge on a trigger channel in param sync mode loads the stored set. With
nothing stored, it does nothing.

- The frequency and both trigger modes change at once: they are shared by all output channels.
- An output channel at its resting voltage takes its new settings at once: waveform, levels,
  play duration, ramps and trigger links.
- An output channel that is playing, off ramp included, finishes on the settings it started
  with, but at the new frequency, and takes its new settings the moment it reaches its resting
  voltage. So the next trigger plays the new settings. A second edge before then gives it the
  newer set.
- The set is copied aside at the edge, so op 85 can store the next set meanwhile.
- A trigger mode in the set applies from the next edge: the edge that loads it only loads it.

**The param sync channel starts and stops nothing.** Its links to output channels are ignored. To
start channels on the same edge, wire the TTL to the other trigger channel as well: the settings
load first, whichever trigger channel's interrupt runs first, so the channels it starts play the
new settings.

**Leaving param sync mode.** Use op 84 (op 85 in param sync mode is stored with the rest of the
set). When no trigger channel is left in the mode, by op 84, the menu, or the default settings
after a comm failure, the stored set is discarded, so putting a channel back into the mode cannot
load a set sent long before.

**Time at the edge.** Loading a set takes about 2 µs of interrupt time with all four channels at
rest, plus about 2.5 µs for each channel whose output range changes (12 µs measured with all four
changing range and a new frequency); a channel started by the same TTL starts that much later
than usual. While channels play, range changes are left to the sample clock, one channel per
tick, and an edge that changes the frequency takes about 3 µs, which can make one output update
late, as op 70 can (see [Timing](#timing)).

### Timing

All channels share one sample clock, which runs only while a channel is playing.

- **Starting from idle.** When no channel is playing, a trigger computes the first samples and
  starts the sample clock from that moment. The first samples reach the outputs a fixed time
  after the trigger, about 8 µs, and the following ones at exact sample intervals.
- **Starting while another channel plays.** The channel starts on the next tick of the running
  sample clock, so its onset can be up to one sample period late (10 µs at 100 kHz).
- **Output updates.** All outputs change together, 7.5 µs after each tick of the sample clock,
  however many channels change. An update whose DAC writes ran late (for example because the
  firmware's interrupts were held off by a frequency change during playback) is counted in op
  71's late updates, which are normally 0.
- **Trigger pulses** must be longer than a few microseconds. Edges are caught by interrupts, not
  by polling, so the pulse only has to outlast the interrupt latency.

## Timeouts and error handling

As in Pulse Pal firmware (see `/Firmware/PROTOCOL.md`): a read gives up if no byte arrives for
100 ms. The device then stops all playback, shows "COMM. FAILURE!", and waits for a joystick
click; it then loads the default settings. Replies are buffered and sent when the command
finishes.

## Default settings

At startup and after a comm failure: 100 Hz, and on every output channel a sine wave of 5 V peak
to peak around a mean voltage of 0 V, resting at 0 V, played for 1 second, with no ramps. Both trigger channels in normal
mode, and output channels 1-4 linked to trigger channel 1 and not to trigger channel 2. The
Python and MATLAB classes program the same defaults when they connect.
