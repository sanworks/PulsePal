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


// USB communication with the PC (MATLAB, Python and other clients). Op codes are listed in enum OpCode
// in PulsePal.ino.
//
// Functions in this file:
//   paramValueBytes()
//   isValidOutputChannel()
//   isValidTriggerChannel()
//   validateOutputParams()
//   validateParamBuffer()
//   requireNonZeroCycles()
//   paramSyncEnabled()
//   updateParamSyncPending()
//   otherCustomTrainBlock()
//   discardBytes()
//   discardUntilQuiet()
//   processUSBCommands()
//   HandleReadTimeout()
//   loadParamsFromBuffer()
//   loadChannelParamsFromBuffer()
//   applyParamBuffer()
//   startParamSync()
//   loadWaitingParamSyncChannels()
//   applyStoredCustomTrains()
//   sendCurrentParams()
//   sendStatus()
//   sendSerialNumber()
//   loadCustomPulseTrain()
//
// Note on reads: every PPUSB read gives up if no byte arrives for 100ms, and sets PPUSB.timedOut(). The rest of the
// command then reads as zeros, and loop() shows a comm failure message and loads the default parameters. See ArCOM.h.
// Replies are buffered, and sent by the PPUSB.flush() call in loop().
//
// Note on confirm bytes: ops that reply send 1 if the command was executed, or 0 if it was rejected. A command is
// rejected when a channel number, parameter code or data length is out of range. The data of a rejected command is
// read and discarded, so that it is not interpreted as the next command. MATLAB and Python raise an error on 0.

// Returns the number of bytes that follow a parameter code in ops 74 and 91, per channel, or 0 if the code is unknown.
// Output channel parameters are described by the parameter table in PulsePal.ino, where the row for a code is at
// index code - 1, and the type of a row is its size in bytes.
byte paramValueBytes(byte paramCode) {
  if (paramCode == PARAM_TRIGGER_MODE) {
    return PARAM_TYPE_BYTE; // A trigger channel parameter, so it is not in the output parameter table
  }
  if ((paramCode >= 1) && (paramCode <= PARAM_CONTINUOUS_LOOP)) {
    return outputParams[paramCode - 1].type;
  }
  return 0; // Unknown parameter code
}

// Channel numbers in USB commands are 1-indexed
bool isValidOutputChannel(byte channel) {
  return (channel >= 1) && (channel <= 4);
}

bool isValidTriggerChannel(byte channel) {
  return (channel >= 1) && (channel <= 2);
}

// Checks the parameters that would otherwise make handler() read outside its arrays or play a phase that never ends,
// and resets any that are invalid. Returns 1 if every parameter was valid, or 0 if any was reset.
// validateParamBuffer() below applies the same rules to a parameter set still in paramBuffer. Change both together.
//
// Times of 0 cycles: handler() finds a transition by comparing SystemTime with its time for equality, starting on the
// cycle after it was scheduled. A time of 0 has passed by then, so a phase 1 or phase 2 of 0 cycles holds its voltage
// until the train ends, and an inter-pulse interval of 0 stops a monophasic train after one pulse. Biphasic pulses
// with no interval are valid: handler() goes straight from phase 2 to phase 1. A channel is checked as a whole, so
// changing a biphasic channel with no interval to monophasic needs the interval set first.
byte validateOutputParams() {
  byte allValid = 1;
  for (int i = 0; i < 4; i++) {
    if (CustomTrainID[i] > N_CUSTOM_PULSE_TRAINS) {CustomTrainID[i] = 0; allValid = 0;}
    if (CustomTrainTarget[i] > 1) {CustomTrainTarget[i] = 0; allValid = 0;}
    if (CustomTrainLoop[i] > 1) {CustomTrainLoop[i] = 0; allValid = 0;}
    if (IsBiphasic[i] > 1) {IsBiphasic[i] = 0; allValid = 0;}
    if (Phase1Duration[i] == 0) {Phase1Duration[i] = 1; allValid = 0;}
    if (Phase2Duration[i] == 0) {Phase2Duration[i] = 1; allValid = 0;}
    if ((InterPulseInterval[i] == 0) && (IsBiphasic[i] == 0)) {InterPulseInterval[i] = 1; allValid = 0;}
  }
  for (int i = 0; i < 2; i++) {
    if (TriggerMode[i] > MAX_TRIGGER_MODE) {TriggerMode[i] = TRIGGER_MODE_NORMAL; allValid = 0;}
  }
  return allValid;
}

// The same checks as validateOutputParams(), applied to a parameter set in op 92's layout (in paramBuffer, or a
// settings file rearranged by RestoreParametersFromSD()) and correcting it in place, so that op 92 can answer for a
// set it has not loaded yet. A set in param sync mode is loaded by handler(), which has no way to report a bad value,
// so it is checked here instead and is known to be safe by the time it is loaded. The walk below matches
// loadChannelParamsFromBuffer(), so the fields it skips are the ones no check applies to.
byte validateParamBuffer(uint8_t *buffer) {
  byte allValid = 1;
  uint8_t *nextByte = buffer;
  uint8_t *phase1Duration = nextByte;     nextByte += sizeof(Phase1Duration);
  nextByte += sizeof(InterPhaseInterval);
  uint8_t *phase2Duration = nextByte;     nextByte += sizeof(Phase2Duration);
  uint8_t *interPulseInterval = nextByte; nextByte += sizeof(InterPulseInterval);
  nextByte += sizeof(BurstDuration) + sizeof(BurstInterval) + sizeof(PulseTrainDuration) + sizeof(PulseTrainDelay);
  nextByte += sizeof(Phase1Voltage) + sizeof(Phase2Voltage) + sizeof(RestingVoltage); // Times and voltages use their full range
  uint8_t *isBiphasic = nextByte;         nextByte += sizeof(IsBiphasic);
  uint8_t *customTrainID = nextByte;      nextByte += sizeof(CustomTrainID);
  uint8_t *customTrainTarget = nextByte;  nextByte += sizeof(CustomTrainTarget);
  uint8_t *customTrainLoop = nextByte;    nextByte += sizeof(CustomTrainLoop);
  nextByte += sizeof(ContinuousLoopMode) + sizeof(TriggerAddress); // Any nonzero value of either is valid
  uint8_t *triggerMode = nextByte;
  for (int i = 0; i < 4; i++) {
    if (customTrainID[i] > N_CUSTOM_PULSE_TRAINS) {customTrainID[i] = 0; allValid = 0;}
    if (customTrainTarget[i] > 1) {customTrainTarget[i] = 0; allValid = 0;}
    if (customTrainLoop[i] > 1) {customTrainLoop[i] = 0; allValid = 0;}
    if (isBiphasic[i] > 1) {isBiphasic[i] = 0; allValid = 0;}
    if (!requireNonZeroCycles(phase1Duration, i)) {allValid = 0;}
    if (!requireNonZeroCycles(phase2Duration, i)) {allValid = 0;}
    if ((isBiphasic[i] == 0) && !requireNonZeroCycles(interPulseInterval, i)) {allValid = 0;}
  }
  for (int i = 0; i < 2; i++) {
    if (triggerMode[i] > MAX_TRIGGER_MODE) {triggerMode[i] = TRIGGER_MODE_NORMAL; allValid = 0;}
  }
  return allValid;
}

