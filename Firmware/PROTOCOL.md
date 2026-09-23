# Pulse Pal serial protocol

This is the reference for the USB serial protocol between a Pulse Pal device and its
client software. It describes firmware v22.

Four clients speak this protocol, and all of them must agree with the firmware:

| Client | Location |
|---|---|
| Python class | `/Python/PulsePal/PulsePal.py` |
| MATLAB class | `/MATLAB/@PulsePalDevice/PulsePalDevice.m` |
| MATLAB legacy functions | `/MATLAB/Legacy/` |
| C++ class | `/c++/PulsePal.cpp` |

The firmware side is `processUSBCommands()` in `/Firmware/PulsePal3/USBOps.ino`, and the
op codes and parameter codes are the `OpCode` and `ParamID` enums in
`/Firmware/PulsePal3/PulsePal3.ino`.

**The numbers on this page are fixed.** Op codes, parameter codes and message layouts are
used by installed copies of the clients, so they must never be renumbered or reordered.
New functionality gets a new op code.

## Message format

Every command from the PC is:

```
213 (OpMenuByte), op code, op-specific data
```

All multi-byte values are little-endian. Times are in hardware timer cycles: the cycle
period is 50 µs (20000 cycles per second), and op 94 reports it. Voltages are 16-bit DAC
codes, where 0 is −10 V, 32768 is 0 V and 65535 is +10 V.

## Confirm bytes

Ops marked "1 / 0" below reply with one byte:

- **1**: the command was executed.
- **0**: the command was rejected, because a channel number, parameter code, data length
  or value was out of range for the connected device.

When the firmware rejects a command whose length it knows, it reads and discards that
command's data, so the data is not mistaken for the next command. If the length is not
known (an unknown parameter code), it discards everything received until the port has
been quiet for 100 ms.

The MATLAB and Python classes raise an error when they receive 0.

## Op codes

| Op | Name | Data sent after the op code | Reply |
|---|---|---|---|
| 72 | Handshake | none | `75` ('K'), then firmware version (uint32) |
| 73 | Program all parameters (legacy) | Per channel 1-4: 8 uint32 times. Then per channel: 3 uint16 voltages. Then per channel: 4 bytes (biphasic, custom train ID, custom train target, custom train loop). Then 8 bytes linking triggers to channels. Then 2 trigger mode bytes | 1 / 0 |
| 74 | Program one parameter | Parameter code, channel (1-4, or 1-2 for trigger parameters), value (1, 2 or 4 bytes, see below) | 1 / 0 |
| 75 | Load custom train 1 (legacy) | Pulse count (uint32), that many uint32 times, that many uint16 voltages | 1 / 0 |
| 76 | Load custom train 2 (legacy) | As op 75 | 1 / 0 |
| 77 | Soft-trigger channels | 1 byte, one bit per output channel (bit 0 = channel 1) | none |
| 78 | Show a message on the screen | Length byte, then that many characters. A character of 254 moves to the second line | none |
| 79 | Set a fixed voltage | Channel (1-4), DAC code (uint16) | 1 / 0 |
| 80 | Stop all channels | none | none |
| 81 | Disconnect | none | none |
| 82 | Set continuous loop mode | Channel (1-4), state (0 or 1) | 1 / 0 |
| 85 | Send the settings file | none | 178 bytes, the settings file without its end marker |
| 86 | Write a pin (debugging) | Pin number, value | none |
| 87 | Read a pin (debugging) | Pin number | Pin state (1 byte) |
| 89 | Set the client name | 6 characters, shown as "NAME Connected" | none |
| 90 | Settings file operation | Operation (1 save, 2 load, 3 delete), name length, name characters | 1 / 0, sent after the file operation has finished |
| 91 | Program one parameter on all channels | Parameter code, then one value per output channel (4 values), or per trigger channel (2 values) for code 128 | 1 / 0 |
| 92 | Program all parameters | 182 bytes: 8 uint32 arrays of 4 (times), 3 uint16 arrays of 4 (voltages), then byte arrays of 4: biphasic, custom train ID, custom train target, custom train loop, continuous loop. Then 8 trigger link bytes, then 2 trigger mode bytes. The device reads them as one block | 1 / 0 |
| 93 | Send all parameters | none | 178 bytes, in the op 92 order but without the continuous loop bytes |
| 94 | Send hardware info | none | Hardware version (byte), timer period in µs (uint32), number of custom trains (byte), maximum pulses per train (uint32) |
| 95 | Load a custom train | Train index (0 = train 1), pulse count (uint32), that many uint32 times, that many uint16 voltages | 1 / 0 |
| 96 | Set DAC calibration | Channel (0-3), zero-code offset (int16) | 1 / 0 |
| 97 | Format the microSD card | none | Pulse Pal 3 only. Lines of ASCII status text; the last contains `!` and ends in `\r\n`. Then 1 / 0, sent after the default parameters have been reloaded, which can be some time after the text. Clients must read this byte before the next command |
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
| 128 | Trigger mode | 1 byte | 0 normal, 1 toggle, 2 pulse gated, 3 param sync (Pulse Pal 3 only). Addresses trigger channels 1-2 |

