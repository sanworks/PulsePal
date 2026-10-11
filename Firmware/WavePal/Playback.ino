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


// Waveform playback: the sample clock interrupt, the trigger inputs, param sync, and starting and stopping output
// channels.
//
// Functions in this file:
//   bufferForChunk()
//   channelStartBlock()
//   endPlayback()
//   fetchNextSample()
//   handler()
//   anyChannelPlaying()
//   startChannels()
//   holdChannels()
//   stopChannels()
//   channelsPlayingBlock()
//   softTrigger()
//   releaseGatedChannels()
//   trigger1ISR(), trigger2ISR()
//   triggerInterrupt()
//   handleTriggerLine()
//   processTriggerEdge()
//   anyParamSyncChannel()
//   applyStoredSet()
//   setSamplingRate()
//
// ---------------------------------------------------------------------------------------------------------------
// WAVEFORMS
//
// The device stores N_WAVEFORMS waveforms, numbered 1-4 over USB (0-3 in arrays). Each output channel plays one of
// them, its current waveform (currentWaveform[], set by op 67: 1-4, or 0 for none), so several channels can play the
// same waveform. A channel with no current waveform, or whose current waveform is empty, ignores triggers.
//
// Each waveform has two memory blocks, each a pre-buffer in RAM (preBuffer[]) and a region of the data file on the
// microSD card (see Storage.ino): waveform w has blocks 2w and 2w+1. One of them is the waveform's current block
// (currentBlock[w]), which channels play. Op 76 loads the other, the unused block (loadWaveform() in USBOps.ino):
//   - Outside param sync mode, loading a waveform stops the channels playing it, and empties its current block, so
//     that triggers ignore it until the new samples are stored. The loaded block then becomes current.
//   - In param sync mode, channels keep playing the current block, and triggers still start it. The loaded block is
//     stored for the next param sync edge (loadStored[]), which makes it current. See "Param sync" below.
// A channel chooses its memory block when it starts (playBlock[], startChannels()), and plays that block until it
// stops, whatever changes meanwhile: its current waveform, or its waveform's current block. Loading a block stops the
// channels still playing it, since its samples are about to be replaced. In param sync mode that is a channel that
// started before the previous edge, and is still playing.
//
// Param sync. As in Pulse Pal and Synth Pal firmware, param sync mode (TRIGGER_MODE_PARAM_SYNC) lets what the next
// trial plays be sent during the current one, and take effect the instant it starts. While a trigger channel is in
// param sync mode, op 76 stores the waveform it loads in the waveform's unused block (loadStored[]), and op 67 stores
// the current waveforms it sends (storedCurrentWaveform[]). A later op replaces what an earlier one stored. A rising
// edge on a trigger channel in param sync mode makes what is stored current (applyStoredSet()). It starts and stops
// nothing: its links are ignored, and channels that are playing finish the memory blocks they play. When the same TTL
// also reaches the other trigger channel, the param sync edge is handled first (triggerInterrupt()), so the channels
// the other edge starts play the new waveforms. When no trigger channel is left in param sync mode, what is stored
// becomes current at once, as at an edge, so nothing is left stored for later. (Pulse Pal and Synth Pal firmware
// discard a stored set then instead. Here the clients' record of the waveforms they loaded stays true.)
//
// ---------------------------------------------------------------------------------------------------------------
// HOW PLAYBACK WORKS
//
// The sample clock. handler() runs once per sample period, from hardwareTimer, while any channel is playing. First it
// writes the samples it fetched on the previous run to the DAC, so that the outputs change at exact intervals however
// long the rest of the run takes. Then it fetches each playing channel's next sample into dacValue[]. When the last
// channel stops, it stops the timer. So a sample is written one sample period after it is fetched.
//
// Starting. startChannels() fetches the first sample. If the sample clock is stopped (no channel was playing), it
// starts the timer, writes that sample to the DAC at once and fetches the second: the waveform starts within
// microseconds of the trigger, and the next sample comes one period later. If the clock is running, the first
// sample is written on its next tick, so a channel that starts while another plays can start up to one sample period
// after its trigger. The channels share the clock, so this cannot be avoided without disturbing the others.
//
// Triggers. TTL edges on the trigger channels raise an interrupt (trigger1ISR() and trigger2ISR()), so their timing
// does not depend on a polling rate. What an edge does to the output channels linked to it depends on the trigger
// channel's mode, as in Pulse Pal firmware (processTriggerEdge()). Soft triggers from USB (op 80) start idle channels,
// as in Pulse Pal firmware, whatever the modes (softTrigger()).
//
// Buffers. A waveform is played in chunks of BUFFER_SAMPLES samples. Chunk 0 is in its memory block's pre-buffer,
// filled when the waveform is loaded and kept in RAM, so a triggered waveform starts at once. Later chunks are read
// from the microSD card by loop() (refillPlaybackBuffers() in Storage.ino) into the channel's two playback buffers:
// odd chunks into buffer A (e.g. channel1BufferA), even chunks into buffer B, so consecutive chunks never share a
// buffer. While one chunk plays, loop() reads the next one into the other buffer. In loop mode, the chunk after the
// last one is chunk 0. bufferChunk[channel][buffer] and bufferBlock[channel][buffer] say which chunk of which memory
// block a buffer holds: a channel can play another block from its next start, so the chunk alone does not say whether
// a buffer holds what the channel needs. loop() sets bufferChunk to CHUNK_NONE before it reads into a buffer, and sets
// both once the read has finished, so handler() never plays from a buffer being filled, nor a chunk of another block.
//
// Underruns. If handler() reaches a chunk that is not in its buffer yet, it holds the output at the last sample and
// keeps counting samples, so the waveform keeps its timing; when the chunk arrives, playback continues from where the
// waveform should be by then. Each chunk that was late is counted in underrunCount[] (reported by op 71).
//
// Stopping. A waveform ends when its last sample has played (not in loop mode), or when the loop duration has elapsed.
// handler() then fetches 0V instead of a sample, writes it on the next tick, and stops the channel (stopAfterWrite).
// A channel stopped from outside (op 88, the menu, a toggle or gated trigger) goes to 0V at once: stopChannels() writes
// it to the DAC immediately.
//
// Interrupt rules. handler() and the trigger interrupts run at the same priority (PLAYBACK_IRQ_PRIORITY), so neither
// can interrupt the other. That makes it safe for both to write to the DAC over SPI. loop() calls the functions below
// only with interrupts disabled (noInterrupts() ... interrupts()), for the same reason: an SPI transfer interrupted by
// another SPI transfer leaves the first waiting forever (see rule 1 in /Firmware/PulsePal3/AGENTS.md). Keep those
// sections short: they delay every interrupt, including the sample clock.
// ---------------------------------------------------------------------------------------------------------------

