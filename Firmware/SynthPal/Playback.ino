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
//   seedNoise()
//   noiseSample()
//   waveformValue()
//   synthesizeCode()
//   setNextCode()
//   endPlayback()
//   fetchNextSample()
//   handler()
//   continueRamp()
//   takePendingOutput()
//   startChannels()
//   stopChannels()
//   isPlaying()
//   isOutputActive()
//   trigger1ISR(), trigger2ISR()
//   triggerInterrupt()
//   handleTriggerLine()
//   processTriggerEdge()
//   releaseGatedChannels()
//   startParamSync()
//   loadSyncedChannel()
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
// scaled by the amplitude around the mean voltage, in DAC codes of the channel's output range. Each channel has its
// own output range on the DAC, the one with the finest steps that holds its whole waveform and its resting voltage
// (Settings.ino). A fixed voltage (WAVEFORM_FIXED_VOLTAGE) is the same code on every sample, so it is written once, as it
// starts: it plays like the other waveforms (start, ramps, play duration, stop, triggers), on the same sample clock.
// White noise (WAVEFORM_WHITE_NOISE) takes a new random value on every sample in place of the waveform's value at its
// phase (noiseSample()), and is otherwise played as a periodic waveform is: it swings around the mean voltage within
// the amplitude, at the sampling rate the frequency sets, through the same ramps. The frequency sets only how often it
// changes, so its spectrum is whatever that sampling rate and the DAC make of it.
//
// Ramps. Each sample has an envelope, 0 to 1, which fades the waveform in from the resting voltage and back to it: at
// envelope e, the output is the resting voltage plus e times (the waveform at full amplitude minus the resting voltage).
// So the amplitude scales with e, and the waveform's mean moves in a straight line from the resting voltage to the mean
// voltage (for a fixed voltage, the output itself does). A playing channel is in one of three stages (rampStage[]):
//   STAGE_ON_RAMP   onRampSamples samples after each start, with envelopes 0, 1/N, 2/N ... (N-1)/N: the first sample
//                   is the resting voltage, and the envelope rises linearly to 1 at the end of the on ramp.
//   STAGE_HOLD      playDurationSamples samples (or until stopped) at full amplitude.
//   STAGE_OFF_RAMP  offRampSamples samples with envelopes 1, (M-1)/M ... 1/M, then the resting voltage, and the channel
//                   stops. The off ramp starts when the play duration ends, or when the channel is stopped (op 88, the
//                   menu, a toggle or gated trigger, a comm failure).
// So the ramps lengthen playback: from a trigger to the resting voltage takes the on ramp, the play duration and the
// off ramp. The ramps count in steps (rampPosition[]), so they are exact to the sample. A channel stopped part way
// through its on ramp falls from the envelope it has reached, at the off ramp's rate; a channel triggered again during
// its off ramp (which counts as stopping, as in isPlaying()) rises from the envelope it has reached, at the on ramp's
// rate, without starting its waveform's cycle again, and plays its play duration again: the output never jumps. With no
// ramps (0, the default), the first sample is at full amplitude and a stop fetches the resting voltage at once.
//
// Starting. A channel starts at phase 0 of its waveform. If the sample clock is stopped (no channel was playing), the
// trigger computes the first samples, starts the clock from that moment, and latches them DAC_LATCH_US later, as on
// every later tick: the waveform starts a fixed time after its trigger (about 8us), and its first sample lasts as long
// as the others. If the clock is running, the channel starts on its next tick, so a channel that starts while another
// plays can start up to one sample period after its trigger. The channels share the clock, so this cannot be avoided
// without disturbing the others.
//
// Stopping. When a channel's play duration has elapsed, or it is stopped, and its off ramp is over (or it has none),
// handler() fetches its resting voltage instead of a sample, writes it on the next tick, and stops the channel
// (stopAfterWrite). When no channel plays, handler() stops the clock.
//
// Changing settings. The frequency, play durations, ramps and trigger settings take effect at once, also during
// playback: a new frequency keeps each playing channel's phase, the time it has left to play and its place in its
// ramps, and new ramp durations keep the envelope a ramp has reached. A channel's waveform, amplitude, mean and resting
// voltage are handed to handler() in pendingOutput[], and it takes one channel's at a time, on a tick, after the
// DAC update (takePendingOutput()). If the output range changes, the DAC then needs a range write and a code write for
// that channel, and its output shows the old code in the new range for a fraction of a microsecond, too short to see on a
// scope (switching from +/-5V to +/-10V during playback). Settings.ino has the details.
//
// Triggers. TTL edges on the trigger channels raise an interrupt (trigger1ISR() and trigger2ISR()), so their timing
// does not depend on a polling rate. What an edge does depends on the trigger channel's mode (processTriggerEdge()).
// Soft triggers (op 80) start idle channels, as in Pulse Pal firmware.
//
// Param sync. As in Pulse Pal firmware, param sync mode (TRIGGER_MODE_PARAM_SYNC) lets the settings of the next trial
// be sent during the current one, and applied the instant it starts. While a trigger channel is in the mode, op 85
// (every setting at once) is stored in storedSettings instead of applied, and the channel's next rising edge loads it
// (startParamSync()); the other ops still apply at once. The edge starts and stops nothing. At the edge:
//   - The frequency and the trigger modes change at once: they are shared by all channels.
//   - An output channel at rest takes its new settings at once (loadSyncedChannel()): what it plays (pendingOutput[],
//     as for a change from loop()), its play duration, ramps and trigger links.
//   - A channel that is playing, off ramp included, finishes on the settings it started with (at the new frequency),
//     and takes the new ones the moment it reaches its resting voltage (paramSyncChannelsWaiting, in handler()). A
//     second edge before then gives it the newer set.
// The set is copied to syncedSettings at the edge, so that op 85 can store the next one meanwhile. The playback
// interrupts never write loop()'s settings arrays (waveform[], amplitudeMicrovolts[], ...): loop() copies a channel's
// synced settings into them on its next pass (takeSyncedSettings() in Settings.ino). When the same TTL reaches both
// trigger channels, the param sync edge is handled first, so that a channel the other edge starts plays the new
// settings (triggerInterrupt()). A start also takes a channel's pendingOutput[] first, for the same reason.
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
// exactly 1 and -1. A fixed voltage and white noise never come here: see synthesizeCode().
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

