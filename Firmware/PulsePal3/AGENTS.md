# Working on the Pulse Pal 3 firmware

This folder holds the current firmware, which builds for **both** Pulse Pal 2 (Arduino Due)
and Pulse Pal 3 (Teensy 4.1). `/Firmware/Old/` holds archived firmware that is no longer
developed. Read this page before changing anything here.

## Where things are

The sketch is split into tabs. Arduino joins them into one file before compiling, in this
order: `PulsePal3.ino` first, then the rest alphabetically. All tabs share the globals
declared in `PulsePal3.ino`.

| Tab | Contents |
|---|---|
| `PulsePal3.ino` | Build configuration, pin maps, named constants, global variables, `setup()`, `loop()` |
| `Playback.ino` | `handler()`, the timer callback that plays pulse trains. Start here for timing questions |
| `USBOps.ino` | Commands from the PC, `processUSBCommands()` |
| `Menu.ino` | Thumb joystick menu and the parameter value editor. The menu map is above `UpdateSettingsMenu()` |
| `SDSettings.ino` | Settings files on the microSD card. The file layout is above `SaveCurrentProgram2SD()` |
| `Display.ino` | Screen output and the splash screen |
| `HardwareIO.ino` | DAC writes, the hardware timer, fast digital I/O, software reset |

Supporting classes: `ArCOM` (serial data types), `LiquidCrystal_U8G2` (Pulse Pal 3 screen).

## Building

```bash
# Both hardware versions
python ../tools/build_check.py

# Both, and compare every compiled function with the last commit
python ../tools/build_check.py --compare HEAD

# Why did a function change?
python ../tools/build_check.py --compare HEAD --show handler
```

- The hardware version can be set on the command line (`-DHARDWARE_VERSION=2`), so builds
  do not need the source to be edited. Without it, the `#define` in `PulsePal3.ino` applies.
- Pulse Pal 3 needs the U8g2 library **with the modification noted at the top of
  `PulsePal3.ino`**. Pulse Pal 2 needs SdFat v1.1.4 and LiquidCrystal.
- `build_check.py` substitutes a do-nothing LiquidCrystal stub so the Pulse Pal 2 build can
  be checked without that library. **Never flash a binary built that way.**

Use the comparison whenever a change is meant to be behaviour-neutral. Comments, renames
and moving code between tabs leave every function identical. Register allocation and
literal pool offsets can shift in functions that call a function whose size changed, so
read the `--show` output before concluding that something really changed.

## Rules that are easy to break

1. **After `setup()`, only `handler()` may call `dacWrite()`.** Other code calls `setDAC()`,
   and the next timer tick writes it. An SPI transfer started in `loop()` and interrupted by
   another SPI transfer in the interrupt leaves `loop()` waiting forever. This bug froze the
   device in the field.
2. **`setDAC()` sets `DACFlags[channel]` before `DACFlag`.** In the other order, an interrupt
   landing between the two lines clears `DACFlag` and loses the update.
3. **Do not write to the screen, wait, or use the microSD card inside the timer interrupt.**
   The screen has the same nesting problem as the DAC. `handler()` sets `abortRequested`, and
   `loop()` shows the message.
4. **Op codes, parameter codes and the settings file layout are fixed.** See
   `/Firmware/PROTOCOL.md`. Installed copies of the clients depend on them. Add new op codes
   rather than changing existing ones.
5. **Validate anything that arrives over USB** before it indexes an array, and reply 0 if it
   is out of range. `paramValueBytes()`, `isValidOutputChannel()` and `validateOutputParams()`
   in `USBOps.ino` do this.
6. **Times are hardware timer cycles, not microseconds.** `SystemTime` counts ticks of
   `TIMER_PERIOD` (50 µs). The joystick time editor in `Menu.ino` currently assumes 50 µs in
   two places, so changing `TIMER_PERIOD` needs those fixed too.
7. **Keep the existing names.** The lead developer navigates this code by memory during
   support calls. Renaming variables or reformatting whole files costs more than it saves.

## What runs in the timer interrupt

`handler()` runs every 50 µs, and calls `killChannel()`, `setDAC()`, `dacWrite()`,
`mirrorAboutZero()`, `AbortAllPulseTrains()` and `digitalWriteDirect()`. `TC3_Handler()` is
its entry point on Pulse Pal 2.

`loop()` can be interrupted at any point; the interrupt cannot be interrupted by `loop()`.
So code in `loop()` that updates several shared variables can be seen half-updated by the
interrupt, and anything `loop()` shares with it should be updated in a safe order (rule 2 is
an example). Variables shared with the interrupt include `SystemTime`, `StimulatingState`,
`DACFlag`, `DACFlags`, `dacValue`, `SoftTriggerScheduled`, `abortRequested` and all of the
output channel parameters.

## Checklists

**Adding an output channel parameter** (the wire format makes this wide-reaching):

1. `PulsePal3.ino`: the global array, and a `ParamID` entry with the next free code.
2. `USBOps.ino`: ops 73, 74, 91, 92 (reading), 93 (sending), and `paramValueBytes()`.
3. `SDSettings.ino`: `SaveCurrentProgram2SD()`, `RestoreParametersFromSD()`, the layout
   comment, and `SETTINGS_FILE_N_PARAM_BYTES`. Changing the file layout invalidates saved
   files, so consider appending instead.
4. `Menu.ino`: the click handler, `RefreshActionMenu()`, the scroll wrap-around limits, and
   `ReturnUserValue()`.
5. `/Firmware/PROTOCOL.md`, and the Python, MATLAB and C++ clients.
6. Run `/Python/PulsePal/tests/test_protocol.py`.

**Adding a USB op:** add it to the `OpCode` enum with the next free number, add the case in
`processUSBCommands()`, validate its inputs, document it in `/Firmware/PROTOCOL.md`, and say
which clients use it.

## Testing

Only hardware can test playback timing, the screen and the joystick. After any change to
playback, the menu or the USB ops, ask the user to check:

- A pulse train on a scope: shape, duration, voltages.
- Trigger channels in normal, toggle and gated modes.
- The joystick menu: edit a parameter, save, load and erase a settings file.
- `stop()` during playback from Python, then trigger again.
- Custom trains, including trains 3 and 4 on Pulse Pal 3.

Known limits worth remembering: a settings file name of 12 characters plus `.pps` cannot be
listed by the menu, and `mirrorAboutZero()` maps a −10 V custom pulse's phase 2 to −10 V.
