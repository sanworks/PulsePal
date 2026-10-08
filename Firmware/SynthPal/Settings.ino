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


// The frequency and the output channel settings: their limits, each channel's output range, and handing new settings
// to the playback interrupts. The USB ops (USBOps.ino) and the joystick menu (Menu.ino) both change settings through
// the functions here, from loop().
//
// Functions in this file:
//   samplesPerCycleFor()
//   durationToSamples()
//   rampReciprocal()
//   durationsInSamples()
//   applyFrequency()
//   setFrequency()
//   setPlayDurations()
//   setRampDurations()
//   isValidOutputLevel()
//   fitAmplitude()
//   outputRangeFor()
//   exactCode()
//   nearestCode()
//   channelOutputFor()
//   updateChannelOutput()
//   takePendingOutputIfIdle()
//   paramSyncEnabled()
//   updateParamSyncPending()
//   isValidSettingsSet()
//   applySettings()
//   storeSettings()
//   takeSyncedSettings()
//   LoadDefaultSettings()
//
// OUTPUT RANGES
// Each channel's periodic waveform spans its mean voltage plus and minus half its amplitude, which must stay within
// +/-10V (isValidOutputLevel()). A fixed voltage (WAVEFORM_FIXED_VOLTAGE) is its amplitude, which is a voltage in its
// own right, -10V to 10V, and ignores the mean voltage. The resting voltage, which the channel outputs while idle, may be
// anywhere within +/-10V, and the ramps lead in straight lines from it to the waveform and back. So the channel's output
// stays within the span of its resting voltage and its waveform, and its output range is the one with the finest steps
// that holds that span (outputRangeFor()): 0-5V (76uV steps), then 0-10V or +/-5V (153uV), then +/-10V (305uV). The DAC
// has one output range register per channel, so the channels' ranges are independent.
// A new waveform, amplitude, mean or resting voltage is worked out into a ChannelOutput (updateChannelOutput()) and handed to
// handler(), which takes it on a tick, after that tick's DAC update (takePendingOutput() in Playback.ino). While the
// sample clock is stopped, loop() takes it at once. If the range changes, the DAC needs a range write as well as a
// code write, so the channel's output shows its old code in the new range for a fraction of a microsecond, as the
// range changes. A playing channel whose range does not change has no glitch: its next sample simply follows the new
// settings.

// The number of samples in one cycle at a frequency (in centiHz): the largest multiple of 4 that keeps the sampling rate
// at or below MAX_SAMPLING_RATE. 100000 at 1Hz, 332 at 300Hz, 4 at 20kHz.
uint32_t samplesPerCycleFor(uint32_t centiHz) {
  return 4 * (((uint32_t)MAX_SAMPLING_RATE * 25) / centiHz); // 25 = 100 centiHz per Hz / 4 samples
}

// A play duration in samples, at a sampling rate in centiHz (ditherDenominator). 0 (until stopped) stays 0, and any
// other duration lasts at least one sample.
uint32_t durationToSamples(uint32_t micros, uint32_t samplingRateCentiHz) {
  if (micros == 0) {
    return 0;
  }
  uint64_t samples = (((uint64_t)micros * samplingRateCentiHz) + 50000000ULL) / 100000000ULL; // Rounded
  return (samples == 0) ? 1 : (uint32_t)samples;
}

// The reciprocal of a ramp's length in samples, which the envelope is worked out with (fetchNextSample()). 0 for no ramp.
float rampReciprocal(uint32_t samples) {
  return (samples == 0) ? 0.0f : (1.0f / (float)samples);
}

// Each channel's play duration and ramps (from the settings arrays) in samples, at a sampling rate in centiHz
void durationsInSamples(uint32_t samplingRateCentiHz, uint32_t *durations, uint32_t *onRamps, uint32_t *offRamps) {
  for (byte i = 0; i < N_CHANNELS; i++) {
    durations[i] = durationToSamples(playDurationMicros[i], samplingRateCentiHz);
    onRamps[i] = durationToSamples(onRampMicros[i], samplingRateCentiHz);
    offRamps[i] = durationToSamples(offRampMicros[i], samplingRateCentiHz);
  }
}

