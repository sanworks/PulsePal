# Using Pulse Pal

This guide is for writing experiment code with a Pulse Pal, and with Wave Pal and Synth Pal, the
alternative firmwares for Pulse Pal 3. It covers the device's limits, the standard calls in
Python and MATLAB, recipes for common experiments, and the mistakes that are easy to make. It is
written for scientists and for AI agents that help them. To change this repository instead (the
firmware, the classes, the protocol), see `AGENTS.md`.

The full API reference is in each class: the [Python documentation](https://sanworks.github.io/PulsePal/Python/),
or `help PulsePalDevice` (and `help WavePalDevice`, `help SynthPalDevice`) in MATLAB, which
starts with an example. Python has example scripts: `/Python/PulsePal/PulsePalExample.py`,
`WavePalExample.py` and `SynthPalExample.py`.

## Which firmware

| Firmware | Plays | Hardware | Class (Python and MATLAB) |
|---|---|---|---|
| Pulse Pal | Pulse trains: monophasic or biphasic pulses, bursts, and custom trains of up to 10000 pulses | Pulse Pal 2 and 3 | `PulsePalDevice` |
| Wave Pal | Four sampled waveforms, each played on any channels, up to 1 million samples at up to 100 kHz | Pulse Pal 3 | `WavePalDevice` |
| Synth Pal | Sine, triangle, square and sawtooth waves, white noise, and fixed voltages, 1 Hz to 20 kHz, with on and off ramps | Pulse Pal 3 | `SynthPalDevice` |

A Pulse Pal 3 runs one firmware at a time. To change it, run `LoadPulsePalFirmware` in MATLAB
(add `/MATLAB/FirmwareLoader` to the path first). Connecting with the wrong class raises an error
that names the firmware the device runs. A class connects to firmware newer than it knows, with a
warning: new firmware only adds commands, so the class works as before. Update the classes to use
what is new.

## Before an experiment

1. **Install.** Python: run `uv sync` in `/Python/PulsePal` (see its `README.md`), and
   `from pulsepal import PulsePalDevice`. MATLAB R2020b or newer: add `/MATLAB` to the path.
2. **Find the port.** `PulsePalDevice.serialportlist()` in Python, `serialportlist` in MATLAB.
   If several are listed, unplug the device and see which one disappears. Only one program at a
   time can have the port open.
3. **Check what the outputs drive.** The outputs swing from -10 V to +10 V. Keep every voltage
   within what the connected equipment accepts: a laser or LED driver that takes 0-5 V can be
   damaged by a voltage the Pulse Pal plays without complaint.
4. **Trigger inputs take TTL:** 0 V low, 3 to 5 V high. A trigger channel reads its input once
   every 50 µs, so a trigger pulse must last at least 100 µs to be seen reliably.
5. **Connecting programs the defaults.** Whatever the device held (a program set with its
   joystick, or by an earlier script) is replaced. To keep a program, save it to the device's
   microSD card and load it after connecting (see "Run without a computer" below). On
   Pulse Pal they are the device's own defaults, which it also loads at startup: 5 V pulses of
   0.1 ms, 1 ms apart, for 1 s, on every output channel.
6. **Closing stops the outputs**, on all three firmwares: `P.close()`, the end of a `with`
   block, or the object being garbage collected in Python; `clear P` or `delete(P)` in MATLAB.
   Keep the object for as long as the outputs should play. The settings stay on the device, so
   TTL triggers still play them after the connection closes.

## How the classes work

The six classes (three firmwares, two languages) work the same way, with snake_case names in
Python and camelCase in MATLAB.

- **Settings are per channel.** Each is a property with one value per output channel: in
  Python a list indexed by channel number, where index 0 is unused and holds `None`; in MATLAB
  a 1x4 array, or a 1x4 cell array for names. `trigger_mode` / `triggerMode` has one value per
  trigger channel (1 and 2).
- **Address channels.** Set one channel by its number, or all of them with one value per
  channel. A single value for a whole setting (`P.phase1_voltage = 5`) raises an error, because
  it does not say which channels it is meant for.
- **Assigning a setting programs the device at once.** To send several changes in one command,
  use a `batch()` block in Python, or turn `autoSync` off in MATLAB and call `syncToDevice()`.
- **Modes are names** (`"Gated"`, `"Sine"`; not case sensitive), and on/off settings are
  `True`/`False` (`true`/`false`).
- **Values are checked before anything is sent.** A value the device cannot play raises an
  error (`pulsepal.PulsePalError` in Python), and the device keeps its previous settings.
- **`trigger(channels)` and `stop(channels)`** take one channel number, or several as a list,
  tuple or NumPy array in Python, or an array in MATLAB. `stop()` with no argument stops all.
- **`print(P)`** in Python, or `P` at the MATLAB prompt, shows every setting.

```python
from pulsepal import PulsePalDevice

P = PulsePalDevice("COM3")
P.phase1_voltage[2] = 5                 # channel 2 only
P.phase1_duration = [0.001] * 4         # all four channels
with P.batch():                         # several changes, sent together at the end
    P.phase1_voltage[1] = 2.5
    P.inter_pulse_interval[1] = 0.02
P.trigger([1, 2])
```

```matlab
P = PulsePalDevice('COM3');
P.phase1Voltage(2) = 5;                 % channel 2 only
P.phase1Duration(:) = 0.001;            % all four channels
P.autoSync = false;                     % several changes, sent together
P.phase1Voltage(1) = 2.5;
P.interPulseInterval(1) = 0.02;
P.syncToDevice();
P.autoSync = true;
P.trigger([1 2]);
```

## Pulse Pal

### Limits

| | |
|---|---|
| Output channels | 4, -10 V to +10 V |
| Voltage steps | 0.305 mV on Pulse Pal 3 (16-bit DAC); 4.88 mV on Pulse Pal 2 (12-bit DAC, which plays the step at or below the voltage) |
| Time steps | 50 µs: every time is rounded to the nearest 50 µs, and the property then holds the rounded time (0.00012 reads back as 0.0001) |
| Longest time | 9999.9999 s, for every time setting and custom pulse time |
| Shortest pulse | 100 µs, for the phase durations, the inter-pulse interval and the pulse train duration: the shortest pulse another Pulse Pal's trigger input detects reliably |
| Custom trains | Pulse Pal 3: 4 trains of up to 10000 pulses. Pulse Pal 2: 2 trains of up to 5000. Pulse times are multiples of 100 µs |
| Trigger channels | 2, TTL. Measured on a Pulse Pal 3, an output starts 58 to 108 µs after a trigger's rising edge, depending on where the edge falls in the device's 50 µs cycle |
| Soft triggers | `trigger()` reaches the device over USB, which adds a variable delay. The channels it names start together, in the same 50 µs cycle |

### Pulse Pal 2 and Pulse Pal 3

| | Pulse Pal 2 | Pulse Pal 3 |
|---|---|---|
| Processor | Arduino Due | Teensy 4.1 |
| DAC | 12-bit, 4.88 mV steps | 16-bit, 0.305 mV steps |
| Custom trains | 2, of up to 5000 pulses | 4, of up to 10000 pulses |
| Param sync trigger mode | No | Yes |
| Programs on the microSD card | Yes, while no channel plays | Yes, and `format_microsd()` |
| Screen saver, zero-code calibration | No | Yes |
| Wave Pal and Synth Pal firmware | No | Yes |

`P.info` holds the connected device's hardware and firmware versions and its limits
(`info.hardware_version`, `info.n_custom_pulse_trains`, `info.max_custom_pulses`, ...), and
`info.circuit_board_revision`, the revision of its circuit board (0-31; 0 on Pulse Pal 2, and on
a board that encodes none). Wave Pal and Synth Pal have the same two fields. Some features need
firmware v22, the current version: continuous loop mode, stopping some channels only, reading
the parameters back, and param sync.

### Parameters

A trigger starts a pulse train on each output channel it reaches. The train waits
`pulse_train_delay`, then plays pulses for `pulse_train_duration`:

```
trigger
 |-- delay --|----------------------------- pulse_train_duration ------------------------------|
             |phase 1|---- inter_pulse_interval ----|phase 1|---- inter_pulse_interval ----|...
biphasic:    |phase 1|ipi|phase 2|-- inter_pulse_interval --|phase 1|ipi|phase 2|-- ...
             (ipi: inter_phase_interval)
```

| Python | MATLAB | Meaning | Default |
|---|---|---|---|
| `phase1_voltage` | `phase1Voltage` | Voltage of each pulse's first phase | 5 V |
| `phase1_duration` | `phase1Duration` | Duration of the first phase | 0.1 ms |
| `inter_pulse_interval` | `interPulseInterval` | From the **end** of one pulse to the start of the next | 1 ms |
| `pulse_train_duration` | `pulseTrainDuration` | Duration of the train | 1 s |
| `pulse_train_delay` | `pulseTrainDelay` | From the trigger to the first pulse | 0 |
| `resting_voltage` | `restingVoltage` | Voltage between pulses, and while the channel is idle | 0 V |
| `is_biphasic` | `isBiphasic` | `True`: each pulse has a second phase | `False` |
| `inter_phase_interval` | `interPhaseInterval` | Rest between the two phases (biphasic only) | 0.1 ms |
| `phase2_voltage`, `phase2_duration` | `phase2Voltage`, `phase2Duration` | The second phase (biphasic only) | -5 V, 0.1 ms |
| `burst_duration` | `burstDuration` | Pulses play in bursts of this length. 0: no bursts | 0 |
| `inter_burst_interval` | `interBurstInterval` | Rest between bursts | 0 |
| `link_trigger_channel1`, `link_trigger_channel2` | `linkTriggerChannel1`, `linkTriggerChannel2` | Whether trigger channel 1 (or 2) starts this output channel | channel 1: `True`; channel 2: `False` |
| `custom_train_id` | `customTrainID` | 0: the train above. 1-4: a custom train (see below) | 0 |
| `custom_train_target` | `customTrainTarget` | `"Pulses"`: custom times are pulse onsets. `"Bursts"`: they are burst onsets, which needs a `burst_duration` | `"Pulses"` |
| `custom_train_loop` | `customTrainLoop` | `True`: repeat the custom train until the train duration ends | `False` |
| `continuous_loop` | `continuousLoop` | `True`: play until stopped, ignoring the train duration | `False` |
| `trigger_mode` | `triggerMode` | Per trigger channel, see "Trigger modes" | `"Normal"` |

A monophasic train plays one pulse every `phase1_duration + inter_pulse_interval` seconds. A
biphasic train plays one every `phase1_duration + inter_phase_interval + phase2_duration +
inter_pulse_interval` seconds.

### Standard calls

| Python | MATLAB | Does |
|---|---|---|
| `P = PulsePalDevice("COM3")` | `P = PulsePalDevice('COM3');` | Connects, and programs the defaults |
| `P.trigger([1, 3])` | `P.trigger([1 3]);` | Starts the channels' trains (from the computer) |
| `P.stop()`, `P.stop(2)` | `P.stop();`, `P.stop(2);` | Stops all channels, or some. They return to their resting voltage |
| `P.set_fixed_voltage(4, 2.5)` | `P.setFixedVoltage(4, 2.5);` | Holds channels at a voltage until they are triggered |
| `with P.batch(): ...` | `P.autoSync = false; ... P.syncToDevice();` | Sends several changes in one command |
| `P.send_custom_pulse_train(1, times, volts)` | `P.sendCustomPulseTrain(1, times, volts);` | Loads custom train 1: pulse onset times and voltages |
| `P.send_custom_waveform(1, period, volts)` | `P.sendCustomWaveform(1, period, volts);` | Loads a sampled waveform as custom train 1 |
| `P.export_params()` | `P.exportParams()` | Every parameter, as a dict or struct, e.g. to save with your data |
| `P.import_params(params)` | `P.importParams(params);` | Programs the parameters `export_params()` returned |
| `P.sync_from_device()` | `P.syncFromDevice();` | Reads the device's parameters into the object, e.g. after using the joystick |
| `P.set_default_params()` | `P.setDefaultParams();` | Programs the defaults |
| `P.save_program_to_microsd("A.pps")` | `P.saveProgramToMicroSD('A.pps');` | Saves the parameters to the microSD card, as a program the joystick menu can load; also `load_program_from_microsd`, `delete_program_from_microsd` |
| `P.set_screen_saver(True, 300)` | `P.setScreenSaver(true, 300);` | Dims the screen after 300 s without activity (Pulse Pal 3). Kept on the device |
| `P.gui()` | `P.gui();` | Opens the parameter editor window |
| `P.close()` | `clear P` | Disconnects, and stops all outputs |

### Trigger modes

Each trigger channel starts the output channels linked to it (`link_trigger_channel1`,
`link_trigger_channel2`), in one of these modes:

| Mode | A TTL rising edge... |
|---|---|
| `"Normal"` | starts the linked channels' trains. Edges during a train are ignored |
| `"Toggle"` | starts the trains, and an edge during a train stops it |
| `"Gated"` | starts the trains, and the falling edge stops them: they play while the TTL is high. A channel linked to both trigger channels, both gated, plays until both are low |
| `"Param Sync"` | starts nothing: it loads the parameters most recently sent with `sync_to_device()` (Pulse Pal 3 only; see the recipe below) |

### Recipes

**A pulse train on each trial's TTL.** For example 20 Hz, 5 ms pulses for 1 s, started by a TTL
from the behavior system on trigger channel 1:

```python
P = PulsePalDevice("COM3")
P.phase1_voltage[1] = 5               # V
P.phase1_duration[1] = 0.005          # 5 ms pulses
P.inter_pulse_interval[1] = 0.045     # 45 ms from the end of one pulse to the next: 20 Hz
P.pulse_train_duration[1] = 1         # 20 pulses
P.link_trigger_channel1[1] = True     # the default
P.trigger_mode[1] = "Normal"          # the default: TTLs during the train are ignored
```

```matlab
P = PulsePalDevice('COM3');
P.phase1Voltage(1) = 5;
P.phase1Duration(1) = 0.005;
P.interPulseInterval(1) = 0.045;
P.pulseTrainDuration(1) = 1;
```

**Play while a TTL is high.** Gated mode stops the train at the TTL's falling edge, and
continuous loop mode keeps it going until then, however long the TTL lasts:

```python
P.trigger_mode[1] = "Gated"
P.continuous_loop[1] = True
```

**Trial-by-trial parameters with param sync (Pulse Pal 3).** Each trial's parameters are sent
during the trial before, and applied at the instant the next one starts. Wire the trial-start
TTL to **both** trigger channels: trigger channel 2, in param sync mode, loads the stored
parameters, and trigger channel 1 starts the train in the same 50 µs cycle, with the new
parameters.

```python
P = PulsePalDevice("COM3")
P.link_trigger_channel1[1] = True     # trigger channel 1 starts output channel 1
P.trigger_mode[2] = "Param Sync"      # sent at once
for voltage in [2.5, 5, 7.5]:
    with P.batch():                   # stored by the device, not applied
        P.phase1_voltage[1] = voltage
    wait_for_end_of_trial()           # your code. The next TTL loads the stored set and starts the train
P.trigger_mode[2] = "Normal"          # leaving param sync mode: assigned outside a batch, so sent at once
```

```matlab
P.triggerMode{2} = 'Param Sync';
for voltage = [2.5 5 7.5]
    P.autoSync = false;
    P.phase1Voltage(1) = voltage;
    P.syncToDevice();                 % Stored for the next TTL on trigger channel 2
    P.autoSync = true;
    waitForEndOfTrial();
end
P.triggerMode{2} = 'Normal';
```

Make each train end before the next trial's TTL: a channel still playing at the edge finishes
its train on the old parameters, and ignores the start trigger on channel 1, so the new trial's
train would not play.

Custom trains work the same way: in param sync mode, `send_custom_pulse_train()` and
`send_custom_waveform()` (MATLAB `sendCustomPulseTrain()`, `sendCustomWaveform()`) store the train
for the next edge, and channels go on playing the old one until then. Send the next trial's
trains with its parameters, during the trial before.

**Charge-balanced biphasic pulses.** 200 µs at +3 V, 100 µs gap, 200 µs at -3 V, 100 times a
second for 2 s:

```python
with P.batch():
    P.is_biphasic[1] = True
    P.phase1_voltage[1] = 3
    P.phase1_duration[1] = 0.0002
    P.inter_phase_interval[1] = 0.0001
    P.phase2_voltage[1] = -3
    P.phase2_duration[1] = 0.0002
    P.inter_pulse_interval[1] = 0.0095   # 0.2 + 0.1 + 0.2 + 9.5 ms = a 10 ms period
    P.pulse_train_duration[1] = 2
```

**Custom pulse trains and waveforms.** A custom train sets each pulse's onset time and voltage;
its pulse width is the channel's `phase1_duration`. A custom waveform is a custom train of
adjoining pulses, one per sample:

```python
P.send_custom_pulse_train(1, [0, 0.1, 0.15, 0.5], [5, 5, 2.5, 5])  # times in multiples of 100 us
P.custom_train_id[2] = 1
P.phase1_duration[2] = 0.002

import numpy as np
samples = 4 * np.sin(2 * np.pi * 10 * np.arange(1000) / 1000)    # 1 s of a 10 Hz sine, at 1 kHz
P.send_custom_waveform(2, 0.001, samples)
P.custom_train_id[3] = 2
P.phase1_duration[3] = 0.001          # each sample lasts the whole sampling period
```

A Pulse Pal 3 holds 10000 samples per train, so a custom waveform lasts at most 1 s at 10 kHz.
For longer or faster waveforms, use Wave Pal.

**Record exactly what was played.** `export_params()` returns every parameter as the device
plays it (times rounded to 50 µs), in a form `json` can save:

```python
import json
with open("session_params.json", "w") as f:
    json.dump(P.export_params(), f)
```

In MATLAB, `P.saveProgramToFile('session.json')` saves them as a program file (below), or
`params = P.exportParams(); save('session.mat', 'params')`.

To record which hardware played them, save `P.info.hardware_version` and
`P.info.circuit_board_revision` (MATLAB: `info.hardwareVersion`, `info.circuitBoardRevision`) with
your data too. Program files hold them in `device_info`.

**Program files.** The Python and MATLAB GUIs save and open the same `.json` program files, and
MATLAB's `saveProgramToFile()` and `loadProgramFromFile()` write and read them too. A program file holds
`format_version` (1), `params` (what `export_params()` returns: Python's snake_case names, mode
names, `true` and `false`), the text of each custom train as typed in a GUI, and `device_info`.
To program a device from one in Python:

```python
import json
with open("MyProgram.json") as f:
    P.import_params(json.load(f)["params"])
```

In MATLAB, `P.loadProgramFromFile('MyProgram.json')`. A GUI's custom trains are loaded only by
opening the file in a GUI. `import_params()`, `importParams()` and `loadProgramFromFile()` skip
names they do not know, with a warning, and the GUIs ignore them, so a file saved by a newer
version still loads.

**Run without a computer.** Save a program to the device's microSD card, then load it with the
joystick menu's Load Program item (or with `load_program_from_microsd()`):

```python
P.save_program_to_microsd("OPTO20HZ.pps")  # at most 11 characters, then .pps
```

### Pitfalls

- **The inter-pulse interval is not the period.** It runs from the end of one pulse to the
  start of the next. 5 ms pulses at 20 Hz need an inter-pulse interval of 45 ms, not 50 ms.
- **Times round to 50 µs.** 0.00012 s plays as 0.0001 s, and 125 µs (2.5 steps) as 100 µs.
  Read the property back to see what the device plays.
- **The end of a train can cut pulses.** A monophasic pulse still playing when the train ends
  is cut short. A biphasic pulse starts only if it can finish before the train ends, and in
  bursts, a pulse starts only if it fits in the burst, so the last pulse may be missing.
- **Changes reach a playing train at once.** A parameter changed while a train plays changes
  that train mid-way, except its duration: a train's end is set when it starts, so a new
  `pulse_train_duration` applies from the next train. Only param sync waits for the next trial.
- **A channel that is playing ignores a new trigger,** soft or TTL in normal mode, and so does
  one waiting out its pulse train delay. Stop it first, or let the train end.
- **Connecting replaces the device's program** with the defaults, and a program set with the
  joystick afterwards is not in the object until `sync_from_device()` reads it.
- **In param sync mode, only `sync_to_device()` waits for the TTL** (including the end of a
  `batch()` block). An assignment with `auto_sync` on programs the device at once. The param
  sync channel itself starts nothing.
- **Custom trains.** A custom train that does not loop plays all its pulses and ends, whatever
  the train duration; one that loops stops at the train duration. Custom pulse times must be
  multiples of 100 µs: a time in between raises an error rather than being rounded. A train
  needs at least one pulse. A train targeting bursts (`custom_train_target` `"Bursts"`) needs a
  burst duration above 0: set `burst_duration` first, or both in a `batch()` block.
- **Loading a custom train stops the channels playing it** (outside param sync mode), and their
  next trigger plays the new train. To change a train between trials without stopping the one
  playing, use param sync mode (see the recipe above).
- **A biphasic custom pulse always plays whole.** If its phases last longer than the gap to the
  next pulse's time, the next pulse starts late, as it ends. Space biphasic custom pulses at
  least `phase1_duration + inter_phase_interval + phase2_duration` apart to keep their times.
- **Continuous loop mode is not saved** in programs on the microSD card, and is not read back
  by `sync_from_device()`. It starts nothing either, from a script or from the joystick menu's
  Continuous item: the next trigger plays the train until it is stopped. (Firmware v21 started
  the channel.)
- **A failed load from the microSD card changes nothing.** `load_program_from_microsd()` raises
  an error for a missing or invalid file, and the device keeps its program.
- **Pulse Pal 2 does not use its microSD card while a channel plays.** The card shares a bus with
  the outputs, so saving, loading or deleting a program raises an error then: call `stop()`
  first. While it uses the card, for a few milliseconds, a trigger waits until the operation
  ends, and a trigger pulse shorter than that is missed. Pulse Pal 3 has neither limit.
- **A non-zero resting voltage is output all the time** the channel is idle, from the moment
  it is set.
- **Unplugging the USB cable during playback** stops it: the device shows "COMM. FAILURE!",
  returns the outputs to their resting voltages, and loads its default parameters.
- **"COMM. FAILURE!"** means a command reached the device only in part (a cable pulled, a
  script stopped while it wrote). The device then reads no command, and every call times out,
  until its joystick is clicked; it then loads its default parameters. Click it, and connect
  again.

## Wave Pal

Wave Pal stores four sampled waveforms on the device's microSD card, numbered 1-4, and each
output channel plays one of them: its `current_waveform`, or none.

### Limits

| | |
|---|---|
| Waveforms | 4, each played by any number of channels. A channel plays none until a script gives it one |
| Waveform length | 1 to 1,000,000 samples per waveform |
| Sampling rate | 1 Hz to 100 kHz, one rate for all four channels. The device divides a 24 MHz clock by a whole number, so the rate played (`actual_sampling_rate`) can differ slightly: 44100 Hz plays at 44117.6 Hz. Rates that divide 24 MHz, such as 10, 25 or 100 kHz, play exactly |
| Output ranges | `"0V:5V"`, `"0V:10V"`, `"-5V:5V"`, `"-10V:10V"`, one for all channels. The smallest range that fits the waveforms gives the finest voltage steps |
| Trigger modes | `"Normal"`, `"Toggle"`, `"Gated"` and `"Param Sync"`, as in Pulse Pal, and `"Master"`: a rising edge restarts channels that are playing |

### Standard calls

| Python | MATLAB | Does |
|---|---|---|
| `W = WavePalDevice("COM3")` | `W = WavePalDevice('COM3');` | Connects, stops playback, programs the defaults (10 kHz, -10 V to 10 V, no channel playing a waveform) |
| `W.sampling_rate = 50000` | `W.samplingRate = 50000;` | Sets the sampling rate, Hz |
| `W.output_range = "-5V:5V"` | `W.outputRange = '-5V:5V';` | Sets the output range |
| `W.load_waveform(1, volts, [1, 3])` | `W.loadWaveform(1, volts, [1 3]);` | Loads waveform 1, in volts, and has channels 1 and 3 play it. Without channels, no channel changes |
| `W.current_waveform[2] = 1` | `W.currentWaveform(2) = 1;` | Channel 2 plays waveform 1 from its next start (0: none) |
| `W.loop_mode[1] = True`, `W.loop_duration[1] = 3` | `W.loopMode(1) = true; W.loopDuration(1) = 3;` | Loops the channel's waveform, for 3 s after each trigger (0: until stopped) |
| `W.trigger([1, 2])`, `W.stop()` | `W.trigger([1 2]);`, `W.stop();` | Starts, or stops, playback |
| `W.set_fixed_voltage(3, 1.5)` | `W.setFixedVoltage(3, 1.5);` | Holds channels at a voltage |
| `W.status()` | `W.status()` | Which channels are playing, each channel's waveform, what waits for a param sync edge, and any underruns |
| `W.export_params()` | `W.exportParams()` | Every setting, as a dict or struct, e.g. to save with your data. Not the waveforms |
| `W.import_params(params)` | `W.importParams(params);` | Programs the settings `export_params()` returned. Load the waveforms first |
| `W.set_screen_saver(True, 300)` | `W.setScreenSaver(true, 300);` | Dims the screen after 300 s without activity. Kept on the device, shared with the other firmwares |

### Recipe: a waveform on each trial's TTL

```python
import numpy as np
from pulsepal import WavePalDevice

W = WavePalDevice("COM3")
W.sampling_rate = 25000
W.output_range = "-5V:5V"                                  # finer steps than -10V:10V, for a waveform within +/-5 V
t = np.arange(25000) / W.actual_sampling_rate               # 1 s
W.load_waveform(1, 3 * np.sin(2 * np.pi * 8 * t), [1, 2])  # an 8 Hz sine, +/-3 V, on channels 1 and 2
W.link_trigger_channel1[1:3] = [True, True]                # the default: trigger channel 1 starts them
```

### Recipe: a new waveform for each trial, with param sync

Each trial's waveform is loaded during the trial before, and played from the instant the next
one starts, as Pulse Pal's param sync does for parameters. Wire the trial-start TTL to **both**
trigger channels: trigger channel 2, in param sync mode, makes the loaded waveform current, and
trigger channel 1 starts it on the same edge (the param sync edge is handled first).

```python
W = WavePalDevice("COM3")
W.load_waveform(1, first_trial_wave, 1)   # outside param sync mode: current at once
W.trigger_mode[2] = "Param Sync"           # sent at once
for wave in next_trial_waves:
    W.load_waveform(1, wave)               # stored: channel 1 plays the current waveform 1 meanwhile
    wait_for_end_of_trial()                # your code. The next TTL makes it current and plays it
W.trigger_mode[2] = "Normal"               # leaving param sync mode makes anything still stored current
```

```matlab
W.loadWaveform(1, firstTrialWave, 1);
W.triggerMode{2} = 'Param Sync';
for i = 1:nTrials
    W.loadWaveform(1, nextTrialWaves{i});
    waitForEndOfTrial();                   % your code
end
W.triggerMode{2} = 'Normal';
```

### Pitfalls

- **A channel plays nothing until it is given a waveform:** pass channels to `load_waveform()`,
  or set `current_waveform`. `trigger()` raises an error for a channel without one, but a TTL
  edge is ignored without a word.
- **Stopped channels output 0 V.** Wave Pal has no resting voltage.
- **Changing the output range stops playback** and loads the waveforms again, encoded for the
  new range. A waveform that does not fit the new range raises an error. The range cannot change
  in param sync mode.
- **Loading a waveform stops the channels playing it,** outside param sync mode. Other channels
  keep playing. Every channel set to that waveform plays the new one from its next start.
- **A channel that is playing finishes its waveform.** A new `current_waveform`, or a waveform
  loaded in param sync mode, plays from its next start.
- **In param sync mode, only `load_waveform()` and `current_waveform` wait for the edge.** Other
  settings apply at once. The object holds what was sent, so until the edge, `waveforms` and
  `current_waveform` show what the next trial will play; `status()` shows what the device plays
  now. A second load of the same waveform before the edge replaces the first, and stops a channel
  still playing that waveform from before the previous edge.
- **Compute sample times from the rate the device plays,** `actual_sampling_rate`, when the
  waveform's frequency matters.
- **The object knows only the waveforms it loaded.** `status()` shows what the device holds,
  which can include waveforms from an earlier session; load them again after connecting.
- **Underruns.** Long waveforms stream from the microSD card. Check `status()` after an
  experiment: an underrun is a block of samples that arrived late, during which the output
  held its last value.
- **Gated mode** plays a waveform for exactly as long as the TTL is high only with loop mode on
  and a loop duration of 0.

## Synth Pal

Synth Pal computes each channel's waveform as it plays: a sine, triangle, square or sawtooth
wave or white noise around a mean voltage, or a step to a fixed voltage, with optional on and off
ramps.

### Limits

| | |
|---|---|
| Frequency | 1 Hz to 20 kHz in steps of 0.01 Hz, **one frequency for all four channels**. It is played exactly: the sampling rate is a whole multiple of it, up to 100 kHz |
| Levels | A periodic waveform, or white noise, swings `peak_to_peak / 2` above and below its `mean_voltage`, within -10 V to 10 V: `abs(mean_voltage) + peak_to_peak / 2 <= 10`. A `"Fixed Voltage"` channel steps to its `fixed_voltage`, -10 V to 10 V |
| White noise | `"White Noise"`: a new random voltage on every sample, uniform over `peak_to_peak` around `mean_voltage`. It is rendered at the sampling rate the frequency sets (`sampling_rate`, 50 to 100 kHz), so no claim is made about its spectrum. Each channel has its own random sequence, new on every playback; the sequences start from the same point when the device powers up |
| Durations | `play_duration` and the ramps: up to 3600 s. A play duration of 0 plays until stopped |
| Trigger modes | `"Normal"`, `"Toggle"`, `"Gated"` and `"Param Sync"`, as in Pulse Pal |

### Standard calls

| Python | MATLAB | Does |
|---|---|---|
| `S = SynthPalDevice("COM3")` | `S = SynthPalDevice('COM3');` | Connects, stops playback, programs the defaults (100 Hz sine, 5 V peak to peak, 1 s) |
| `S.frequency = 40` | `S.frequency = 40;` | Sets the frequency of all channels, Hz |
| `S.configure(1, waveform="Sine", peak_to_peak=4, mean_voltage=1)` | `S.configure(1, 'waveform', 'Sine', 'peakToPeak', 4, 'meanVoltage', 1);` | Sets several of a channel's settings, checked together |
| `S.play_duration[1] = 2` | `S.playDuration(1) = 2;` | Seconds at full amplitude after each trigger |
| `S.on_ramp_duration[1] = 0.5` | `S.onRampDuration(1) = 0.5;` | Fade-in after each trigger; `off_ramp_duration` fades out |
| `S.resting_voltage[1] = 0` | `S.restingVoltage(1) = 0;` | Output between playbacks |
| `S.trigger([1, 2])`, `S.stop()` | `S.trigger([1 2]);`, `S.stop();` | Starts, or stops (over the off ramp), playback |
| `with S.batch(): ...` | `S.autoSync = false; ... S.syncToDevice();` | Sends several changes in one command |
| `S.status()` | `S.status()` | Which channels are playing, and their output ranges |
| `S.export_params()` | `S.exportParams()` | Every setting, as a dict or struct, e.g. to save with your data |
| `S.import_params(params)` | `S.importParams(params);` | Programs the settings `export_params()` returned, in one command |
| `S.sync_from_device()` | `S.syncFromDevice();` | Reads the device's settings into the object, e.g. after using the joystick |
| `S.set_screen_saver(True, 300)` | `S.setScreenSaver(true, 300);` | Dims the screen after 300 s without activity. Kept on the device, shared with the other firmwares |

### Recipes

**A 40 Hz sine for 30 s, faded in and out over 2 s:**

```python
from pulsepal import SynthPalDevice

S = SynthPalDevice("COM3")
S.frequency = 40
S.configure(1, waveform="Sine", peak_to_peak=2, mean_voltage=0, play_duration=30,
            on_ramp_duration=2, off_ramp_duration=2)
S.trigger(1)            # from trigger to rest: 2 + 30 + 2 = 34 s
```

**Trial-by-trial frequency with param sync.** As in Pulse Pal: trigger channel 2 in param sync
mode, the trial-start TTL wired to both trigger channels, and each trial's settings sent in a
`batch()` block during the trial before. At the edge, the frequency and the trigger modes
change at once; a channel at rest takes its new settings at once, and one still playing
finishes on its old settings (at the new frequency) and takes the new ones when it reaches its
resting voltage.

### Pitfalls

- **The frequency is shared** by all four channels.
- **Levels are checked together.** Raising `peak_to_peak` beyond what the mean voltage allows
  raises an error: change the mean voltage first, or set both in one `configure()` call.
- **`peak_to_peak` or `fixed_voltage`:** the waveform decides which one plays. A channel keeps
  both. The device holds only the one that plays, so `sync_from_device()` reads that one, and
  the object keeps its own value of the other.
- **Settings changed with the joystick** are not in the object until `sync_from_device()` reads
  them.
- **The ramps lengthen playback:** a trigger plays `on_ramp_duration + play_duration +
  off_ramp_duration`, and a stop (including closing the connection) fades out over the off
  ramp. A trigger during the off ramp fades the channel back in without restarting its cycle.
- **Gated mode** plays for exactly as long as the TTL is high (plus the off ramp) only with a
  play duration of 0.
- **Waveforms start at the trigger:** sine and triangle at the mean voltage, rising; square
  high for the first half of each cycle; sawtooth rising from its lowest voltage.
- **For white noise, the frequency sets only the sampling rate,** not a band: every sample is
  a new value. To shape its spectrum, filter the output.

## More

- `/Firmware/PROTOCOL.md`, `/Firmware/WavePal/PROTOCOL.md` and `/Firmware/SynthPal/PROTOCOL.md`
  document the USB protocols, for writing a client in another language. `/c++/` holds a C++
  class for Pulse Pal.
- The Pulse Pal wiki, https://sites.google.com/site/pulsepalwiki/, has the hardware
  documentation and a parameter guide with diagrams.
