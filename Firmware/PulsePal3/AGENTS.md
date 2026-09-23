# Working on the Pulse Pal firmware

This folder holds the current firmware (v22). One sketch builds for **both** Pulse Pal 2
(Arduino Due) and Pulse Pal 3 (Teensy 4.1). Read this page before changing anything here.
The USB protocol it implements is documented in `/Firmware/PROTOCOL.md`.

## Where things are

The sketch is split into tabs. Arduino joins them into one file before compiling:
`PulsePal3.ino` first, then the rest alphabetically. All tabs share the globals declared in
`PulsePal3.ino`, and can call each other's functions.

| Tab | Contents |
|---|---|
| `PulsePal3.ino` | Build configuration, pin maps, named constants, global variables, the output parameter table, `setup()`, `loop()` |
| `Playback.ino` | `handler()`, the timer callback that plays pulse trains. Start here for timing questions |
| `USBOps.ino` | Commands from the PC (`processUSBCommands()`), input validation, param sync mode |
| `Menu.ino` | Thumb joystick menu and the parameter value editor. The menu map is above `UpdateSettingsMenu()` |
| `SDSettings.ino` | Settings files on the microSD card. The file layout is above `SaveCurrentProgram2SD()` |
| `Display.ino` | Screen output and the splash screen |
| `HardwareIO.ino` | DAC writes, the hardware timer, fast digital I/O, software reset |

Supporting classes: `ArCOM.h` (USB serial reads and writes), `LiquidCrystal_U8G2` (Pulse Pal 3
screen).

## Building

```bash
# Both hardware versions
python ../tools/build_check.py

# Both, and compare every compiled function with the last commit
python ../tools/build_check.py --compare HEAD

# Why did a function change?
python ../tools/build_check.py --compare HEAD --show handler
```

`build_check.py` needs arduino-cli with the `teensy:avr` and `arduino:sam` cores; its
docstring lists the details.

- **Target.** Two macros at the top of `PulsePal3.ino` select the build: `HARDWARE_VERSION`
  (2 or 3) and, on Pulse Pal 3, `PIN_MAP_VERSION` (0 for PCB v3.0.4 and older, 1 for v3.0.5
  and newer). `build_check.py` sets `HARDWARE_VERSION` in a temporary copy of the sketch.
  A compiler flag (`-DHARDWARE_VERSION=2`) works only for Pulse Pal 2: the Teensy core has no
  `compiler.cpp.extra_flags`, so arduino-cli accepts the flag and silently drops it.
- **Libraries.** Pulse Pal 2 needs SdFat v2 (verified with v2.1.2 and v2.3.0) and
  LiquidCrystal. Pulse Pal 3 needs U8g2 (verified with v2.36.19), unmodified; its SdFat comes
  with the Teensy core. If U8g2 ever stops supporting the second SPI bus, an `#error` in
  `PulsePal3.ino` stops the build, because the screen would otherwise stay dark.
- **Never flash a Pulse Pal 2 binary from a default `build_check.py` run.** It links a
  do-nothing LiquidCrystal stub so the build can be checked without the library. Pass
  `--real-libraries` to build one that can be flashed.

Use `--compare` whenever a change is meant to leave behavior unchanged. Comments, renames and
moving code between tabs leave every function identical. Some differences are harmless: a
function that calls another whose size changed can show shifted register allocation or
literal pool offsets, and compiler-generated switch tables (`CSWTCH.nnn`) get renumbered. Read
the `--show` output before concluding that a function really changed.

## Rules that are easy to break

1. **After `setup()`, only `handler()` may call `dacWrite()`.** Other code calls `setDAC()`,
   and the next timer tick writes the value. An SPI transfer started in `loop()` and
   interrupted by another in the timer interrupt leaves `loop()` waiting forever; this froze
   devices in the field. The one exception is `loop()`'s comm failure handling, which calls
   `dacWrite()` between `stopHardwareTimer()` and `startHardwareTimer()`, when no interrupt
   can run.
2. **`setDAC()` sets `DACFlags[channel]` before `DACFlag`.** In the other order, an interrupt
   landing between the two lines clears `DACFlag` and loses the update.
3. **Do not write to the screen, wait, or use the microSD card inside the timer interrupt.**
   The screen has the same nesting problem as the DAC. For example, `handler()` sets
   `abortRequested`, and `loop()` shows the message.
4. **Op codes, parameter codes, message layouts and the settings file layout are fixed.**
   Installed copies of the clients depend on them. Add new op codes rather than changing
   existing ones.
5. **Validate input from USB and from the microSD card before it indexes an array.** Over
   USB, reply 0 if a value is out of range. `paramValueBytes()`, `isValidOutputChannel()` and
   `validateOutputParams()` in `USBOps.ino` do this. `validateParamBuffer()` applies the same
   rules to a parameter set still in `paramBuffer`, because op 92 must reply for a set that
   param sync mode loads later, in `handler()`, where nothing can be reported: change it and
   `validateOutputParams()` together. `RestoreParametersFromSD()` rejects a settings file that
   is missing, short, unterminated or fails `validateOutputParams()`.
6. **Anything except op 92 that sets `TriggerMode` must then call `updateParamSyncPending()`**
   (Pulse Pal 3). Otherwise a parameter set stored for param sync mode survives the channel
   leaving the mode, and loads at a later edge.