// Sets the frequency of all output channels, in centiHz, and the sample clock that plays it, with each channel's play
// duration and ramps in samples at the new sampling rate (durationsInSamples()). Each playing channel keeps its place in
// the cycle, the time it has left to play and the envelope its ramp has reached, and the next sample period has the new
// length. Playback interrupt context (a param sync edge), or loop() with interrupts disabled (setFrequency()).
void applyFrequency(uint32_t newCentiHz, const uint32_t *newDurations, const uint32_t *newOnRamps,
                    const uint32_t *newOffRamps) {
  uint32_t newSamplesPerCycle = samplesPerCycleFor(newCentiHz);
  uint32_t newDenominator = newCentiHz * newSamplesPerCycle; // The sampling rate in centiHz, at most 10 million
  uint32_t oldSamplesPerCycle = samplesPerCycle;
  uint32_t oldDenominator = ditherDenominator;
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (isOutputActive(i)) {
      phase[i] = (uint32_t)((((uint64_t)phase[i] * newSamplesPerCycle) + (oldSamplesPerCycle / 2)) / oldSamplesPerCycle);
      if (phase[i] >= newSamplesPerCycle) {
        phase[i] = 0;
      }
      samplesPlayed[i] = (uint32_t)((((uint64_t)samplesPlayed[i] * newDenominator) + (oldDenominator / 2)) / oldDenominator);
      holdSamples[i] = (uint32_t)((((uint64_t)holdSamples[i] * newDenominator) + (oldDenominator / 2)) / oldDenominator);
    }
    playDurationSamples[i] = newDurations[i];
    bool rampChanges = ((rampStage[i] == STAGE_ON_RAMP) && (newOnRamps[i] != onRampSamples[i])) ||
                       ((rampStage[i] == STAGE_OFF_RAMP) && (newOffRamps[i] != offRampSamples[i]));
    onRampSamples[i] = newOnRamps[i];
    offRampSamples[i] = newOffRamps[i];
    onRampReciprocal[i] = rampReciprocal(newOnRamps[i]);
    offRampReciprocal[i] = rampReciprocal(newOffRamps[i]);
    if (rampChanges && isOutputActive(i)) {
      continueRamp(i);
    }
  }
  frequencyCentiHz = newCentiHz;
  samplesPerCycle = newSamplesPerCycle;
  samplesPerQuarter = newSamplesPerCycle / 4;
  samplesPerQuarterFloat = samplesPerQuarter;
  sawtoothDenominator = newSamplesPerCycle - 1;
  samplePeriodTicks = TIMER_TICKS_PER_SAMPLE_NUMERATOR / newDenominator;
  ditherRemainder = TIMER_TICKS_PER_SAMPLE_NUMERATOR % newDenominator;
  ditherDenominator = newDenominator;
  ditherAccumulator = 0;
  if (timerRunning) { // The period in progress keeps its length. The ones after it have the new length.
    nextPeriodLoad = nextSamplePeriodTicks() - 1;
    sampleClockChannel->LDVAL = nextPeriodLoad;
  }
}

// Sets the frequency of all output channels, in centiHz (MIN_FREQUENCY_CENTIHZ to MAX_FREQUENCY_CENTIHZ). Takes effect at
// once, also during playback (see applyFrequency()). Called from loop(): the durations are worked out before interrupts
// are disabled, to keep that short.
void setFrequency(uint32_t newCentiHz) {
  uint32_t newDurations[N_CHANNELS];
  uint32_t newOnRamps[N_CHANNELS];
  uint32_t newOffRamps[N_CHANNELS];
  durationsInSamples(newCentiHz * samplesPerCycleFor(newCentiHz), newDurations, newOnRamps, newOffRamps);
  noInterrupts();
  applyFrequency(newCentiHz, newDurations, newOnRamps, newOffRamps);
  interrupts();
}

