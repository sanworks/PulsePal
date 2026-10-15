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


// Settings files on the microSD card. See the file layout above SaveCurrentProgram2SD().
//
// Functions in this file:
//   LoadDefaultParameters()
//   writeLongToSD()
//   writeShortToSD()
//   readLongFromSD()
//   readShortFromSD()
//   readByteFromSD()
//   SaveCurrentProgram2SD()
//   RestoreParametersFromSD()
//   rewindDirectory()
//   isDefaultSettingsFile()
//   findListedFile()
//   formatCard() (HW3 only)

// Loads the default parameters, sets the outputs to their resting voltage, and makes the default settings file the current
// settings file. It does not write to the microSD card: setup() and op 97 write the default settings file with SaveCurrentProgram2SD().
// The MATLAB and Python classes' default parameters are the same (set_default_params()), so after a comm failure or a
// format the device plays what a client that just connected programs. Continuous loop mode goes off, and a channel
// playing in continuous loop mode stops, as op 92 stops it.
void LoadDefaultParameters() {
  for (int x = 0; x < 4; x++) {
      if (ContinuousLoopMode[x]) {
        ContinuousLoopMode[x] = 0;
        killChannel(x);
      }
      Phase1Duration[x] = 2;
      InterPhaseInterval[x] = 2;
      Phase2Duration[x] = 2;
      InterPulseInterval[x] = 20;
      BurstDuration[x] = 0;
      BurstInterval[x] = 0;
      PulseTrainDuration[x] = 20000;
      PulseTrainDelay[x] = 0;
      IsBiphasic[x] = 0;
      Phase1Voltage[x] = 49152;
      Phase2Voltage[x] = 16384;
      RestingVoltage[x] = 32768;
      CustomTrainID[x] = 0;
      CustomTrainTarget[x] = 0;
      CustomTrainLoop[x] = 0;
      updateUsesBursts(x);
    }
    for (int y = 0; y < 4; y++) {
      TriggerAddress[0][y] = 1;
    }
    for (int y = 0; y < 4; y++) {
      TriggerAddress[1][y] = 0;
    }
   TriggerMode[0] = 0; 
   TriggerMode[1] = 0;
   updateParamSyncPending(); // Neither trigger channel is in param sync mode now, so a waiting set is discarded
   pauseTimerForSD(); // Pulse Pal 2: the card shares the SPI bus with the DAC
   settingsFile.close();
   strcpy(currentSettingsFileNameChar, DEFAULT_SETTINGS_FILE_NAME);
   settingsFile.open(currentSettingsFileNameChar, O_READ);
   resumeTimerAfterSD();
   outputRestingVoltages(); // After file operations: on Pulse Pal 2 the DAC and microSD card share the SPI bus
}

// Write a 32-bit or 16-bit integer to the settings file, least significant byte first
void writeLongToSD(uint32_t value) {
  settingsFile.write((uint8_t)value);
  settingsFile.write((uint8_t)(value >> 8));
  settingsFile.write((uint8_t)(value >> 16));
  settingsFile.write((uint8_t)(value >> 24));
}
void writeShortToSD(uint16_t value) {
  settingsFile.write((uint8_t)value);
  settingsFile.write((uint8_t)(value >> 8));
}
// Read a 32-bit, 16-bit or 8-bit integer from the settings file. Each returns 0 if the read fails: a failed read
// leaves its buffer untouched, and the buffers are shared, so without clearing them first a read from a missing
// file would return the bytes of the last file read, including its valid end marker.
uint32_t readLongFromSD() {
  uint32_t output = 0;
  typeCast.uint32 = 0;
  settingsFile.read(typeCast.byteArray, 4);
  output = typeCast.uint32;
  return output;
}
uint16_t readShortFromSD() {
  uint16_t output = 0;
  typeCast.uint32 = 0;
  settingsFile.read(typeCast.byteArray, 2);
  output = typeCast.uint16;
  return output;
}
byte readByteFromSD() {
  byte myByte = 0;
  buf[0] = 0;
  settingsFile.read(buf, sizeof(buf));
  myByte = buf[0];
  return myByte;
}