// For validateParamBuffer(): sets an output channel's value in a time array in paramBuffer to 1 cycle if it is 0 (see
// validateOutputParams()), and returns 0 if it did. The array is part of a byte buffer, so it is read and written with
// memcpy.
byte requireNonZeroCycles(uint8_t *timeArray, int channel) {
  uint32_t cycles;
  memcpy(&cycles, timeArray + channel * sizeof(cycles), sizeof(cycles));
  if (cycles == 0) {
    cycles = 1;
    memcpy(timeArray + channel * sizeof(cycles), &cycles, sizeof(cycles));
    return 0;
  }
  return 1;
}

#if (HARDWARE_VERSION > 2)
  // True if a trigger channel is in param sync mode, in which case op 92 holds its parameter set in paramBuffer
  // until a rising edge on that channel loads it.
  bool paramSyncEnabled() {
    return (TriggerMode[0] == TRIGGER_MODE_PARAM_SYNC) || (TriggerMode[1] == TRIGGER_MODE_PARAM_SYNC);
  }
#endif

// Discards a parameter set waiting in paramBuffer once no trigger channel is left in param sync mode, and makes the
// custom trains loaded for the next param sync edge current at once. Everything that sets TriggerMode other than op 92
// calls this once the trigger modes are final: ops 73, 74 and 91 after validating, LoadDefaultParameters(),
// RestoreParametersFromSD() and the joystick menu. Without it, a channel taken out of param sync mode and later put
// back would load a set sent long before, instead of waiting for a new one. A custom train load is not discarded but
// applied, as Wave Pal applies its waveform loads: op 95 replaced the train, and the client knows no other.
// Op 92 clears the flag itself. Pulse Pal 2 has no param sync mode and nothing to discard. Call from loop() only.
void updateParamSyncPending() {
  #if (HARDWARE_VERSION > 2)
    if (!paramSyncEnabled()) {
      paramSyncPending = false;
      noInterrupts(); // handler() reads the trains' current blocks when it starts a channel
      applyStoredCustomTrains();
      interrupts();
    }
  #endif
}

// Train t's memory block that is not its current block (see "Custom train memory" in PulsePal.ino). On Pulse Pal 2,
// which has one block per train, the current block.
byte otherCustomTrainBlock(byte train) {
  #if (HARDWARE_VERSION > 2)
    return (currentCustomTrainBlock[train] == train) ? train + N_CUSTOM_PULSE_TRAINS : train;
  #else
    return currentCustomTrainBlock[train];
  #endif
}

// Reads and discards the data of a command that cannot be executed
void discardBytes(uint64_t nBytes) {
  for (uint64_t i = 0; i < nBytes; i++) {
    PPUSB.readByte();
    if (PPUSB.timedOut()) { // The rest of the message never arrived
      return;
    }
  }
}

// Reads and discards bytes until the USB port has been quiet for 100ms. Used when a command is rejected before its
// data length is known, so that its data is not interpreted as new commands.
void discardUntilQuiet() {
  uint32_t lastByteTime = millis();
  while ((millis() - lastByteTime) < 100) {
    if (PPUSB.available()) {
      PPUSB.readByte();
      lastByteTime = millis();
    }
  }
}

