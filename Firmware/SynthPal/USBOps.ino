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


// USB communication with the PC. Op codes are listed in enum OpCode in SynthPal.ino, and documented in PROTOCOL.md.
//
// Functions in this file:
//   allAtMost()
//   processUSBCommands()
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
  screenSaverActivity = true; // Every command wakes the screen, once its reply has been sent (see updateScreenSaver())
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

    case OP_SET_SCREEN_SAVER: { // Op 99, as in Pulse Pal firmware: state (0 or 1), then the timeout in seconds (uint16)
      byte state = PPUSB.readByte();
      uint16_t timeout = PPUSB.readUint16();
      if ((state > 1) || (timeout == 0)) {
        PPUSB.writeByte(0);
        break;
      }
      screenSaverEnabled = state;
      screenSaverTimeout = timeout;
      screenSaverSavePending = true; // Saved to the EEPROM once no channel is playing (see updateScreenSaver())
      PPUSB.writeByte(1);
    } break;

    case OP_HARDWARE_INFO: { // Op 78 ('N')
      PPUSB.writeByte(HARDWARE_VERSION);
      PPUSB.writeByte(N_CHANNELS);
      PPUSB.writeUint32(MIN_FREQUENCY_CENTIHZ);
      PPUSB.writeUint32(MAX_FREQUENCY_CENTIHZ);
      PPUSB.writeUint32(MAX_SAMPLING_RATE);
      PPUSB.writeUint32(TIMER_CLOCK_HZ);
      PPUSB.writeUint32(MAX_PLAY_DURATION_MICROS);
    } break;

    case OP_SET_FREQUENCY: { // Op 70 ('F'). Frequency in centiHz. Replies with the confirm byte, then samples per cycle.
      uint32_t newFrequency = PPUSB.readUint32();
      if ((newFrequency < MIN_FREQUENCY_CENTIHZ) || (newFrequency > MAX_FREQUENCY_CENTIHZ)) {
        PPUSB.writeByte(0);
      } else {
        setFrequency(newFrequency);
        PPUSB.writeByte(1);
      }
      PPUSB.writeUint32(samplesPerCycle); // Always sent, so the reply has a fixed length
    } break;

    case OP_SET_WAVEFORM: { // Op 87 ('W'). One byte per output channel. See enum WaveformValue.
      byte newWaveforms[N_CHANNELS];
      PPUSB.readByteArray(newWaveforms, N_CHANNELS);
      if (!allAtMost(newWaveforms, N_CHANNELS, MAX_WAVEFORM)) {
        PPUSB.writeByte(0);
        break;
      }
      for (byte i = 0; i < N_CHANNELS; i++) {
        if (newWaveforms[i] != waveform[i]) {
          waveform[i] = newWaveforms[i];
          updateChannelOutput(i);
        }
      }
      PPUSB.writeByte(1);
    } break;

    case OP_SET_AMPLITUDE: { // Op 65 ('A'). One uint32 per output channel: peak to peak, in microvolts.
      uint32_t newAmplitudes[N_CHANNELS];
      PPUSB.readUint32Array(newAmplitudes, N_CHANNELS);
      bool valid = true;
      for (byte i = 0; i < N_CHANNELS; i++) {
        valid = valid && isValidOutputLevel(restingVoltageMicrovolts[i], newAmplitudes[i]);
      }
      if (!valid) {
        PPUSB.writeByte(0);
        break;
      }
      for (byte i = 0; i < N_CHANNELS; i++) {
        if (newAmplitudes[i] != amplitudeMicrovolts[i]) {
          amplitudeMicrovolts[i] = newAmplitudes[i];
          updateChannelOutput(i);
        }
      }
      PPUSB.writeByte(1);
    } break;

    case OP_SET_RESTING_VOLTAGE: { // Op 86 ('V'). One int32 per output channel, in microvolts.
      int32_t newVoltages[N_CHANNELS];
      PPUSB.readUint32Array((uint32_t*)newVoltages, N_CHANNELS); // Two's complement, so the bytes are the same
      bool valid = true;
      for (byte i = 0; i < N_CHANNELS; i++) {
        valid = valid && isValidOutputLevel(newVoltages[i], amplitudeMicrovolts[i]);
      }
      if (!valid) {
        PPUSB.writeByte(0);
        break;
      }
      for (byte i = 0; i < N_CHANNELS; i++) {
        if (newVoltages[i] != restingVoltageMicrovolts[i]) {
          restingVoltageMicrovolts[i] = newVoltages[i];
          updateChannelOutput(i);
        }
      }
      PPUSB.writeByte(1);
    } break;

    case OP_SET_PLAY_DURATION: { // Op 68 ('D'). One uint32 per output channel, in microseconds. 0 = until stopped.
      uint32_t newDurations[N_CHANNELS];
      PPUSB.readUint32Array(newDurations, N_CHANNELS);
      bool valid = true;
      for (byte i = 0; i < N_CHANNELS; i++) {
        valid = valid && (newDurations[i] <= MAX_PLAY_DURATION_MICROS);
      }
      if (!valid) {
        PPUSB.writeByte(0);
        break;
      }
      setPlayDurations(newDurations);
      PPUSB.writeByte(1);
    } break;

    case OP_SET_TRIGGER_LINKS: { // Op 73 ('I'). Trigger channel 1's links to output channels 1-4, then trigger channel 2's.
      byte newLinks[2 * N_CHANNELS];
      PPUSB.readByteArray(newLinks, 2 * N_CHANNELS);
      if (!allAtMost(newLinks, 2 * N_CHANNELS, 1)) {
        PPUSB.writeByte(0);
        break;
      }
      noInterrupts(); // So that a trigger sees all eight new values, or none
      for (byte i = 0; i < N_CHANNELS; i++) {
        TriggerAddress[0][i] = newLinks[i];
        TriggerAddress[1][i] = newLinks[N_CHANNELS + i];
      }
      interrupts();
      PPUSB.writeByte(1);
    } break;

    case OP_SET_TRIGGER_MODE: { // Op 84 ('T'). One byte per trigger channel. See enum TriggerModeValue.
      byte newModes[2];
      PPUSB.readByteArray(newModes, 2);
      if (!allAtMost(newModes, 2, MAX_TRIGGER_MODE)) {
        PPUSB.writeByte(0);
        break;
      }
      noInterrupts();
      TriggerMode[0] = newModes[0];
      TriggerMode[1] = newModes[1];
      interrupts();
      PPUSB.writeByte(1);
    } break;

    case OP_PLAY: { // Op 80 ('P'). Soft trigger: one bit per output channel. Channels that are playing ignore it.
      byte channelBits = PPUSB.readByte();
      noInterrupts();
      startChannels(channelBits & ALL_CHANNELS);
      interrupts();
    } break;

    case OP_STOP: { // Op 88 ('X'). One bit per output channel.
      byte channelBits = PPUSB.readByte();
      noInterrupts();
      stopChannels(channelBits & ALL_CHANNELS);
      interrupts();
    } break;

    case OP_GET_STATUS: { // Op 71 ('G')
      byte playingBits = 0;
      byte ranges[N_CHANNELS];
      noInterrupts();
      for (byte i = 0; i < N_CHANNELS; i++) {
        if (isPlaying(i)) {
          bitSet(playingBits, i);
        }
        ranges[i] = activeOutput[i].range;
      }
      uint32_t longestCycles = longestHandlerCycles;
      longestHandlerCycles = 0;
      uint32_t late = lateUpdates;
      lateUpdates = 0;
      interrupts();
      PPUSB.writeByte(playingBits);
      PPUSB.writeUint32(samplesPerCycle);
      PPUSB.writeByteArray(ranges, N_CHANNELS);
      PPUSB.writeUint32((uint32_t)(((uint64_t)longestCycles * 1000000000ULL) / F_CPU_ACTUAL)); // Nanoseconds
      PPUSB.writeUint32(late);
    } break;

    case OP_GET_PLAYBACK_CHECKSUMS: { // Op 90 ('Z'). For testing, like Wave Pal's op 90.
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

// A USB message started but did not finish (see ArCOM.h). Stops playback and waits for a joystick click, as Pulse Pal
// firmware does: a timeout usually means a faulty cable, hub or client, and the user should know.
void HandleReadTimeout() {
  noInterrupts();
  triggersEnabled = false;
  stopChannels(ALL_CHANNELS); // handler() returns the outputs to their resting voltages on its next tick
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
  screenSaverActivity = true; // The click that ended the message. The screen saver's idle time starts again here.
  noInterrupts();
  for (byte i = 0; i < 2; i++) {
    digitalWriteFast(InputLEDLines[i], triggerLineActive[i]);
  }
  triggersEnabled = true;
  interrupts();
  showTopScreen();
}
