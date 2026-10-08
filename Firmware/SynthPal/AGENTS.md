# Working on the Synth Pal firmware

Synth Pal is alternative firmware for Pulse Pal 3 hardware (Teensy 4.1): a four channel waveform
synthesizer. Each output channel plays a sine, triangle, square or sawtooth wave, or steps to a
fixed voltage, with its own amplitude, mean voltage, resting voltage, play duration, and linear
on and off ramps, when a TTL edge, a USB command or the joystick menu triggers it. The trigger
channels have Pulse Pal's trigger modes, param sync included. One
frequency, 1 Hz to 20 kHz in steps of 0.01 Hz, applies to all four. The USB protocol is in
`PROTOCOL.md` in this folder. Read this page before changing anything here.

## Where things are

The sketch is split into tabs. Arduino joins them into one file before compiling:
`SynthPal.ino` first, then the rest alphabetically. All tabs share the globals declared in
`SynthPal.ino`. A `static inline` function must come before its callers in that order (Arduino
generates prototypes only for the others): `nextSamplePeriodTicks()` is in `HardwareIO.ino` for
that reason.

| Tab | Contents |
|---|---|
| `SynthPal.ino` | Build configuration, pin map, named constants, global variables, `setup()`, `loop()` |
| `Playback.ino` | `handler()` (the sample clock interrupt), waveform synthesis, the trigger interrupts, starting and stopping channels. "How playback works" at the top explains the timing |
| `Settings.ino` | The frequency and the output channel settings: limits, output ranges, and handing new settings to `handler()` |
| `USBOps.ino` | Commands from the PC (`processUSBCommands()`), comm failure handling |
| `Menu.ino` | Thumb joystick menu, with its map, and the value editors |
| `Display.ino` | Screen output, the screen saver and the splash screen |
| `HardwareIO.ino` | DAC writes, output ranges, the sample clock's timer and its time base, software reset |

`ArCOM.h` and `LiquidCrystal_U8G2` are copies of the files in `/Firmware/PulsePal3`, as Wave
Pal's are. Keep them in step: a fix to one usually belongs in all three. `GFXData.h` holds the
Sanworks logo and the Synth Pal logo.

## Building

```bash
# Compile
python ../tools/build_check.py --sketch synthpal

# Compile, and compare every compiled function with the last commit
python ../tools/build_check.py --sketch synthpal --compare HEAD
```

Synth Pal builds for Teensy 4.1 only (`teensy:avr:teensy41`), with U8g2 (verified with
v2.36.19), at the default 600 MHz: the DAC timing counts CPU cycles, and a `static_assert`
stops the build at a CPU speed that is not a whole multiple of 24 MHz. `PIN_MAP_VERSION` at the
top of `SynthPal.ino` selects the Pulse Pal 3 PCB, as in Pulse Pal firmware: 1 (the default)
for PCB v3.0.5 and newer, 0 for v3.0.4 and older. A binary built for the wrong PCB drives the
joystick button line as the DAC's SYNC output. Synth Pal needs no microSD card.

## Rules that are easy to break

1. **Only interrupts at `PLAYBACK_IRQ_PRIORITY`, or `loop()` with interrupts disabled, may use
   the DAC.** `handler()` and the trigger pin interrupts share that priority, so neither can
   interrupt the other's SPI transfer. `loop()` calls `startChannels()`, `stopChannels()`,
   `processTriggerEdge()`, `takePendingOutput()` and the DAC functions only between
   `noInterrupts()` and `interrupts()`. An SPI transfer interrupted by another one leaves the
   first waiting forever; this froze Pulse Pal devices in the field. The one exception is
   `setup()`, before the interrupts are attached.
2. **Never change `activeOutput[]` from `loop()` while the sample clock runs.** It is what
   `handler()` plays, in the channel's current output range. Settings go through
   `updateChannelOutput()`, which leaves them in `pendingOutput[]` for `handler()` to take after
   a DAC update (one channel per tick), and takes them itself only while the clock is stopped. A
   code computed for one range and latched in another is a wrong voltage for a whole sample.
