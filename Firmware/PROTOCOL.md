# Pulse Pal serial protocol

This is the reference for the USB serial protocol between a Pulse Pal and its client
software, as of firmware v22. It applies to Pulse Pal 2 and Pulse Pal 3 except where noted.

Four clients use this protocol, and all of them must agree with the firmware:

| Client | Location |
|---|---|
| Python class | `pulsepal.PulsePalDevice`, in `/Python/PulsePal/pulsepal/pulse_pal.py` |
| MATLAB class | `/MATLAB/@PulsePalDevice/PulsePalDevice.m` |
| MATLAB legacy functions | `/MATLAB/Legacy/` |
| C++ class | `/c++/PulsePal.cpp` |

On the device, `processUSBCommands()` in `/Firmware/PulsePal3/USBOps.ino` executes commands,
and the `OpCode` and `ParamID` enums in `/Firmware/PulsePal3/PulsePal3.ino` define the codes.

**The numbers on this page are fixed.** Installed copies of the clients use these op codes,
parameter codes and message layouts, so they must never be renumbered or reordered. New
functionality gets a new op code.

## Message format

Every command from the PC is:

```
213 (OpMenuByte), op code, op-specific data
```

- Multi-byte values are little-endian.
- Times are in hardware timer cycles of 50 µs (20000 cycles per second). Op 94 reports the
  cycle period.
- Voltages are 16-bit DAC codes: 0 is −10 V, 32768 is 0 V and 65535 is +10 V.
- Output channels are numbered 1-4 and trigger channels 1-2, except in op 96 (0-3).

## Connecting

The Python, MATLAB and C++ classes connect in this order:

1. Op 72 (handshake). The reply is `75` ('K') and the firmware version. A device running
   Wave Pal firmware (`/Firmware/WavePal`) replies `87` ('W') instead, and one running Synth
   Pal firmware (`/Firmware/SynthPal`) `83` ('S'). The Python, MATLAB and C++ classes then
   say which firmware the device runs, and that it needs Pulse Pal firmware. They refuse
   firmware older than v21, and connect to v21 with a notice that v22 is available.
   A firmware version newer than the client knows is used, with a warning: new firmware only
   adds ops, so the client's ops work as before.
   The Python, MATLAB and C++ classes discard any bytes waiting in the port before op 72: the end
   of a reply that an earlier session did not read would be taken as the handshake. That does not
   cover a command that an earlier session sent just before it closed, if the device has not
   reached it yet (it may be redrawing its screen, which takes a few ms): the device answers it
   after the discard, just before op 72. So the classes read the reply to op 72 until no byte has
   arrived for 50 ms (at most 1 s), and take the last 5 bytes. This adds about 50 ms to
   connecting.
