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


// USB communication with the PC. Op codes are listed in enum OpCode in WavePal.ino, and documented in PROTOCOL.md.
//
// Functions in this file:
//   isValidOutputChannel()
//   discardBytes()
//   allAtMost()
//   processUSBCommands()
//   loadWaveform()
//   applyOutputRange()
//   LoadDefaultSettings()
//   HandleReadTimeout()
//
// Note on reads: every PPUSB read gives up if no byte arrives for 100ms, and sets PPUSB.timedOut(). The rest of the
// command then reads as zeros, and loop() shows a comm failure message and loads the default settings. See ArCOM.h.
// Replies are buffered, and sent by the PPUSB.flush() call in loop().
//
// Note on confirm bytes: ops that reply send 1 if the command was executed, or 0 if it was rejected because a value
// was out of range. A rejected command changes nothing. Its data is still read, so that it is not taken for the next
// command.
//
// Note on playback: the playback functions in Playback.ino are called with interrupts disabled, because the playback
// interrupts use the same variables and the same SPI bus. See "Interrupt rules" in Playback.ino.

// Channel numbers in USB commands are 1-indexed
bool isValidOutputChannel(byte channel) {
  return (channel >= 1) && (channel <= N_CHANNELS);
}

// Reads and discards the data of a command that cannot be executed
void discardBytes(uint64_t nBytes) {
  byte scratch[64];
  while ((nBytes > 0) && !PPUSB.timedOut()) {
    size_t nThisRead = (nBytes > sizeof(scratch)) ? sizeof(scratch) : nBytes;
    PPUSB.readByteArray(scratch, nThisRead);
    nBytes -= nThisRead;
  }
}

// True if every value in an array is at most maxValue
bool allAtMost(const byte *values, byte nValues, byte maxValue) {
  for (byte i = 0; i < nValues; i++) {
    if (values[i] > maxValue) {
      return false;
    }
  }
  return true;
}