// Returns the playback buffer (0 = A, 1 = B) that holds a chunk of a waveform, for chunks 1 and up
static inline byte bufferForChunk(uint32_t chunk) {
  return (chunk & 1) ? 0 : 1;
}

// The memory block a channel plays if it starts now: its current waveform's current block. NO_BLOCK if the channel
// has no current waveform, or that waveform is empty.
byte channelStartBlock(byte channel) {
  byte waveform = currentWaveform[channel];
  if (waveform == 0) {
    return NO_BLOCK;
  }
  byte block = currentBlock[waveform - 1];
  return (nSamples[block] > 0) ? block : NO_BLOCK;
}

// The waveform on a channel has ended: fetch 0V, and stop the channel once handler() has written it
static inline void endPlayback(byte channel) {
  dacValue[channel] = DACBits_ZeroVolts;
  DACFlags[channel] = 1;
  stopAfterWrite[channel] = true;
}

// Fetches the next sample of a playing channel into dacValue[], for the next dacWrite(). Called from the playback
// interrupts, or from loop() with interrupts disabled. The channel's memory block (playBlock[]) does not change while
// it plays: loading a block stops the channels playing it first.
static inline void fetchNextSample(byte channel) {
  byte block = playBlock[channel];
  if (loopMode[channel] && (loopDuration[channel] > 0) && (samplesPlayed[channel] >= loopDuration[channel])) {
    endPlayback(channel); // The loop duration has elapsed
    return;
  }
  if (wavePos[channel] >= nSamples[block]) { // The last sample has been fetched
    if (!loopMode[channel]) {
      endPlayback(channel);
      return;
    }
    wavePos[channel] = 0; // Loop back to the first sample
    playChunk[channel] = 0;
    chunkPos[channel] = 0;
  }
  uint32_t chunk = playChunk[channel];
  uint32_t pos = chunkPos[channel];
  if (chunk == 0) {
    dacValue[channel] = preBuffer[block][pos];
  } else {
    byte buffer = bufferForChunk(chunk);
    if ((bufferChunk[channel][buffer] == (int32_t)chunk) && (bufferBlock[channel][buffer] == block)) {
      dacValue[channel] = playbackBuffers[channel][buffer][pos];
    } else if (pos == 0) {
      underrunCount[channel]++; // The chunk has not been read yet: hold the last sample (dacValue is unchanged)
    }
  }
  sampleSum[channel] += dacValue[channel];
  DACFlags[channel] = 1;
  wavePos[channel]++;
  samplesPlayed[channel]++;
  chunkPos[channel]++;
  if (chunkPos[channel] == BUFFER_SAMPLES) {
    chunkPos[channel] = 0;
    playChunk[channel]++;
  }
}

