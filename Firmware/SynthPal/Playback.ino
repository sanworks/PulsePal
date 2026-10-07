/*
----------------------------------------------------------------------------

This file is part of the Pulse Pal Project
Copyright (C) 2026 Sanworks LLC, Rochester, NY, USA

----------------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed  WITHOUT ANY WARRANTY and without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


// Playback: the sample clock interrupt, waveform synthesis, the trigger inputs, and starting and stopping channels.
//
// Functions in this file:
//   fillSineTable()
//   quarterSine()
//   unitWaveform()
//   synthesizeCode()
//   setNextCode()
//   endPlayback()
//   fetchNextSample()
//   handler()
//   takePendingOutput()
//   startChannels()
//   stopChannels()
//   isPlaying()
//   trigger1ISR(), trigger2ISR()
//   handleTriggerLine()
//   processTriggerEdge()
//   releaseGatedChannels()
//
// ---------------------------------------------------------------------------------------------------------------
// HOW PLAYBACK WORKS
//
// Samples per cycle. Every channel plays the same frequency, and each cycle of its waveform is samplesPerCycle samples:
// the largest multiple of 4 whose sampling rate (samplesPerCycle times the frequency) is at most 100kHz. Because it is
// a whole number, every cycle is rendered the same way, and because it is a multiple of 4, a sample falls exactly on
// every vertical edge (square, sawtooth), peak and trough (sine, triangle): edges are a whole number of samples apart,
// and peaks reach their full amplitude. For example, 100Hz and 500Hz play at 100kHz (1000 and 200 samples per cycle),
// 300Hz at 99.6kHz (332), and 20kHz at 80kHz (4). setFrequency() in Settings.ino works these out.
//
// The sample clock. handler() runs once per sample, from a PIT timer channel, while any channel is playing. The timer
// counts a 24MHz clock, so a sample period must be a whole number of ticks (41.7ns); the exact period, 2.4e9 ticks /
// (frequency in centiHz * samplesPerCycle), usually is not. So each period is samplePeriodTicks or one tick more,
// chosen by an accumulator (ditherAccumulator, as in Bresenham's line algorithm) so that the time of every sample is
// within one tick of its exact time: the frequency is exact, cycle boundaries never drift, and no two periods differ
// by more than one tick. The PIT loads a new period length only when the current period ends, and handler() runs
// just after that, so each run of handler() loads the length of the period after the one that has just begun.
//
// DAC updates. handler() writes the samples it fetched on its previous run, and latches them all at DAC_LATCH_US
// after the tick (dacLoadTimed() and dacLatchTimed() in HardwareIO.ino), however many channels changed and however late
// the interrupt started. Between the writes and the latch, it fetches each playing channel's next sample into
// dacValue[]. So a sample is written one sample period after it is fetched, and only channels whose code changes are
// written (a square wave is written at its edges only, so it has no digital feedthrough from its own writes between
// them). While any channel writes, handler() takes about 7.5us of each sample period; loop() runs in the rest.
//
// Synthesis. Each sample is computed when it is fetched (synthesizeCode()), from the channel's waveform at its phase,
// scaled by the amplitude around the resting voltage, in DAC codes of the channel's output range. Each channel has its
// own output range on the DAC, the one with the finest steps that holds its whole waveform (Settings.ino). A fixed
// voltage (WAVEFORM_FIXED_VOLTAGE) is the same code on every sample, so it is written once, as it starts: it plays like
// the other waveforms (start, play duration, stop, triggers), on the same sample clock.
//
// Starting. A channel starts at phase 0 of its waveform. If the sample clock is stopped (no channel was playing), the
// trigger computes the first samples, starts the clock from that moment, and latches them DAC_LATCH_US later, as on
// every later tick: the waveform starts a fixed time after its trigger (about 8us), and its first sample lasts as long
// as the others. If the clock is running, the channel starts on its next tick, so a channel that starts while another
// plays can start up to one sample period after its trigger. The channels share the clock, so this cannot be avoided
// without disturbing the others.
//
// Stopping. When a channel's play duration has elapsed, or it is stopped (op 88, the menu, a toggle or gated trigger),
// handler() fetches its resting voltage instead of a sample, writes it on the next tick, and stops the channel
// (stopAfterWrite). When no channel plays, handler() stops the clock.
//
// Changing settings. The frequency, play durations and trigger settings take effect at once, also during playback: a
// new frequency keeps each playing channel's phase and the time it has left to play. A channel's waveform, amplitude and
// resting voltage are handed to handler() in pendingOutput[], and it takes one channel's at a time, on a tick, after the
// DAC update (takePendingOutput()). If the output range changes, the DAC then needs a range write and a code write for
// that channel, and its output shows the old code in the new range for a fraction of a microsecond, too short to see on a
// scope (switching from +/-5V to +/-10V during playback). Settings.ino has the details.
//
// Triggers. TTL edges on the trigger channels raise an interrupt (trigger1ISR() and trigger2ISR()), so their timing
// does not depend on a polling rate. What an edge does depends on the trigger channel's mode (processTriggerEdge()).
// Soft triggers (op 80) start idle channels, as in Pulse Pal firmware.
//
// Interrupt rules. handler() and the trigger interrupts run at the same priority (PLAYBACK_IRQ_PRIORITY), so neither
// can interrupt the other. That makes it safe for both to write to the DAC over SPI. loop() calls the functions below
// only with interrupts disabled (noInterrupts() ... interrupts()), for the same reason: an SPI transfer interrupted by
// another SPI transfer leaves the first waiting forever (see rule 1 in /Firmware/PulsePal3/AGENTS.md). Keep those
// sections short: with four channels to write, the timed DAC update starts writing 0.95us after the tick, and handler()
// normally starts about 0.7us after it, so a delay of more than about 0.25us makes that tick's update late (counted in
// lateUpdates, which op 71 reports). Fewer channels leave more time.
// ---------------------------------------------------------------------------------------------------------------

// Fills sineTable with the first quarter of a sine wave. Startup only, so it runs from flash (FLASHMEM).
FLASHMEM void fillSineTable() {
  for (int i = 0; i <= SINE_TABLE_SIZE; i++) {
    sineTable[i] = (float)sin(M_PI / 2 * i / SINE_TABLE_SIZE);
  }
}

// sin(pi/2 * r / samplesPerQuarter), for r from 0 to samplesPerQuarter: the first quarter of the sine wave. Read from
// sineTable with linear interpolation. The table index is worked out in integers, so a peak (r = samplesPerQuarter) is
// exactly 1.
static inline float quarterSine(uint32_t r) {
  uint32_t scaled = r * SINE_TABLE_SIZE; // At most 25000 * 4096, so it fits
  uint32_t index = scaled / samplesPerQuarter;
  if (index >= SINE_TABLE_SIZE) {
    return 1.0f;
  }
  float fraction = (float)(scaled - (index * samplesPerQuarter)) / samplesPerQuarterFloat;
  return sineTable[index] + (fraction * (sineTable[index + 1] - sineTable[index]));
}

// The value of a periodic waveform at sample n of its cycle (0 to samplesPerCycle - 1), from -1 to 1. Each waveform's
// values are symmetric about 0, so its mean is exactly the resting voltage, and its highest and lowest samples are
// exactly 1 and -1. A fixed voltage never comes here: see synthesizeCode().
static inline float unitWaveform(byte shape, uint32_t n) {
  uint32_t quarter = samplesPerQuarter;
  switch (shape) {
    case WAVEFORM_SINE: { // Rising from 0. Peak at n = quarter, trough at n = 3 * quarter.
      uint32_t q = n / quarter; // Quarter of the cycle, 0-3
      uint32_t r = n - (q * quarter);
      float value = (q & 1) ? quarterSine(quarter - r) : quarterSine(r); // Falling in quarters 1 and 3
      return (q < 2) ? value : -value; // Negative in the second half
    }
    case WAVEFORM_TRIANGLE: { // Rising from 0. Peak at n = quarter, trough at n = 3 * quarter.
      int32_t x;
      if (n <= quarter) {
        x = n;
      } else if (n <= 3 * quarter) {
        x = (int32_t)(2 * quarter) - (int32_t)n;
      } else {
        x = (int32_t)n - (int32_t)(4 * quarter);
      }
      return (float)x / samplesPerQuarterFloat;
    }
    case WAVEFORM_SQUARE: { // High for the first half cycle, low for the second
      return (n < 2 * quarter) ? 1.0f : -1.0f;
    }
    default: { // WAVEFORM_SAWTOOTH. Rises in equal steps from -1 at n = 0 to 1 at the last sample, then falls back.
      return (float)((int32_t)(2 * n) - (int32_t)(samplesPerCycle - 1)) / sawtoothDenominator;
    }
  }
}

// The DAC code of sample n (0 to samplesPerCycle - 1) of a channel's waveform, in the channel's output range. The
// offset from the resting voltage's code is rounded half away from zero, so samples the same distance above and below
// the resting voltage are the same number of codes from it: when the resting voltage falls on a DAC code (as 0V does in
// the bipolar ranges), the waveform's mean is exactly that code. Adding the offset to the code in floating point first
// would round values above 32768 more coarsely than those below it.
static inline uint16_t synthesizeCode(byte channel, uint32_t n) {
  const ChannelOutput &out = activeOutput[channel];
  if (out.waveform == WAVEFORM_FIXED_VOLTAGE) { // Not periodic, and not centred on the resting voltage
    return out.fixedCode;
  }
  float offset = out.restCodeFraction + (out.halfAmplitudeCodes * unitWaveform(out.waveform, n));
  int32_t code = (int32_t)out.restCode + (int32_t)roundf(offset);
  if (code < 0) {
    return 0;
  }
  if (code > 65535) {
    return 65535; // The top of a range (code 65536) is one step above the DAC's highest code
  }
  return (uint16_t)code;
}

// Sets the code a channel's output takes on the next tick. The channel is written only if its code changes.
static inline void setNextCode(byte channel, uint16_t code) {
  dacValue[channel] = code;
  DACFlags[channel] = (code != dacOutput[channel]);
}

// A channel has stopped: fetch its resting voltage, and mark it stopped once handler() has written it
static inline void endPlayback(byte channel) {
  setNextCode(channel, activeOutput[channel].restCode);
  stopAfterWrite[channel] = true;
}

// Fetches the next sample of a playing channel into dacValue[], for the next tick. Called from the playback
// interrupts, or from loop() with interrupts disabled.
static inline void fetchNextSample(byte channel) {
  if (playDurationSamples[channel] && (samplesPlayed[channel] >= playDurationSamples[channel])) {
    endPlayback(channel); // The play duration has elapsed
    return;
  }
  uint16_t code = synthesizeCode(channel, phase[channel]);
  setNextCode(channel, code);
  sampleSum[channel] += code;
  samplesPlayed[channel]++;
  phase[channel]++;
  if (phase[channel] == samplesPerCycle) {
    phase[channel] = 0;
  }
}

// The sample clock callback. Runs once per sample while any channel is playing. See "How playback works" above.
void handler() {
  uint32_t startCycles = ARM_DWT_CYCCNT;
  advanceSampleClock(); // The time of this tick, and the length of the period after the one that has begun
  // New output settings for one channel per tick, taken after the update below: a range change takes about 2.5us of
  // DAC writes. That channel's next sample is fetched after them, with its new settings.
  byte pendingChannel = pendingOutputChannels ? __builtin_ctz(pendingOutputChannels) : N_CHANNELS;
  byte nWritten = dacLoadTimed(); // Write the samples fetched on the previous run
  // Fetch the next samples while the writes settle, before the latch
  boolean anyPlaying = false;
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (stopAfterWrite[i]) { // Its resting voltage is in this update
      playing[i] = false;
      stopAfterWrite[i] = false;
      digitalWriteFast(OutputLEDLines[i], LOW);
    } else if (playing[i]) {
      anyPlaying = true;
      if (i != pendingChannel) {
        fetchNextSample(i);
      }
    }
  }
  if (nWritten) {
    dacLatchTimed(); // DAC_LATCH_US after the tick
  }
  if (pendingChannel < N_CHANNELS) {
    takePendingOutput(pendingChannel);
    if (playing[pendingChannel]) {
      fetchNextSample(pendingChannel);
    }
  }
  if (!anyPlaying) {
    stopSampleClock();
  }
  uint32_t elapsedCycles = ARM_DWT_CYCCNT - startCycles;
  if (elapsedCycles > longestHandlerCycles) {
    longestHandlerCycles = elapsedCycles;
  }
}

// Takes a channel's new output settings from pendingOutput[] (see updateChannelOutput() in Settings.ino), and puts its
// output at the new settings at once: the resting voltage if it is idle, or the sample on its output now if it plays.
// A change of output range is written to the DAC between the code and the latch (dacSwitchRange()). A playing channel
// in the same range needs no write here: its next sample is fetched with the new settings.
// Called by handler() after the tick's DAC update, or by loop() with interrupts disabled while the sample clock is
// stopped.
void takePendingOutput(byte channel) {
  bitClear(pendingOutputChannels, channel);
  byte oldRange = activeOutput[channel].range;
  activeOutput[channel] = pendingOutput[channel];
  // The zero code calibration was measured in the -10V to 10V range, so it is only applied there
  activeCalibration[channel] = (activeOutput[channel].range == RANGE_PLUS_MINUS_10V) ? ZeroCodeCalibration[channel] : 0;
  boolean channelPlays = isPlaying(channel);
  uint16_t code = activeOutput[channel].restCode;
  if (channelPlays) {
    uint32_t current = (phase[channel] == 0) ? (samplesPerCycle - 1) : (phase[channel] - 1); // The sample latched last
    code = synthesizeCode(channel, current);
  }
  if (activeOutput[channel].range != oldRange) {
    dacSwitchRange(channel, code);
  } else if (!channelPlays && (code != dacOutput[channel])) {
    dacWriteNow(channel, code);
  }
}

// Starts the selected channels (one bit per channel) from phase 0 of their waveforms. Channels that are playing are
// left alone. Interrupt context, or loop() with interrupts disabled.
void startChannels(byte channelBits) {
  byte toStart = 0;
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (bitRead(channelBits, i) && !isPlaying(i)) {
      bitSet(toStart, i);
    }
  }
  if (!toStart) {
    return;
  }
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (bitRead(toStart, i)) {
      phase[i] = 0;
      samplesPlayed[i] = 0;
      sampleSum[i] = 0;
      stopAfterWrite[i] = false;
      playing[i] = true;
      digitalWriteFast(OutputLEDLines[i], HIGH);
      fetchNextSample(i); // The first sample
    }
  }
  if (!timerRunning) {
    // No other channel was playing, so the sample clock is stopped. Start it from this moment, and latch the first
    // samples DAC_LATCH_US later, as handler() latches each later one DAC_LATCH_US after its tick: the waveform starts a
    // fixed time after its trigger, and its first sample lasts as long as the others. The first samples were fetched
    // first, so that the writes can start on time. handler() cannot run before this function returns, since it has
    // the same interrupt priority (or interrupts are disabled). No other channel has a sample waiting.
    startSampleClock();
    dacWriteTimed();
    for (byte i = 0; i < N_CHANNELS; i++) {
      if (bitRead(toStart, i)) {
        fetchNextSample(i); // The second sample, written by the first run of handler()
      }
    }
  }
}

// Stops the selected channels (one bit per channel). Their outputs return to their resting voltages on the next tick.
// Interrupt context, or loop() with interrupts disabled.
void stopChannels(byte channelBits) {
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (bitRead(channelBits, i) && isPlaying(i)) {
      // The resting voltage replaces the sample fetched for the next tick, so that sample is never played: take it
      // out of the counts that op 90 reports. (A channel that ends at its play duration fetches no extra sample.)
      samplesPlayed[i]--;
      sampleSum[i] -= dacValue[i];
      endPlayback(i);
    }
  }
}

// True if a channel is playing. A channel that has stopped and is waiting for its resting voltage to be written counts
// as stopped, so that a trigger can start it again at once.
boolean isPlaying(byte channel) {
  return playing[channel] && !stopAfterWrite[channel];
}

// Pin interrupts for the trigger channels, on every edge. See setup() for their priority.
void trigger1ISR() {
  handleTriggerLine(0, digitalReadFast(TriggerLines[0]) == TriggerLevel);
}

void trigger2ISR() {
  handleTriggerLine(1, digitalReadFast(TriggerLines[1]) == TriggerLevel);
}

// Handles a change on a trigger channel (0 or 1). isActive is its level now: true if the TTL is high.
void handleTriggerLine(byte triggerChannel, boolean isActive) {
  if (!triggersEnabled) { // Startup, or a comm failure message is on the screen
    triggerLineActive[triggerChannel] = isActive; // Keep the level, so that the next edge is read correctly
    return;
  }
  digitalWriteFast(InputLEDLines[triggerChannel], isActive);
  if (isActive == triggerLineActive[triggerChannel]) {
    // The level is the same as after the last edge, so it changed twice before this interrupt ran: a pulse (or a gap)
    // shorter than the interrupt latency. Handle the edge that was missed first.
    triggerLineActive[triggerChannel] = !isActive;
    processTriggerEdge(triggerChannel, !isActive);
  }
  triggerLineActive[triggerChannel] = isActive;
  processTriggerEdge(triggerChannel, isActive);
}

// An edge on a trigger channel (0 or 1), or a trigger from the joystick menu (a rising edge). What it does to each
// linked output channel depends on the trigger channel's mode, as in Pulse Pal firmware: a rising edge starts an idle
// channel in every mode, and in toggle mode it also stops a playing one. A falling edge stops a playing channel in
// gated mode (releaseGatedChannels()).
void processTriggerEdge(byte triggerChannel, boolean isRisingEdge) {
  if (!isRisingEdge) {
    if (TriggerMode[triggerChannel] == TRIGGER_MODE_GATED) {
      releaseGatedChannels(triggerChannel);
    }
    return;
  }
  screenSaverActivity = true; // loop() wakes the screen, after this interrupt (see updateScreenSaver())
  byte toStart = 0;
  byte toStop = 0;
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (!TriggerAddress[triggerChannel][i]) {
      continue;
    }
    if (!isPlaying(i)) {
      bitSet(toStart, i);
    } else if (TriggerMode[triggerChannel] == TRIGGER_MODE_TOGGLE) {
      bitSet(toStop, i);
    }
  }
  stopChannels(toStop);
  startChannels(toStart);
}

// A falling edge on a trigger channel in gated mode: stops the output channels it is linked to, unless the other
// trigger channel is also linked to them, in gated mode and still high
void releaseGatedChannels(byte triggerChannel) {
  byte otherChannel = 1 - triggerChannel;
  byte toStop = 0;
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (TriggerAddress[triggerChannel][i] && isPlaying(i)) {
      boolean heldByOther = TriggerAddress[otherChannel][i] && (TriggerMode[otherChannel] == TRIGGER_MODE_GATED) &&
                            triggerLineActive[otherChannel];
      if (!heldByOther) {
        bitSet(toStop, i);
      }
    }
  }
  stopChannels(toStop);
}