7. **Use ArCOM for USB, and only from `loop()`.** A read gives up after 100 ms without a new
   byte, returns zeros and sets `PPUSB.timedOut()`, which `loop()` checks once per pass.
   Replies are buffered until `loop()` calls `PPUSB.flush()`, so no reply leaves the device
   during an op. Array reads and writes are single block copies, which rely on both boards
   being little-endian, like the wire format. See `ArCOM.h`.
8. **Times are hardware timer cycles, not microseconds.** `SystemTime` counts ticks of
   `TIMER_PERIOD` (50 µs). The joystick time editor in `Menu.ino` assumes 50 µs in two places,
   so changing `TIMER_PERIOD` needs those fixed too.
9. **Keep the existing names.** The lead developer navigates this code from memory during
   support calls. Renaming variables or reformatting whole files costs more than it saves.

## What runs in the timer interrupt

`handler()` runs every 50 µs. `TC3_Handler()` is its entry point on Pulse Pal 2; on Pulse
Pal 3, `IntervalTimer` calls it directly. It calls `killChannel()`, `setDAC()`, `dacWrite()`,
`mirrorAboutZero()`, `AbortAllPulseTrains()`, `digitalReadDirect()` and
`digitalWriteDirect()`. On Pulse Pal 3 it also calls `startParamSync()` when a trigger
channel in param sync mode goes high, and `loadWaitingParamSyncChannels()` on each cycle while
an output channel is still finishing the train it was playing at that edge.

Param sync costs, measured on a Teensy 4.1 against the 50 µs cycle:

| Event | Cost |
|---|---|
| Sync edge, all four channels idle (worst case) | 0.74 µs |
| Sync edge, all four channels playing | 0.22 µs |
| A waiting channel taking its parameters later | 0.08 µs |
| Checking for waiting channels, every cycle | 0.007 µs |

The interrupt can run at any point in `loop()`, but `loop()` never runs inside the interrupt.
So when `loop()` updates several variables that the interrupt reads, the interrupt can see
them half-updated; update them in a safe order (rule 2 is an example). Shared variables
include `SystemTime`, `StimulatingState`, `DACFlag`, `DACFlags`, `dacValue`,
`SoftTriggerScheduled`, `abortRequested`, all output channel parameters and, on Pulse Pal 3,
`paramBuffer`, `paramSyncPending` and `paramSyncChannelsWaiting`.

## Checklists

**Adding an output channel parameter.** The wire format makes this wide-reaching.

1. `PulsePal3.ino`: the global array, a `ParamID` entry with the next free code, and a row in
   the `outputParams` table. Rows are in parameter code order; a `static_assert` checks the
   count.
2. `PulsePal3.ino`: to show it in the joystick menu, add its code to `menuActionParams`. The
   menu needs nothing else: labels, limits, units, storage and the monophasic skip all come
   from the table.
3. `USBOps.ino`: op 74 and op 91. `paramValueBytes()` reads the table, so it needs no change.
4. Ops 73, 92 and 93 and the settings file have fixed lengths that installed clients depend
   on (rule 4), so adding the parameter to them breaks those clients. Decide with the
   maintainer whether it goes in them, or is set with ops 74 and 91 only (continuous loop mode
   is already left out of op 93 and the settings file). If it goes in:
   - Op 92 reads the set into `paramBuffer`. That layout is described only by
     `loadChannelParamsFromBuffer()` and the walk in `validateParamBuffer()`; update both and
     `PARAM_BUFFER_N_BYTES`, following the wire order.
   - Op 93 is `sendCurrentParams()`; op 73 is in `processUSBCommands()`.
   - Settings file: `SaveCurrentProgram2SD()`, `RestoreParametersFromSD()`, the layout
     comment above them and `SETTINGS_FILE_N_PARAM_BYTES`. Appending after the end marker
     keeps existing files loadable.
5. `/Firmware/PROTOCOL.md`, and the Python, MATLAB and C++ clients.
6. Run `/Python/PulsePal/tests/test_protocol.py`.

**Adding a USB op.** Add it to the `OpCode` enum with the next free number, add its case in
`processUSBCommands()`, validate its inputs (rule 5), document it in `/Firmware/PROTOCOL.md`,
and say which clients use it.

## Testing on a device

Only a device can test playback timing, the screen and the joystick. After a change to
playback, the menu or the USB ops, ask the user to check:

- A pulse train on a scope: shape, duration, voltages.
- Trigger channels in normal, toggle and gated modes.
- Param sync mode (Pulse Pal 3): `sync_to_device()` during a train does not change the
  output; the next rising edge on the param sync channel updates idle channels only; and a
  channel that was playing at that edge finishes its train unchanged, then plays the next one
  with the new parameters.
- The joystick menu: edit a parameter; save, load and erase a settings file.
- `stop()` during playback from Python, then trigger again.
- Custom trains, including trains 3 and 4 on Pulse Pal 3.
- After a change to serial code: USB latency and throughput, with
  `/Python/PulsePal/tests/benchmark_serial.py`, before and after the change. Also unplug the
  USB cable during a pulse: the device shows "COMM. FAILURE!" and the outputs return to their
  resting voltage.

## Known limits

- The joystick menu cannot list a settings file whose name is 12 characters plus `.pps`.
- `mirrorAboutZero()` maps a −10 V custom pulse's phase 2 to +10 V (DAC code 65535), because
  the exact mirror of code 0 would be 65536, which is out of range.