// Reads one command from the USB serial port (if available) and executes it. See enum OpCode for the list of ops.
void processUSBCommands() {
    if (PPUSB.available()) { // If bytes are available in the serial port buffer and a custom pulse train transfer is not ongoing
    CommandByte = PPUSB.readByte(); // Read a byte
    if (CommandByte == OpMenuByte) { // The first byte must be 213. Now, read the actual command byte. (Reduces interference from port scanning applications)
      screenSaverActivity = true; // loop() wakes the screen once the command is done and its reply sent
      CommandByte = PPUSB.readByte(); // Read the command byte (an op code for the operation to execute)
      switch (CommandByte) {
        case OP_HANDSHAKE: { // Op 72. Handshake
          PPUSB.writeByte(75); // Send 'K' (as in ok)
          PPUSB.writeUint32(FIRMWARE_VERSION); // Send the firmware version as a 4 byte unsigned integer
          inMenu = MENU_TOP;
        } break;
        case OP_PROGRAM_ALL_PARAMS_LEGACY: { // Op 73. Program the module - legacy method for backwards compatability. See op 92 for the more efficient method used by the current Python and MATLAB classes
          for (int x = 0; x < 4; x++) { // Read timing parameters (4 byte integers)
            Phase1Duration[x] = PPUSB.readUint32();
            InterPhaseInterval[x] = PPUSB.readUint32();
            Phase2Duration[x] = PPUSB.readUint32();
            InterPulseInterval[x] = PPUSB.readUint32();
            BurstDuration[x] = PPUSB.readUint32();
            BurstInterval[x] = PPUSB.readUint32();
            PulseTrainDuration[x] = PPUSB.readUint32();
            PulseTrainDelay[x] = PPUSB.readUint32();
          }
          for (int x = 0; x < 4; x++) { // Read voltage parameters (2 byte integers)
            Phase1Voltage[x] = PPUSB.readUint16();
            Phase2Voltage[x] = PPUSB.readUint16();
            RestingVoltage[x] = PPUSB.readUint16();
          }
          for (int x = 0; x < 4; x++) { // Read single byte parameters
            IsBiphasic[x] = PPUSB.readByte();
            CustomTrainID[x] = PPUSB.readByte();
            CustomTrainTarget[x] = PPUSB.readByte();
            CustomTrainLoop[x] = PPUSB.readByte();
          }
         for (int x = 0; x < 2; x++) { // Read 8 bytes that link trigger channels to specific output channels
           for (int y = 0; y < 4; y++) {
             TriggerAddress[x][y] = PPUSB.readByte();
           }
         }
         TriggerMode[0] = PPUSB.readByte(); // Read bytes that set interpretation of trigger channel voltage
         TriggerMode[1] = PPUSB.readByte();
         byte paramsValid = validateOutputParams();
         updateParamSyncPending(); // This op may have taken a trigger channel out of param sync mode
         for (int x = 0; x < 4; x++) {
           updateUsesBursts(x);
           setRestingVoltageIfIdle(x);
         }
         PPUSB.writeByte(paramsValid); // Send confirm byte (0 if any parameter was out of range)
        } break;
        
        case OP_PROGRAM_ONE_PARAM: { // Op 74. Program one parameter on one channel. Used by the Python class to set one channel, by the MATLAB class
                                     // with firmware v21, and by the legacy MATLAB interface. See op 91 to set one parameter on all channels.
          inByte2 = PPUSB.readByte(); // Parameter code
          inByte3 = PPUSB.readByte(); // Channel: 1-4, or 1-2 for trigger channel parameters
          byte nValueBytes = paramValueBytes(inByte2);
          if (nValueBytes == 0) { // Unknown parameter code, so the length of the value that follows is unknown
            discardUntilQuiet();
            PPUSB.writeByte(0);
            break;
          }
          if ((inByte2 == PARAM_TRIGGER_MODE) ? !isValidTriggerChannel(inByte3) : !isValidOutputChannel(inByte3)) {
            discardBytes(nValueBytes);
            PPUSB.writeByte(0);
            break;
          }
          inByte3 = inByte3 - 1; // Convert channel for zero-indexing
          ContinuousLoopModeOriginal[inByte3] = ContinuousLoopMode[inByte3];
          switch (inByte2) { 
             case PARAM_IS_BIPHASIC: {IsBiphasic[inByte3] = PPUSB.readByte();} break;
             case PARAM_PHASE1_VOLTAGE: {Phase1Voltage[inByte3] = PPUSB.readUint16();} break;
             case PARAM_PHASE2_VOLTAGE: {Phase2Voltage[inByte3] = PPUSB.readUint16();} break;
             case PARAM_PHASE1_DURATION: {Phase1Duration[inByte3] = PPUSB.readUint32();} break;
             case PARAM_INTER_PHASE_INTERVAL: {InterPhaseInterval[inByte3] = PPUSB.readUint32();} break;
             case PARAM_PHASE2_DURATION: {Phase2Duration[inByte3] = PPUSB.readUint32();} break;
             case PARAM_INTER_PULSE_INTERVAL: {InterPulseInterval[inByte3] = PPUSB.readUint32();} break;
             case PARAM_BURST_DURATION: {BurstDuration[inByte3] = PPUSB.readUint32();} break;
             case PARAM_BURST_INTERVAL: {BurstInterval[inByte3] = PPUSB.readUint32();} break;
             case PARAM_PULSE_TRAIN_DURATION: {PulseTrainDuration[inByte3] = PPUSB.readUint32();} break;
             case PARAM_PULSE_TRAIN_DELAY: {PulseTrainDelay[inByte3] = PPUSB.readUint32();} break;
             case PARAM_LINK_TRIGGER1: {inByte4 = PPUSB.readByte(); TriggerAddress[0][inByte3] = inByte4;} break;
             case PARAM_LINK_TRIGGER2: {inByte4 = PPUSB.readByte(); TriggerAddress[1][inByte3] = inByte4;} break;
             case PARAM_CUSTOM_TRAIN_ID: {CustomTrainID[inByte3] = PPUSB.readByte();} break;
             case PARAM_CUSTOM_TRAIN_TARGET: {CustomTrainTarget[inByte3] = PPUSB.readByte();} break;
             case PARAM_CUSTOM_TRAIN_LOOP: {CustomTrainLoop[inByte3] = PPUSB.readByte();} break;
             case PARAM_RESTING_VOLTAGE: {RestingVoltage[inByte3] = PPUSB.readUint16();} break;
             case PARAM_CONTINUOUS_LOOP: {ContinuousLoopMode[inByte3] = PPUSB.readByte();} break;
             case PARAM_TRIGGER_MODE: {TriggerMode[inByte3] = PPUSB.readByte();} break;
          }
          byte paramValid = validateOutputParams();
          updateParamSyncPending(); // This op may have taken a trigger channel out of param sync mode
          updateUsesBursts(inByte3);
          if (inByte2 == PARAM_RESTING_VOLTAGE) {
            setRestingVoltageIfIdle(inByte3);
          }
          if (inByte2 == PARAM_CONTINUOUS_LOOP) {
            if (!ContinuousLoopMode[inByte3] && ContinuousLoopModeOriginal[inByte3]) {
              endContinuousLoop(inByte3);
            }
          }
          PPUSB.writeByte(paramValid); // Send confirm byte (0 if the value was out of range)
        } break;
  
        case OP_LOAD_CUSTOM_TRAIN1_LEGACY: { // Op 75. Legacy op to program custom pulse train 1. Used by the MATLAB and Python classes with firmware v21. Otherwise they use op 95
          usbLoadTarget = 0;
          usbLoadFlag = true;
        } break;
        
        case OP_LOAD_CUSTOM_TRAIN2_LEGACY: { // Op 76. Legacy op to program custom pulse train 2. Used by the MATLAB and Python classes with firmware v21. Otherwise they use op 95
          usbLoadTarget = 1;
          usbLoadFlag = true;
        } break;      
        
        case OP_SOFT_TRIGGER: { // Op 77. Soft-trigger specific output channels. Which channels are indicated as bits of a single byte read.
          inByte2 = PPUSB.readByte();
          // All the channels are scheduled with interrupts off: when handler() ran between two of them, one op 77
          // started its channels 50us apart. A timer tick in that time is handled as soon as interrupts() is called,
          // a few instructions late, and no output edge moves: dacWriteTimed() times the DAC from the timer's counter.
          noInterrupts();
          for (int i = 0; i < 4; i++) {
            // Serial reading takes up too much time so the channel trigger logic is scheduled for the next cycle
            // (albeit at the expense of ~50us latency). Channels are only ever added: two op 77s read before that cycle
            // (e.g. in one USB packet) must trigger both sets of channels, not just the second.
            if (bitRead(inByte2, i)) {
              SoftTriggerScheduled[i] = 1;
            }
          }
          interrupts();
        } break;
        case OP_DISPLAY_MESSAGE: { // Op 78. Display a custom message on the oLED screen
          LCD_clear();
          LCD_home(); 
          byte ByteCount = 0;
          // read all the available characters
          inByte2 = PPUSB.readByte(); // Total length of message to follow (including newline)
          while (ByteCount < inByte2) {
              // display each character to the LCD
              inByte = PPUSB.readByte();
              if (inByte != 254) {
                lcd.write(inByte);
              } else {
                LCD_setCursor(0, 1);
              }
              ByteCount++;
          }
          #if (HARDWARE_VERSION == 3)
            lcd.render();
          #endif
        } break;
        case OP_SET_FIXED_VOLTAGE: { // Op 79. Write specific voltage to an output channel (not a pulse train) 
          uint8_t myChannel = PPUSB.readByte();
          uint16_t val = PPUSB.readUint16();
          if (!isValidOutputChannel(myChannel)) {
            PPUSB.writeByte(0);
            break;
          }
          myChannel = myChannel - 1; // Convert for zero-indexing
          setDAC(myChannel, val);
          if (val == RestingVoltage[myChannel]) {
            digitalWriteDirect(OutputLEDLines[myChannel], LOW);
          } else {
            digitalWriteDirect(OutputLEDLines[myChannel], HIGH);
          }
          PPUSB.writeByte(1); // Send confirm byte
        } break;
        case OP_ABORT_ALL: { // Op 80. Soft-abort ongoing stimulation without disconnecting from client
         for (int i = 0; i < 4; i++) {
          killChannel(i);
        }
       } break;
       case OP_DISCONNECT: { // Op 81. Disconnect from PC app
          inMenu = MENU_TOP;
          for (int i = 0; i < 4; i++) {
            killChannel(i);
          }
          for (int i = 0; i < 16; i++) {
           CommanderString[i] = DefaultCommanderString[i];
         } 
          write2Screen(CommanderString," Click for menu");
         } break;
        case OP_SET_CONTINUOUS_LOOP: { // Op 82. Set Continuous Loop mode (play the current parametric pulse train indefinitely)
          inByte2 = PPUSB.readByte(); // Channel
          inByte3 = PPUSB.readByte(); // State (0 = off, 1 = on)
          if (!isValidOutputChannel(inByte2) || (inByte3 > 1)) {
            PPUSB.writeByte(0);
            break;
          }
          inByte2 = inByte2 - 1; // Convert for zero-indexing
          ContinuousLoopMode[inByte2] = inByte3;
          if (!inByte3) {
            endContinuousLoop(inByte2);
          }
          PPUSB.writeByte(1);
        } break;
      case OP_SEND_SETTINGS_FILE: { // Op 85. Return the currently loaded parameter file from the microSD card
          settingsFile.rewind();
          for (int i = 0; i < SETTINGS_FILE_N_PARAM_BYTES; i++) {
            settingsFile.read(buf, sizeof(buf));
            PPUSB.writeByte(buf[0]);
          }
        } break;
        
        case OP_DEBUG_WRITE_PIN: { // Op 86. Override Arduino IO Lines (for development and debugging only - may disrupt normal function)
          inByte2 = PPUSB.readByte();
          inByte3 = PPUSB.readByte();
          pinMode(inByte2, OUTPUT); digitalWrite(inByte2, inByte3);
        } break; 
        
        case OP_DEBUG_READ_PIN: { // Op 87. Direct Read IO Lines (for development and debugging only - may disrupt normal function)
          inByte2 = PPUSB.readByte();
          pinMode(inByte2, INPUT);
          delayMicroseconds(10);
          LogicLevel = digitalRead(inByte2);
          PPUSB.writeByte(LogicLevel);
        } break; 
        case OP_SET_CLIENT_NAME: { // Op 89. Receive new CommanderString (displayed on top line of OLED, i.e. "MATLAB connected"
          for (int x = 0; x < 6; x++) {
            CommanderString[x] = PPUSB.readByte();
          }
          for (int x = 6; x < 16; x++) {
            CommanderString[x] = ClientStringSuffix[x-6];
          }
          write2Screen(CommanderString," Click for menu");
        } break;
        case OP_SD_SETTINGS_FILE: { // Op 90. Save, load or delete the current microSD settings file
          byte confirmBit = 1;
          settingsOp = PPUSB.readByte();
          settingsFileNameLength = PPUSB.readByte();
          currentSettingsFileName = "";
          for (int i = 0; i < settingsFileNameLength; i++) {
            currentSettingsFileName = currentSettingsFileName + (char)PPUSB.readByte();
          }
          if ((settingsOp < 1) || (settingsOp > 3) || (settingsFileNameLength == 0)) {
            PPUSB.writeByte(0); // Unknown file operation, or empty file name
            break;
          }
          pauseTimerForSD(); // Pulse Pal 2: handler() must not use the SPI bus while the card does
          #if (HARDWARE_VERSION == 2)
            if (playbackOngoing()) { // The card cannot be used without pausing playback, so nothing is done
              resumeTimerAfterSD();
              PPUSB.writeByte(2); // Refused while a channel plays (Pulse Pal 2 only, see /Firmware/PROTOCOL.md)
              break;
            }
          #endif
          // The settings file the device had, to keep if this op does not make another one current
          char previousFileName[sizeof(currentSettingsFileNameChar)];
          strcpy(previousFileName, currentSettingsFileNameChar);
          settingsFile.close();
          currentSettingsFileName.toCharArray(currentSettingsFileNameChar, sizeof(currentSettingsFileNameChar));
          if (settingsOp == 1) { // Save. The saved file becomes the current settings file
            confirmBit = SaveCurrentProgram2SD();
          } else if (settingsOp == 2) { // Load
            settingsFile.open(currentSettingsFileNameChar, O_READ);
            validProgram = RestoreParametersFromSD();
            if (validProgram != SETTINGS_FILE_END_MARKER) {
              // No such file, or not a valid one. Nothing was loaded: the device keeps its parameters, and its settings file
              confirmBit = 0;
              settingsFile.close();
              strcpy(currentSettingsFileNameChar, previousFileName);
              settingsFile.open(currentSettingsFileNameChar, O_READ);
            }
          } else if (settingsOp == 3) { // Delete
            confirmBit = sd.remove(currentSettingsFileNameChar); // 0 if there was no such file
            if (strcasecmp(previousFileName, currentSettingsFileNameChar) != 0) {
              strcpy(currentSettingsFileNameChar, previousFileName); // Another file was deleted: the current one stays
            } else {
              strcpy(currentSettingsFileNameChar, DEFAULT_SETTINGS_FILE_NAME); // The current one was deleted
            }
            settingsFile.open(currentSettingsFileNameChar, O_READ);
          }
          settingsFile.rewind();
          resumeTimerAfterSD();
          PPUSB.writeByte(confirmBit); // Send confirm byte (0 if the file could not be saved, loaded or deleted)
        } break;

        case OP_PROGRAM_PARAM_ALL_CHANNELS: { // Op 91. Program one parameter on all 4 output channels (or both trigger channels).
                                              // Used by the MATLAB class, and by the Python class when all channels are set at once.
                                              // See op 74 to set one channel.
          for (int i = 0; i < 4; i++) {
            ContinuousLoopModeOriginal[i] = ContinuousLoopMode[i];
          }
          inByte2 = PPUSB.readByte(); // Parameter code
          if (paramValueBytes(inByte2) == 0) { // Unknown parameter code, so the length of the values that follow is unknown
            discardUntilQuiet();
            PPUSB.writeByte(0);
            break;
          }
          switch (inByte2) {
             case PARAM_IS_BIPHASIC: {PPUSB.readByteArray(IsBiphasic, 4);} break;
             case PARAM_PHASE1_VOLTAGE: {PPUSB.readUint16Array(Phase1Voltage, 4);} break;
             case PARAM_PHASE2_VOLTAGE: {PPUSB.readUint16Array(Phase2Voltage, 4);} break;
             case PARAM_PHASE1_DURATION: {PPUSB.readUint32Array(Phase1Duration,4);} break;
             case PARAM_INTER_PHASE_INTERVAL: {PPUSB.readUint32Array(InterPhaseInterval,4);} break;
             case PARAM_PHASE2_DURATION: {PPUSB.readUint32Array(Phase2Duration, 4);} break;
             case PARAM_INTER_PULSE_INTERVAL: {PPUSB.readUint32Array(InterPulseInterval, 4);} break;
             case PARAM_BURST_DURATION: {PPUSB.readUint32Array(BurstDuration, 4);} break;
             case PARAM_BURST_INTERVAL: {PPUSB.readUint32Array(BurstInterval, 4);} break;
             case PARAM_PULSE_TRAIN_DURATION: {PPUSB.readUint32Array(PulseTrainDuration, 4);} break;
             case PARAM_PULSE_TRAIN_DELAY: {PPUSB.readUint32Array(PulseTrainDelay, 4);} break;
             case PARAM_LINK_TRIGGER1: {inByte3 = PPUSB.readByte(); TriggerAddress[0][0] = inByte3;
                       inByte3 = PPUSB.readByte(); TriggerAddress[0][1] = inByte3;
                       inByte3 = PPUSB.readByte(); TriggerAddress[0][2] = inByte3;
                       inByte3 = PPUSB.readByte(); TriggerAddress[0][3] = inByte3;} break;
             case PARAM_LINK_TRIGGER2: {inByte3 = PPUSB.readByte(); TriggerAddress[1][0] = inByte3;
                       inByte3 = PPUSB.readByte(); TriggerAddress[1][1] = inByte3;
                       inByte3 = PPUSB.readByte(); TriggerAddress[1][2] = inByte3;
                       inByte3 = PPUSB.readByte(); TriggerAddress[1][3] = inByte3;} break;
             case PARAM_CUSTOM_TRAIN_ID: {PPUSB.readByteArray(CustomTrainID, 4);} break;
             case PARAM_CUSTOM_TRAIN_TARGET: {PPUSB.readByteArray(CustomTrainTarget, 4);} break;
             case PARAM_CUSTOM_TRAIN_LOOP: {PPUSB.readByteArray(CustomTrainLoop, 4);} break;
             case PARAM_RESTING_VOLTAGE: {PPUSB.readUint16Array(RestingVoltage, 4);} break;
             case PARAM_CONTINUOUS_LOOP: {PPUSB.readByteArray(ContinuousLoopMode, 4);} break;
             case PARAM_TRIGGER_MODE: {PPUSB.readByteArray(TriggerMode, 2);} break;
          }
          byte paramsValid = validateOutputParams();
          updateParamSyncPending(); // This op may have taken a trigger channel out of param sync mode
          for (int iChan = 0; iChan < 4; iChan++) {
            updateUsesBursts(iChan);
            if (inByte2 == PARAM_RESTING_VOLTAGE) {
              setRestingVoltageIfIdle(iChan);
            }
            if (inByte2 == PARAM_CONTINUOUS_LOOP) {
              if (!ContinuousLoopMode[iChan] && ContinuousLoopModeOriginal[iChan]) {
                endContinuousLoop(iChan);
              }
            }
          }
          PPUSB.writeByte(paramsValid); // Send confirm byte (0 if any value was out of range)
        } break;

        case OP_PROGRAM_ALL_PARAMS: {// Op 92. Program all parameters. More efficient than op 73. This method is used by current MATLAB and Python classes.
          #if (HARDWARE_VERSION > 2)
            paramSyncPending = false; // A set buffered earlier and never loaded is replaced by this one
          #endif
          PPUSB.readBlock(paramBuffer, PARAM_BUFFER_N_BYTES); // The whole parameter set arrives as one block
          byte allParamsValid = validateParamBuffer(paramBuffer); // Checked here, because a set loaded by handler() cannot be reported on
          bool waitForSyncEdge = false;
          #if (HARDWARE_VERSION > 2)
            waitForSyncEdge = paramSyncEnabled(); // A trigger channel is in param sync mode: hold the set for its next rising edge
            // Set after the read, so that handler() never loads a partly written buffer. A set whose read timed out is
            // mostly zeros; it is not held, and loop() loads the default parameters.
            paramSyncPending = waitForSyncEdge && !PPUSB.timedOut();
          #endif
          if (!waitForSyncEdge) {
            applyParamBuffer(paramBuffer); // No channel is waiting for a TTL, so program the device now
          }
          PPUSB.writeByte(allParamsValid); // Send confirm byte (0 if any parameter was out of range)
        } break;
        case OP_SEND_CURRENT_PARAMS: { // Op 93. Send all current parameters
          sendCurrentParams();
        } break;
        case OP_SEND_HARDWARE_INFO: { // Op 94. Send hardware info
          PPUSB.writeByte(HARDWARE_VERSION);
          PPUSB.writeUint32(TIMER_PERIOD);
          PPUSB.writeByte(N_CUSTOM_PULSE_TRAINS);
          PPUSB.writeUint32(MAX_CUSTOM_PULSES);
        } break;
        case OP_LOAD_CUSTOM_TRAIN: { // Op 95. Load custom pulse train. The next byte is the train index (0 = train 1). Used by the MATLAB and Python classes. See legacy ops 75 and 76 above.
          usbLoadTarget = PPUSB.readByte();
          usbLoadFlag = true;
        } break;
        case OP_SET_ZERO_CODE_CALIBRATION: { // Op 96. Set Calibration to offset DAC Zero Code Error on a single channel
          inByte = PPUSB.readByte(); // Output channel, zero-indexed
          uint16_t calibrationValue = PPUSB.readUint16();
          if (inByte > 3) {
            PPUSB.writeByte(0);
            break;
          }
          ZeroCodeCalibration[inByte] = calibrationValue;
          PPUSB.writeByte(1); // Send confirm byte
          #if (HARDWARE_VERSION > 2)
            EEPROM.put(EEPROM_ZERO_CODE_CALIBRATION_ADDRESS, ZeroCodeCalibration);
          #endif
          setRestingVoltageIfIdle(inByte);
        } break;
        case OP_FORMAT_SD_CARD: { // Op 97. Format microSD card
          byte key[sizeof(FORMAT_SD_CARD_KEY) - 1];
          PPUSB.readByteArray(key, sizeof(key));
          if (memcmp(key, FORMAT_SD_CARD_KEY, sizeof(key)) != 0) {
            break; // Not a format command, e.g. 213 and 97 in the data of a command that was not read as a whole: ignored
          }
          #if (HARDWARE_VERSION > 2)
            mountOK = formatCard();
            PPUSB.writeByte(mountOK);
            if (!mountOK) {
              write2Screen("SD CARD ERROR"," Click for menu");
            }
            LoadDefaultParameters();
            SaveCurrentProgram2SD(); // Recreate the default settings file
          #endif
        } break;
        case OP_ABORT_CHANNELS: { // Op 98. Terminate ongoing stimulation on a specific set of output channels
         inByte = PPUSB.readByte();
         for (int i = 0; i < 4; i++) {
          if bitRead(inByte, i) {
            killChannel(i);
          }
        }
       } break;
        case OP_SET_SCREEN_SAVER: { // Op 99. Switch the screen saver on (1) or off (0), and set its timeout in seconds. Used by the
                                    // MATLAB and Python classes. Saved to the EEPROM once no channel is playing (see updateScreenSaver()).
          inByte2 = PPUSB.readByte(); // State
          uint16_t timeout = PPUSB.readUint16();
          if ((inByte2 > MAX_SCREEN_SAVER_STATE) || (timeout == 0)) {
            PPUSB.writeByte(0);
            break;
          }
          screenSaverEnabled = inByte2;
          screenSaverTimeout = timeout;
          screenSaverSavePending = true;
          PPUSB.writeByte(1); // Send confirm byte. Like every command, this one wakes the screen if it is dimmed.
        } break;
        case OP_SEND_HARDWARE_REVISION: { // Op 100. Send the circuit board revision, read at startup by readHardwareRevision()
                                          // (Pulse Pal 3; Pulse Pal 2 sends 0). Used by the MATLAB, Python and C++ classes.
          PPUSB.writeByte(hardwareRevision);
        } break;
        case OP_SEND_STATUS: { // Op 101. Send the output channels playing, and the counters since startup or the last op 101
          sendStatus();
        } break;
        case OP_SEND_SERIAL_NUMBER: { // Op 102. Send the USB serial number. Used by the MATLAB, Python and C++ classes
          sendSerialNumber();
        } break;
     }
    }
  }
}