// The sample clock callback. Runs every sample period while any channel is playing. See "How playback works" above.
void handler() {
  uint32_t startCycles = ARM_DWT_CYCCNT;
  dacWrite(); // The samples fetched on the previous run, written first so that they change at even intervals
  boolean anyPlaying = false;
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (!playing[i]) {
      continue;
    }
    if (stopAfterWrite[i]) { // The 0V after the waveform has just been written
      playing[i] = false;
      stopAfterWrite[i] = false;
      digitalWriteFast(OutputLEDLines[i], LOW);
      continue;
    }
    fetchNextSample(i);
    anyPlaying = true;
  }
  if (!anyPlaying) {
    hardwareTimer.end();
    timerRunning = false;
  }
  uint32_t elapsedCycles = ARM_DWT_CYCCNT - startCycles;
  if (elapsedCycles > longestHandlerCycles) {
    longestHandlerCycles = elapsedCycles;
  }
}

boolean anyChannelPlaying() {
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (playing[i]) {
      return true;
    }
  }
  return false;
}

// Starts the selected channels (one bit per channel) from the first sample of their current waveform, restarting any
// that are playing. Channels without a waveform are skipped. Interrupt context, or loop() with interrupts disabled.
void startChannels(byte channelBits) {
  byte started = 0;
  for (byte i = 0; i < N_CHANNELS; i++) {
    byte block = bitRead(channelBits, i) ? channelStartBlock(i) : NO_BLOCK;
    if (block != NO_BLOCK) {
      playBlock[i] = block; // Played until the channel stops, whatever is loaded or made current meanwhile
      wavePos[i] = 0;
      playChunk[i] = 0;
      chunkPos[i] = 0;
      samplesPlayed[i] = 0;
      sampleSum[i] = 0;
      stopAfterWrite[i] = false;
      playing[i] = true;
      digitalWriteFast(OutputLEDLines[i], HIGH);
      fetchNextSample(i); // The first sample
      bitSet(started, i);
    }
  }
  if (started && !timerRunning) {
    // No channel was playing, so the sample clock is stopped. Start it from this moment, and write the first sample
    // now, so that the waveform starts within microseconds of its trigger. The clock starts first: handler() writes
    // each later sample as soon as it runs, so starting it after this write would make the first sample longer than
    // the others by the time the write takes. handler() cannot run before this function returns, since it has the
    // same interrupt priority (or interrupts are disabled). No other channel has a sample waiting, since none was
    // playing.
    hardwareTimer.begin(handler, samplePeriodMicros);
    timerRunning = true;
    dacWriteChannels(started);
    for (byte i = 0; i < N_CHANNELS; i++) {
      if (bitRead(started, i)) {
        fetchNextSample(i); // The second sample, written by the first run of handler()
      }
    }
  }
}

// Stops playback on the selected channels (one bit per channel), and holds their outputs at a DAC code, starting now.
// Their LEDs light if the code is not 0V. Interrupt context, or loop() with interrupts disabled.
void holdChannels(byte channelBits, uint16_t dacCode) {
  channelBits &= ALL_CHANNELS;
  if (!channelBits) {
    return;
  }
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (bitRead(channelBits, i)) {
      playing[i] = false;
      stopAfterWrite[i] = false;
      dacValue[i] = dacCode;
      digitalWriteFast(OutputLEDLines[i], dacCode != DACBits_ZeroVolts);
    }
  }
  dacWriteChannels(channelBits); // Also clears their DACFlags, so that a sample fetched for the next tick is not written
  if (timerRunning && !anyChannelPlaying()) {
    hardwareTimer.end();
    timerRunning = false;
  }
}

// Stops playback on the selected channels (one bit per channel). Their outputs go to 0V at once.
void stopChannels(byte channelBits) {
  holdChannels(channelBits, DACBits_ZeroVolts);
}

// The channels (one bit per channel) playing a memory block. Interrupt context, or loop() with interrupts disabled.
byte channelsPlayingBlock(byte block) {
  byte channelBits = 0;
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (playing[i] && (playBlock[i] == block)) {
      bitSet(channelBits, i);
    }
  }
  return channelBits;
}

// A channel is playing unless its waveform has just ended (stopAfterWrite), which counts as stopped
static inline boolean isPlaying(byte channel) {
  return playing[channel] && !stopAfterWrite[channel];
}

// A soft trigger (op 80) on the selected output channels (one bit per channel): starts those that are idle, as in Pulse
// Pal firmware. Channels that are playing ignore it, whatever the trigger modes, and startChannels() skips channels
// without a waveform. Interrupt context, or loop() with interrupts disabled.
void softTrigger(byte channelBits) {
  byte toStart = 0;
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (bitRead(channelBits, i) && !isPlaying(i)) {
      bitSet(toStart, i);
    }
  }
  startChannels(toStart);
}

