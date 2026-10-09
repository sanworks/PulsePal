# Wave Pal serial protocol

Wave Pal is alternative firmware for Pulse Pal 3 hardware. It stores one sampled waveform per
output channel on the microSD card, and plays it when the channel is triggered. This page is
the reference for its USB serial protocol, as of Wave Pal firmware v1.

| Client | Location |
|---|---|
| Python class | `pulsepal.WavePalDevice`, in `/Python/PulsePal/pulsepal/wave_pal.py` |
| MATLAB class | `/MATLAB/@WavePalDevice/WavePalDevice.m` |

On the device, `processUSBCommands()` in `/Firmware/WavePal/USBOps.ino` executes commands, and
the `OpCode` enum in `/Firmware/WavePal/WavePal.ino` defines the codes.

**The numbers on this page are fixed** once a client has been released: installed clients use
these op codes and message layouts. New functionality gets a new op code.

## Message format

Every command from the PC is:

```
213, op code, op-specific data
```

This is the Pulse Pal framing: bytes that do not start with 213 are ignored. Op codes are the
ASCII letters of the matching WavePlayer ops (Bpod Analog Output Module), where one exists.

- Multi-byte values are little-endian.
- Samples and voltages are 16-bit DAC codes, mapped onto the current output range: code 0 is
  the bottom of the range and 65535 the top. 0 V is code 32768 in the bipolar ranges and 0 in
  the unipolar ones.
- Ops that take one output channel number it 1-4. Ops that take several use one bit per
  channel (bit 0 = channel 1); bits 4-7 must be 0.
- Times are in samples of the current sampling rate.

## Connecting

The Python and MATLAB classes connect in this order:

1. Op 72 (handshake). A Wave Pal replies `87` ('W'). A Pulse Pal replies `75` ('K') to the
   same op, and a Synth Pal (`/Firmware/SynthPal`) `83` ('S'), so each client can tell which
   firmware a device runs, and say so. The Pulse Pal and Synth Pal clients do the same the
   other way round.
2. Op 78 ('N'): hardware properties.
3. Op 89: the client's name, "PYTHON" or "MATLAB", shown as "PYTHON Connected".
4. Op 88 ('X') with all four channel bits, then the default settings: ops 83, 82, 79, 68,
   84 and 73.

When they close, they send op 81, which stops playback and puts "Wave Pal v3.0" back on the
screen.

Waveforms loaded by an earlier session stay on the device unless op 82 changes the output
range.

## Confirm bytes

Ops marked "1 / 0" reply with one byte: 1 if the command was executed, 0 if it was rejected
because a value was out of range. A rejected command changes nothing. Op 76 also replies 0 if
the waveform could not be written to the microSD card; that channel is then left empty.

## Op codes