FLASHMEM void HandleReadTimeout() { // Waits for a click: run from flash
  byte FlashState = 0;
  write2Screen("COMM. FAILURE!","Click joystick->");
  ClickerButtonState = 1;
  SerialReadStartTime = millis(); // Reused Serial time vars to conserve memory
  while (ClickerButtonState != 0) {
    ClickerButtonState = digitalRead(ClickerButtonLine);
    SerialCurrentTime = millis();
    if ((SerialCurrentTime - SerialReadStartTime) > 100) { // Time to flash
      if (FlashState == 0) {
        digitalWriteDirect(InputLEDLines[0], LOW);
        digitalWriteDirect(InputLEDLines[1], LOW);
        FlashState = 1;
        SerialReadStartTime = millis();
      } else {
        digitalWriteDirect(InputLEDLines[0], HIGH);
        digitalWriteDirect(InputLEDLines[1], HIGH);
        FlashState = 0;
        SerialReadStartTime = millis();
      }
    }
  }
  digitalWriteDirect(InputLEDLines[0], LOW);
  digitalWriteDirect(InputLEDLines[1], LOW);
  write2Screen("Loading default","parameters...");
  LoadDefaultParameters();
  delayMicroseconds(2000000);
  write2Screen(CommanderString," Click for menu");
}