3. **Keep the interrupts-off sections short.** With four channels to write, `handler()` starts
   writing 0.95 µs after the tick and normally starts about 0.7 µs after it, so a section longer
   than about 0.25 µs that delays it makes that tick's update late. Never call the screen or
   ArCOM inside one. They do not nest: `interrupts()` ends every section, so do not call a
   function that disables interrupts from inside one.
4. **A DAC latch needs time after the last write.** The AD5754R drops a write when an LDAC
   falling edge comes sooner than about 60 ns after it (measured: 30 ns failed, 60 ns worked):
   a channel keeps its old output, and a control register write is lost. `digitalWrite()`
   alone, the delay in `dacLatch()`, is shorter than that. The timed updates leave
   `DAC_LATCH_GAP_US`, `dacWriteNow()` and `ProgramDAC()` wait `DAC_WRITE_TO_LATCH_NS`, and
   `dacSwitchRange()` has its range write in between. Any new write-then-latch needs a gap.
   `ProgramDAC()` had none, so `setup()` lost the power up command, and a device started from
   power on had no output. Flashing over other firmware hid this, because the DAC was already
   powered up: check a change to `setup()` after a power cycle.
5. **Time DAC updates from `ARM_DWT_CYCCNT`, not the PIT's counter.** See "The time base" in
   `HardwareIO.ino`: polling the PIT placed updates up to 0.14 µs late, because each PIT
   register read takes about 0.1 µs. `tickCycles` follows the period lengths loaded into the
   PIT, so anything that changes a period's length must go through `nextSamplePeriodTicks()` and
   the `currentPeriodLoad` / `nextPeriodLoad` pipeline, as `setFrequency()` does.
6. **`samplesPerCycle` must stay a multiple of 4.** The waveforms put their edges, peaks and
   troughs at quarter cycles (`unitWaveform()`), and the user chose this over sampling rates
   closer to 100 kHz (e.g. 99.9 kHz for 300 Hz). Its maximum, 100000 at 1 Hz, sizes the integer
   arithmetic in `quarterSine()`.
7. **Do not write to the screen, wait, or use the EEPROM in an interrupt.** EEPROM writes stop
   all interrupts (about 20 µs per byte, and now and then tens of ms), so `updateScreenSaver()`
   saves the screen saver settings only while the sample clock is stopped.
8. **Validate input from USB before it is used.** Reply 0 if a value is out of range, and read
   the rest of the command's data so that it is not taken for the next command.
9. **The op codes, waveform codes, trigger mode values and range indices in `PROTOCOL.md` are
   fixed** once a client is released. Add new op codes rather than changing existing ones. The
   EEPROM addresses are Pulse Pal firmware's, and must stay so.
10. **Use ArCOM for USB, and only from `loop()`**, as in Pulse Pal firmware. See `ArCOM.h`.
11. **Anything except a param sync edge that sets `TriggerMode` must then call
    `updateParamSyncPending()`**, as in Pulse Pal firmware: otherwise a set stored for param sync
    mode survives the channel leaving the mode, and loads at a later edge. Op 84, op 85 applied at
    once, the menu and `LoadDefaultSettings()` do.
12. **The playback interrupts never write `loop()`'s settings arrays** (`waveform[]`,
    `amplitudeMicrovolts[]`, `playDurationMicros[]`, ...). At a param sync edge they change only
    what playback reads (`pendingOutput[]`, the durations in samples, `TriggerAddress`,
    `TriggerMode`, the sample clock), and `loop()` copies the synced settings into its arrays on
    its next pass (`takeSyncedSettings()`). An op that `loop()` is part way through when the edge
    comes would otherwise work from half old, half new settings.

## What runs in the playback interrupts

