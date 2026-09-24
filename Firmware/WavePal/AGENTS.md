# Working on the Wave Pal firmware

Wave Pal is alternative firmware for Pulse Pal 3 hardware (Teensy 4.1): a four channel waveform
player, ported from the WavePlayer firmware of the Bpod Analog Output Module. Each output
channel plays one waveform of up to 1 million samples, streamed from the microSD card, when a
TTL edge, a USB command or the joystick menu triggers it. The USB protocol is in `PROTOCOL.md`
in this folder. Read this page before changing anything here.

## Where things are

The sketch is split into tabs. Arduino joins them into one file before compiling:
`WavePal.ino` first, then the rest alphabetically. All tabs share the globals declared in
`WavePal.ino`.

| Tab | Contents |
|---|---|
| `WavePal.ino` | Build configuration, pin map, named constants, global variables, `setup()`, `loop()` |
| `Playback.ino` | `handler()` (the sample clock interrupt), the trigger interrupts, starting and stopping channels. "How playback works" at the top explains the buffering |
| `Storage.ino` | The waveform data file, and `refillPlaybackBuffers()` |
| `USBOps.ino` | Commands from the PC (`processUSBCommands()`), `loadWaveform()`, comm failure handling |
| `Menu.ino` | Thumb joystick menu, with its map |
| `Display.ino` | Screen output and the splash screen |
| `HardwareIO.ino` | DAC writes, the output range, software reset |

`ArCOM.h` and `LiquidCrystal_U8G2` are copies of the files in `/Firmware/PulsePal3`. Keep them
in step: a fix to one usually belongs in both. `GFXData.h` holds the Sanworks logo and the Wave
Pal logo.

## Building

```bash
# Compile, and compare every compiled function with the last commit
python ../tools/build_check.py --sketch wavepal --compare HEAD
```

Wave Pal builds for Teensy 4.1 only (`teensy:avr:teensy41`), with U8g2 (verified with
v2.36.19). `PIN_MAP_VERSION` at the top of `WavePal.ino` selects the Pulse Pal 3 PCB, as in
Pulse Pal firmware: 1 (the default) for PCB v3.0.5 and newer, 0 for v3.0.4 and older. A binary
built for the wrong PCB drives the joystick button line as the DAC's SYNC output.

## Rules that are easy to break

1. **Only interrupts at `PLAYBACK_IRQ_PRIORITY`, or `loop()` with interrupts disabled, may use
   the DAC.** `handler()` and the trigger pin interrupts share that priority, so neither can
   interrupt the other's SPI transfer. `loop()` calls `startChannels()`, `stopChannels()`,
   `holdChannels()`, `triggerChannels()` and the DAC functions only between `noInterrupts()`
   and `interrupts()`. An SPI transfer interrupted by another one leaves the first waiting
   forever; this froze Pulse Pal devices in the field. The one exception is `setup()`, before
   the interrupts are attached.
2. **Keep the interrupts-off sections short**, and never call SdFat, the screen or ArCOM inside
   one: every interrupt, including the sample clock, waits for it. They do not nest:
   `interrupts()` ends every section, so do not call a function that disables interrupts from
   inside one.
3. **`loop()` must never wait for long.** It refills the playback buffers, so a wait of more
   than one buffer's duration (164 ms at 100 kHz) causes underruns. The joystick menu runs
   during playback and must not use delays. Long ops call `refillPlaybackBuffers()` between
   blocks, as `loadWaveform()` does.
4. **`handler()` must not read a playback buffer unless `bufferChunk[][]` says it holds the
   chunk it wants.** `refillPlaybackBuffers()` sets `CHUNK_NONE` before a read and the chunk
   after it, and only ever reads into the buffer that `handler()` is not playing. The
   hardware test (below) checks every sample against this.
5. **Do not write to the screen, wait, or use the microSD card in an interrupt.**
6. **Validate input from USB before it indexes an array.** Reply 0 if a value is out of range,
   and read the rest of the command's data so that it is not taken for the next command.
7. **The op codes, trigger mode values and range indices in `PROTOCOL.md` are fixed** once a
   client is released. Add new op codes rather than changing existing ones.
8. **Use ArCOM for USB, and only from `loop()`**, as in Pulse Pal firmware. See `ArCOM.h`.

## What runs in the playback interrupts

| Interrupt | Entry point | When |
|---|---|---|
| Sample clock | `handler()` | Every sample period while a channel plays. `startChannels()` starts it, and it stops itself when no channel is playing |
| Trigger channels | `trigger1ISR()`, `trigger2ISR()` | Every edge on the trigger inputs |

They call `startChannels()`, `stopChannels()`, `holdChannels()`, `triggerChannels()`,
`releaseGatedChannels()`, `fetchNextSample()` and the DAC functions. Shared with `loop()`:
the playback state (`playing`, `stopAfterWrite`, `playChunk`, ...), `bufferChunk`,
`nSamples`, the settings (`loopMode`, `loopDuration`, `triggerMode`, `TriggerAddress`) and
`dacValue`.

Measured on a Teensy 4.1 (op 71, `longest_interrupt_us`): `handler()` takes 1.6 µs with one
channel playing and 5.2 µs with four, against a 10 µs sample period at 100 kHz.

## Checks

Without a device:

```bash
cd /Python/PulsePal && uv run python tests/test_wavepal_protocol.py
```

With a Wave Pal on a USB port (about 70 s; `--quick` skips the 1M sample tests):

```bash
cd /Python/PulsePal && uv run python tests/wavepal_hardware_test.py COM3
```

It checks every sample played against the waveforms it loaded, using op 90, including
waveforms at the buffer size and one sample either side, loop wraps, restarts part way through
a streamed waveform, rate changes during playback, four channels streaming at 100 kHz, and a
waveform loaded while three channels stream. It also reports transfer speed, underruns and the
longest sample clock interrupt.

The MATLAB class has its own test, in the same way (about 20 s, verified with R2020b and
R2025b):

```bash
matlab -batch "addpath('MATLAB', 'MATLAB/tests'); testWavePalDevice('COM3')"
```

A change to the protocol needs both classes, `/Python/PulsePal/WavePal.py` and
`/MATLAB/@WavePalDevice/WavePalDevice.m`, updated to match.

Only a scope and a person can check the rest. After a change to playback, triggers or the
menu, check:

- The waveform on a scope, in each output range. 0 V between playbacks. The first sample lasts
  as long as the others.
- Trigger latency: from a TTL edge to the first sample, with all channels idle (a few µs), and
  with another channel playing (up to one sample period).
- TTL triggers in normal, master, toggle and gated modes, from both trigger channels. Gated
  mode with loop mode on plays for as long as the TTL is high. The trigger LEDs follow the
  TTL, and the output LEDs light while a channel plays.
- The joystick menu: scroll through the channels, device info, reboot and exit; play and stop
  a channel from its item, and see the item change back when the waveform ends. The splash
  screen shows the Wave Pal logo.
- Unplug the USB cable during a waveform load: the device shows "COMM. FAILURE!", and a click
  loads the default settings.

## Known limits

- A channel that starts while another plays starts on the next tick of the shared sample
  clock, so its onset can be up to one sample period late.
- Waveforms are not kept through a restart: the data file is created again at startup.
- The zero code calibration is applied in the -10 V to 10 V range only, where it was measured.
- Only built and tested with `PIN_MAP_VERSION 1`.