// Copies a complete parameter set in op 92's layout (paramBuffer, or a settings file rearranged by
// RestoreParametersFromSD()) into the parameter arrays, by loading each output channel in turn and then the trigger
// modes. The set must already have been through validateParamBuffer().
//
// This is deliberately separate from the USB read: it touches parameters that handler() also reads, and it is short
// enough to run inside one timer cycle, which is what lets param sync mode load a set at a trial's onset. Keep it
// free of anything that waits.
void loadParamsFromBuffer(const uint8_t *buffer) {
  for (int x = 0; x < 4; x++) {
    loadChannelParamsFromBuffer(buffer, x);
  }
  memcpy(TriggerMode, buffer + PARAM_BUFFER_TRIGGER_MODE_OFFSET, sizeof(TriggerMode));
}

// Copies one output channel's parameters out of a buffer holding an op 92 parameter set. The buffer holds the
// parameters in the order they arrive on the wire, which is the order of the copies below, and is also the order
// sendCurrentParams() writes them in. Each parameter array in it holds one value per output channel, so this takes
// only this channel's value from each and steps over the rest. Both supported boards are little-endian, like the
// wire format, so the multi-byte values need no rearranging. See /Firmware/PROTOCOL.md.
//
// This is the only description of the buffer's layout that the loading path has: loadParamsFromBuffer() above is a
// loop over it, and param sync mode uses it to load one channel at a time. Trigger mode is not an output channel
// parameter, so it is not copied here. validateParamBuffer() walks the same layout to range-check a set.
void loadChannelParamsFromBuffer(const uint8_t *buffer, byte channel) {
  const uint8_t *nextByte = buffer;
  memcpy(&Phase1Duration[channel], nextByte + channel*sizeof(Phase1Duration[0]), sizeof(Phase1Duration[0])); nextByte += sizeof(Phase1Duration);
  memcpy(&InterPhaseInterval[channel], nextByte + channel*sizeof(InterPhaseInterval[0]), sizeof(InterPhaseInterval[0])); nextByte += sizeof(InterPhaseInterval);
  memcpy(&Phase2Duration[channel], nextByte + channel*sizeof(Phase2Duration[0]), sizeof(Phase2Duration[0])); nextByte += sizeof(Phase2Duration);
  memcpy(&InterPulseInterval[channel], nextByte + channel*sizeof(InterPulseInterval[0]), sizeof(InterPulseInterval[0])); nextByte += sizeof(InterPulseInterval);
  memcpy(&BurstDuration[channel], nextByte + channel*sizeof(BurstDuration[0]), sizeof(BurstDuration[0])); nextByte += sizeof(BurstDuration);
  memcpy(&BurstInterval[channel], nextByte + channel*sizeof(BurstInterval[0]), sizeof(BurstInterval[0])); nextByte += sizeof(BurstInterval);
  memcpy(&PulseTrainDuration[channel], nextByte + channel*sizeof(PulseTrainDuration[0]), sizeof(PulseTrainDuration[0])); nextByte += sizeof(PulseTrainDuration);
  memcpy(&PulseTrainDelay[channel], nextByte + channel*sizeof(PulseTrainDelay[0]), sizeof(PulseTrainDelay[0])); nextByte += sizeof(PulseTrainDelay);
  memcpy(&Phase1Voltage[channel], nextByte + channel*sizeof(Phase1Voltage[0]), sizeof(Phase1Voltage[0])); nextByte += sizeof(Phase1Voltage);
  memcpy(&Phase2Voltage[channel], nextByte + channel*sizeof(Phase2Voltage[0]), sizeof(Phase2Voltage[0])); nextByte += sizeof(Phase2Voltage);
  memcpy(&RestingVoltage[channel], nextByte + channel*sizeof(RestingVoltage[0]), sizeof(RestingVoltage[0])); nextByte += sizeof(RestingVoltage);
  IsBiphasic[channel] = nextByte[channel]; nextByte += sizeof(IsBiphasic);
  CustomTrainID[channel] = nextByte[channel]; nextByte += sizeof(CustomTrainID);
  CustomTrainTarget[channel] = nextByte[channel]; nextByte += sizeof(CustomTrainTarget);
  CustomTrainLoop[channel] = nextByte[channel]; nextByte += sizeof(CustomTrainLoop);
  ContinuousLoopMode[channel] = nextByte[channel]; nextByte += sizeof(ContinuousLoopMode);
  TriggerAddress[0][channel] = nextByte[channel]; // 8 bytes linking trigger channels to output channels, 4 per trigger channel
  TriggerAddress[1][channel] = nextByte[channel + sizeof(TriggerAddress[0])];
}