// Sets the play duration of each output channel (one value per channel), in microseconds: 0 plays until stopped. Takes
// effect at once: a playing channel stops when its new duration has elapsed, or on its next sample if it already has.
void setPlayDurations(const uint32_t *newMicros) {
  uint32_t newSamples[N_CHANNELS];
  for (byte i = 0; i < N_CHANNELS; i++) {
    newSamples[i] = durationToSamples(newMicros[i], ditherDenominator); // Only loop() changes ditherDenominator
  }
  noInterrupts();
  for (byte i = 0; i < N_CHANNELS; i++) {
    playDurationMicros[i] = newMicros[i];
    playDurationSamples[i] = newSamples[i];
  }
  interrupts();
}

// Sets the on and off ramp durations of each output channel (one value per channel in each array), in microseconds: 0
// for no ramp. Takes effect at once: a channel in a ramp whose length changes keeps the envelope it has reached, and
// goes on at the new ramp's rate (see "Ramps" in Playback.ino).
void setRampDurations(const uint32_t *newOnMicros, const uint32_t *newOffMicros) {
  uint32_t newOnRamps[N_CHANNELS];
  uint32_t newOffRamps[N_CHANNELS];
  for (byte i = 0; i < N_CHANNELS; i++) {
    newOnRamps[i] = durationToSamples(newOnMicros[i], ditherDenominator); // Only loop() changes ditherDenominator
    newOffRamps[i] = durationToSamples(newOffMicros[i], ditherDenominator);
  }
  noInterrupts();
  for (byte i = 0; i < N_CHANNELS; i++) {
    bool rampChanges = ((rampStage[i] == STAGE_ON_RAMP) && (newOnRamps[i] != onRampSamples[i])) ||
                       ((rampStage[i] == STAGE_OFF_RAMP) && (newOffRamps[i] != offRampSamples[i]));
    onRampMicros[i] = newOnMicros[i];
    offRampMicros[i] = newOffMicros[i];
    onRampSamples[i] = newOnRamps[i];
    offRampSamples[i] = newOffRamps[i];
    onRampReciprocal[i] = rampReciprocal(newOnRamps[i]);
    offRampReciprocal[i] = rampReciprocal(newOffRamps[i]);
    if (rampChanges && isOutputActive(i)) {
      continueRamp(i);
    }
  }
  interrupts();
}

// True if a waveform (enum WaveformValue) can be played with a resting voltage, mean voltage and amplitude: every
// voltage on the output must stay within +/-10V. A periodic waveform's amplitude is peak to peak, 0 or more, and the
// waveform swings half of it either side of the mean voltage. A fixed voltage's amplitude is the voltage itself, and may
// be negative; the mean voltage does not apply to it. The resting voltage may be anywhere within +/-10V.
bool isValidOutputLevel(byte shape, int32_t restingMicrovolts, int32_t meanMicrovolts, int32_t amplitudeMicrovolts) {
  if ((restingMicrovolts > MAX_VOLTAGE_MICROVOLTS) || (restingMicrovolts < -MAX_VOLTAGE_MICROVOLTS) ||
      (meanMicrovolts > MAX_VOLTAGE_MICROVOLTS) || (meanMicrovolts < -MAX_VOLTAGE_MICROVOLTS)) {
    return false;
  }
  if (shape == WAVEFORM_FIXED_VOLTAGE) {
    return (amplitudeMicrovolts >= -MAX_VOLTAGE_MICROVOLTS) && (amplitudeMicrovolts <= MAX_VOLTAGE_MICROVOLTS);
  }
  if ((amplitudeMicrovolts < 0) || (amplitudeMicrovolts > MAX_AMPLITUDE_MICROVOLTS)) {
    return false;
  }
  return (2 * abs(meanMicrovolts)) + amplitudeMicrovolts <= 2 * MAX_VOLTAGE_MICROVOLTS;
}

