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
void LoadDefaultParameters() {
  for (int x = 0; x < 4; x++) {
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
   settingsFile.close();
   strcpy(currentSettingsFileNameChar, DEFAULT_SETTINGS_FILE_NAME);
   settingsFile.open(currentSettingsFileNameChar, O_READ);
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
uint32_t readLongFromSD() {
  uint32_t output = 0;
  settingsFile.read(typeCast.byteArray, 4);
  output = typeCast.uint32;
  return output;
}
uint16_t readShortFromSD() {
  uint16_t output = 0;
  settingsFile.read(typeCast.byteArray, 2);
  output = typeCast.uint16;
  return output;
}
byte readByteFromSD() {
  byte myByte = 0;
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
void SaveCurrentProgram2SD() {
  settingsFile.close();
  settingsFile.open(currentSettingsFileNameChar, O_CREAT | O_TRUNC | O_RDWR);
  // This function saves all parameters to the SD card, using the settings file layout above
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
  settingsFile.close();
  settingsFile.open(currentSettingsFileNameChar, O_READ);
}

// Reads parameters from the current settings file. Returns the last byte read, which is SETTINGS_FILE_END_MARKER if the file is valid.
// If the file is invalid, the caller must load valid parameters (e.g. with LoadDefaultParameters()).
byte RestoreParametersFromSD() {
  settingsFile.rewind();
  for (int chan = 0; chan < 4; chan++) {
    Phase1Duration[chan] = readLongFromSD();
    InterPhaseInterval[chan] = readLongFromSD();
    Phase2Duration[chan] = readLongFromSD();
    InterPulseInterval[chan] = readLongFromSD();
    BurstDuration[chan] = readLongFromSD();
    BurstInterval[chan] = readLongFromSD();
    PulseTrainDuration[chan] = readLongFromSD();
    PulseTrainDelay[chan] = readLongFromSD();
    IsBiphasic[chan] = readByteFromSD();
    Phase1Voltage[chan] = readShortFromSD();
    Phase2Voltage[chan] = readShortFromSD();
    RestingVoltage[chan] = readShortFromSD();
    CustomTrainID[chan] =  readByteFromSD();
    CustomTrainTarget[chan] = readByteFromSD();
    CustomTrainLoop[chan] = readByteFromSD();
    updateUsesBursts(chan);
  }
  for (int chan = 0; chan < 2; chan++) {
    TriggerMode[chan] = readByteFromSD();
    settingsFile.read(buf4, sizeof(buf4));
    TriggerAddress[chan][0] = buf4[0];
    TriggerAddress[chan][1] = buf4[1];
    TriggerAddress[chan][2] = buf4[2];
    TriggerAddress[chan][3] = buf4[3];
  }
  byte isValidProgram = readByteFromSD();
  return isValidProgram;
}

void rewindDirectory() {
  #if (HARDWARE_VERSION > 2)
    root.rewindDirectory();
  #else
    sd.vwd()->rewind();
  #endif
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
  candidateSettingsFile.close();
  rewindDirectory();
  uint16_t nListed = 0;
  #if (HARDWARE_VERSION > 2)
  while (candidateSettingsFile.openNext(&root, O_READ)) {
  #else
  while (candidateSettingsFile.openNext(sd.vwd(), O_READ)) {
  #endif
    candidateSettingsFile.getName(candidateSettingsFileChar, sizeof(candidateSettingsFileChar));
    candidateSettingsFile.close();
    if (!isDefaultSettingsFile(candidateSettingsFileChar)) {
      nListed++;
      if (nListed == position) {
        return true;
      }
    }
    memset(candidateSettingsFileChar, 0, sizeof(candidateSettingsFileChar));
  }
  return false;
}

#if (HARDWARE_VERSION == 3)
  bool formatCard() {
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