// ---------------------------------------------------------------------------------------------------------------
// Settings file layout (179 bytes, little-endian)
//   For each output channel 1-4 (42 bytes each):
//     Phase1Duration, InterPhaseInterval, Phase2Duration, InterPulseInterval,
//     BurstDuration, BurstInterval, PulseTrainDuration, PulseTrainDelay       uint32 each
//     IsBiphasic                                                              uint8
//     Phase1Voltage, Phase2Voltage, RestingVoltage                            uint16 each
//     CustomTrainID, CustomTrainTarget, CustomTrainLoop                       uint8 each
//   For each trigger channel 1-2 (5 bytes each):
//     TriggerMode, TriggerAddress[ch][0-3]                                    uint8 each
//   SETTINGS_FILE_END_MARKER                                                  uint8
// ContinuousLoopMode is not saved. Op 85 sends the first SETTINGS_FILE_N_PARAM_BYTES bytes of this file.
// Note: ops 73, 92 and 93 send parameters in a different order, with each parameter grouped across all channels.
// If this layout changes, update SaveCurrentProgram2SD(), RestoreParametersFromSD() and the constants above.
// ---------------------------------------------------------------------------------------------------------------
// Saves all parameters to the current settings file (currentSettingsFileNameChar), using the settings file layout
// above. Returns true if the file was written, or false if it could not be (no card, or the card refused the write).
FLASHMEM bool SaveCurrentProgram2SD() { // microSD speed limits it: run from flash (see FLASHMEM in PulsePal3.ino)
  pauseTimerForSD(); // Pulse Pal 2: the card shares the SPI bus with the DAC
  settingsFile.close();
  if (!settingsFile.open(currentSettingsFileNameChar, O_CREAT | O_TRUNC | O_RDWR)) {
    resumeTimerAfterSD();
    return false;
  }
  for (int chan = 0; chan < 4; chan++) {
    writeLongToSD(Phase1Duration[chan]);
    writeLongToSD(InterPhaseInterval[chan]);
    writeLongToSD(Phase2Duration[chan]);
    writeLongToSD(InterPulseInterval[chan]);
    writeLongToSD(BurstDuration[chan]);
    writeLongToSD(BurstInterval[chan]);
    writeLongToSD(PulseTrainDuration[chan]);
    writeLongToSD(PulseTrainDelay[chan]);
    settingsFile.write(IsBiphasic[chan]);
    writeShortToSD(Phase1Voltage[chan]);
    writeShortToSD(Phase2Voltage[chan]);
    writeShortToSD(RestingVoltage[chan]);
    settingsFile.write(CustomTrainID[chan]);
    settingsFile.write(CustomTrainTarget[chan]);
    settingsFile.write(CustomTrainLoop[chan]);
  }
  for (int chan = 0; chan < 2; chan++) {
    settingsFile.write(TriggerMode[chan]);
    settingsFile.write(TriggerAddress[chan][0]);
    settingsFile.write(TriggerAddress[chan][1]);
    settingsFile.write(TriggerAddress[chan][2]);
    settingsFile.write(TriggerAddress[chan][3]);
  }
  settingsFile.write(SETTINGS_FILE_END_MARKER);
  bool written = !settingsFile.getWriteError();
  written = settingsFile.close() && written; // close() writes what is still cached, and reports whether it could
  settingsFile.open(currentSettingsFileNameChar, O_READ);
  resumeTimerAfterSD();
  return written;
}