// Seeds each channel's white noise generator (noiseSample()) at startup, with splitmix32 (the MurmurHash3 finalizer
// applied to a Weyl sequence), so that the four channels' sequences are unrelated and no state is all zeros. The
// sequences start from the same point at every power-up. Startup only, so it runs from flash (FLASHMEM).
FLASHMEM void seedNoise() {
  uint32_t weyl = 0;
  for (byte i = 0; i < N_CHANNELS; i++) {
    for (byte k = 0; k < 4; k++) {
      uint32_t z = (weyl += 0x9E3779B9);
      z = (z ^ (z >> 16)) * 0x85EBCA6B;
      z = (z ^ (z >> 13)) * 0xC2B2AE35;
      noiseState[i][k] = z ^ (z >> 16);
    }
  }
}

// White noise: the next value of a channel's random sequence, uniform from -1 to 1. Each channel has its own generator
// (xoshiro128+, Blackman and Vigna 2018, which its authors recommend for floating point values; period 2^128 - 1). It
// carries on from one playback to the next, so every playback is new noise, and the channels are unrelated: a 32-bit
// generator shared by four channels at different points of one 2^32 cycle could, after hours of uneven use, replay one
// channel's noise on another. The 32 bits of its output, as a signed integer times 2^-31, are uniform from -1 to 1; the
// float keeps their top 24 bits, the generator's best. About 10 integer operations: less time than a sine sample.
static inline float noiseSample(byte channel) {
  uint32_t *s = noiseState[channel];
  uint32_t result = s[0] + s[3];
  uint32_t t = s[1] << 9;
  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];
  s[2] ^= t;
  s[3] = (s[3] << 11) | (s[3] >> 21);
  return (float)(int32_t)result * 4.656612873077393e-10f; // 2^-31
}

// The value of a channel's waveform for sample n of its cycle, from -1 to 1: a periodic waveform's value at that phase,
// or the next value of the channel's white noise
static inline float waveformValue(byte channel, byte shape, uint32_t n) {
  return (shape == WAVEFORM_WHITE_NOISE) ? noiseSample(channel) : unitWaveform(shape, n);
}