| Op | Char | Name | Data after the op code | Reply |
|---|---|---|---|---|
| 72 | `H` | Handshake | none | `87` ('W'), then firmware version (uint32) |
| 78 | `N` | Hardware info | none | Hardware version (uint8), number of output channels (uint8), maximum samples per waveform (uint32), maximum sampling rate in Hz (uint32), samples per playback buffer (uint32), sample clock in Hz (uint32) |
| 83 | `S` | Set sampling rate | Rate in Hz (uint32), 1 to the maximum | 1 / 0 |
| 82 | `R` | Set output range | Range index (uint8), see [Output ranges](#output-ranges) | 1 / 0 |
| 76 | `L` | Load waveform | Channel (uint8, 1-4), sample count (uint32, 1 to the maximum), then that many samples (uint16 each) | 1 / 0, once the waveform is on the microSD card |
| 80 | `P` | Soft trigger: starts idle channels | Channel bits (uint8) | none |
| 88 | `X` | Stop | Channel bits (uint8) | none |
| 33 | `!` | Set fixed voltage | Channel bits (uint8), DAC code (uint16) | 1 / 0 |
| 79 | `O` | Set loop mode | 4 bytes, one per output channel: 0 off, 1 on | 1 / 0 |
| 68 | `D` | Set loop duration | 4 uint32, one per output channel: samples, 0 = loop until stopped | 1 / 0 |
| 84 | `T` | Set trigger modes | 2 bytes, one per trigger channel, see [Triggers](#triggers) | 1 / 0 |
| 73 | `I` | Set trigger links | 8 bytes: trigger channel 1's links to output channels 1-4, then trigger channel 2's. Each is 1 (linked) or 0 | 1 / 0 |
| 71 | `G` | Get status | none | Playing channel bits (uint8), samples loaded on channels 1-4 (4 uint32, 0 = empty), underruns on channels 1-4 (4 uint32, counted since startup), longest playback interrupt since the previous op 71, in nanoseconds (uint32) |
| 89 | `Y` | Set the client name | 6 characters, shown on the top screen as "NAME Connected" | none |
| 81 | `Q` | Disconnect: stop all channels, and show the device's own name on the top screen again | none | none |
| 90 | `Z` | Playback checksums (testing) | none | For channels 1-4: samples played since the channel last started (4 uint32), then the sum of their DAC codes, modulo 2^32 (4 uint32) |

Ops 72, 81 and 89 are Pulse Pal's ops of the same numbers, so that a client shows its name on the
screen in the same way. Like Pulse Pal's op 81, Wave Pal's stops every channel, as op 88 does, so
that closing a client stops its outputs on all three firmwares. The settings and waveforms stay:
TTL triggers still play them after the client closes.

Op 90 is for testing, like Pulse Pal's debugging ops 86 and 87: `/Python/PulsePal/tests/wavepal_hardware_test.py`
and `/MATLAB/tests/testWavePalDevice.m` use it to check every sample a device played against the waveform
they loaded.

Op 76 stops its channel before it reads the samples. Other channels keep playing, and are fed
from the microSD card between the blocks of the transfer.

Op 33 stops playback on its channels. The voltage holds until the channel plays or is stopped.

Op 83 takes effect at once, also during playback. Loop durations are counted in samples, so a
client that keeps them in seconds sends op 68 again after op 83, as the Python and MATLAB
classes do.

### Output ranges

The DAC is an AD5754R with its internal 2.5 V reference. The ranges are WavePlayer's, without its
12 V ranges, which need the Analog Output Module's external reference. So the indices differ
from WavePlayer's.

| Index | Range | 0 V code |
|---|---|---|
| 0 | 0 V to 5 V | 0 |
| 1 | 0 V to 10 V | 0 |
| 2 | -5 V to 5 V | 32768 |
| 3 | -10 V to 10 V (default) | 32768 |

Samples are DAC codes, so they mean a different voltage in another range. When op 82 changes
the range, the device stops all playback, sets every output to 0 V and **unloads every
waveform**, so that nothing plays at a voltage it was not made for. The client sends them
again, encoded for the new range. Op 82 with the range already in use changes nothing.

The zero code calibration that Pulse Pal firmware stores in EEPROM (Pulse Pal op 96) is applied
in the -10 V to 10 V range, the range it was measured in.

## Triggers

Each trigger channel has a trigger mode (op 84), as in Pulse Pal firmware, and modes 0-2 are Pulse
Pal's trigger modes, with the same codes. An edge on a trigger channel acts on the output channels
linked to it (op 73):

| Mode | Name | Rising edge | Falling edge |
|---|---|---|---|
| 0 | Normal | Starts idle channels. Channels that are playing ignore it | Nothing |
| 1 | Toggle | Starts idle channels, and stops channels that are playing | Nothing |
| 2 | Gated | Starts idle channels. Channels that are playing ignore it | Stops the channels, unless the other trigger channel is also gated, linked to them, and still high |
| 4 | Master | Starts idle channels, and restarts channels that are playing from their first sample | Nothing |

Mode 3 is Pulse Pal's param sync mode, which Wave Pal does not have: op 84 replies 0 to it. With
loop mode on and a loop duration of 0, a channel in gated mode plays for exactly as long as the TTL
is high.

A soft trigger (op 80) starts idle channels, and channels that are playing ignore it, as in Pulse
Pal firmware, whatever the trigger modes. A channel with no waveform ignores triggers. Op 88 stops
channels.

A channel stops when:

- its last sample has played and loop mode is off;
- in loop mode, when the loop duration has elapsed (a duration of 0 loops until stopped);
- op 88, op 33, op 82 (range change), op 76 on that channel, a toggle or gated trigger, or the
  joystick menu stops it.

A stopped channel outputs 0 V. After the last sample, the output returns to 0 V one sample
period later.

### Timing

All channels share one sample clock, which runs only while a channel is playing.

- **Starting from idle.** When no channel is playing, a trigger writes the first sample at
  once, a few microseconds after the edge, and starts the sample clock from that moment. The
  following samples come at exact sample intervals.
- **Starting while another channel plays.** The channel starts on the next tick of the running
  sample clock, so its onset can be up to one sample period late (10 µs at 100 kHz).
- **Rate.** The sample clock divides a 24 MHz clock (op 78 reports it) by a whole number, so the
  rate played is 24 MHz / round(24 MHz / rate). For example, 44100 Hz plays at 44117.6 Hz.
- **Trigger pulses** must be longer than a few microseconds. Edges are caught by interrupts,
  not by polling, so the pulse only has to outlast the interrupt latency.

## Storage and buffering

- Each waveform's first 16384 samples (op 78 reports the number) are kept in RAM from the
  moment it is loaded, so playback starts without waiting for the microSD card. A waveform no
  longer than that plays entirely from RAM.
- The rest is read from the card in blocks of the same size, while the previous block plays.
  Each channel has two buffers for this (`channel1BufferA` and `channel1BufferB` for channel 1,
  and so on).
- If a block is not read in time (an **underrun**), the output holds its last value until the
  block arrives, then continues where the waveform should be by then, so the waveform keeps its
  timing. Op 71 counts underruns per channel. Underruns are most likely while op 76 writes to a
  slow card during playback on other channels.
- The waveforms are kept in `/Wave_Pal/WaveData.wfm`, which the device creates at startup, with
  a 2 MiB region per channel. Loaded waveforms do not survive a restart.
- Wave Pal needs a FAT16 or FAT32 card (Pulse Pal's own format). It stops at startup with an
  error message on an exFAT card.

## Timeouts and error handling

As in Pulse Pal firmware (see `/Firmware/PROTOCOL.md`): a read gives up if no byte arrives for
100 ms. The device then stops all playback, shows "COMM. FAILURE!", and waits for a joystick
click; it then loads the default settings. A waveform whose transfer was cut short is left
unloaded. Replies are buffered and sent when the command finishes.

## Default settings

At startup and after a comm failure: 10 kHz sampling rate, -10 V to 10 V range, loop mode off,
loop durations 0, both trigger channels in normal mode, output channels 1-4 linked to trigger
channel 1 and not to trigger channel 2. The Python and MATLAB classes program the same defaults when they connect.

## Differences from WavePlayer

| WavePlayer | Wave Pal |
|---|---|
| Up to 64 or 128 waveforms, played on any channel | One waveform per output channel |
| Handshake 227, reply 228, no framing byte | Framing byte 213; op 72, reply 87 |
| Triggered over the Bpod UART, or by op `P` / `>` | Triggered by TTL on the trigger channels, op 80, or the joystick |
| Trigger mode for the whole device | Trigger mode per trigger channel, as in Pulse Pal, plus gated mode |
| Trigger profiles, Bpod events | Not implemented |
| `D`: loop duration required in loop mode | `D`: 0 loops until stopped |
| `S` sends the sample period (float32) | Op 83 sends the rate in Hz (uint32) |
| `X` stops every channel | Op 88 takes channel bits |
| Six output ranges, including 0-12 V and ±12 V | Four output ranges, without the 12 V ones, indexed 0-3 |