| Interrupt | Entry point | When |
|---|---|---|
| Sample clock | `handler()` | Every sample period while a channel plays. `startChannels()` starts the clock, and `handler()` stops it when no channel plays |
| Trigger channels | `trigger1ISR()`, `trigger2ISR()` (both call `triggerInterrupt()`) | Every edge on the trigger inputs |

They call `startChannels()`, `stopChannels()`, `processTriggerEdge()`,
`releaseGatedChannels()`, `startParamSync()`, `loadSyncedChannel()`, `takePendingOutput()`,
`fetchNextSample()`, `continueRamp()`, `applyFrequency()`, `startSampleClock()`,
`stopSampleClock()` and the DAC functions. Shared with `loop()`: the playback state (`playing`,
`stopAfterWrite`, `phase`, `rampStage`, `rampPosition`, `holdSamples`, `fetchedEnvelope`,
`samplesPlayed`, ...), the sample clock variables, `activeOutput`, `pendingOutput`,
`pendingOutputChannels`, `playDurationSamples`, the ramp lengths (`onRampSamples`,
`offRampSamples` and their reciprocals), the trigger settings (`TriggerMode`, `TriggerAddress`),
param sync's `storedSettings`, `syncedSettings`, `paramSyncPending`,
`paramSyncChannelsWaiting` and `syncedChannelsForLoop`, and the DAC state.

When one TTL reaches both trigger channels and one is in param sync mode, the GPIO interrupt
handles trigger channel 1's pin first. `triggerInterrupt()` therefore handles the other
channel's param sync edge before a start: without it, a channel started by trigger channel 1
played the old settings (10 times out of 10, with the set loaded on trigger channel 2). "Param
sync" in `Playback.ino` explains the rest.

Param sync costs, measured with the trigger interrupt's own cycle count:

| At a sync edge | Trigger interrupt |
|---|---|
| Four channels at rest, all changing output range, new frequency | 12.1 µs |
| Four channels at rest, no range change | 2.0 µs |
| Four channels playing (they wait), new frequency | 2.8 µs |
| For comparison: a normal edge starting four idle channels | 2.8 µs |

A range change needs DAC writes (about 2.5 µs per channel), made at the edge only while the
sample clock is stopped; while it runs, `handler()` takes them one channel per tick.

A channel in its off ramp is `playing` but not `isPlaying()`: a trigger starts it again, as if
it had stopped, and `isOutputActive()` (op 71's playing bits, the settings that rescale a
playing channel) still counts it. "Ramps" in `Playback.ino` explains the stages and why the
envelope never jumps.

Measured on a Pulse Pal 3 (op 71): `handler()` takes up to 7.5 µs of each 10 µs sample period
while any channel's code changes, mostly waiting for the fixed update time, and 0 late updates
in 10 s of four channels playing sine waves at 100 kHz, 4 s of it in their ramps (7.07 µs).
`loop()` gets the rest, so the menu and USB replies are slower during playback than when idle.

## Checks

Without a device:

```bash
cd /Python/PulsePal && uv run python tests/test_synthpal_protocol.py
```

With a Synth Pal on a USB port (about 25 s; `--quick` skips the 10 s four channel test):

```bash
cd /Python/PulsePal && uv run python tests/synthpal_hardware_test.py COM3

# With a Pulse Pal (Pulse Pal firmware) whose outputs 1 and 2 drive the trigger channels 1 and 2
cd /Python/PulsePal && uv run python tests/synthpal_hardware_test.py COM3 --driver COM4
```