// Loads a parameter set in op 92's layout and brings the device into line with it: UsesBursts is recomputed, the new
// resting voltage is sent to the DAC for idle channels, and a channel whose continuous loop mode was switched off stops
// (see endContinuousLoop()). The set must
// already have been through validateParamBuffer(). Op 92 calls this from loop() with paramBuffer when no trigger
// channel is in param sync mode, and RestoreParametersFromSD() with a settings file. A param sync edge takes the other
// route, through startParamSync().
void applyParamBuffer(const uint8_t *buffer) {
  for (int i = 0; i < 4; i++) {
    ContinuousLoopModeOriginal[i] = ContinuousLoopMode[i]; // Read before the buffer overwrites it
  }
  loadParamsFromBuffer(buffer);
  for (int x = 0; x < 4; x++) {
    updateUsesBursts(x);
    setRestingVoltageIfIdle(x);
    if (!ContinuousLoopMode[x] && ContinuousLoopModeOriginal[x]) {
      endContinuousLoop(x);
    }
  }
}

#if (HARDWARE_VERSION > 2)
  // Called by handler() on a rising edge of a trigger channel in param sync mode. The set op 92 left in paramBuffer
  // is copied to syncedParamBuffer, so that a later op 92 can replace paramBuffer while channels are still waiting,
  // and every output channel is marked as owing a load. loadWaitingParamSyncChannels() then loads the channels that
  // are idle, which is usually all of them.
  //
  // Trigger mode is not an output channel parameter, and takes effect here: it is what a trigger line does, not part
  // of a pulse train, and a set that leaves param sync mode has to be able to do so at the edge.
  void startParamSync() {
    memcpy(syncedParamBuffer, paramBuffer, PARAM_BUFFER_N_BYTES);
    memcpy(TriggerMode, syncedParamBuffer + PARAM_BUFFER_TRIGGER_MODE_OFFSET, sizeof(TriggerMode));
    paramSyncChannelsWaiting = 0x0F; // All four output channels
    loadWaitingParamSyncChannels();
  }

  // Loads the synced parameter set into every waiting output channel that is not playing a pulse train, and leaves
  // the others waiting. handler() calls this at a param sync edge and on every cycle afterwards while any channel is
  // still waiting, so a channel takes its new parameters in the cycle its pulse train ends, before the next cycle
  // can trigger it again. A parametric train in continuous loop mode has no end, and waits until something stops it.
  //
  // Everything here is safe inside the timer callback: it neither waits nor touches the screen, the microSD card or
  // the USB port, and setDAC() is called by handler() in any case. A waiting channel is idle, so its resting voltage
  // can be sent without cutting a pulse short.
  void loadWaitingParamSyncChannels() {
    for (int x = 0; x < 4; x++) {
      if (bitRead(paramSyncChannelsWaiting, x) && (StimulusStatus[x] == 0) && (PreStimulusStatus[x] == 0)) {
        loadChannelParamsFromBuffer(syncedParamBuffer, x);
        updateUsesBursts(x);
        setDAC(x, RestingVoltage[x]);
        bitClear(paramSyncChannelsWaiting, x);
      }
    }
  }

  // Makes the custom trains op 95 loaded in param sync mode current: each one's loaded memory block becomes the block
  // channels play from their next start, and channels playing the old block finish their trains on it. Called by
  // handler() at a param sync edge, and by updateParamSyncPending() with interrupts off when the last trigger channel
  // leaves param sync mode. A few instructions, so safe in the timer callback.
  void applyStoredCustomTrains() {
    for (int t = 0; t < N_CUSTOM_PULSE_TRAINS; t++) {
      if (bitRead(customTrainLoadsStored, t)) {
        currentCustomTrainBlock[t] = otherCustomTrainBlock(t);
      }
    }
    customTrainLoadsStored = 0;
  }