// Loads the parameters in the current settings file (settingsFile). Returns SETTINGS_FILE_END_MARKER if the file is
// valid, or 0 if it is missing, too short, has no end marker, or holds a value the device cannot play (e.g. custom
// train 3, in a file saved on Pulse Pal 3 and loaded on Pulse Pal 2). The whole file is read into a parameter set in
// op 92's layout, and checked, before anything is loaded: an invalid file changes nothing, and the device keeps its
// parameters. A valid one is loaded as op 92 loads a set (applyParamBuffer()), at once, also in param sync mode.
// Continuous loop mode is not in the file, so each channel keeps its own.
FLASHMEM byte RestoreParametersFromSD() { // microSD speed limits it: run from flash
  if (!settingsFile.isOpen() || (settingsFile.fileSize() < SETTINGS_FILE_N_PARAM_BYTES + 1)) {
    return 0; // The file did not open (e.g. no file of that name), or is too short to hold a program
  }
  settingsFile.rewind();
  // The set's layout, as in loadChannelParamsFromBuffer() in USBOps.ino: each parameter's array, for output channels 1-4
  uint8_t set[PARAM_BUFFER_N_BYTES];
  uint8_t *times = set; // The 8 time arrays, in the order the file holds them for each channel
  uint8_t *voltages = times + 8 * sizeof(Phase1Duration); // Phase 1, phase 2 and resting voltages
  uint8_t *isBiphasic = voltages + 3 * sizeof(Phase1Voltage);
  uint8_t *customTrainID = isBiphasic + sizeof(IsBiphasic);
  uint8_t *customTrainTarget = customTrainID + sizeof(CustomTrainID);
  uint8_t *customTrainLoop = customTrainTarget + sizeof(CustomTrainTarget);
  uint8_t *continuousLoop = customTrainLoop + sizeof(CustomTrainLoop);
  uint8_t *triggerLinks = continuousLoop + sizeof(ContinuousLoopMode);
  uint8_t *triggerModes = triggerLinks + sizeof(TriggerAddress);
  for (int chan = 0; chan < 4; chan++) {
    for (int i = 0; i < 8; i++) {
      uint32_t cycles = readLongFromSD();
      memcpy(times + i * sizeof(Phase1Duration) + chan * sizeof(cycles), &cycles, sizeof(cycles));
    }
    isBiphasic[chan] = readByteFromSD();
    for (int i = 0; i < 3; i++) {
      uint16_t code = readShortFromSD();
      memcpy(voltages + i * sizeof(Phase1Voltage) + chan * sizeof(code), &code, sizeof(code));
    }
    customTrainID[chan] = readByteFromSD();
    customTrainTarget[chan] = readByteFromSD();
    customTrainLoop[chan] = readByteFromSD();
    continuousLoop[chan] = ContinuousLoopMode[chan];
  }
  for (int chan = 0; chan < 2; chan++) {
    triggerModes[chan] = readByteFromSD();
    for (int i = 0; i < 4; i++) {
      triggerLinks[chan * 4 + i] = readByteFromSD();
    }
  }
  if (readByteFromSD() != SETTINGS_FILE_END_MARKER) {
    return 0;
  }
  if (!validateParamBuffer(set)) { // It corrects out of range values in the set, which is then not loaded
    return 0;
  }
  applyParamBuffer(set);
  updateParamSyncPending(); // The file may have taken a trigger channel out of param sync mode
  return SETTINGS_FILE_END_MARKER;
}

void rewindDirectory() {
  root.rewindDirectory();
}

// Returns true if a file name is the default settings file name (not case sensitive, like FAT file names)
bool isDefaultSettingsFile(const char* fileName) {
  return strcasecmp(fileName, DEFAULT_SETTINGS_FILE_NAME) == 0;
}

// Finds the file at a position (1 = first) in the file lists shown by the joystick load, save and erase menus,
// and copies its name to candidateSettingsFileChar. If includeDefault is true, the default settings file is first
// in the list; otherwise it is omitted. Returns false if the list has no file at this position.
bool findListedFile(uint16_t position, bool includeDefault) {
  memset(candidateSettingsFileChar, 0, sizeof(candidateSettingsFileChar));
  if (includeDefault) {
    if (position == 1) {
      strcpy(candidateSettingsFileChar, DEFAULT_SETTINGS_FILE_NAME);
      return true;
    }
    position--;
  }
  pauseTimerForSD(); // Pulse Pal 2: the card shares the SPI bus with the DAC
  candidateSettingsFile.close();
  rewindDirectory();
  uint16_t nListed = 0;
  while (candidateSettingsFile.openNext(&root, O_READ)) {
    candidateSettingsFile.getName(candidateSettingsFileChar, sizeof(candidateSettingsFileChar));
    candidateSettingsFile.close();
    if (!isDefaultSettingsFile(candidateSettingsFileChar)) {
      nListed++;
      if (nListed == position) {
        resumeTimerAfterSD();
        return true;
      }
    }
    memset(candidateSettingsFileChar, 0, sizeof(candidateSettingsFileChar));
  }
  resumeTimerAfterSD();
  return false;
}

#if (HARDWARE_VERSION == 3)
  FLASHMEM bool formatCard() { // Run from flash: rarely used, and limited by the card
    if (candidateSettingsFile) {
      candidateSettingsFile.close();
    }
    Serial.println("STATUS: Starting Format...");
    FatFormatter formatter;
    // The formatter needs a 512-byte temporary workspace (cache)
    uint8_t cache[512];

    if (!formatter.format(sd.card(), cache, &Serial)) {
      Serial.println("ERROR: Format failed!");
      return false;
    }
    if (!sd.begin(SdioConfig(FIFO_SDIO))) {
      Serial.println("ERROR: Card re-init failed!");
      return false;
    }
    sd.mkdir("Pulse_Pal");
    sd.chdir("Pulse_Pal");
    if (!root.open("/Pulse_Pal")) {
      Serial.println("ERROR: Card re-init failed!");
      return false;
    }
    currentSettingsFileName.toCharArray(currentSettingsFileNameChar, sizeof(currentSettingsFileNameChar));
    settingsFile.open(currentSettingsFileNameChar, O_READ);
    Serial.println("SUCCESS: Card format complete!");
    return true;
  }
#endif