2. Op 94, on firmware v22 and newer: hardware version, timer period, and custom train limits.
3. Op 100, on firmware v22 and newer: the [circuit board revision](#circuit-board-revision).
4. Op 89, to show the client's name on the device's screen (Python and MATLAB). The C++
   class leaves this to the program, which calls `setClientIDString()`.
5. On Pulse Pal 3: set both trigger modes to normal, with op 74 (Python) or op 91 (MATLAB and
   C++), so that a device left in [param sync mode](#param-sync-mode-pulse-pal-3-trigger-mode-3)
   runs the next step instead of storing it.
6. Op 92, to program the client's default parameters.

## Confirm bytes

Ops marked "1 / 0" below reply with one byte:

- **1**: the command was executed.
- **0**: the command was rejected, because a channel number, parameter code, data length or
  value was out of range for the connected device.

The MATLAB and Python classes raise an error when they receive 0, and the C++ class returns
false.

When the firmware rejects a command, it discards the command's data so that the data is not
read as the next command. If it cannot tell how long the data is (an unknown parameter
code), it discards everything received until the port has been quiet for 100 ms.

## Op codes

Ops 73, 92 and 93 carry every parameter, in this order:

- Times: phase 1 duration, inter-phase interval, phase 2 duration, inter-pulse interval,
  burst duration, burst interval, pulse train duration, pulse train delay.
- Voltages: phase 1, phase 2, resting.
- Trigger links: 4 bytes for trigger channel 1 (one per output channel 1-4), then 4 for
  trigger channel 2. Each byte is 1 if that output channel is linked to that trigger channel.

| Op | Name | Data sent after the op code | Reply |
|---|---|---|---|
| 72 | Handshake | none | `75` ('K'), then firmware version (uint32) |
| 73 | Program all parameters (legacy) | For each output channel: 8 uint32 times. Then for each output channel: 3 uint16 voltages. Then for each output channel: 4 bytes (biphasic, custom train ID, custom train target, custom train loop). Then 8 trigger link bytes, then 2 trigger mode bytes | 1 / 0 |
| 74 | Program one parameter | Parameter code, channel (1-4, or 1-2 for trigger parameters), value (1, 2 or 4 bytes, see [Parameter codes](#parameter-codes-ops-74-and-91)) | 1 / 0 |
| 75 | Load custom train 1 (legacy) | Pulse count (uint32), that many uint32 times, that many uint16 voltages | 1 / 0 |
| 76 | Load custom train 2 (legacy) | As op 75 | 1 / 0 |
| 77 | Soft-trigger channels | 1 byte, one bit per output channel (bit 0 = channel 1) | none |
| 78 | Show a message on the screen | Length byte, then that many characters. A character of 254 moves to the second line | none |
| 79 | Set a fixed voltage | Channel (1-4), DAC code (uint16) | 1 / 0 |
| 80 | Stop all channels | none | none |
| 81 | Disconnect: stop all channels and restore the device's own screen title. Wave Pal and Synth Pal firmware do the same | none | none |
| 82 | Set continuous loop mode | Channel (1-4), state (0 or 1) | 1 / 0 |
| 85 | Send the current settings file | none | 178 bytes: the file without its end marker |
| 86 | Write a pin (debugging) | Pin number, value | none |
| 87 | Read a pin (debugging) | Pin number | Pin state (1 byte) |
| 89 | Set the client name | 6 characters, shown as "NAME Connected" | none |
| 90 | Settings file operation | Operation (1 save, 2 load, 3 delete), name length, name characters | 1 / 0 / 2, after the file operation has finished. 0 if the file could not be written, loaded or deleted (see [Settings file](#settings-file)). 2 (Pulse Pal 2 only): refused, and nothing done, because a channel is playing |
| 91 | Program one parameter on all channels | Parameter code, then one value per output channel (4 values), or per trigger channel (2 values) for code 128 | 1 / 0 |
| 92 | Program all parameters | 182 bytes, grouped by parameter: each array holds one parameter for output channels 1-4. 8 uint32 arrays (times), 3 uint16 arrays (voltages), then byte arrays: biphasic, custom train ID, custom train target, custom train loop, continuous loop. Then 8 trigger link bytes, then 2 trigger mode bytes | 1 / 0 |
| 93 | Send all parameters | none | 178 bytes, in op 92 order without the continuous loop bytes |
| 94 | Send hardware info | none | Hardware version (byte), timer period in µs (uint32), number of custom trains (byte), maximum pulses per train (uint32) |
| 95 | Load a custom train | Train index (0 = train 1), pulse count (uint32, 1 or more), that many uint32 times, that many uint16 voltages. See [Custom trains](#custom-trains) | 1 / 0 |
| 96 | Set DAC calibration | Channel (0-3), zero-code offset (int16). Pulse Pal 3 keeps it through power cycles; Pulse Pal 2 does not | 1 / 0 |
| 97 | Format the microSD card (Pulse Pal 3; Pulse Pal 2 reads the key and does nothing more) | The 4 ASCII characters `WIPE`, so that two stray bytes can never erase the card. With any other 4 bytes, the op does nothing and sends nothing | Lines of ASCII status text; the last contains `!` and ends in `\r\n`. Then 1 / 0, once the default parameters have been reloaded, which can be well after the text. Read this byte before sending the next command |
| 98 | Stop channels | 1 byte, one bit per output channel | none |
| 99 | Set the [screen saver](#screen-saver) | State (0 off, 1 on; Pulse Pal 2 accepts 0 only), timeout in seconds (uint16, 1-65535). Pulse Pal 3 keeps both in its EEPROM; a new device starts with the screen saver on and 1800 s. Used by the MATLAB and Python classes | 1 / 0 |
| 100 | Send the [circuit board revision](#circuit-board-revision) | none | Revision (byte, 0-31), read at startup. Pulse Pal 2 sends 0 |

### Soft triggers and stops

A soft trigger (op 77) starts a channel's pulse train in the next 50 µs cycle. A channel that is
already playing, or waiting out its pulse train delay, ignores it, as it ignores a trigger line
in normal mode. Soft triggers read before that cycle add up: two op 77s in one USB packet trigger
the channels of both. A stop (ops 80, 81 and 98) also cancels a soft trigger that the channel has
not started yet.

### Programming during playback

Parameters programmed during playback (ops 73, 74, 91, 92, a settings file load) take effect at
once, including in trains that are playing, except a train's duration: its end is set when it
starts, so a new pulse train duration applies from the next train. A new resting voltage goes to
an idle channel's output at once. A channel playing a pulse train is not interrupted: it moves to
the new resting voltage at its next transition to rest. A change that switches a channel's bursts
off between two bursts (a burst duration or interval of 0, or a custom train targeting pulses)
starts its pulses again when its next burst would have started. A custom train load (op 95) stops the
channels playing that train, outside param sync mode (see [Custom trains](#custom-trains)).

Transitions are scheduled in timer cycles, and a channel makes each one in the first cycle at
or after its time. A time that has already passed when it is scheduled (for example, a delay
shortened during the delay) is made late, in the next cycle, rather than missed.

### Custom trains

A custom train (op 95) lists pulse onset times and voltages. A channel whose custom train ID
names it plays it, with the channel's own phase durations. A train needs at least one pulse:
op 95 refuses an empty one, and a channel set to a train that holds none (one never loaded since
power-up) ends at once when it is triggered.

On Pulse Pal 3 each custom train has two memory blocks, as each Wave Pal waveform has: its
current block, which channels play, and another, which op 95 loads. A channel chooses its block
when it is triggered, and plays it to the end of its train.

- **Outside param sync mode**, op 95 stops the channels playing the train, and the train is
  empty while it loads, so a trigger meanwhile plays nothing. Once the train has arrived, it is
  current. Pulse Pal 2, which has one block per train, does the same.
- **In param sync mode**, channels go on playing the current block, and triggers still start it.
  Once the train has arrived, it waits for the next param sync edge, which makes it current (see
  [Param sync mode](#param-sync-mode-pulse-pal-3-trigger-mode-3)). A later op 95 for the same
  train before the edge replaces the stored one. A channel still playing the train from before
  the previous edge is stopped first, since its block is the one being loaded.
- A train whose data does not all arrive (a comm failure) is not stored: outside param sync
  mode it is left empty, and in param sync mode the current block plays on.

A biphasic custom pulse always plays whole, and a pulse whose time comes while it plays is
dropped. A pulse exactly one pulse length later plays, with no rest between the two. A dropped
pulse leaves its place in the train empty: the pulses after it play at their own times, and if
the train's last pulse is dropped, the train (or this pass of a looping train) ends when that
pulse would have ended. A monophasic pulse whose next pulse starts no later than its phase 1
would end is a sample of a custom waveform: it holds until the next pulse's time.

A train targeting bursts (custom train target 1) drops bursts the same way: a burst whose time
comes while the burst before plays is dropped, a burst starting exactly as the one before ends
follows it with no rest between them, and if the last burst is dropped, the train ends when that
burst would have ended.

### Screen saver

A Pulse Pal 3 with its screen saver on dims its screen once it has been left alone for the
timeout: no command, no rising edge on a trigger channel, and no joystick click or push. The
next of these brings the screen back, showing the menu as it was left. A joystick click or push
that wakes the screen does nothing else. The screen is written after the reply to the command
that woke it has been sent, and after the timer cycle that handled the trigger, so neither is
delayed.

Op 99 switches the screen saver on or off and sets the timeout; the joystick menu's Screen Saver
item switches it on or off. The device saves both to its EEPROM, but only once no channel is
playing: on a Teensy 4.1, interrupts are off while the EEPROM is written. That takes about
20 µs for each byte that changed, which could move an output edge by up to about 13 µs, and once
in about 2000 changes tens of milliseconds, while a flash sector is erased, which would stop
playback timing and delay a trigger. Change the setting before an experiment rather than during
one.

### Circuit board revision

A Pulse Pal 3 circuit board encodes its revision in binary on Teensy pins 18-22
(`pcbVersionMap`): a pin grounded on the board is a 1 bit, pin 18 the lowest. The firmware reads
them once, in `setup()`, with the pins' pull-ups on, and then leaves them high impedance, so no
current flows into the grounded pins. Op 100 returns the number, 0-31. A board that grounds none
of the pins reports 0, as does Pulse Pal 2, whose board encodes no revision. The Bpod state
machine encodes and reads its revision in the same way. Wave Pal and Synth Pal firmware have the
same op 100.

The Python and MATLAB classes read it when they connect, into `info.circuit_board_revision` /
`info.circuitBoardRevision` (0 on firmware v21, which has no op 100), and the C++ class returns
it from `getHardwareRevision()`. A script can copy it to its data, with the hardware version, to
record exactly which hardware ran an experiment.

## Parameter codes (ops 74 and 91)

| Code | Parameter | Value | Range |
|---|---|---|---|
| 1 | Biphasic pulse | 1 byte | 0 or 1 |
| 2 | Phase 1 voltage | uint16 | DAC code |
| 3 | Phase 2 voltage | uint16 | DAC code |
| 4 | Phase 1 duration | uint32 | 1 or more cycles |
| 5 | Inter-phase interval | uint32 | cycles |
| 6 | Phase 2 duration | uint32 | 1 or more cycles |
| 7 | Inter-pulse interval | uint32 | 1 or more cycles; 0 is allowed for biphasic pulses |
| 8 | Burst duration | uint32 | cycles |
| 9 | Burst interval | uint32 | cycles |
| 10 | Pulse train duration | uint32 | cycles |
| 11 | Pulse train delay | uint32 | cycles |
| 12 | Link to trigger channel 1 | 1 byte | 0 or 1 |
| 13 | Link to trigger channel 2 | 1 byte | 0 or 1 |
| 14 | Custom train ID | 1 byte | 0 (none) to the number of custom trains |
| 15 | Custom train target | 1 byte | 0 pulses, 1 bursts |
| 16 | Custom train loop | 1 byte | 0 or 1 |
| 17 | Resting voltage | uint16 | DAC code |
| 18 | Continuous loop mode | 1 byte | 0 or 1 |
| 128 | Trigger mode (trigger channels 1-2) | 1 byte | 0 normal, 1 toggle, 2 pulse gated, 3 param sync (Pulse Pal 3 only) |

The Python and MATLAB classes name these parameters (`phase1_voltage`, `phase1Voltage`, ...), and
give the byte parameters as names (trigger modes "Normal", "Toggle", "Gated", "Param Sync";
custom train targets "Pulses", "Bursts") or as true and false. They send the codes in this table.
Code 18 is `continuous_loop` / `continuousLoop` in the classes.

A phase of 0 cycles would never end, and an inter-pulse interval of 0 would stop a monophasic
train after its first pulse, so ops 73, 74, 91 and 92 and settings files refuse them: the reply
is 0, and the value is set to 1 cycle. A biphasic channel with no inter-pulse interval plays its
pulses back to back. The device checks each channel's parameters together, so to change such a
channel to monophasic, set its inter-pulse interval first.

The clients (MATLAB, Python, C++) and the joystick menu are stricter: see "Value limits" below.

### Value limits

The MATLAB, Python and C++ classes check every value against these limits, and refuse one outside
them before anything is sent. The joystick menu keeps to them too. The device itself accepts any
value that fits its data type, apart from the times of 0 cycles above, so a value outside these
limits can still reach it from older software or an old settings file; the classes read such a
value back as it is (op 93).

| Value | Limit |
|---|---|
| Voltages | -10 to 10 V, sent as the nearest DAC code (a halfway value to the even code) |
| Times | 0 to 9999.9999 s, rounded to the nearest 50 µs cycle (a halfway value to the even cycle). 9999.9999 s (199999998 cycles) is the longest time the joystick menu shows and edits: four digits before the point. The Python and MATLAB classes hold the rounded time, as the device plays it |
| Phase 1 and 2 durations, inter-pulse interval, pulse train duration | At least 2 cycles (100 µs). A trigger channel reads its input once per cycle, so a 1 cycle pulse from another Pulse Pal can fall between two reads; 2 cycles is the shortest pulse a trigger channel detects reliably |
| Custom pulse times, and a custom waveform's sampling period | Multiples of 2 cycles (100 µs), up to 9999.9999 s, each later than the one before. A time between two steps is refused, not rounded, so that a train plays as written |
| Pulses per custom train | 1 to 5000 on Pulse Pal 2, 1 to 10000 on Pulse Pal 3 (op 94 reports the maximum) |
| Custom train target "Bursts" (1) | Needs a burst duration above 0 on that channel. The classes check it whenever either is sent: a parameter assigned alone against the other as the device holds it, a whole set as a whole |
| Custom train ID | 0 to 2 on Pulse Pal 2, 0 to 4 on Pulse Pal 3 |
| Trigger mode | 0 to 2 on Pulse Pal 2; 3 (param sync) on Pulse Pal 3 only |
| Zero code calibration (op 96) | -0.1 to 0.1 V, on Pulse Pal 3 only: its EEPROM stores it |
| Settings file name (op 90) | 1 to 11 printable ASCII characters followed by `.pps`, 15 in all, so that the joystick menu can list the file |
| Screen saver (op 99) | Timeout 1 to 65535 s. On Pulse Pal 3 only |

In a burst, a pulse starts only if it ends before the burst does: its phase 1, or for a biphasic
pulse the whole pulse, so that the end of the burst never cuts off phase 2. Likewise, a biphasic
pulse starts only if it ends by the end of the pulse train (it may end in the train's last cycle).
A monophasic pulse still starts, and the end of the train cuts it short. Trains in continuous loop
mode, and custom trains that do not loop, do not end at the train duration, so this does not apply
to them.

### Param sync mode (Pulse Pal 3, trigger mode 3)

Param sync mode delays op 92 until a TTL edge, so that the parameters of the next trial can
be sent during the current one and applied the instant it starts.

**Storing a set.** While either trigger channel is in param sync mode, op 92 stores its
parameter set instead of programming it. The confirm byte still reports whether every value
was in range, because values are checked when they arrive. A later op 92 replaces the stored
set, so only the most recent one is ever loaded. An op 92 whose data does not all arrive is
not stored.

**Only ops 92 and 95 are delayed.** Ops 73, 74 and 91 program the device immediately, in param
sync mode as in any other. In the clients, only `sync_to_device()` (Python, also at the end of a
`batch()` block), `syncToDevice()` (MATLAB) and `syncAllParams()` (C++) store a set, and the
custom train loads store a train (see [Custom trains](#custom-trains)); assigning a parameter
with `auto_sync` / `autoSync` on, and the C++ class's methods that set one parameter, take effect
at once.

**At the edge.** A rising edge on a trigger channel in param sync mode loads the stored set,
and makes the custom trains stored since the last edge current. With nothing stored, it does
nothing.

- An idle output channel takes its new parameters in the timer cycle the edge is detected,
  and moves to its new resting voltage.
- An output channel **playing a pulse train** finishes it on the parameters, and the custom
  train memory block, it started with, and takes the new ones in the cycle the train ends. So a train that crosses into the next
  trial keeps one shape throughout, and the next trigger plays a whole train with the new
  parameters.
- A channel in continuous loop mode has no train end, so it waits until something stops it.
  A stored set that switches continuous loop mode off does not stop the channel, unlike op 92
  outside param sync mode: nothing on this path interrupts a train in progress.
- The set is copied aside at the edge, so a new op 92 can arrive while channels are still
  finishing their trains. A second edge before they finish replaces what they are waiting for
  with the newer set.
- Nothing else cancels a channel's wait. When the channel takes the set, it replaces
  anything programmed on that channel since the edge: by ops 73, 74 or 91, by op 92 outside
  param sync mode, by loading a settings file, or by the default parameters loaded after a
  comm failure.
- Trigger mode is not an output channel parameter, so it changes at the edge for both
  trigger channels.

**The param sync channel starts and stops nothing.** Its links to output channels are
ignored. This includes the edge that loads a set moving it to another trigger mode: that edge
only loads the set, and the new mode applies from the next edge. To start a train on the same
edge, send the TTL to the other trigger channel as well. Both edges land in the same timer
cycle, and the parameters are loaded first.

**Leaving param sync mode.** Use op 74 or 91. A trigger mode sent with op 92 is stored with
the rest of the set, and takes effect only at a sync edge. When the last trigger channel
leaves the mode, by any route, the stored set is discarded, so putting a channel back into
the mode cannot load a set sent long before. The routes are ops 73, 74 and 91, the joystick
menu, loading a settings file, and the default parameters loaded after a comm failure or an
op 97 format. Stored custom trains are not discarded but made current, as Wave Pal does with its
waveforms: a load replaces its train, and the clients keep no other.

The Python, MATLAB and C++ classes take both trigger channels out of param sync mode when
they connect, before programming their default parameters. Otherwise a device left in the
mode by an earlier session would store those defaults instead of running them.

## Default parameters

The device loads its default parameters at startup, after a comm failure and after op 97
formats its card, and writes them to `default.pps` at startup: on every output channel,
monophasic pulses of 5 V (DAC code 49152) lasting 2 cycles (0.1 ms), 20 cycles (1 ms) apart, for
a pulse train of 20000 cycles (1 s), with no delay and no bursts, resting at 0 V; for biphasic
pulses, an inter-phase interval of 2 cycles and a phase 2 of -5 V lasting 2 cycles; no custom
train, and continuous loop mode off. Every output channel is linked to trigger channel 1 and not
to trigger channel 2, and both trigger channels are in normal mode. The Python and MATLAB
classes program the same defaults when they connect (`set_default_params()` /
`setDefaultParams()`), so the device plays the same program whichever way it got its defaults.
The legacy MATLAB functions use the same values; the C++ class and the legacy Python classes keep
their own (1 ms pulses, 10 ms apart).

## Settings file

Settings files are 179 bytes, in the `Pulse_Pal` folder on the microSD card:

```
For each output channel 1-4 (42 bytes each):
  phase 1 duration, inter-phase interval, phase 2 duration, inter-pulse interval,
  burst duration, burst interval, pulse train duration, pulse train delay   uint32 each
  biphasic                                                                 uint8
  phase 1 voltage, phase 2 voltage, resting voltage                        uint16 each
  custom train ID, custom train target, custom train loop                  uint8 each
For each trigger channel 1-2 (5 bytes each):
  trigger mode, then 4 bytes linking the trigger to output channels 1-4    uint8 each
End marker: 252                                                            uint8
```

Continuous loop mode is not saved. The device writes `default.pps` with the default
parameters at startup, and the joystick menu cannot overwrite or erase it.

The device reads and checks the whole file before it loads anything. A load (op 90 or the
joystick menu) fails, and the device keeps its parameters and its current settings file, if the
file:

- is missing, or shorter than 179 bytes;
- lacks the end marker;
- or holds a value the device cannot play: a custom train ID above the device's number of
  custom trains, a biphasic, custom train target or custom train loop byte above 1, a phase or
  monophasic inter-pulse interval of 0, or a trigger mode the device does not have.

So a file saved on Pulse Pal 3 that uses custom train 3 or 4, or param sync mode, does not
load on Pulse Pal 2. A valid file is loaded at once, as op 92 loads a set outside param sync
mode; each channel keeps its continuous loop mode. Op 90 replies 0 to a failed load, to a save
the card did not take, and to the deletion of a file that does not exist. Deleting the current
settings file makes `default.pps` current.

On Pulse Pal 2 the microSD card shares the SPI bus with the DAC, and the timer interrupt that
plays pulse trains writes the DAC, so the device holds that interrupt off while it uses the card
(op 90, and the joystick menu's Save, Load and Erase Program items), for a few milliseconds. A
channel playing would pause, so op 90 replies 2, and does nothing, while a channel plays, in its
delay or train, or a soft trigger is about to start one. A trigger that arrives while the card
is in use is acted on when the operation ends, and a trigger pulse that ends before then is
missed. Pulse Pal 3's card has a bus of its own, and none of this applies to it.

## Timeouts and error handling

- A read gives up if no further byte arrives for 100 ms. This limits the gap between bytes,
  not the length of a transfer, so a large custom pulse train is never cut short. Clients
  should still send each command in a single write, so that a pause in the client cannot
  split a command.
- On a timeout the device stops all playback, returns the outputs to their resting voltages,
  shows "COMM. FAILURE!", and waits for a joystick click. Until then it reads no commands and
  ignores triggers. After the click it loads the default parameters, and discards the bytes
  it receives until the port has been quiet for 100 ms: the rest of the broken command, whose
  data could otherwise be read as commands. A timeout usually means a faulty cable, hub or
  client.
- The interrupted op still sends its reply, if it has one, and that reply can be 1. A confirm
  byte therefore does not reveal a timeout.
- Replies are buffered and sent as one USB packet when the command finishes, so a reply means
  the command is done and the next command can follow at once. For example, a client can read
  the parameters (op 93) as soon as op 90 confirms a settings file load.

## Differences by firmware version

| Firmware | Notes |
|---|---|
| v22 | Current. Adds ops 91-100, and on Pulse Pal 3, custom trains 3 and 4 and param sync trigger mode |
| v21 | No ops 91-100. Clients program parameters with ops 73 and 74, and custom trains with ops 75 and 76 |

Clients read the firmware version from the handshake (op 72), and the device properties from
op 94.