#endif

void sendCurrentParams() {
    PPUSB.writeUint32Array(Phase1Duration, 4);
    PPUSB.writeUint32Array(InterPhaseInterval, 4);
    PPUSB.writeUint32Array(Phase2Duration, 4);
    PPUSB.writeUint32Array(InterPulseInterval, 4);
    PPUSB.writeUint32Array(BurstDuration, 4);
    PPUSB.writeUint32Array(BurstInterval, 4);
    PPUSB.writeUint32Array(PulseTrainDuration, 4);
    PPUSB.writeUint32Array(PulseTrainDelay, 4);
    PPUSB.writeUint16Array(Phase1Voltage, 4);
    PPUSB.writeUint16Array(Phase2Voltage, 4);
    PPUSB.writeUint16Array(RestingVoltage, 4);
    PPUSB.writeByteArray(IsBiphasic, 4);
    PPUSB.writeByteArray(CustomTrainID, 4);
    PPUSB.writeByteArray(CustomTrainTarget, 4);
    PPUSB.writeByteArray(CustomTrainLoop, 4);
     for (int x = 0; x < 2; x++) { // Read 8 trigger address bytes
       for (int y = 0; y < 4; y++) {
        PPUSB.writeByte(TriggerAddress[x][y]);
       }
     }
     PPUSB.writeByteArray(TriggerMode, 2);
}