// Reads one command from the USB serial port (if available) and executes it. See enum OpCode for the list of ops.
void processUSBCommands() {
  if (!PPUSB.available()) {
    return;
  }
  CommandByte = PPUSB.readByte();
  if (CommandByte != OpMenuByte) { // The first byte must be 213. Others are ignored (reduces interference from port scanning applications)
    return;
  }
  CommandByte = PPUSB.readByte(); // The op code
  switch (CommandByte) {
    case OP_HANDSHAKE: { // Op 72
      PPUSB.writeByte(HANDSHAKE_REPLY);
      PPUSB.writeUint32(FIRMWARE_VERSION);
      showTopScreen();
    } break;

    case OP_SET_CLIENT_NAME: { // Op 89, as in Pulse Pal firmware: 6 characters, shown as "NAME Connected"
      PPUSB.readByteArray((byte*)CommanderString, 6);
      strcpy(CommanderString + 6, ClientStringSuffix); // 6 + 10 characters, and the terminator
      showTopScreen();
    } break;

    case OP_DISCONNECT: { // Op 81. Unlike Pulse Pal's op 81, playback continues: TTL triggers still play the waveforms.
      strcpy(CommanderString, DefaultCommanderString);
      showTopScreen();
    } break;

    case OP_HARDWARE_INFO: { // Op 78 ('N')
      PPUSB.writeByte(HARDWARE_VERSION);
      PPUSB.writeByte(N_CHANNELS);
      PPUSB.writeUint32(MAX_WAVE_SAMPLES);
      PPUSB.writeUint32(MAX_SAMPLING_RATE);
      PPUSB.writeUint32(BUFFER_SAMPLES);
      PPUSB.writeUint32(TIMER_CLOCK_HZ);
    } break;

    case OP_SET_SAMPLING_RATE: { // Op 83 ('S'). Rate in Hz.
      uint32_t newRate = PPUSB.readUint32();
      if ((newRate == 0) || (newRate > MAX_SAMPLING_RATE)) {
        PPUSB.writeByte(0);
        break;
      }
      setSamplingRate(newRate);
      PPUSB.writeByte(1);
    } break;

    case OP_SET_OUTPUT_RANGE: { // Op 82 ('R'). See enum OutputRange.
      byte newRange = PPUSB.readByte();
      if (newRange >= N_OUTPUT_RANGES) {
        PPUSB.writeByte(0);
        break;
      }
      applyOutputRange(newRange);
      PPUSB.writeByte(1);
    } break;

    case OP_LOAD_WAVEFORM: { // Op 76 ('L')
      loadWaveform();
    } break;

    case OP_PLAY: { // Op 80 ('P'). Soft trigger: one bit per output channel.
      byte channelBits = PPUSB.readByte();
      noInterrupts();
      triggerChannels(channelBits & ALL_CHANNELS);
      interrupts();
    } break;

    case OP_STOP: { // Op 88 ('X'). One bit per output channel.
      byte channelBits = PPUSB.readByte();
      noInterrupts();
      stopChannels(channelBits & ALL_CHANNELS);
      interrupts();
    } break;

    case OP_SET_FIXED_VOLTAGE: { // Op 33 ('!'). One bit per output channel, then the DAC code.
      byte channelBits = PPUSB.readByte();
      uint16_t dacCode = PPUSB.readUint16();
      if (channelBits & ~ALL_CHANNELS) {
        PPUSB.writeByte(0);
        break;
      }
      noInterrupts();
      holdChannels(channelBits, dacCode);
      interrupts();
      PPUSB.writeByte(1);
    } break;

    case OP_SET_LOOP_MODE: { // Op 79 ('O'). One byte per output channel: 0 or 1.
      byte newModes[N_CHANNELS];
      PPUSB.readByteArray(newModes, N_CHANNELS);
      if (!allAtMost(newModes, N_CHANNELS, 1)) {
        PPUSB.writeByte(0);
        break;
      }
      noInterrupts(); // So that a trigger sees all four new values, or none
      for (byte i = 0; i < N_CHANNELS; i++) {
        loopMode[i] = newModes[i];
      }
      interrupts();
      PPUSB.writeByte(1);
    } break;

    case OP_SET_LOOP_DURATION: { // Op 68 ('D'). One uint32 per output channel: samples, 0 = loop until stopped.
      uint32_t newDurations[N_CHANNELS];
      PPUSB.readUint32Array(newDurations, N_CHANNELS);
      noInterrupts();
      for (byte i = 0; i < N_CHANNELS; i++) {
        loopDuration[i] = newDurations[i];
      }
      interrupts();
      PPUSB.writeByte(1);
    } break;

    case OP_SET_TRIGGER_MODE: { // Op 84 ('T'). One byte per output channel. See enum TriggerModeValue.
      byte newModes[N_CHANNELS];
      PPUSB.readByteArray(newModes, N_CHANNELS);
      if (!allAtMost(newModes, N_CHANNELS, MAX_TRIGGER_MODE)) {
        PPUSB.writeByte(0);
        break;
      }
      noInterrupts();
      for (byte i = 0; i < N_CHANNELS; i++) {
        triggerMode[i] = newModes[i];
      }
      interrupts();
      PPUSB.writeByte(1);
    } break;

    case OP_SET_TRIGGER_LINKS: { // Op 73 ('I'). Trigger channel 1's links to output channels 1-4, then trigger channel 2's.
      byte newLinks[2 * N_CHANNELS];
      PPUSB.readByteArray(newLinks, 2 * N_CHANNELS);
      if (!allAtMost(newLinks, 2 * N_CHANNELS, 1)) {
        PPUSB.writeByte(0);
        break;
      }
      noInterrupts();
      for (byte i = 0; i < N_CHANNELS; i++) {
        TriggerAddress[0][i] = newLinks[i];
        TriggerAddress[1][i] = newLinks[N_CHANNELS + i];
      }
      interrupts();
      PPUSB.writeByte(1);
    } break;

    case OP_GET_STATUS: { // Op 71 ('G')
      byte playingBits = 0;
      uint32_t loadedSamples[N_CHANNELS];
      uint32_t underruns[N_CHANNELS];
      noInterrupts();
      for (byte i = 0; i < N_CHANNELS; i++) {
        if (playing[i] && !stopAfterWrite[i]) {
          bitSet(playingBits, i);
        }
        loadedSamples[i] = nSamples[i];
        underruns[i] = underrunCount[i];
      }
      uint32_t longestCycles = longestHandlerCycles;
      longestHandlerCycles = 0;
      interrupts();
      PPUSB.writeByte(playingBits);
      PPUSB.writeUint32Array(loadedSamples, N_CHANNELS);
      PPUSB.writeUint32Array(underruns, N_CHANNELS);
      PPUSB.writeUint32((uint32_t)(((uint64_t)longestCycles * 1000000000ULL) / F_CPU_ACTUAL)); // Nanoseconds
    } break;

    case OP_GET_PLAYBACK_CHECKSUMS: { // Op 90 ('Z'). For testing, like Pulse Pal's debugging ops 86 and 87.
      uint32_t played[N_CHANNELS];
      uint32_t sums[N_CHANNELS];
      noInterrupts();
      for (byte i = 0; i < N_CHANNELS; i++) {
        played[i] = samplesPlayed[i];
        sums[i] = sampleSum[i];
      }
      interrupts();
      PPUSB.writeUint32Array(played, N_CHANNELS);
      PPUSB.writeUint32Array(sums, N_CHANNELS);
    } break;
  }
}