It checks every sample played against a model of the firmware's synthesis, which computes each
DAC code as `synthesizeCode()` does, in single precision (`ChannelModel`): every waveform at
4, 32 and 12868 samples per cycle, each output range, fixed voltages and their ranges, means
exactly at the mean voltage, whatever the resting voltage, play durations exact to the sample,
every code of the on and off ramps, a stop at full amplitude, a trigger during the off ramp,
frequency and setting changes during playback, a channel joining a running clock, the
firmware's own checks of the levels (with commands sent past the class's checks), op 85
applied at once and stored in param sync mode, and the timing budget with four channels in their
ramps. With `--driver`, it also sends param sync edges: a set loaded into channels at rest, a
channel playing at the edge finishing first, and one TTL on both trigger channels. A change to the synthesis must change the
model too. `synthesizeCode()`'s multiply-adds are explicit `fmaf()` calls, so that each rounds
once whatever the compiler would do: it fused the full amplitude one by itself when the ramps
were added, and the model went one code wrong in a few samples.

The MATLAB class has its own test (about 15 s, verified with R2025a):

```bash
matlab -batch "addpath('MATLAB', 'MATLAB/tests'); testSynthPalDevice('COM3')"
```

A change to the protocol needs both classes, `/Python/PulsePal/pulsepal/synth_pal.py` and
`/MATLAB/@SynthPalDevice/SynthPalDevice.m`, updated to match.

Only a scope, a TTL source and a person can check the rest. After a change to playback,
triggers or the menu, check:

- Each waveform on a scope, with levels in each output range: shape, frequency, amplitude, mean
  voltage, and the resting voltage between playbacks.
- Ramps on a scope: the amplitude and the mean rise and fall in straight lines over the on and
  off ramps, a fixed voltage ramps in a straight line, a gated channel ramps off from the falling
  edge, and a channel triggered during its off ramp (or stopped during its on ramp) turns back
  without a jump. A square wave's edges should be within a few tens of ns
  of evenly spaced, also at frequencies whose sample period is not a whole number of timer ticks
  (300 Hz, 333.33 Hz).
- Trigger latency: from a TTL edge to the first sample, with all channels idle (about 8 µs), and
  with another channel playing (up to one sample period more).
- TTL triggers in normal, toggle and gated modes, from both trigger channels, including gated
  mode with two trigger channels linked to one output. The trigger LEDs follow the TTL, and the
  output LEDs light while a channel plays.
- The joystick menu: edit each setting of an output channel, the frequency and the trigger mode;
  play and stop a channel from its menu and see the item change back when its play duration
  ends; trigger a trigger channel; screen saver, device info, reset and exit. The splash screen
  shows the Synth Pal logo. Mean Voltage comes before Resting Voltage, and On Ramp and Off Ramp
  follow Play Duration; a ramp of 0 shows "None". The mean voltage edits within what the
  amplitude allows, and the resting voltage over the whole -10 V to 10 V. The waveform list
  reads downwards, Sine at the top: down moves to Triangle, and on to Fixed Voltage (the other
  lists, as in Pulse Pal firmware, move to their next item with up). With "Fixed Voltage": the
  amplitude shows in V (not Vpp) and edits from -10.00 to +10.00 V, and switching the waveform
  to and from it takes the nearest amplitude that suits the new waveform (`fitAmplitude()`: a
  fixed voltage of -5 V becomes 5 Vpp; 20 Vpp becomes 10 V).
- After a power cycle (unplug the USB cable, plug it back in), all four outputs play. Flashing
  the device does not reset the DAC, so a test right after flashing over other firmware cannot
  catch a fault in `setup()` (rule 4).
- Param sync from the menu: the trigger mode editor offers "Param Sync"; "Trigger Now" on a
  param sync channel loads a stored set (and starts nothing).
- Unplug the USB cable during a transfer: the device shows "COMM. FAILURE!", playback stops, and
  a click loads the default settings.

## Known limits

- A channel that starts while another plays starts on the next tick of the shared sample clock,
  so its onset can be up to one sample period late.
- Settings are not kept through a restart.
- Editing a value with the joystick holds up USB commands until the edit is finished, as in
  Pulse Pal firmware.
- The zero code calibration is applied in the -10 V to 10 V range only, where it was measured.
- Only built and tested with `PIN_MAP_VERSION 1`.
