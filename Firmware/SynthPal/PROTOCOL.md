# Synth Pal serial protocol

Synth Pal is alternative firmware for Pulse Pal 3 hardware. Each output channel plays a sine,
triangle, square or sawtooth wave when it is triggered, with its own amplitude, resting voltage
and play duration, at one frequency shared by all four channels. This page is the reference for
its USB serial protocol, as of Synth Pal firmware v1.

| Client | Location |
|---|---|
| Python class | `/Python/PulsePal/SynthPal.py` |
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
   so. The Pulse Pal and Wave Pal clients do the same for a Synth Pal.
2. Op 78 ('N'): hardware properties and limits.
3. Op 89: the client's name, "PYTHON" or "MATLAB", shown as "PYTHON Connected".
4. Op 88 ('X') with all four channel bits, then the default settings: ops 70, 87, 86, 65, 68,
   84 and 73. The resting voltages (op 86) go before the amplitudes (op 65): 0 V is valid with
   any amplitude the device may hold, and the default amplitude is then valid too.

When they close, they send op 81, which puts "Synth Pal v3.0" back on the screen.

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
| 65 | `A` | Set amplitudes | 4 uint32, one per output channel: peak to peak, in µV, 0 to 20000000. See [Levels](#levels) | 1 / 0 |
| 86 | `V` | Set resting voltages | 4 int32, one per output channel: in µV, -10000000 to 10000000. See [Levels](#levels) | 1 / 0 |
| 68 | `D` | Set play durations | 4 uint32, one per output channel: in µs, 0 to 3600000000. 0 plays until stopped | 1 / 0 |
| 84 | `T` | Set trigger modes | 2 bytes, one per trigger channel, see [Triggers](#triggers) | 1 / 0 |
| 73 | `I` | Set trigger links | 8 bytes: trigger channel 1's links to output channels 1-4, then trigger channel 2's. Each is 1 (linked) or 0 | 1 / 0 |
| 80 | `P` | Play (soft trigger) | Channel bits (uint8) | none |
| 88 | `X` | Stop | Channel bits (uint8) | none |
| 71 | `G` | Get status | none | Playing channel bits (uint8), samples per cycle (uint32), the output range of channels 1-4 (4 uint8, see [Output ranges](#output-ranges)), longest sample clock interrupt since the previous op 71, in nanoseconds (uint32), late output updates since the previous op 71 (uint32, see [Timing](#timing)) |
| 89 | `Y` | Set the client name | 6 characters, shown on the top screen as "NAME Connected" | none |
| 81 | `Q` | Disconnect: show the device's own name on the top screen again | none | none |
| 99 | `c` | Set the screen saver | State (uint8: 0 off, 1 on), timeout in seconds (uint16, 1-65535) | 1 / 0 |
| 90 | `Z` | Playback checksums (testing) | none | For channels 1-4: samples played since the channel last started (4 uint32), then the sum of their DAC codes, modulo 2^32 (4 uint32) |

Ops 72, 81, 89 and 99 are Pulse Pal's ops of the same numbers. Unlike Pulse Pal's op 81, Synth
Pal's does not stop playback: a device left playing, or waiting for TTL triggers, carries on
after the client closes. Op 99 works as in Pulse Pal firmware (see its
[screen saver](../PROTOCOL.md#screen-saver) notes), with the settings at the same EEPROM
addresses, so they carry over when a device changes firmware.

Op 90 is for testing, like Wave Pal's op 90: `/Python/PulsePal/tests/synthpal_hardware_test.py`
and `/MATLAB/tests/testSynthPalDevice.m` use it to check the samples a device played.

Every setting takes effect at once, also during playback. A new frequency keeps each playing
channel's place in its cycle and the time it has left to play. A new play duration counts from
the channel's trigger: a channel that has already played longer stops on its next sample.

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
| 0 | Sine | Starts at the resting voltage, rising. Peak at a quarter cycle, trough at three quarters |
| 1 | Triangle | Starts at the resting voltage, rising. Peak at a quarter cycle, trough at three quarters |
| 2 | Square | High for the first half cycle, low for the second |
| 3 | Sawtooth | Rises in equal steps from its lowest voltage, at the first sample, to its highest, at the last, then falls back |

A channel that stops, at the end of its play duration or otherwise, returns to its resting
voltage on the next sample, part way through a cycle if need be.

## Levels

A channel's waveform swings half its amplitude above and below its resting voltage. It outputs
the resting voltage while it is idle, and the resting voltage is the waveform's mean. The whole
waveform must stay within -10 V to 10 V:

```
2 * |resting voltage| + amplitude <= 20 V
```

Op 65 checks the new amplitudes against the resting voltages the device holds, and op 86 the new
resting voltages against its amplitudes, and either replies 0 if any channel would go beyond.
So to raise an amplitude beyond what a channel's resting voltage allows, send the resting
voltage first.

Each sample is the resting voltage's DAC code plus an offset rounded half away from zero, so
samples the same distance above and below the resting voltage are the same number of codes from
it: when the resting voltage falls exactly on a DAC code (as 0 V does in the bipolar ranges), the
mean of any whole number of cycles is exactly that code. The top of a range is one DAC step above
the DAC's highest code, so a waveform that reaches it stops one step short.

### Output ranges

The DAC is an AD5754R with its internal 2.5 V reference, and each output channel has its own
output range. The device chooses it from the channel's amplitude and resting voltage: the first
range in this order that holds the whole waveform, which gives the finest steps.

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

## Triggers

Each trigger channel has a trigger mode (op 84). These are Pulse Pal's trigger modes, with the
same codes. An edge on a trigger channel acts on the output channels linked to it (op 73):

| Mode | Name | Rising edge | Falling edge |
|---|---|---|---|
| 0 | Normal | Starts idle channels. Channels that are playing ignore it | Nothing |
| 1 | Toggle | Starts idle channels, and stops channels that are playing | Nothing |
| 2 | Gated | Starts idle channels. Channels that are playing ignore it | Stops the channels, unless the other trigger channel is also gated, linked to them, and still high |

With a play duration of 0, a channel in gated mode plays for exactly as long as the TTL is high.
A soft trigger (op 80) starts idle channels, and channels that are playing ignore it, as in Pulse
Pal firmware. Op 88 stops channels.

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
to peak around a resting voltage of 0 V, played for 1 second. Both trigger channels in normal
mode, and output channels 1-4 linked to trigger channel 1 and not to trigger channel 2. The
Python and MATLAB classes program the same defaults when they connect.