// Op 101. Sends one bit per output channel playing (bit 0 = channel 1), in its train or its delay, then the counters
// handler() and loop() keep (see triggerEdgeCount in PulsePal.ino): rising edges on trigger channels 1-2, starts of
// output channels 1-4 and comm failures, uint32 each. They count from the last op 101, or from startup, and are
// cleared here. The playing bits and the counters are read, and the counters cleared, with interrupts off, so an event
// handler() counts is in this reply or the next one, never lost or in both.
void sendStatus() {
  byte playing = 0;
  uint32_t edges[2];
  uint32_t starts[4];
  noInterrupts();
  for (int x = 0; x < 4; x++) {
    if (StimulusStatus[x] || PreStimulusStatus[x]) {
      bitSet(playing, x);
    }
    starts[x] = channelStartCount[x];
    channelStartCount[x] = 0;
  }
  for (int x = 0; x < 2; x++) {
    edges[x] = triggerEdgeCount[x];
    triggerEdgeCount[x] = 0;
  }
  interrupts();
  PPUSB.writeByte(playing);
  PPUSB.writeUint32Array(edges, 2);
  PPUSB.writeUint32Array(starts, 4);
  PPUSB.writeUint32(commFailureCount);
  commFailureCount = 0;
}

// Op 102. Sends the USB serial number: its length (byte), then its characters (ASCII). It is the serial number the
// computer sees in the device's USB descriptor, so a client can find the device's port by it, and record which unit
// ran an experiment. On Pulse Pal 3 it is the Teensy's, set by the Teensy core from the chip's unique ID: a number of
// up to 10 digits, the same with every firmware. The Arduino Due core sends no unique serial number, so Pulse Pal 2
// sends an empty one (length 0).
void sendSerialNumber() {
  #if (HARDWARE_VERSION > 2)
    byte length = (usb_string_serial_number.bLength - 2) / 2; // UTF-16 characters after the descriptor's 2-byte header
    PPUSB.writeByte(length);
    for (byte i = 0; i < length; i++) {
      PPUSB.writeByte((byte)usb_string_serial_number.wString[i]); // Digits: ASCII in UTF-16
    }
  #else
    PPUSB.writeByte(0);
  #endif
}

// Loads a custom pulse train from the USB serial port (ops 75, 76 and 95). trainID is zero-indexed.
// Each pulse has a 4-byte time and a 2-byte voltage, so the data after the pulse count is nPulses*6 bytes.
//
// The train is loaded into its other memory block (see "Custom train memory" in PulsePal.ino), as a Wave Pal waveform is:
// - Outside param sync mode, the channels playing the train stop first, and the train is left empty while it loads,
//   so that a trigger meanwhile plays nothing (an empty train ends at once). Once stored, the loaded block is current.
//   Writing over a train while a channel played it once left the channel "playing" at rest, ignoring triggers.
// - In param sync mode, channels go on playing the current block. Once stored, the loaded block waits for the next
//   param sync edge (customTrainLoadsStored), which makes it current. A channel still playing the other block, from
//   before the previous edge, stops first. A later load of the same train before the edge replaces this one.
// Pulse Pal 2 has one block per train and no param sync mode, so its channels playing the train always stop.
// A train whose transfer is cut short is not stored: outside param sync mode it is left empty, and in param sync mode
// the current block plays on.
void loadCustomPulseTrain(byte trainID) {
  uint32_t nPulses = PPUSB.readUint32();
  if ((trainID >= N_CUSTOM_PULSE_TRAINS) || (nPulses == 0) || (nPulses > MAX_CUSTOM_PULSES)) {
    discardBytes((uint64_t)nPulses * 6);
    PPUSB.writeByte(0); // Unknown custom train, an empty train, or more pulses than the device can store
    return;
  }
  byte block = otherCustomTrainBlock(trainID);
  noInterrupts(); // handler() reads the blocks, and starts channels on them
  bool paramSync = false;
  #if (HARDWARE_VERSION > 2)
    paramSync = paramSyncEnabled(); // Trigger modes change only through ops, and at param sync edges (checked again below)
    bitClear(customTrainLoadsStored, trainID); // A load stored earlier for this train is in this block: this one replaces it
  #endif
  if (paramSync) {
    stopCustomTrainChannels(trainID, block);
  } else {
    stopCustomTrainChannels(trainID, ANY_CUSTOM_TRAIN_BLOCK);
    CustomTrainNpulses[currentCustomTrainBlock[trainID]] = 0;
  }
  CustomTrainNpulses[block] = 0;
  interrupts();
  PPUSB.readUint32Array(CustomPulseTimes[block], nPulses); // One block read for the times, and one for the voltages
  PPUSB.readUint16Array(CustomVoltages[block], nPulses);
  if (PPUSB.timedOut()) { // The rest of the train never arrived. loop() reports the failure.
    PPUSB.writeByte(0);
    return;
  }
  noInterrupts();
  CustomTrainNpulses[block] = nPulses;
  #if (HARDWARE_VERSION > 2)
    // A param sync edge during the load can have taken the trigger channels out of param sync mode, with a set from op
    // 92. The load then applies at once, as one stored before that edge would have when the mode was left.
    if (paramSync && paramSyncEnabled()) {
      bitSet(customTrainLoadsStored, trainID); // The next param sync edge makes it current
    } else {
      currentCustomTrainBlock[trainID] = block;
    }
  #endif
  interrupts();
  PPUSB.writeByte(1); // Send confirm byte
}