// A falling edge on a trigger channel in gated mode: stops the output channels linked to it, unless the other trigger
// channel is also linked to them, in gated mode and still high
void releaseGatedChannels(byte triggerChannel) {
  byte otherChannel = 1 - triggerChannel;
  byte toStop = 0;
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (TriggerAddress[triggerChannel][i] && isPlaying(i)) {
      boolean heldByOther = TriggerAddress[otherChannel][i] && (triggerMode[otherChannel] == TRIGGER_MODE_GATED) &&
                            triggerLineActive[otherChannel];
      if (!heldByOther) {
        bitSet(toStop, i);
      }
    }
  }
  stopChannels(toStop);
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
// is handled first, and the stored waveforms become current before this edge starts anything (see "Param sync"
// above). Its own interrupt then finds the edge handled. As in Synth Pal firmware.
void triggerInterrupt(byte triggerChannel) {
  byte otherChannel = 1 - triggerChannel;
  boolean levels[2] = {digitalReadFast(TriggerLines[0]) == TriggerLevel, digitalReadFast(TriggerLines[1]) == TriggerLevel};
  if ((triggerMode[otherChannel] == TRIGGER_MODE_PARAM_SYNC) && levels[otherChannel] &&
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
  if (!triggersEnabled) { // A comm failure message is on the screen, and loop() is not refilling the playback buffers
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

// An edge on a trigger channel (0 or 1). What it does to each linked output channel depends on the trigger channel's
// mode, as in Pulse Pal firmware: a rising edge starts an idle channel in every mode, stops a playing one in toggle
// mode, and restarts a playing one from the first sample of its current waveform in master mode (or stops it, if it
// has no current waveform now). A falling edge stops a playing channel in gated mode (releaseGatedChannels()).
// startChannels() skips channels without a waveform. In param sync mode, a rising edge makes what op 76 and op 67
// stored current, and starts and stops nothing: its links are ignored.
void processTriggerEdge(byte triggerChannel, boolean isRisingEdge) {
  byte mode = triggerMode[triggerChannel];
  if (mode == TRIGGER_MODE_PARAM_SYNC) {
    if (isRisingEdge) {
      screenSaverActivity = true;
      applyStoredSet();
    }
    return;
  }
  if (!isRisingEdge) {
    if (mode == TRIGGER_MODE_GATED) {
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
    } else if (mode == TRIGGER_MODE_TOGGLE) {
      bitSet(toStop, i);
    } else if (mode == TRIGGER_MODE_MASTER) {
      if (channelStartBlock(i) != NO_BLOCK) {
        bitSet(toStart, i); // startChannels() restarts a channel that is playing
      } else {
        bitSet(toStop, i); // Its current waveform is now none, or empty: there is nothing to restart it with
      }
    }
  }
  stopChannels(toStop);
  startChannels(toStart);
}

// True if a trigger channel is in param sync mode. Op 76 and op 67 then store what they send for its next rising edge.
boolean anyParamSyncChannel() {
  return (triggerMode[0] == TRIGGER_MODE_PARAM_SYNC) || (triggerMode[1] == TRIGGER_MODE_PARAM_SYNC);
}

// A rising edge on a trigger channel in param sync mode, or the last trigger channel leaving the mode: the waveforms
// and current waveforms that op 76 and op 67 stored become current (see "Param sync" above). Channels that are playing
// finish the memory blocks they play. With nothing stored, it changes nothing. Playback interrupt context, or loop()
// with interrupts disabled.
void applyStoredSet() {
  for (byte w = 0; w < N_WAVEFORMS; w++) {
    if (loadStored[w]) {
      currentBlock[w] ^= 1; // The waveform's other block, which holds the stored load
      loadStored[w] = false;
    }
  }
  if (currentWaveformsStored) {
    for (byte i = 0; i < N_CHANNELS; i++) {
      currentWaveform[i] = storedCurrentWaveform[i];
    }
    currentWaveformsStored = false;
  }
}

// Sets the sampling rate of all channels, in Hz. It takes effect from the next sample, also during playback.
// The sample clock counts cycles of a 24MHz clock, so the period is rounded to a whole number of them. The period is
// given to IntervalTimer in microseconds, which it converts back to exactly that number of cycles.
void setSamplingRate(uint32_t newRate) {
  uint32_t periodCycles = (TIMER_CLOCK_HZ + (newRate / 2)) / newRate;
  noInterrupts();
  samplingRate = newRate;
  samplePeriodMicros = (double)periodCycles / (TIMER_CLOCK_HZ / 1000000);
  if (timerRunning) {
    hardwareTimer.update(samplePeriodMicros);
  }
  interrupts();
}