// The valid amplitude nearest a channel's amplitude, for a waveform and mean voltage (with isValidOutputLevel()).
// The joystick menu uses it when it changes a channel's waveform: a fixed voltage of -5V becomes a 5V peak to peak wave,
// and a 20V peak to peak wave a fixed voltage of 10V.
int32_t fitAmplitude(byte shape, int32_t meanMicrovolts, int32_t amplitudeMicrovolts) {
  if (shape == WAVEFORM_FIXED_VOLTAGE) {
    return constrain(amplitudeMicrovolts, -MAX_VOLTAGE_MICROVOLTS, MAX_VOLTAGE_MICROVOLTS);
  }
  return min(abs(amplitudeMicrovolts), (2 * MAX_VOLTAGE_MICROVOLTS) - (2 * abs(meanMicrovolts)));
}

// The output range for a channel: the first range, in the order of enum OutputRange, that holds the lowest and highest
// voltages of its waveform and its resting voltage (see "Output ranges" above). They are compared at twice their value,
// so that half the amplitude needs no rounding.
byte outputRangeFor(byte shape, int32_t restingMicrovolts, int32_t meanMicrovolts, int32_t amplitudeMicrovolts) {
  int32_t lowest2 = min(2 * restingMicrovolts, (2 * meanMicrovolts) - amplitudeMicrovolts);
  int32_t highest2 = max(2 * restingMicrovolts, (2 * meanMicrovolts) + amplitudeMicrovolts);
  if (shape == WAVEFORM_FIXED_VOLTAGE) { // The resting voltage and the fixed voltage
    lowest2 = 2 * min(restingMicrovolts, amplitudeMicrovolts);
    highest2 = 2 * max(restingMicrovolts, amplitudeMicrovolts);
  }
  for (byte range = 0; range < RANGE_PLUS_MINUS_10V; range++) {
    if ((lowest2 >= 2 * rangeMinMicrovolts[range]) && (highest2 <= 2 * rangeMaxMicrovolts[range])) {
      return range;
    }
  }
  return RANGE_PLUS_MINUS_10V;
}

// A voltage as a DAC code in an output range: exact (not rounded), from 0 at the bottom of the range to 65536 at its top
double exactCode(int32_t microvolts, byte range) {
  return (microvolts - rangeMinMicrovolts[range]) * (65536.0 / (rangeMaxMicrovolts[range] - rangeMinMicrovolts[range]));
}

// The DAC code nearest an exact code. The top of a range (code 65536) is one step above the DAC's highest code.
uint16_t nearestCode(double code) {
  double nearest = floor(code + 0.5);
  return (nearest > 65535) ? 65535 : (uint16_t)nearest;
}

// What an output channel plays, worked out from a waveform and levels that isValidOutputLevel() accepts. See "Output
// ranges" above.
ChannelOutput channelOutputFor(byte shape, int32_t restingMicrovolts, int32_t meanMicrovolts, int32_t amplitudeMicrovolts) {
  ChannelOutput out;
  out.waveform = shape;
  out.range = outputRangeFor(shape, restingMicrovolts, meanMicrovolts, amplitudeMicrovolts);
  double codesPerMicrovolt = 65536.0 / (rangeMaxMicrovolts[out.range] - rangeMinMicrovolts[out.range]);
  double restingCode = exactCode(restingMicrovolts, out.range);
  out.restCode = nearestCode(restingCode);
  out.restCodeFraction = (float)(restingCode - floor(restingCode + 0.5));
  double meanCode = exactCode(meanMicrovolts, out.range);
  out.meanCode = nearestCode(meanCode);
  out.meanCodeFraction = (float)(meanCode - floor(meanCode + 0.5));
  if (shape == WAVEFORM_FIXED_VOLTAGE) { // The amplitude is a voltage, rounded to a code as the resting voltage is
    double fixedCode = exactCode(amplitudeMicrovolts, out.range);
    out.fixedCode = nearestCode(fixedCode);
    out.halfAmplitudeCodes = 0;
    out.rampOffsetCodes = (float)(fixedCode - restingCode);
  } else {
    out.fixedCode = 0;
    out.halfAmplitudeCodes = (float)(amplitudeMicrovolts * 0.5 * codesPerMicrovolt);
    out.rampOffsetCodes = (float)(meanCode - restingCode);
  }
  return out;
}