Parameter codes are also the order of the parameter name list in the Python class, where
a name's position gives its code.

### Param sync mode (Pulse Pal 3, trigger mode 3)

Param sync mode changes when op 92 takes effect, so that the parameters of the next trial can
be sent during the current one and applied the instant it starts.

- While either trigger channel is in param sync mode, op 92 stores its parameter set in the
  device and does not program it. The confirm byte still reports whether every value was in
  range, because the values are checked when they arrive.
- A rising edge on a trigger channel in param sync mode takes the stored set. An output channel
  that is idle takes its new parameters in the timer cycle the edge is detected, and goes to its
  new resting voltage.
- An output channel that is **playing a pulse train** at the edge finishes that train on the
  parameters it started with, and takes the new ones in the cycle the train ends. So a train
  that crosses into the next trial keeps one shape throughout, and the next trigger plays a
  whole train with the new parameters. A channel in continuous loop mode has no train end, and
  waits until something stops it.
- Trigger mode is not an output channel parameter and takes effect at the edge, so a stored set
  that leaves param sync mode does so straight away.
- Switching continuous loop mode off in a stored set does not stop a channel that is playing,
  unlike op 92 outside param sync mode. Nothing on this path interrupts a train in progress.
- A trigger channel in param sync mode starts and stops nothing. Its links to output channels
  are ignored. This includes the edge that loads a set moving the channel to another mode: that
  edge only loads the set, and the new mode applies from the next edge. To start a train on the
  same edge, send the TTL to the other trigger channel as well: both edges land in the same
  timer cycle, and the parameters are loaded first.
- The stored set is discarded when a later op 92 replaces it, so only the most recent set is
  ever loaded. It is also discarded when the last param sync channel leaves the mode, whether
  by op 73, 74 or 91, the joystick menu, loading a settings file, or the default parameters
  loaded after a comm failure or an op 97 format. So putting a channel back into param sync
  mode cannot load a set sent long before. An op 92 whose data does not all arrive is never
  stored. A rising edge with nothing stored does nothing.
- A set taken at an edge is copied aside, so a later op 92 can arrive while channels are still
  finishing their trains. If a second edge arrives while they are, it replaces what they are
  waiting for with the newer set.
- **Only op 92 is deferred.** Ops 73, 74 and 91 program the device immediately, in param sync
  mode as in any other. In the clients, that means only `sync_to_device()` (Python) and
  `syncToDevice()` (MATLAB) pre-load a parameter set; `set_output_param()`,
  `set_trigger_param()` and their MATLAB equivalents take effect at once.
- Leaving param sync mode therefore needs op 74 or 91. A trigger mode sent with op 92 does not
  take effect until a sync edge.
- Both clients send op 74 or 91 to clear param sync mode when they connect, before programming
  their default parameters, so a device left in the mode by an earlier session does not hold
  those defaults back.

## Settings file

Settings files live in the `Pulse_Pal` folder on the microSD card, and are 179 bytes:

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

Continuous loop mode is not saved. `default.pps` is written at startup with the default
parameters, and the joystick menu cannot overwrite or erase it.

A load (op 90 or the joystick menu) fails, and the device loads the default parameters, if the
file is missing, shorter than 179 bytes, lacks the end marker, or holds a value the device
cannot play: a custom train ID above the device's number of custom trains, a custom train
target, custom train loop or biphasic byte above 1, or a trigger mode the device does not
have. A file saved on Pulse Pal 3 with custom train 3 or 4, or with param sync mode, therefore
does not load on Pulse Pal 2.

## Differences by firmware version

| Firmware | Notes |
|---|---|
| v22 | Current. Adds ops 93-98, a second trigger-linked custom train pair on Pulse Pal 3 (4 custom trains), and param sync trigger mode on Pulse Pal 3 |
| v21 | No ops 91-98. Clients use ops 73 and 74 to program parameters, and ops 75 and 76 for custom trains |

Clients read the firmware version from the handshake, and the device properties from op 94.

## Timeouts and error handling

- Every read gives up if no further byte arrives for 100 ms. The timeout measures the gap
  between bytes, not the duration of a transfer, so a large custom pulse train is never cut
  short by it.
- On a timeout the device shows "COMM. FAILURE!", loads the default parameters, and waits
  for a joystick click. Its reply, if the op has one, is not sent. This is meant to be
  noticed: it usually means a faulty cable, hub or client, not a normal condition.
- The device never waits indefinitely, so a client that stops mid-message cannot hang it.
- Replies are buffered and sent as one USB packet when the command finishes. A reply therefore
  means the command is done, so clients do not need to wait before sending the next one. After
  loading a settings file (op 90), a client can read the parameters back (op 93) immediately.
- Clients should still send each command in a single write.