// The DAC code of sample n (0 to samplesPerCycle - 1) of a channel's waveform, at an envelope (see "Ramps" above), in
// the channel's output range. At full amplitude (envelope 1), the offset from the mean voltage's code is rounded half
// away from zero, so samples the same distance above and below the mean voltage are the same number of codes from it:
// when the mean voltage falls on a DAC code (as 0V does in the bipolar ranges), the waveform's mean is exactly that
// code. Adding the offset to the code in floating point first would round values above 32768 more coarsely than those
// below it. In a ramp, the envelope scales the offset from the resting voltage's code instead. The multiply-adds are
// fused (fmaf()) by name, so that they round once whatever the compiler would choose, as the hardware test's model
// expects: the compiler fused the full amplitude one, unasked, once the ramps were added.
static inline uint16_t synthesizeCode(byte channel, uint32_t n, float envelope) {
  const ChannelOutput &out = activeOutput[channel];
  if (envelope <= 0.0f) {
    return out.restCode;
  }
  bool isFixed = (out.waveform == WAVEFORM_FIXED_VOLTAGE); // Not periodic, and not around the mean voltage
  int32_t code;
  if (envelope >= 1.0f) {
    if (isFixed) {
      return out.fixedCode;
    }
    float offset = fmaf(out.halfAmplitudeCodes, waveformValue(channel, out.waveform, n), out.meanCodeFraction);
    code = (int32_t)out.meanCode + (int32_t)roundf(offset);
  } else {
    float target = isFixed ? out.rampOffsetCodes
                           : fmaf(out.halfAmplitudeCodes, waveformValue(channel, out.waveform, n),
                                  out.rampOffsetCodes);
    code = (int32_t)out.restCode + (int32_t)roundf(fmaf(target, envelope, out.restCodeFraction));
  }
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

// Fetches the next sample of a playing channel into dacValue[], for the next tick, and moves the channel on through its
// ramps and play duration (see "Ramps" above). Called from the playback interrupts, or from loop() with interrupts
// disabled.
static inline void fetchNextSample(byte channel) {
  float envelope = 1.0f;
  switch (rampStage[channel]) {
    case STAGE_ON_RAMP: {
      uint32_t position = rampPosition[channel];
      if (position < onRampSamples[channel]) {
        envelope = (float)position * onRampReciprocal[channel];
        rampPosition[channel] = position + 1;
        break;
      }
      rampStage[channel] = STAGE_HOLD; // The on ramp is over (or there is none)
    } // Fall through
    case STAGE_HOLD: {
      if (!playDurationSamples[channel] || (holdSamples[channel] < playDurationSamples[channel])) {
        holdSamples[channel]++;
        break;
      }
      rampStage[channel] = STAGE_OFF_RAMP; // The play duration has elapsed: the off ramp starts from full amplitude
      rampPosition[channel] = offRampSamples[channel];
    } // Fall through
    default: { // STAGE_OFF_RAMP
      uint32_t position = rampPosition[channel];
      if (position == 0) {
        endPlayback(channel); // The off ramp is over (or there is none)
        return;
      }
      envelope = (position >= offRampSamples[channel]) ? 1.0f : ((float)position * offRampReciprocal[channel]);
      rampPosition[channel] = position - 1;
    } break;
  }
  uint16_t code = synthesizeCode(channel, phase[channel], envelope);
  fetchedEnvelope[channel] = envelope;
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
  byte stoppedWaiting = 0; // Channels that stop now, and were playing at a param sync edge
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (stopAfterWrite[i]) { // Its resting voltage is in this update
      playing[i] = false;
      stopAfterWrite[i] = false;
      digitalWriteFast(OutputLEDLines[i], LOW);
      if (bitRead(paramSyncChannelsWaiting, i)) {
        bitSet(stoppedWaiting, i);
      }
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
  for (byte i = 0; i < N_CHANNELS; i++) { // At rest now: they take the settings of the param sync edge (after the latch)
    if (bitRead(stoppedWaiting, i)) {
      loadSyncedChannel(i);
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

// Places a playing channel's next sample one step of its ramp on from the envelope of the sample it fetched last, in the
// ramp it is in (rampStage[]): so a channel that starts again in its off ramp, stops in its on ramp, or whose ramp
// lengths change keeps the envelope it has reached, and goes on at its ramp's rate (see "Ramps" above). The step is
// rounded: a jump of at most half a step. Interrupt context, or loop() with interrupts disabled.
static inline void continueRamp(byte channel) {
  float envelope = fetchedEnvelope[channel];
  if (rampStage[channel] == STAGE_ON_RAMP) { // Rising from the fetched sample (with no on ramp, to full amplitude)
    rampPosition[channel] = (uint32_t)(((double)envelope * onRampSamples[channel]) + 0.5) + 1;
  } else if (rampStage[channel] == STAGE_OFF_RAMP) { // Falling from it (with no off ramp, to the resting voltage)
    uint32_t steps = (uint32_t)(((double)envelope * offRampSamples[channel]) + 0.5);
    rampPosition[channel] = (steps > 0) ? (steps - 1) : 0;
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
  boolean channelPlays = isOutputActive(channel);
  uint16_t code = activeOutput[channel].restCode;
  if (channelPlays) {
    uint32_t current = (phase[channel] == 0) ? (samplesPerCycle - 1) : (phase[channel] - 1); // The sample latched last
    code = synthesizeCode(channel, current, fetchedEnvelope[channel]);
  }
  if (activeOutput[channel].range != oldRange) {
    dacSwitchRange(channel, code);
  } else if (!channelPlays && (code != dacOutput[channel])) {
    dacWriteNow(channel, code);
  }
}

// Starts the selected channels (one bit per channel) from phase 0 of their waveforms, at the start of their on ramps.
// Channels that are playing are left alone. A channel in its off ramp rises again from the envelope it has reached,
// where its waveform is (see "Ramps" above). Interrupt context, or loop() with interrupts disabled.
void startChannels(byte channelBits) {
  byte toStart = 0;
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (bitRead(channelBits, i) && !isPlaying(i)) {
      if (isOutputActive(i)) { // In its off ramp
        rampStage[i] = STAGE_ON_RAMP;
        continueRamp(i);
        holdSamples[i] = 0; // The play duration starts again
      } else {
        bitSet(toStart, i);
      }
    }
  }
  if (!toStart) {
    return;
  }
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (bitRead(toStart, i)) {
      if (bitRead(pendingOutputChannels, i)) { // New settings handler() has not taken yet: the channel plays them
        takePendingOutput(i);
      }
      phase[i] = 0;
      rampStage[i] = STAGE_ON_RAMP;
      rampPosition[i] = 0;
      holdSamples[i] = 0;
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

// Stops the selected channels (one bit per channel). A channel with an off ramp starts it, from the envelope of the
// sample fetched for the next tick (see "Ramps" above). One without returns to its resting voltage on the next tick.
// Interrupt context, or loop() with interrupts disabled.
void stopChannels(byte channelBits) {
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (bitRead(channelBits, i) && isPlaying(i)) {
      if (offRampSamples[i]) { // The sample fetched for the next tick plays, and the off ramp goes on from it
        rampStage[i] = STAGE_OFF_RAMP;
        continueRamp(i);
      } else {
        // The resting voltage replaces the sample fetched for the next tick, so that sample is never played: take it
        // out of the counts that op 90 reports. (A channel that ends at its play duration fetches no extra sample.)
        samplesPlayed[i]--;
        sampleSum[i] -= dacValue[i];
        endPlayback(i);
      }
    }
  }
}

// True if a channel is playing: in its on ramp, or at full amplitude. A channel in its off ramp, or that has stopped and
// is waiting for its resting voltage to be written, counts as stopping, so that a trigger can start it again at once.
boolean isPlaying(byte channel) {
  return playing[channel] && !stopAfterWrite[channel] && (rampStage[channel] != STAGE_OFF_RAMP);
}

// True if a channel's output is not at its resting voltage, or will not be after the next tick: it is playing, or in
// its off ramp. Op 71 reports these channels as playing.
boolean isOutputActive(byte channel) {
  return playing[channel] && !stopAfterWrite[channel];
}

// Pin interrupts for the trigger channels, on every edge. See setup() for their priority.
void trigger1ISR() {
  triggerInterrupt(0);
}

void trigger2ISR() {
  triggerInterrupt(1);
}

// A trigger channel's (0 or 1) pin interrupt. When the same TTL reaches both trigger channels and one is in param sync
// mode, either interrupt can run first: so if the other channel is in param sync mode and has just gone high, its edge
// is handled first, and the settings load before this edge starts anything (see "Param sync" above). Its own interrupt
// then finds the edge handled.
void triggerInterrupt(byte triggerChannel) {
  byte otherChannel = 1 - triggerChannel;
  boolean levels[2] = {digitalReadFast(TriggerLines[0]) == TriggerLevel, digitalReadFast(TriggerLines[1]) == TriggerLevel};
  if ((TriggerMode[otherChannel] == TRIGGER_MODE_PARAM_SYNC) && levels[otherChannel] &&
      !triggerLineActive[otherChannel]) {
    handleTriggerLine(otherChannel, true);
    edgeHandledEarly[otherChannel] = true;
  }
  if (edgeHandledEarly[triggerChannel]) {
    edgeHandledEarly[triggerChannel] = false;
    if (levels[triggerChannel] == triggerLineActive[triggerChannel]) {
      return; // The other channel's interrupt handled this edge, and the level has not changed since
    }
  }
  handleTriggerLine(triggerChannel, levels[triggerChannel]);
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
// gated mode (releaseGatedChannels()). In param sync mode, a rising edge loads the stored settings, and starts and stops
// nothing: its links are ignored. That includes an edge whose set moves it to another mode, which applies from the
// next edge.
void processTriggerEdge(byte triggerChannel, boolean isRisingEdge) {
  if (TriggerMode[triggerChannel] == TRIGGER_MODE_PARAM_SYNC) {
    if (isRisingEdge) {
      screenSaverActivity = true;
      if (paramSyncPending) {
        startParamSync();
      }
    }
    return;
  }
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

// A rising edge on a trigger channel in param sync mode, with a set stored (see "Param sync" above). The set becomes
// syncedSettings, the frequency and the trigger modes change at once, and each output channel at rest takes its new
// settings; the others take them when they stop. Playback interrupt context: it neither waits nor touches the screen
// or the USB port. Measured: about 2us with all four channels at rest, plus about 2.5us for each one whose output range
// changes (the DAC writes are made here only while the sample clock is stopped), and about 3us with four channels
// playing and a new frequency.
void startParamSync() {
  paramSyncPending = false;
  syncedSettings = storedSettings;
  TriggerMode[0] = syncedSettings.triggerMode[0];
  TriggerMode[1] = syncedSettings.triggerMode[1];
  if (syncedSettings.frequencyCentiHz != frequencyCentiHz) {
    // The channels still playing keep their durations from the settings arrays; those that take the set get its own
    uint32_t newCentiHz = syncedSettings.frequencyCentiHz;
    uint32_t newDurations[N_CHANNELS];
    uint32_t newOnRamps[N_CHANNELS];
    uint32_t newOffRamps[N_CHANNELS];
    durationsInSamples(newCentiHz * samplesPerCycleFor(newCentiHz), newDurations, newOnRamps, newOffRamps);
    applyFrequency(newCentiHz, newDurations, newOnRamps, newOffRamps);
  }
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (isOutputActive(i)) {
      bitSet(paramSyncChannelsWaiting, i); // It takes the set when it reaches its resting voltage
    } else {
      loadSyncedChannel(i);
    }
  }
}

// Gives an output channel at rest its settings from syncedSettings: what it plays (pendingOutput[], taken as a change
// from loop() is), its play duration and ramps in samples at the sampling rate in use, and its trigger links. loop()
// copies the rest into its settings arrays (takeSyncedSettings()). Playback interrupt context.
void loadSyncedChannel(byte channel) {
  pendingOutput[channel] = syncedSettings.output[channel];
  if (timerRunning) {
    bitSet(pendingOutputChannels, channel); // handler() takes it on a tick, or a start takes it first
  } else {
    takePendingOutput(channel);
  }
  uint32_t onRamp = durationToSamples(syncedSettings.onRampMicros[channel], ditherDenominator);
  uint32_t offRamp = durationToSamples(syncedSettings.offRampMicros[channel], ditherDenominator);
  playDurationSamples[channel] = durationToSamples(syncedSettings.playDurationMicros[channel], ditherDenominator);
  onRampSamples[channel] = onRamp;
  offRampSamples[channel] = offRamp;
  onRampReciprocal[channel] = rampReciprocal(onRamp);
  offRampReciprocal[channel] = rampReciprocal(offRamp);
  TriggerAddress[0][channel] = syncedSettings.triggerLinks[0][channel];
  TriggerAddress[1][channel] = syncedSettings.triggerLinks[1][channel];
  bitClear(paramSyncChannelsWaiting, channel);
  bitSet(syncedChannelsForLoop, channel);
}