// Works out what an output channel plays from its settings (waveform[], amplitudeMicrovolts[], meanVoltageMicrovolts[]
// and restingVoltageMicrovolts[], which the caller has checked with isValidOutputLevel()), and hands it to the playback
// interrupts. Called from loop().
void updateChannelOutput(byte channel) {
  ChannelOutput out = channelOutputFor(waveform[channel], restingVoltageMicrovolts[channel],
                                       meanVoltageMicrovolts[channel], amplitudeMicrovolts[channel]);
  noInterrupts();
  pendingOutput[channel] = out;
  if (timerRunning) {
    bitSet(pendingOutputChannels, channel); // handler() takes it on a tick
  } else {
    takePendingOutput(channel);
  }
  interrupts();
}

// Takes the output settings that handler() left pending when the sample clock stopped, one channel per call. loop()
// calls this on every pass.
void takePendingOutputIfIdle() {
  noInterrupts();
  if (!timerRunning && pendingOutputChannels) {
    takePendingOutput(__builtin_ctz(pendingOutputChannels));
  }
  interrupts();
}

// True if a trigger channel is in param sync mode, in which case op 85 stores its settings for the channel's next
// rising edge instead of applying them
bool paramSyncEnabled() {
  return (TriggerMode[0] == TRIGGER_MODE_PARAM_SYNC) || (TriggerMode[1] == TRIGGER_MODE_PARAM_SYNC);
}

// Discards a set that op 85 stored, once no trigger channel is left in param sync mode, so that a channel put back into
// the mode later cannot load a set sent long before. Call it after anything except a param sync edge changes
// TriggerMode: op 84, op 85 outside param sync mode, the menu, and the default settings.
void updateParamSyncPending() {
  if (!paramSyncEnabled()) {
    paramSyncPending = false;
  }
}

// True if every setting in a set is in range, and every channel's levels suit its waveform
bool isValidSettingsSet(const SettingsSet &set) {
  if ((set.frequencyCentiHz < MIN_FREQUENCY_CENTIHZ) || (set.frequencyCentiHz > MAX_FREQUENCY_CENTIHZ) ||
      (set.triggerMode[0] > MAX_TRIGGER_MODE) || (set.triggerMode[1] > MAX_TRIGGER_MODE)) {
    return false;
  }
  for (byte i = 0; i < N_CHANNELS; i++) {
    if ((set.waveform[i] > MAX_WAVEFORM) ||
        !isValidOutputLevel(set.waveform[i], set.restingVoltageMicrovolts[i], set.meanVoltageMicrovolts[i],
                            set.amplitudeMicrovolts[i]) ||
        (set.playDurationMicros[i] > MAX_PLAY_DURATION_MICROS) || (set.onRampMicros[i] > MAX_PLAY_DURATION_MICROS) ||
        (set.offRampMicros[i] > MAX_PLAY_DURATION_MICROS) || (set.triggerLinks[0][i] > 1) ||
        (set.triggerLinks[1][i] > 1)) {
      return false;
    }
  }
  return true;
}

// Applies a whole set of settings at once (op 85 while no trigger channel is in param sync mode), as the ops for each
// setting would. The set has been through isValidSettingsSet().
void applySettings(const SettingsSet &set) {
  if (set.frequencyCentiHz != frequencyCentiHz) {
    setFrequency(set.frequencyCentiHz);
  }
  for (byte i = 0; i < N_CHANNELS; i++) {
    waveform[i] = set.waveform[i];
    amplitudeMicrovolts[i] = set.amplitudeMicrovolts[i];
    meanVoltageMicrovolts[i] = set.meanVoltageMicrovolts[i];
    restingVoltageMicrovolts[i] = set.restingVoltageMicrovolts[i];
    updateChannelOutput(i);
  }
  setPlayDurations(set.playDurationMicros);
  setRampDurations(set.onRampMicros, set.offRampMicros);
  noInterrupts(); // So that a trigger sees all the new links and modes, or none
  memcpy((void*)TriggerAddress, set.triggerLinks, sizeof(TriggerAddress));
  TriggerMode[0] = set.triggerMode[0];
  TriggerMode[1] = set.triggerMode[1];
  interrupts();
  updateParamSyncPending();
}