// Op 76: loads one output channel's waveform. The samples arrive in blocks of BUFFER_SAMPLES. Block 0 is read straight
// into the channel's pre-buffer and the rest into its playback buffers, which are free because the channel is stopped
// first. Each block is then written to the channel's region of the data file (see Storage.ino). Between blocks, the
// other channels' playback buffers are refilled, so that they keep playing.
void loadWaveform() {
  byte channel = PPUSB.readByte();
  uint32_t nNewSamples = PPUSB.readUint32();
  if (!isValidOutputChannel(channel) || (nNewSamples == 0) || (nNewSamples > MAX_WAVE_SAMPLES)) {
    discardBytes((uint64_t)nNewSamples * 2);
    PPUSB.writeByte(0);
    return;
  }
  channel = channel - 1; // Convert for zero-indexing
  noInterrupts();
  stopChannels(bit(channel));
  nSamples[channel] = 0; // No waveform until the new one is stored, so triggers ignore the channel meanwhile
  bufferChunk[channel][0] = CHUNK_NONE;
  bufferChunk[channel][1] = CHUNK_NONE;
  interrupts();
  bool stored = true;
  uint32_t samplesLeft = nNewSamples;
  uint32_t chunk = 0;
  while (samplesLeft > 0) {
    uint32_t nChunkSamples = (samplesLeft > BUFFER_SAMPLES) ? BUFFER_SAMPLES : samplesLeft;
    uint16_t *block = (chunk == 0) ? preBuffer[channel] : playbackBuffers[channel][bufferForChunk(chunk)];
    PPUSB.readUint16Array(block, nChunkSamples);
    if (PPUSB.timedOut()) { // The rest of the waveform never arrived. loop() reports the failure.
      stored = false;
      break;
    }
    if (stored) { // After a failed write, the rest of the samples are still read, so they are not taken for commands
      stored = waveFile.seekSet(chunkFilePosition(channel, chunk)) &&
               (waveFile.write(block, nChunkSamples * 2) == nChunkSamples * 2);
    }
    samplesLeft -= nChunkSamples;
    chunk++;
    refillPlaybackBuffers();
  }
  if (stored) {
    stored = waveFile.sync(); // Write any part-sector still in SdFat's cache, so that a failure is reported here
  }
  if (stored) {
    // The playback buffers hold the last two blocks read, which are this waveform's own, so they need not be read again
    uint32_t lastChunk = chunk - 1;
    if (lastChunk >= 1) {
      bufferChunk[channel][bufferForChunk(lastChunk)] = lastChunk;
    }
    if (lastChunk >= 2) {
      bufferChunk[channel][bufferForChunk(lastChunk - 1)] = lastChunk - 1;
    }
    nSamples[channel] = nNewSamples; // Last: the channel can be triggered from here on
  }
  PPUSB.writeByte(stored);
}

// Changes the output range (op 82). The waveforms are DAC codes, which would mean other voltages in the new range, so
// they are unloaded, and the client loads them again. The same range again changes nothing.
void applyOutputRange(byte newRange) {
  if (newRange == rangeIndex) {
    return;
  }
  noInterrupts();
  stopChannels(ALL_CHANNELS);
  unloadWaveforms();
  setOutputRange(newRange);
  for (byte i = 0; i < N_CHANNELS; i++) {
    dacValue[i] = DACBits_ZeroVolts;
  }
  dacWriteChannels(ALL_CHANNELS); // 0V in the new range
  interrupts();
}

// The settings the device starts with, also loaded after a comm failure. The Python class programs the same ones when
// it connects. Waveforms are kept, unless the output range changes.
void LoadDefaultSettings() {
  setSamplingRate(DEFAULT_SAMPLING_RATE);
  applyOutputRange(RANGE_PLUS_MINUS_10V);
  noInterrupts();
  for (byte i = 0; i < N_CHANNELS; i++) {
    loopMode[i] = 0;
    loopDuration[i] = 0;
    triggerMode[i] = TRIGGER_MODE_NORMAL;
    TriggerAddress[0][i] = 1; // All output channels are triggered by trigger channel 1
    TriggerAddress[1][i] = 0;
  }
  interrupts();
}

// A USB message started but did not finish (see ArCOM.h). Stops playback and waits for a joystick click, as Pulse Pal
// firmware does: a timeout usually means a faulty cable, hub or client, and the user should know.
void HandleReadTimeout() {
  noInterrupts();
  triggersEnabled = false; // loop() waits here, so it cannot refill the playback buffers
  stopChannels(ALL_CHANNELS);
  interrupts();
  write2Screen("COMM. FAILURE!", "Click joystick->");
  byte flashState = 0;
  uint32_t flashTime = millis();
  while (digitalRead(ClickerButtonLine) == HIGH) {
    if ((millis() - flashTime) > 100) { // Flash the trigger channel LEDs
      flashState = 1 - flashState;
      digitalWriteFast(InputLEDLines[0], flashState);
      digitalWriteFast(InputLEDLines[1], flashState);
      flashTime = millis();
    }
  }
  write2Screen("Loading default", "settings...");
  LoadDefaultSettings();
  PPUSB.clearTimedOut(); // First: until it is cleared, reads return at once without taking any bytes
  while (PPUSB.available()) { // The rest of the failed message, which must not be read as commands
    PPUSB.readByte();
  }
  delay(1000);
  LastClickerButtonState = 1; // The click that ended the wait must not also be taken as a click in the menu
  noInterrupts();
  for (byte i = 0; i < 2; i++) {
    digitalWriteFast(InputLEDLines[i], triggerLineActive[i]);
  }
  triggersEnabled = true;
  interrupts();
  showTopScreen();
}
