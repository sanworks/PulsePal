# Pulse Pal serial protocol

This is the reference for the USB serial protocol between a Pulse Pal and its client
software, as of firmware v22. It applies to Pulse Pal 2 and Pulse Pal 3 except where noted.

Four clients use this protocol, and all of them must agree with the firmware:

| Client | Location |
|---|---|
| Python class | `/Python/PulsePal/PulsePal.py` |
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
   Wave Pal firmware (`/Firmware/WavePal`) replies `87` ('W') instead, and the Python, MATLAB
   and C++ classes then say that the device needs Pulse Pal firmware.
2. Op 94, on firmware v22 and newer: hardware version, timer period, and custom train limits.
3. Op 89, to show the client's name on the device's screen (Python and MATLAB). The C++
   class leaves this to the program, which calls `setClientIDString()`.
4. On Pulse Pal 3: set both trigger modes to normal, with op 74 (Python) or op 91 (MATLAB and
   C++), so that a device left in [param sync mode](#param-sync-mode-pulse-pal-3-trigger-mode-3)
   runs the next step instead of storing it.
5. Op 92, to program the client's default parameters.

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
| 81 | Disconnect: stop all channels and restore the device's own screen title | none | none |
| 82 | Set continuous loop mode | Channel (1-4), state (0 or 1) | 1 / 0 |
| 85 | Send the current settings file | none | 178 bytes: the file without its end marker |
| 86 | Write a pin (debugging) | Pin number, value | none |
| 87 | Read a pin (debugging) | Pin number | Pin state (1 byte) |
| 89 | Set the client name | 6 characters, shown as "NAME Connected" | none |
| 90 | Settings file operation | Operation (1 save, 2 load, 3 delete), name length, name characters | 1 / 0, after the file operation has finished |
| 91 | Program one parameter on all channels | Parameter code, then one value per output channel (4 values), or per trigger channel (2 values) for code 128 | 1 / 0 |
| 92 | Program all parameters | 182 bytes, grouped by parameter: each array holds one parameter for output channels 1-4. 8 uint32 arrays (times), 3 uint16 arrays (voltages), then byte arrays: biphasic, custom train ID, custom train target, custom train loop, continuous loop. Then 8 trigger link bytes, then 2 trigger mode bytes | 1 / 0 |
| 93 | Send all parameters | none | 178 bytes, in op 92 order without the continuous loop bytes |
| 94 | Send hardware info | none | Hardware version (byte), timer period in µs (uint32), number of custom trains (byte), maximum pulses per train (uint32) |
| 95 | Load a custom train | Train index (0 = train 1), pulse count (uint32), that many uint32 times, that many uint16 voltages | 1 / 0 |
| 96 | Set DAC calibration | Channel (0-3), zero-code offset (int16). Pulse Pal 3 keeps it through power cycles; Pulse Pal 2 does not | 1 / 0 |
| 97 | Format the microSD card (Pulse Pal 3; Pulse Pal 2 ignores it and does not reply) | none | Lines of ASCII status text; the last contains `!` and ends in `\r\n`. Then 1 / 0, once the default parameters have been reloaded, which can be well after the text. Read this byte before sending the next command |
| 98 | Stop channels | 1 byte, one bit per output channel | none |

## Parameter codes (ops 74 and 91)

| Code | Parameter | Value | Range |
|---|---|---|---|
| 1 | Biphasic pulse | 1 byte | 0 or 1 |
| 2 | Phase 1 voltage | uint16 | DAC code |
| 3 | Phase 2 voltage | uint16 | DAC code |
| 4 | Phase 1 duration | uint32 | cycles |
| 5 | Inter-phase interval | uint32 | cycles |
| 6 | Phase 2 duration | uint32 | cycles |
| 7 | Inter-pulse interval | uint32 | cycles |
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

In the Python class, a name's position in the parameter name list gives its code, so the list
must stay in this order.

### Param sync mode (Pulse Pal 3, trigger mode 3)

Param sync mode delays op 92 until a TTL edge, so that the parameters of the next trial can
be sent during the current one and applied the instant it starts.

**Storing a set.** While either trigger channel is in param sync mode, op 92 stores its
parameter set instead of programming it. The confirm byte still reports whether every value
was in range, because values are checked when they arrive. A later op 92 replaces the stored
set, so only the most recent one is ever loaded. An op 92 whose data does not all arrive is
not stored.

**Only op 92 is delayed.** Ops 73, 74 and 91 program the device immediately, in param sync
mode as in any other. In the clients, only `sync_to_device()` (Python), `syncToDevice()`
(MATLAB) and `syncAllParams()` (C++) store a set; `set_output_param()`,
`set_trigger_param()` and their MATLAB and C++ equivalents take effect at once.

**At the edge.** A rising edge on a trigger channel in param sync mode loads the stored set.
With nothing stored, it does nothing.

- An idle output channel takes its new parameters in the timer cycle the edge is detected,
  and moves to its new resting voltage.
- An output channel **playing a pulse train** finishes it on the parameters it started with,
  and takes the new ones in the cycle the train ends. So a train that crosses into the next
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
op 97 format.

The Python, MATLAB and C++ classes take both trigger channels out of param sync mode when
they connect, before programming their default parameters. Otherwise a device left in the
mode by an earlier session would store those defaults instead of running them.

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

A load (op 90 or the joystick menu) fails, and the device loads the default parameters
instead, if the file:

- is missing, or shorter than 179 bytes;
- lacks the end marker;
- or holds a value the device cannot play: a custom train ID above the device's number of
  custom trains, a biphasic, custom train target or custom train loop byte above 1, or a
  trigger mode the device does not have.

So a file saved on Pulse Pal 3 that uses custom train 3 or 4, or param sync mode, does not
load on Pulse Pal 2.

## Timeouts and error handling

- A read gives up if no further byte arrives for 100 ms. This limits the gap between bytes,
  not the length of a transfer, so a large custom pulse train is never cut short. Clients
  should still send each command in a single write, so that a pause in the client cannot
  split a command.
- On a timeout the device stops all playback, returns the outputs to their resting voltages,
  shows "COMM. FAILURE!", and waits for a joystick click. Until then it reads no commands and
  ignores triggers. After the click it loads the default parameters. A timeout usually means
  a faulty cable, hub or client.
- The interrupted op still sends its reply, if it has one, and that reply can be 1. A confirm
  byte therefore does not reveal a timeout.
- Replies are buffered and sent as one USB packet when the command finishes, so a reply means
  the command is done and the next command can follow at once. For example, a client can read
  the parameters (op 93) as soon as op 90 confirms a settings file load.

## Differences by firmware version

| Firmware | Notes |
|---|---|
| v22 | Current. Adds ops 91-98, and on Pulse Pal 3, custom trains 3 and 4 and param sync trigger mode |
| v21 | No ops 91-98. Clients program parameters with ops 73 and 74, and custom trains with ops 75 and 76 |

Clients read the firmware version from the handshake (op 72), and the device properties from
op 94.