// Stores a set of settings for the next param sync edge (op 85 while a trigger channel is in param sync mode),
// replacing any set stored before. What each channel plays is worked out here, so that the edge only copies it. The
// set has been through isValidSettingsSet().
void storeSettings(SettingsSet &set) {
  for (byte i = 0; i < N_CHANNELS; i++) {
    set.output[i] = channelOutputFor(set.waveform[i], set.restingVoltageMicrovolts[i], set.meanVoltageMicrovolts[i],
                                     set.amplitudeMicrovolts[i]);
  }
  noInterrupts(); // An edge must never load a half-copied set
  storedSettings = set;
  paramSyncPending = true;
  interrupts();
}

// Copies the settings that output channels took at a param sync edge into the settings arrays, which loop() works
// from: the menu, the USB ops and frequency changes. The playback interrupts never write those arrays (see "Param
// sync" in Playback.ino). The channels' settings are then handed to the playback interrupts again, from the arrays:
// if an op or a menu edit changed a channel while the edge was being handled, the synced set wins, as it would have a
// moment later. loop() calls this on every pass, before it reads a command.
void takeSyncedSettings() {
  if (!syncedChannelsForLoop) {
    return;
  }
  noInterrupts();
  byte channels = syncedChannelsForLoop;
  syncedChannelsForLoop = 0;
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (bitRead(channels, i)) {
      waveform[i] = syncedSettings.waveform[i];
      amplitudeMicrovolts[i] = syncedSettings.amplitudeMicrovolts[i];
      meanVoltageMicrovolts[i] = syncedSettings.meanVoltageMicrovolts[i];
      restingVoltageMicrovolts[i] = syncedSettings.restingVoltageMicrovolts[i];
      playDurationMicros[i] = syncedSettings.playDurationMicros[i];
      onRampMicros[i] = syncedSettings.onRampMicros[i];
      offRampMicros[i] = syncedSettings.offRampMicros[i];
    }
  }
  interrupts();
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (bitRead(channels, i)) {
      updateChannelOutput(i); // The same output the channel already took, unless an op changed it meanwhile
    }
  }
  setPlayDurations(playDurationMicros);
  setRampDurations(onRampMicros, offRampMicros);
}

// The settings the device starts with, also loaded after a comm failure. The Python and MATLAB classes program the same
// ones when they connect.
void LoadDefaultSettings() {
  setFrequency(DEFAULT_FREQUENCY_CENTIHZ);
  uint32_t durations[N_CHANNELS];
  uint32_t noRamps[N_CHANNELS] = {0};
  for (byte i = 0; i < N_CHANNELS; i++) {
    waveform[i] = WAVEFORM_SINE;
    amplitudeMicrovolts[i] = DEFAULT_AMPLITUDE_MICROVOLTS;
    restingVoltageMicrovolts[i] = 0;
    meanVoltageMicrovolts[i] = 0;
    updateChannelOutput(i);
    durations[i] = DEFAULT_PLAY_DURATION_MICROS;
  }
  setPlayDurations(durations);
  setRampDurations(noRamps, noRamps);
  noInterrupts();
  for (byte i = 0; i < N_CHANNELS; i++) {
    TriggerAddress[0][i] = 1; // All output channels are triggered by trigger channel 1
    TriggerAddress[1][i] = 0;
  }
  TriggerMode[0] = TRIGGER_MODE_NORMAL;
  TriggerMode[1] = TRIGGER_MODE_NORMAL;
  interrupts();
  updateParamSyncPending(); // Neither trigger channel is in param sync mode now, so a stored set is discarded
}
