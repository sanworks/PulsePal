/*
----------------------------------------------------------------------------

This file is part of the Pulse Pal Project
Copyright (C) 2017 Joshua I. Sanders, Sanworks LLC, NY, USA

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

// PULSE PAL firmware v2.0.1 
// Josh Sanders, May 2017
//
// ** DEPENDENCY YOU NEED TO INSTALL FIRST **

// 1. This firmware uses the sdFat library, developed by Bill Greiman. (Thanks Bill!!)
// Download it from here: https://github.com/greiman/SdFat
// and copy it to your /Arduino/Libraries folder.

// Note: This firmware uses the open source DueTimer library, developed by Ivan Seidel. (Thanks Ivan!!)
// Download it from here: https://github.com/ivanseidel/DueTimer
// and copy it to your /Arduino/Libraries folder.
// The DueTimer library is open source, and protected by the MIT License.

// ** Next, upload the firmware to Pulse Pal 2's Arduino Due **
// See here for driver installation and upload instructions: https://www.arduino.cc/en/Guide/ArduinoDue

#include <LiquidCrystal.h>
#include "DueTimer.h"
#include <stdio.h>
#include <stdint.h>
#include <SPI.h>
#include <SdFat.h>

// Define macros for compressing sequential bytes read from the serial port into long and short ints
#define makeUnsignedLong(msb, byte2, byte3, lsb) ((msb << 24) | (byte2 << 16) | (byte3 << 8) | (lsb))
#define makeUnsignedShort(msb, lsb) ((msb << 8) | (lsb))

// Trigger line level configuration. This defines the logic level when the trigger is activated.
// The optoisolator in Pulse Pal 2 is inverting, so its output is high by default, and becomes low 
// when voltage is applied to the trigger channel. Set this to 1 if using a non-inverting isolator.
#define TriggerLevel 0

// Firmware build number. Pulse Pal 1.X ranges from 1-19. Pulse Pal 2.X ranges from 20+
unsigned long FirmwareVersion = 21;

// initialize Arduino LCD library with the numbers of the interface pins
LiquidCrystal lcd(10, 9, 8, 7, 6, 5);

// Variables that define other hardware pins
byte TriggerLines[2] = {12,11}; // Trigger channels 1 and 2
byte InputLEDLines[2] = {13, A0}; // LEDs above trigger channels 1-2. An = Arduino Analog Channel n.
byte OutputLEDLines[4] = {A1,A7,A11,A10}; // LEDs above output channels 1-4
int ClickerXLine = A8; // Analog line that reports the thumb joystick x axis
int ClickerYLine = A9; // Analog line that reports the thumb joystick y axis
byte ClickerButtonLine = 15; // Digital line that reports the thumb joystick click state
byte SyncPin=44; // AD5724 Pin 7 (Sync)
byte LDACPin=A2; // AD5724 Pin 10 (LDAC)
byte SDChipSelect=14; // microSD CS Pin 

// Variables for SPI bus
SPISettings DACSettings(25000000, MSBFIRST, SPI_MODE2); // Settings for DAC

// Parameters that define pulse trains currently loaded on the 4 output channels
// For a visual description of these parameters, see https://sites.google.com/site/pulsepalwiki/parameter-guide
// The following parameters are times in microseconds:
unsigned long Phase1Duration[4] = {0}; // Pulse Duration in monophasic mode, first phase in biphasic mode
unsigned long InterPhaseInterval[4] = {0}; // Interval between phases in biphasic mode (at resting voltage)
unsigned long Phase2Duration[4] = {0}; // Second phase duration in biphasic mode
unsigned long InterPulseInterval[4] = {0}; // Interval between pulses
unsigned long BurstDuration[4] = {0}; // Duration of sequential bursts of pulses (0 if not using bursts)
unsigned long BurstInterval[4] = {0}; // Interval between sequential bursts of pulses (0 if not using bursts)
unsigned long PulseTrainDuration[4] = {0}; // Duration of pulse train
unsigned long PulseTrainDelay[4] = {0}; // Delay between trigger and pulse train onset
// The following are volts in bits. 16 bits span -10V to +10V.
uint16_t Phase1Voltage[4] = {0}; // The pulse voltage in monophasic mode, and phase 1 voltage in biphasic mode
uint16_t Phase2Voltage[4] = {0}; // Phase 2 voltage in biphasic mode.
uint16_t RestingVoltage[4] = {32768}; // Voltage the system returns to between pulses (32768 bits = 0V)
// The following are single byte parameters
int CustomTrainID[4] = {0}; // If 0, uses above params. If 1 or 2, pulse times and voltages are played back from CustomTrain1 or 2
int CustomTrainTarget[4] = {0}; // If 0, custom times define start-times of pulses. If 1, custom times are start-times of bursts.
int CustomTrainLoop[4] = {0}; // if 0, custom stim plays once. If 1, custom stim loops until PulseTrainDuration.
byte TriggerAddress[2][4] = {0}; // This specifies which output channels get triggered by trigger channel 1 (row 1) or trigger channel 2 (row 2)
byte TriggerMode[2] = {0}; // if 0, "Normal mode", low to high transitions on trigger channels start stimulation (but do not cancel it) 
//                            if 1, "Toggle mode", same as normal mode, but low-to-high transitions do cancel ongoing pulse trains
//                            if 2, "Pulse Gated mode", low to high starts playback and high to low stops it.

// Variables used in programming
byte OpMenuByte = 213; // This byte must be the first byte in any serial transmission to Pulse Pal. Reduces the probability of interference from port-scanning software
unsigned long CustomTrainNpulses[2] = {0}; // Stores the total number of pulses in the custom pulse train
boolean SerialReadTimedout = 0; // Goes to 1 if a serial read timed out, causing all subsequent serial reads to skip until next main loop iteration.
int SerialCurrentTime = 0; // Current time (millis) for serial read timeout
int SerialReadStartTime = 0; // Time the serial read was started
int Timeout = 500; // Times out after 500ms
byte BrokenBytes[4] = {0}; // Used to store sequential bytes when converting bytes to short and long ints

// Variables used in stimulus playback
byte inByte; byte inByte2; byte inByte3; byte inByte4; byte CommandByte;
byte LogicLevel = 0;
unsigned long SystemTime = 0; // Number of cycles since stimulation start
unsigned long MicrosTime = 0; // Actual system time (microseconds from boot, wraps over every 72m
unsigned long BurstTimestamps[4] = {0};
unsigned long PrePulseTrainTimestamps[4] = {0};
unsigned long PulseTrainTimestamps[4] = {0};
unsigned long NextPulseTransitionTime[4] = {0}; // Stores next pulse-high or pulse-low timestamp for each channel
unsigned long NextBurstTransitionTime[4] = {0}; // Stores next burst-on or burst-off timestamp for each channel
unsigned long PulseTrainEndTime[4] = {0}; // Stores time the stimulus train is supposed to end
unsigned long CustomPulseTimes[2][5001] = {0};
uint16_t CustomVoltages[2][5001] = {0};
int CustomPulseTimeIndex[4] = {0}; // Keeps track of the pulse number of the custom train currently being played on each channel
unsigned long LastLoopTime = 0;
byte PulseStatus[4] = {0}; // This is 0 if not delivering a pulse, 1 if phase 1, 2 if inter phase interval, 3 if phase 2.
boolean BurstStatus[4] = {0}; // This is "true" during bursts and false during inter-burst intervals.
boolean StimulusStatus[4] = {0}; // This is "true" for a channel when the stimulus train is actively being delivered
boolean PreStimulusStatus[4] = {0}; // This is "true" for a channel during the pre-stimulus delay
boolean InputValues[2] = {0}; // The values read directly from the two inputs (for analog, digital equiv. after thresholding)
boolean InputValuesLastCycle[2] = {0}; // The values on the last cycle. Used to detect low to high transitions.
byte LineTriggerEvent[2] = {0}; // 0 if no line trigger event detected, 1 if low-to-high, 2 if high-to-low.
unsigned long InputLineDebounceTimestamp[2] = {0}; // Last time the line went from high to low
boolean UsesBursts[4] = {0};
unsigned long PulseDuration[4] = {0}; // Duration of a pulse (sum of 3 phases for biphasic pulse)
boolean IsBiphasic[4] = {0};
boolean IsCustomBurstTrain[4] = {0};
boolean ContinuousLoopMode[4] = {0}; // If true, the channel loops its programmed stimulus train continuously
byte StimulatingState = 0; // 1 if ANY channel is stimulating, 2 if this is the first cycle after the system was triggered. 
byte LastStimulatingState = 0;
boolean WasStimulating = 0; // true if any channel was stimulating on the previous loop. Used to force a DAC write after all channels end their stimulation, to return lines to 0
int nStimulatingChannels = 0; // number of actively stimulating channels
boolean DACFlag = 0; // true if any DAC channel needs to be updated
byte DefaultInputLevel = 0; // 0 for PulsePal 0.3, 1 for 0.2 and 0.1. Logic is inverted by optoisolator

// SD variables
//const size_t BUF_SIZE = 1;
uint8_t buf[1];
uint8_t buf2[2];
uint8_t buf4[4];
SdFat sd;
SdFile settingsFile;
SdFile candidateSettingsFile;
String currentSettingsFileName = "default.pps"; // Filename is a string so it can be easily resized
byte settingsFileNameLength = 0; // Set when a new file name is entered
char currentSettingsFileNameChar[100]; // Filename must be converted from string to character array for use with sdFAT
char candidateSettingsFileChar[16];
byte settingsOp = 0; // Reports whether to load an existing settings file, or create/overwrite, or delete
byte validProgram = 0; // Reports whether the program just loaded from the SD card is valid 
uint16_t myFilePos = 2; // Index of current file position in folder. 0 and 1 are . and ..

// variables used in thumb joystick menus
char Value2Display[18] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0'}; // Holds text for sprintf
int ScrollSpeedDelay = 200000; // Microseconds before scrolling values while joystick is held in one direction
byte CursorPos = 0;
byte CursorPosRightLimit = 0;
byte CursorPosLeftLimit = 0;
byte ValidCursorPositions[9] = {0};
int Digits[9] = {0};
unsigned int DACBits = pow(2,16);
float CandidateVoltage = 0; // used to see if voltage will go over limits for DAC
float FractionalVoltage = 0;
unsigned long CursorToggleTimer = 0; 
unsigned long CursorToggleThreshold = 20000;
boolean CursorOn = 0;
int ClickerX = 0; // Value of analog reads from X line of joystick input device
int ClickerY = 0; // Value of analog reads from Y line of joystick input device
int ClickerMinThreshold = 300; // Joystick position to consider an upwards or leftwards movement (on Y and X lines respectively)
int ClickerMaxThreshold = 700;
boolean ClickerButtonState = 0; // Value of digital reads from button line of joystick input device
boolean LastClickerButtonState = 1;
unsigned int DebounceTime = 0; // Time since the joystick button changed states
int LastClickerYState = 0; // 0 for neutral, 1 for up, 2 for down.
int LastClickerXState = 0; // 0 for neutral, 1 for left, 2 for right.
int inMenu = 0; // Menu level: 0 for top, 1 for channel menu, 2 for action menu
int SelectedChannel = 0; // Channel the user has selected
int SelectedAction = 1; // Action the user has selected
byte isNegativeZero = 0; // Keeps track of negative zero in digit-wise voltage adjustment menu
byte SelectedInputAction = 1; // Trigger channel action the user has selected
int SelectedStimMode = 1; // Manual trigger from joystick menu. 1 = Single train, 2 = Single pulse, 3 = continuous stimulation
int lastDebounceTime = 0; // to debounce the joystick button
boolean lastButtonState = 0; // last logic state of joystick button
boolean ChoiceMade = 0; // determines whether user has chosen a value from a list
unsigned int UserValue = 0; // The current value displayed on a list of values (written to LCD when choosing parameters)
char CommanderString[16] = " PULSE PAL v2.0"; // Displayed at the menu top when disconnected from software
char DefaultCommanderString[16] = " PULSE PAL v2.0"; // The CommanderString can be overwritten. This stores the original.
char ClientStringSuffix[11] = " Connected"; // Displayed after 6-character client ID string (as in, "MATLAB Connected")
char centeredText[16] = {0}; // Global for returning centered text to display on a 16-char screen line
byte fileNameOffset = 0; // Offset of centered string (for display on 16-char screen)
char tempText[16] = {0}; // Temporary buffer for holding a file name or other text
boolean NeedUpdate = 0; // If a new menu item is selected, the screen must be updated

// Screen saver variables
boolean useScreenSaver = 0; // Disabled by default
boolean SSactive = 0; // Bit indicating whether screen saver is currently active
unsigned long SSdelay = 60000; // Idle cycles until screen saver is activated
unsigned long SScount = 0; // Counter of idle cycles

// Other variables
int ConnectedToApp = 0; // 0 if disconnected, 1 if connected
byte CycleDuration = 50; // in microseconds, time between hardware cycles (each cycle = read trigger channels, update output channels)
unsigned int CycleFrequency = 20000; // in Hz, same idea as CycleDuration
void handler(void);
boolean SoftTriggered[4] = {0}; // If a software trigger occurred this cycle (for timing reasons, it is scheduled to occur on the next cycle)
boolean SoftTriggerScheduled[4] = {0}; // If a software trigger is scheduled for the next cycle
unsigned long callbackStartTime = 0;
boolean DACFlags[4] = {0}; // Flag to indicate whether each output channel needs to be updated in a call to dacWrite()
byte dacBuffer[3] = {0}; // Holds bytes about to be written via SPI (for improved transfer speed with array writes)
union {
    byte byteArray[8];
    uint16_t uint16[4];
} dacValue; // Union allows faster type conversion between 16-bit DAC values and bytes to write via SPI

void setup() {
  pinMode(SyncPin, OUTPUT); // Configure SPI bus pins as outputs
  pinMode(LDACPin, OUTPUT);
  SPI.begin();
  SPI.beginTransaction(DACSettings);
  digitalWriteDirect(LDACPin, LOW);
  ProgramDAC(12, 0, 4); // Set DAC output range to +/- 10V
  // Set DAC to resting voltage on all channels
  for (int i = 0; i < 4; i++) {
    RestingVoltage[i] = 32768; // 16-bit code for 0, in the range of -10 to +10
    dacValue.uint16[i] = RestingVoltage[i];
    DACFlags[i] = 1; // DACFlags must be set to 1 on each channel, so the channels aren't skipped in dacWrite()
  }
  ProgramDAC(16, 0, 31); // Power up DACs
  dacWrite(); // Update the DAC
  SerialUSB.begin(115200); // Initialize Serial USB interface at 115.2kbps
  // set up the LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.home();
  lcd.noDisplay() ;
  delay(100);
  lcd.display() ;
  
  // Pin modes
  pinMode(TriggerLines[0], INPUT); // Configure trigger pins as digital inputs
  pinMode(TriggerLines[1], INPUT);
  pinMode(ClickerButtonLine, INPUT_PULLUP); // Configure clicker button as digital input with an internal pullup resistor
 
  for (int x = 0; x < 4; x++) {
    pinMode(OutputLEDLines[x], OUTPUT); // Configure channel LED pins as outputs
    digitalWrite(OutputLEDLines[x], LOW); // Initialize channel LEDs to low (off)
  }
    pinMode(SDChipSelect, OUTPUT);
    // microSD setup
    delay(100);
    if (!sd.begin(SDChipSelect, SPI_FULL_SPEED)) {
      sd.initErrorHalt();
    }
    if (!sd.chdir("Pulse_Pal")) { // Create/enter a unique folder for Pulse Pal
      sd.mkdir("Pulse_Pal");
      sd.chdir("Pulse_Pal");
    }
    currentSettingsFileName.toCharArray(currentSettingsFileNameChar, sizeof(currentSettingsFileName));
    settingsFile.open(currentSettingsFileNameChar, O_READ);
    
    for (int x = 0; x < 2; x++) {
      pinMode(InputLEDLines[x], OUTPUT);
      digitalWrite(InputLEDLines[x], LOW);
    }
    validProgram = RestoreParametersFromSD();
    if (validProgram != 252) { // 252 is the last byte in a real program file, returned from RestoreParametersFromSD()
      LoadDefaultParameters();
    }
    write2Screen(CommanderString," Click for menu");
    DefaultInputLevel = 1 - TriggerLevel;
    InputValuesLastCycle[0] = digitalRead(TriggerLines[0]); // Pre-read trigger channels
    InputValuesLastCycle[1] = digitalRead(TriggerLines[1]);
    SystemTime = 0;
    LastLoopTime = SystemTime;
    Timer3.attachInterrupt(handler);
    Timer3.start(50); // Calls handler precisely every 50us
}

void loop() {

}

void handler(void) {                     
  if (SerialReadTimedout == 1) { // A serial USB message started, but didn't finish as expected
    Timer3.stop();
    HandleReadTimeout(); // Notifies user of error, then prompts to click and restores DEFAULT channel settings.
    SerialReadTimedout = 0;
    Timer3.start();
  }
  if (StimulatingState == 0) {
      if (LastStimulatingState == 1) { // The cycle on which all pulse trains have finished
        dacWrite(); // Update DAC to final voltages (should be resting voltage)
        DACFlag = 0;
      }
      UpdateSettingsMenu(); // Check for joystick button click, handle if detected
      SystemTime = 0;
      if (!inMenu) { // If at the thumb joystick menu top
        if (useScreenSaver) { // Screensaver logic, if enabled
          SScount++;
          if (SScount > SSdelay) {
            if (!SSactive) {
              SSactive = 1;
              lcd.clear();
            }
          }
        }
      }
   } else {
//     if (StimulatingState == 2) {
//                // Place to include code that executes on the first cycle of a pulse train
//     }
       StimulatingState = 1;
       if (DACFlag == 1) { // A DAC update was requested
         dacWrite(); // Update DAC
         DACFlag = 0;
       }
       SystemTime++; // Increment system time (# of hardware timer cycles since stim start)
       ClickerButtonState = digitalReadDirect(ClickerButtonLine); // Read the joystick button
       if (ClickerButtonState == 0){ // A button click (pulls line to ground, = logic 0) and ends ongoing stimulation on all channels.
         AbortAllPulseTrains();
       }
    }
    for (int i = 0; i<4; i++) {
      if(SoftTriggerScheduled[i]) { // Soft triggers are "scheduled" to be handled on the next cycle, since the serial read took too much time.
        SoftTriggered[i] = 1;
        SoftTriggerScheduled[i] = 0;
      }
    }
    LastStimulatingState = StimulatingState;
      
  if (SerialUSB.available()) { // If bytes are available in the serial port buffer
    CommandByte = SerialUSB.read(); // Read a byte
    if (CommandByte == OpMenuByte) { // The first byte must be 213. Now, read the actual command byte. (Reduces interference from port scanning applications)
      CommandByte = SerialReadByte(); // Read the command byte (an op code for the operation to execute)
      switch (CommandByte) {
        case 72: { // Handshake
          SerialUSB.write(75); // Send 'K' (as in ok)
          SerialWriteLong(FirmwareVersion); // Send the firmware version as a 4 byte unsigned integer
          ConnectedToApp = 1;
        } break;
        case 73: { // Program the module - total program (can be faster than item-wise, if many parameters have changed)
          for (int x = 0; x < 4; x++) { // Read timing parameters (4 byte integers)
            Phase1Duration[x] = SerialReadLong();
            InterPhaseInterval[x] = SerialReadLong();
            Phase2Duration[x] = SerialReadLong();
            InterPulseInterval[x] = SerialReadLong();
            BurstDuration[x] = SerialReadLong();
            BurstInterval[x] = SerialReadLong();
            PulseTrainDuration[x] = SerialReadLong();
            PulseTrainDelay[x] = SerialReadLong();
          }
          for (int x = 0; x < 4; x++) { // Read voltage parameters (2 byte integers)
            Phase1Voltage[x] = SerialReadShort();
            Phase2Voltage[x] = SerialReadShort();
            RestingVoltage[x] = SerialReadShort();
          }
          for (int x = 0; x < 4; x++) { // Read single byte parameters
            IsBiphasic[x] = SerialReadByte();
            CustomTrainID[x] = SerialReadByte();
            CustomTrainTarget[x] = SerialReadByte();
            CustomTrainLoop[x] = SerialReadByte();
          }
         for (int x = 0; x < 2; x++) { // Read 8 bytes that link trigger channels to specific output channels
           for (int y = 0; y < 4; y++) {
             TriggerAddress[x][y] = SerialReadByte();
           }
         }
         TriggerMode[0] = SerialReadByte(); // Read bytes that set interpretation of trigger channel voltage
         TriggerMode[1] = SerialReadByte();
         SerialUSB.write(1); // Send confirm byte
         for (int x = 0; x < 4; x++) {
           if ((BurstDuration[x] == 0) || (BurstInterval[x] == 0)) {UsesBursts[x] = false;} else {UsesBursts[x] = true;}
           if (CustomTrainTarget[x] == 1) {UsesBursts[x] = true;}
           if ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 0)) {UsesBursts[x] = false;}
           PulseDuration[x] = ComputePulseDuration(IsBiphasic[x], Phase1Duration[x], InterPhaseInterval[x], Phase2Duration[x]);
           if ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 1)) {
            IsCustomBurstTrain[x] = 1;
           } else {
            IsCustomBurstTrain[x] = 0;
           }
           dacValue.uint16[x] = RestingVoltage[x]; 
         }
         dacWrite();
        } break;
        
        case 74: { // Program one parameter
          inByte2 = SerialReadByte();
          inByte3 = SerialReadByte(); // inByte3 = channel (1-4)
          inByte3 = inByte3 - 1; // Convert channel for zero-indexing
          switch (inByte2) { 
             case 1: {IsBiphasic[inByte3] = SerialReadByte();} break;
             case 2: {Phase1Voltage[inByte3] = SerialReadShort();} break;
             case 3: {Phase2Voltage[inByte3] = SerialReadShort();} break;
             case 4: {Phase1Duration[inByte3] = SerialReadLong();} break;
             case 5: {InterPhaseInterval[inByte3] = SerialReadLong();} break;
             case 6: {Phase2Duration[inByte3] = SerialReadLong();} break;
             case 7: {InterPulseInterval[inByte3] = SerialReadLong();} break;
             case 8: {BurstDuration[inByte3] = SerialReadLong();} break;
             case 9: {BurstInterval[inByte3] = SerialReadLong();} break;
             case 10: {PulseTrainDuration[inByte3] = SerialReadLong();} break;
             case 11: {PulseTrainDelay[inByte3] = SerialReadLong();} break;
             case 12: {inByte4 = SerialReadByte(); TriggerAddress[0][inByte3] = inByte4;} break;
             case 13: {inByte4 = SerialReadByte(); TriggerAddress[1][inByte3] = inByte4;} break;
             case 14: {CustomTrainID[inByte3] = SerialReadByte();} break;
             case 15: {CustomTrainTarget[inByte3] = SerialReadByte();} break;
             case 16: {CustomTrainLoop[inByte3] = SerialReadByte();} break;
             case 17: {RestingVoltage[inByte3] = SerialReadShort();} break;
             case 128: {TriggerMode[inByte3] = SerialReadByte();} break;
          }
          if (inByte2 < 14) {
            if ((BurstDuration[inByte3] == 0) || (BurstInterval[inByte3] == 0)) {UsesBursts[inByte3] = false;} else {UsesBursts[inByte3] = true;}
            if (CustomTrainTarget[inByte3] == 1) {UsesBursts[inByte3] = true;}
            if ((CustomTrainID[inByte3] > 0) && (CustomTrainTarget[inByte3] == 0)) {UsesBursts[inByte3] = false;}
          }
          if (inByte2 == 17) {
            dacValue.uint16[inByte3] = RestingVoltage[inByte3];
            dacWrite();
          }
          PulseDuration[inByte3] = ComputePulseDuration(IsBiphasic[inByte3], Phase1Duration[inByte3], InterPhaseInterval[inByte3], Phase2Duration[inByte3]);
          if ((CustomTrainID[inByte3] > 0) && (CustomTrainTarget[inByte3] == 1)) {
            IsCustomBurstTrain[inByte3] = 1;
          } else {
            IsCustomBurstTrain[inByte3] = 0;
          }
          SerialUSB.write(1); // Send confirm byte
        } break;
  
        case 75: { // Program custom pulse train 1
          CustomTrainNpulses[0] = SerialReadLong();
          for (int x = 0; x < CustomTrainNpulses[0]; x++) {
            CustomPulseTimes[0][x] = SerialReadLong();
          }
          for (int x = 0; x < CustomTrainNpulses[0]; x++) {
            CustomVoltages[0][x] = SerialReadShort();
          }
          SerialUSB.write(1); // Send confirm byte
        } break;
        
        case 76: { // Program custom pulse train 2
          CustomTrainNpulses[1] = SerialReadLong();
          for (int x = 0; x < CustomTrainNpulses[1]; x++) {
            CustomPulseTimes[1][x] = SerialReadLong();
          }
          for (int x = 0; x < CustomTrainNpulses[1]; x++) {
            CustomVoltages[1][x] = SerialReadShort();
          }
          SerialUSB.write(1); // Send confirm byte
        } break;      
        
        case 77: { // Soft-trigger specific output channels. Which channels are indicated as bits of a single byte read.
          inByte2 = SerialReadByte();
          for (int i = 0; i < 4; i++) {
            // Serial reading takes up too much time so the channel trigger logic is scheduled for the next cycle
            // (albeit at the expense of ~50us latency)
            SoftTriggerScheduled[i] = bitRead(inByte2, i); 
          }
        } break;
        case 78: { // Display a custom message on the oLED screen
          lcd.clear();
           lcd.home(); 
           byte ByteCount = 0;
          // read all the available characters
          inByte2 = SerialReadByte(); // Total length of message to follow (including newline)
          while (ByteCount < inByte2) {
              // display each character to the LCD
              inByte = SerialReadByte();
              if (inByte != 254) {
                lcd.write(inByte);
              } else {
                lcd.setCursor(0, 1);
              }
              ByteCount++;
          }
        } break;
        case 79: { // Write specific voltage to an output channel (not a pulse train) 
          byte myChannel = SerialReadByte();
          myChannel = myChannel - 1; // Convert for zero-indexing
          uint16_t val = SerialReadShort();
          dacValue.uint16[myChannel] = val;
          DACFlags[myChannel] = 1;
          dacWrite();
          if (dacValue.uint16[myChannel] == RestingVoltage[myChannel]) {
            digitalWriteDirect(OutputLEDLines[myChannel], LOW);
          } else {
            digitalWriteDirect(OutputLEDLines[myChannel], HIGH);
          }
          SerialUSB.write(1); // Send confirm byte
        } break;
        case 80: { // Soft-abort ongoing stimulation without disconnecting from client
         for (int i = 0; i < 4; i++) {
          killChannel(i);
          DACFlags[i] = 1;
        }
        dacWrite();
       } break;
       case 81: { // Disconnect from client
          ConnectedToApp = 0;
          inMenu = 0;
          for (int i = 0; i < 4; i++) {
            killChannel(i);
            DACFlags[i] = 1;
          }
          dacWrite();
          for (int i = 0; i < 16; i++) {
           CommanderString[i] = DefaultCommanderString[i];
         } 
          write2Screen(CommanderString," Click for menu");
         } break;
        case 82:{ // Set Continuous Loop mode (play the current parametric pulse train indefinitely)
          inByte2 = SerialReadByte(); // Channel
          inByte2 = inByte2 - 1; // Convert for zero-indexing
          inByte3 = SerialReadByte(); // State (0 = off, 1 = on)
          ContinuousLoopMode[inByte2] = inByte3;
          if (inByte3) {
            SoftTriggerScheduled[inByte2] = 1;
          } else {
            killChannel(inByte2);
            DACFlags[inByte2] = 1;
            dacWrite();
          }
          SerialUSB.write(1);
        } break;
//        case 83: { // Clear stored parameters from EEPROM in Pulse Pal 1 (currently unused in Pulse Pal 2)
//
//       } break;
//       case 84: { // Write a page of memory to EEPROM in Pulse Pal 1 (currently unused in Pulse Pal 2)
//
//        } break; 
      case 85: { // Return the currently loaded parameter file from the microSD card
          settingsFile.rewind();
          for (int i = 0; i < 178; i++) {
            settingsFile.read(buf, sizeof(buf));
            SerialUSB.write(buf[0]);
          }
        } break;
        
        case 86: { // Override Arduino IO Lines (for development and debugging only - may disrupt normal function)
          inByte2 = SerialReadByte();
          inByte3 = SerialReadByte();
          pinMode(inByte2, OUTPUT); digitalWrite(inByte2, inByte3);
        } break; 
        
        case 87: { // Direct Read IO Lines (for development and debugging only - may disrupt normal function)
          inByte2 = SerialReadByte();
          pinMode(inByte2, INPUT);
          delayMicroseconds(10);
          LogicLevel = digitalRead(inByte2);
          SerialUSB.write(LogicLevel);
        } break; 
//        case 88: { // Unused
//        } break;
        case 89: { // Receive new CommanderString (displayed on top line of OLED, i.e. "MATLAB connected"
          for (int x = 0; x < 6; x++) {
            CommanderString[x] = SerialReadByte();
          }
          for (int x = 6; x < 16; x++) {
            CommanderString[x] = ClientStringSuffix[x-6];
          }
          write2Screen(CommanderString," Click for menu");
        } break;
        case 90: { // Save, load or delete the current microSD settings file
          byte confirmBit = 1;
          while (SerialUSB.available()==0){}
          settingsOp = SerialUSB.read();
          while (SerialUSB.available()==0){}
          settingsFileNameLength = SerialUSB.read();
          currentSettingsFileName = "";
          for (int i = 0; i < settingsFileNameLength; i++) {
            while (SerialUSB.available()==0){}
            currentSettingsFileName = currentSettingsFileName + (char)SerialUSB.read();
          }
          settingsFile.close();
          currentSettingsFileName.toCharArray(currentSettingsFileNameChar, settingsFileNameLength+1);
          if (settingsOp == 1) { // Save
            SaveCurrentProgram2SD();
          } else if (settingsOp == 2) { // Load
            settingsFile.open(currentSettingsFileNameChar, O_READ);
            validProgram = RestoreParametersFromSD();
            if (validProgram != 252) { // If load failed, load defaults and report error
              LoadDefaultParameters();
              settingsFile.close();
              currentSettingsFileName = "defaultSettings.pps";
              currentSettingsFileName.toCharArray(currentSettingsFileNameChar, sizeof(currentSettingsFileName));
              settingsFile.open(currentSettingsFileNameChar, O_READ);
              confirmBit = 0;
            } else {
              // Return parameters from file to update client
                for (int x = 0; x < 4; x++) {
                  SerialWriteLong(Phase1Duration[x]);
                  SerialWriteLong(InterPhaseInterval[x]);
                  SerialWriteLong(Phase2Duration[x]);
                  SerialWriteLong(InterPulseInterval[x]);
                  SerialWriteLong(BurstDuration[x]);
                  SerialWriteLong(BurstInterval[x]);
                  SerialWriteLong(PulseTrainDuration[x]);
                  SerialWriteLong(PulseTrainDelay[x]);
                } 
                for (int x = 0; x < 4; x++) {
                  SerialWriteShort(Phase1Voltage[x]);
                  SerialWriteShort(Phase2Voltage[x]);
                  SerialWriteShort(RestingVoltage[x]);
                }
                for (int x = 0; x < 4; x++) {
                  SerialUSB.write(IsBiphasic[x]);
                  SerialUSB.write(CustomTrainID[x]);
                  SerialUSB.write(CustomTrainTarget[x]);
                  SerialUSB.write(CustomTrainLoop[x]);
                }
               for (int x = 0; x < 2; x++) { // Read 8 trigger address bytes
                 for (int y = 0; y < 4; y++) {
                  SerialUSB.write(TriggerAddress[x][y]);
                 }
               }
               SerialUSB.write(TriggerMode[0]);
               SerialUSB.write(TriggerMode[1]);
             }
          } else if (settingsOp == 3) { // Delete
            sd.remove(currentSettingsFileNameChar);
          }
          settingsFile.rewind();
        } break;
     }
    }
  }

    // Read values of trigger pins
    LineTriggerEvent[0] = 0; LineTriggerEvent[1] = 0;
    for (int x = 0; x < 2; x++) {
         InputValues[x] = digitalReadDirect(TriggerLines[x]);
         if (InputValues[x] == TriggerLevel) {
           digitalWriteDirect(InputLEDLines[x], HIGH);
         } else {
           digitalWriteDirect(InputLEDLines[x], LOW);
         }
         // update LineTriggerEvent with logic representing logic transition
         if ((InputValues[x] == TriggerLevel) && (InputValuesLastCycle[x] == DefaultInputLevel)) {
           LineTriggerEvent[x] = 1; // Low to high transition
         } else if ((InputValues[x] == DefaultInputLevel) && (InputValuesLastCycle[x] == TriggerLevel)) {
           LineTriggerEvent[x] = 2; // High to low transition
         }
         InputValuesLastCycle[x] = InputValues[x];
    }
       
    for (int x = 0; x < 4; x++) {
      byte KillChannel = 0;
       // If trigger channels are in toggle mode and a trigger arrived, or in gated mode and line is low, shut down any governed channels that are playing a pulse train
       if (((StimulusStatus[x] == 1) || (PreStimulusStatus[x] == 1))) {
          for (int y = 0; y < 2; y++) {
            if (TriggerAddress[y][x]) {
                if ((TriggerMode[y] == 1) && (LineTriggerEvent[y] == 1)) {
                     KillChannel = 1;
                }
                if ((TriggerMode[y] == 2) && (LineTriggerEvent[y] == 2)) {
                    if ((TriggerMode[1-y] == 2) && (TriggerAddress[1-y][x])) {
                      if (InputValues[1-y] == DefaultInputLevel) {
                        KillChannel = 1;
                      }
                    } else {
                      KillChannel = 1;
                    }
                }
            }
          }   
          if (KillChannel) {
             killChannel(x);
          }
        
      } else {
       // Adjust StimulusStatus to reflect any new trigger events
       if ((TriggerAddress[0][x] && (LineTriggerEvent[0] == 1)) || SoftTriggered[x]) {
         if (StimulatingState == 0) {SystemTime = 0; StimulatingState = 2;}
         PreStimulusStatus[x] = 1; BurstStatus[x] = 1; PrePulseTrainTimestamps[x] = SystemTime; PulseStatus[x] = 0; 
         SoftTriggered[x] = 0;
       }
       if (TriggerAddress[1][x] && (LineTriggerEvent[1] == 1)) {
         if (StimulatingState == 0) {SystemTime = 0; StimulatingState = 2;}
         PreStimulusStatus[x] = 1; BurstStatus[x] = 1; PrePulseTrainTimestamps[x] = SystemTime; PulseStatus[x] = 0;
       }
      }
    }
    if (StimulatingState != 2) {
     StimulatingState = 0; // null condition, will be overridden in loop if any channels are still stimulating.
    }
    // Check clock and adjust line levels for new time as per programming
    for (int x = 0; x < 4; x++) {
      byte thisTrainID = CustomTrainID[x];
      byte thisTrainIDIndex = thisTrainID-1;
      if (PreStimulusStatus[x] == 1) {
          if (StimulatingState != 2) {
           StimulatingState = 1;
          }
        if (SystemTime == (PrePulseTrainTimestamps[x] + PulseTrainDelay[x])) {
          PreStimulusStatus[x] = 0;
          StimulusStatus[x] = 1;
          PulseStatus[x] = 0;
          PulseTrainTimestamps[x] = SystemTime;
          PulseTrainEndTime[x] = SystemTime + PulseTrainDuration[x];
          if (CustomTrainTarget[x] == 1)  {
            if (CustomTrainID[x] == 1) {
              NextBurstTransitionTime[x] = SystemTime + CustomPulseTimes[0][0];
            } else {
              NextBurstTransitionTime[x] = SystemTime + CustomPulseTimes[1][0];
            }
            BurstStatus[x] = 0;
          } else {
            NextBurstTransitionTime[x] = SystemTime+BurstDuration[x];
          }
          if (CustomTrainID[x] == 0) {
            NextPulseTransitionTime[x] = SystemTime;
            dacValue.uint16[x] = Phase1Voltage[x]; DACFlag = 1; DACFlags[x] = 1;
          } else {
            NextPulseTransitionTime[x] = SystemTime + CustomPulseTimes[thisTrainIDIndex][0]; 
            CustomPulseTimeIndex[x] = 0;
          }
        }
      }
      if (StimulusStatus[x] == 1) { // if this output line has been triggered and is delivering a pulse train
          if (StimulatingState != 2) {
           StimulatingState = 1; 
          }
        if (BurstStatus[x] == 1) { // if this output line is currently gated "on"
          switch (PulseStatus[x]) { // depending on the phase of the pulse
           case 0: { // if this is the inter-pulse interval
            // determine if the next pulse should start now
            if ((CustomTrainID[x] == 0) || ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 1))) {
              if (SystemTime == NextPulseTransitionTime[x]) {
                NextPulseTransitionTime[x] = SystemTime + Phase1Duration[x];
                    if (!((UsesBursts[x] == 1) && (NextPulseTransitionTime[x] >= NextBurstTransitionTime[x]))){ // so that it doesn't start a pulse it can't finish due to burst end
                      PulseStatus[x] = 1;
                      digitalWriteDirect(OutputLEDLines[x], HIGH);
                      if ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 1)) {
                        dacValue.uint16[x] = CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]]; DACFlag = 1; DACFlags[x] = 1;
                      } else {
                        dacValue.uint16[x] = Phase1Voltage[x]; DACFlag = 1; DACFlags[x] = 1;
                      }
                    }
                 }
              } else {
               if (SystemTime == NextPulseTransitionTime[x]) {
                     int SkipNextInterval = 0;
                     if ((CustomTrainLoop[x] == 1) && (CustomPulseTimeIndex[x] == CustomTrainNpulses[thisTrainIDIndex])) {
                            CustomPulseTimeIndex[x] = 0;
                            PulseTrainTimestamps[x] = SystemTime;
                     }
                     if (CustomPulseTimeIndex[x] < CustomTrainNpulses[thisTrainIDIndex]) {
                       if ((CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]+1] - CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]]) > Phase1Duration[x]) {
                         NextPulseTransitionTime[x] = SystemTime + Phase1Duration[x];
                       } else {
                         NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]+1];  
                         SkipNextInterval = 1;
                       }
                     }
                     if (SkipNextInterval == 0) {
                        PulseStatus[x] = 1;
                     }
                     dacValue.uint16[x] = CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]]; DACFlag = 1; DACFlags[x] = 1;
                     digitalWriteDirect(OutputLEDLines[x], HIGH);
                     if (IsBiphasic[x] == 0) {
                        CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
                     }
                     if (CustomPulseTimeIndex[x] > (CustomTrainNpulses[thisTrainIDIndex])){
                       CustomPulseTimeIndex[x] = 0;
                       if (CustomTrainLoop[x] == 0) {
                         killChannel(x);
                       }
                     }
                  }
              } 
            } break;
            
            case 1: { // if this is the first phase of the pulse
             // determine if this phase should end now
             if (SystemTime == NextPulseTransitionTime[x]) {
                if (IsBiphasic[x] == 0) {
                  if (CustomTrainID[x] == 0) {
                      NextPulseTransitionTime[x] = SystemTime + InterPulseInterval[x];
                      PulseStatus[x] = 0;
                      digitalWriteDirect(OutputLEDLines[x], LOW);
                      dacValue.uint16[x] = RestingVoltage[x]; DACFlag = 1; DACFlags[x] = 1;
                  } else {
                    if (CustomTrainTarget[x] == 0) {
                      NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]];
                    } else {
                      NextPulseTransitionTime[x] = SystemTime + InterPulseInterval[x];
                    }
                    if (CustomPulseTimeIndex[x] == CustomTrainNpulses[thisTrainIDIndex]) {
                      if (CustomTrainLoop[x] == 1) {
                              CustomPulseTimeIndex[x] = 0;
                              PulseTrainTimestamps[x] = SystemTime;
                              dacValue.uint16[x] = CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]]; DACFlag = 1; DACFlags[x] = 1;
                              if ((CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]+1] - CustomPulseTimes[thisTrainIDIndex][CustomPulseTimeIndex[x]]) > Phase1Duration[x]) {
                                PulseStatus[x] = 1;
                              } else {
                                PulseStatus[x] = 0;
                              }
                              NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + Phase1Duration[x];
                              CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
                      } else {
                        killChannel(x);
                      }
                    } else {
                      PulseStatus[x] = 0;
                      digitalWriteDirect(OutputLEDLines[x], LOW);
                      dacValue.uint16[x] = RestingVoltage[x]; DACFlag = 1; DACFlags[x] = 1;
                    }
                  }
     
                } else {
                  if (InterPhaseInterval[x] == 0) {
                    NextPulseTransitionTime[x] = SystemTime + Phase2Duration[x];
                    PulseStatus[x] = 3;
                    if (CustomTrainID[x] == 0) {
                      dacValue.uint16[x] = Phase2Voltage[x]; DACFlag = 1; DACFlags[x] = 1;
                    } else {
                      
                       if (CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]] < 32768) {
                         dacValue.uint16[x] = 32768 + (32768 - CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]]); DACFlag = 1; DACFlags[x] = 1;
                       } else {
                         dacValue.uint16[x] = 32768 - (CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]] - 32768); DACFlag = 1; DACFlags[x] = 1;
                       }
                       if (CustomTrainTarget[x] == 0) {
                           CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
                       }
                    } 
                  } else {
                    NextPulseTransitionTime[x] = SystemTime + InterPhaseInterval[x];
                    PulseStatus[x] = 2;
                    dacValue.uint16[x] = RestingVoltage[x]; DACFlag = 1; DACFlags[x] = 1;
                  }
                }
              }
            } break;
            case 2: {
               if (SystemTime == NextPulseTransitionTime[x]) {
                 NextPulseTransitionTime[x] = SystemTime + Phase2Duration[x];
                 PulseStatus[x] = 3;
                 if (CustomTrainID[x] == 0) {
                   dacValue.uint16[x] = Phase2Voltage[x]; DACFlag = 1; DACFlags[x] = 1;  
                 } else {
                   if (CustomTrainID[x] == 1) {
                     if (CustomVoltages[0][CustomPulseTimeIndex[x]] < 32768) {
                       dacValue.uint16[x] = 32768 + (32768 - CustomVoltages[0][CustomPulseTimeIndex[x]]); DACFlag = 1; DACFlags[x] = 1;
                     } else {
                       dacValue.uint16[x] = 32768 - (CustomVoltages[0][CustomPulseTimeIndex[x]] - 32768); DACFlag = 1; DACFlags[x] = 1;
                     }
                   } else {
                     if (CustomVoltages[1][CustomPulseTimeIndex[x]] < 32768) {
                       dacValue.uint16[x] = 32768 + (32768 - CustomVoltages[1][CustomPulseTimeIndex[x]]); DACFlag = 1; DACFlags[x] = 1;
                     } else {
                       dacValue.uint16[x] = 32768 - (CustomVoltages[1][CustomPulseTimeIndex[x]]-32768); DACFlag = 1; DACFlags[x] = 1;
                     } 
                   }
                   if (CustomTrainTarget[x] == 0) {
                       CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
                   }
                 }
               }
            } break;
            case 3: {
              if (SystemTime == NextPulseTransitionTime[x]) {
                  if (CustomTrainID[x] == 0) {
                      NextPulseTransitionTime[x] = SystemTime + InterPulseInterval[x];
                  } else if (CustomTrainID[x] == 1) {  
                    if (CustomTrainTarget[x] == 0) {
                      NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[0][CustomPulseTimeIndex[x]];
                      if (CustomPulseTimeIndex[x] == (CustomTrainNpulses[0])){
                          killChannel(x);
                     }
                    } else {
                      NextPulseTransitionTime[x] = SystemTime + InterPulseInterval[x];
                    }  
                  } else {
                    if (CustomTrainTarget[x] == 0) {
                        NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[1][CustomPulseTimeIndex[x]];
                        if (CustomPulseTimeIndex[x] == (CustomTrainNpulses[1])){
                         killChannel(x);
                       }
                    } else {
                        NextPulseTransitionTime[x] = SystemTime + InterPulseInterval[x];
                    } 
                  }
                 if (!((CustomTrainID[x] == 0) && (InterPulseInterval[x] == 0))) { 
                   PulseStatus[x] = 0;
                   digitalWriteDirect(OutputLEDLines[x], LOW);
                   dacValue.uint16[x] = RestingVoltage[x]; DACFlag = 1; DACFlags[x] = 1;
                 } else {
                   PulseStatus[x] = 1;
                   NextPulseTransitionTime[x] = (NextPulseTransitionTime[x] - InterPulseInterval[x]) + (Phase1Duration[x]);
                   dacValue.uint16[x] = Phase1Voltage[x]; DACFlag = 1; DACFlags[x] = 1;
                 }
               }
            } break;
            
          }
        }
          // Determine if burst status should go to 0 now
       if (UsesBursts[x] == true) {
        if (SystemTime == NextBurstTransitionTime[x]) {
          if (BurstStatus[x] == 1) {
            if (CustomTrainID[x] == 0) {
                     NextPulseTransitionTime[x] = SystemTime + BurstInterval[x];
                     NextBurstTransitionTime[x] = SystemTime + BurstInterval[x];              
            } else if (CustomTrainTarget[x] == 1) {
              CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
              if (CustomTrainID[x] == 1) {
                     if (CustomPulseTimeIndex[x] == (CustomTrainNpulses[0])){
                         killChannel(x);
                     }
                     NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[0][CustomPulseTimeIndex[x]];
                     NextBurstTransitionTime[x] = NextPulseTransitionTime[x];
              } else if  (CustomTrainID[x] == 2) {
                      if (CustomPulseTimeIndex[x] == (CustomTrainNpulses[1])){ 
                          killChannel(x);
                      }
                      NextPulseTransitionTime[x] = PulseTrainTimestamps[x] + CustomPulseTimes[1][CustomPulseTimeIndex[x]];
                      NextBurstTransitionTime[x] = NextPulseTransitionTime[x];
              }
            }
              BurstStatus[x] = 0;
              dacValue.uint16[x] = RestingVoltage[x]; DACFlag = 1; DACFlags[x] = 1;
          } else {
          // Determine if burst status should go to 1 now
            NextBurstTransitionTime[x] = SystemTime + BurstDuration[x];
            NextPulseTransitionTime[x] = SystemTime + Phase1Duration[x];
            PulseStatus[x] = 1;
            if ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 1)) {              
              if (CustomTrainID[x] == 1) {
                 if (CustomPulseTimeIndex[x] < CustomTrainNpulses[0]){
                    dacValue.uint16[x] = CustomVoltages[0][CustomPulseTimeIndex[x]]; DACFlag = 1; DACFlags[x] = 1;
                 }
              } else {
                if (CustomPulseTimeIndex[x] < CustomTrainNpulses[1]){
                    dacValue.uint16[x] = CustomVoltages[1][CustomPulseTimeIndex[x]]; DACFlag = 1; DACFlags[x] = 1;
                 }
              }
            } else {
                 dacValue.uint16[x] = Phase1Voltage[x]; DACFlag = 1; DACFlags[x] = 1;
            }
            BurstStatus[x] = 1;
         }
        }
       } 
        // Determine if Stimulus Status should go to 0 now
        if ((SystemTime == PulseTrainEndTime[x]) && (StimulusStatus[x] == 1)) {
          if (((CustomTrainID[x] > 0) && (CustomTrainLoop[x] == 1)) || (CustomTrainID[x] == 0)) {
            if (ContinuousLoopMode[x] == false) {
                killChannel(x);
            }
          }
        }
     }
   }
}
// End main loop


unsigned long SerialReadLong() {
   // Generic routine for getting a 4-byte long int over the serial port
   unsigned long OutputLong = 0;
          inByte = SerialReadByte();
          inByte2 = SerialReadByte();
          inByte3 = SerialReadByte();
          inByte4 = SerialReadByte();
          OutputLong =  makeUnsignedLong(inByte4, inByte3, inByte2, inByte);
  return OutputLong;
}

uint16_t SerialReadShort() {
   // Generic routine for getting a 2-byte unsigned int over the serial port
   unsigned long MyOutput = 0;
          inByte = SerialReadByte();
          inByte2 = SerialReadByte();
          MyOutput =  makeUnsignedShort(inByte2, inByte);
  return MyOutput;
}

byte* Long2Bytes(long LongInt2Break) {
  byte Output[4] = {0};
  return Output;
}


void killChannel(byte outputChannel) {
  CustomPulseTimeIndex[outputChannel] = 0;
  PreStimulusStatus[outputChannel] = 0;
  StimulusStatus[outputChannel] = 0;
  PulseStatus[outputChannel] = 0;
  BurstStatus[outputChannel] = 0;
  dacValue.uint16[outputChannel] = RestingVoltage[outputChannel]; DACFlag = 1; DACFlags[outputChannel] = 1;
  digitalWriteDirect(OutputLEDLines[outputChannel], LOW);
}

void dacWrite() {
  digitalWriteDirect(LDACPin,HIGH);
  for (int i = 0; i<4; i++) {
    if (DACFlags[i]) {
      digitalWriteDirect(SyncPin,LOW);
      dacBuffer[0] = i;
      dacBuffer[1] = dacValue.byteArray[1+(i*2)];
      dacBuffer[2] = dacValue.byteArray[0+(i*2)];
      SPI.transfer(dacBuffer,3);
      digitalWriteDirect(SyncPin,HIGH);
      DACFlags[i] = 0;
    }
  }
  digitalWriteDirect(LDACPin,LOW);
}

void ProgramDAC(byte Data1, byte Data2, byte Data3) {
  digitalWriteDirect(LDACPin,HIGH);
  digitalWriteDirect(SyncPin,LOW);
  SPI.transfer (Data1);
  SPI.transfer (Data2);
  SPI.transfer (Data3);
  digitalWriteDirect(SyncPin,HIGH);
  digitalWriteDirect(LDACPin,LOW);
}

void digitalWriteDirect(int pin, boolean val){
  if(val) g_APinDescription[pin].pPort -> PIO_SODR = g_APinDescription[pin].ulPin;
  else    g_APinDescription[pin].pPort -> PIO_CODR = g_APinDescription[pin].ulPin;
}

byte digitalReadDirect(int pin){
  return !!(g_APinDescription[pin].pPort -> PIO_PDSR & g_APinDescription[pin].ulPin);
}

void UpdateSettingsMenu() {
    ClickerX = analogRead(ClickerXLine);
    ClickerY = analogRead(ClickerYLine);
    ClickerButtonState = ReadDebouncedButton();
    if (ClickerButtonState == 1 && LastClickerButtonState == 0) {
      if (SSactive) {
        SSactive = 0;
        SScount = 0;
        write2Screen(CommanderString," Click for menu");
        delayMicroseconds(100000);
       } else {
        LastClickerButtonState = 1;
        switch(inMenu) {
          case 0: { // Menu top
            inMenu = 1;
            SelectedChannel = 1;
            write2Screen("Output Channels","<  Channel 1  >");
            NeedUpdate = 1;
          } break;
          case 1: { // Channel / Save-Load / Reset Menu
            switch(SelectedChannel) {
              case 5:{
                inMenu = 4; // trigger menu
                SelectedInputAction = 1;
                SelectedChannel = 1;
                write2Screen("< Trigger Now  >"," ");
              } break;  
              case 6: {
                inMenu = 4; // trigger menu
                SelectedInputAction = 1;
                SelectedChannel = 2;
                write2Screen("< Trigger Now  >"," ");
              } break;
              case 7: { // Save settings
                inMenu = 6; // file save menu
                myFilePos = 1;
                write2Screen("<   New File   >", "");
              } break;
              case 8: { // Load settings
                inMenu = 5; // file load menu
                settingsFile.close();
                sd.vwd()->rewind();
                myFilePos = 1;
                if (candidateSettingsFile.openNext(sd.vwd(), O_READ)) {
                  for (int i = 0; i < 16; i++) {candidateSettingsFileChar[i] = 0;}
                  candidateSettingsFile.getName(candidateSettingsFileChar, 16);
                  // Center settings file name
                  centerText(candidateSettingsFileChar);
                  for (int i = 0; i < 16; i++) {
                    candidateSettingsFileChar[i] = centeredText[i];
                  }
                  write2Screen("<Click to load >", candidateSettingsFileChar);
                  candidateSettingsFile.close();
                } else {
                  write2Screen("!Error reading", "SD Card!");
                }
                settingsFile.open(currentSettingsFileNameChar, O_READ);
              } break;
              case 9: { // Delete settings
                inMenu = 7; 
                settingsFile.close();
                sd.vwd()->rewind();
                myFilePos = 1;
                if (candidateSettingsFile.openNext(sd.vwd(), O_READ)) {
                  for (int i = 0; i < 16; i++) {candidateSettingsFileChar[i] = 0;}
                  candidateSettingsFile.getName(candidateSettingsFileChar, 16);
                  // Center settings file name
                  centerText(candidateSettingsFileChar);
                  for (int i = 0; i < 16; i++) {
                    candidateSettingsFileChar[i] = centeredText[i];
                  }
                  write2Screen("<Click to erase>", candidateSettingsFileChar);
                  candidateSettingsFile.close();
                } else {
                  write2Screen("!Error reading", "SD Card!");
                }
              } break;
              case 10: { // Reset
              write2Screen(" "," ");
              delayMicroseconds(1000000);
                Software_Reset();
              } break;
              case 11: {
                inMenu = 0;
                write2Screen(CommanderString," Click for menu");
              } break;
              
              default: {
                inMenu = 2; // output menu
                SelectedAction = 1;
                write2Screen("< Trigger Now  >"," ");
              } break;
           }
         } break;
         case 2: { // Channel menu
          switch (SelectedAction) {
            case 1: {
              inMenu = 3; // soft-trigger menu
              write2Screen("< Single Train >"," ");
              SelectedStimMode = 1;
            } break;
            case 2: {IsBiphasic[SelectedChannel-1] = ReturnUserValue(0, 1, 1, 3);} break; // biphasic (on /off)
            case 3: {Phase1Voltage[SelectedChannel-1] = ReturnUserValue(0, 65535, 1, 2);} break; // Get user to input phase 1 voltage
            case 4: {Phase1Duration[SelectedChannel-1] = ReturnUserValue(1, 72000000, 1, 1);} break; // phase 1 duration
            case 5: {InterPhaseInterval[SelectedChannel-1] = ReturnUserValue(1, 72000000, 1, 1);} break; // inter-phase interval
            case 6: {Phase2Voltage[SelectedChannel-1] = ReturnUserValue(0, 65535, 1, 2);} break; // Get user to input phase 2 voltage
            case 7: {Phase2Duration[SelectedChannel-1] = ReturnUserValue(1, 72000000, 1, 1);} break; // phase 2 duration
            case 8: {InterPulseInterval[SelectedChannel-1] = ReturnUserValue(1, 72000000, 1, 1);} break; // pulse interval
            case 9: {BurstDuration[SelectedChannel-1] = ReturnUserValue(1, 72000000, 1, 1);} break; // burst width
            case 10: {BurstInterval[SelectedChannel-1] = ReturnUserValue(1, 72000000, 1, 1);} break; // burst interval
            case 11: {PulseTrainDelay[SelectedChannel-1] = ReturnUserValue(1, 72000000, 1, 1);} break; // stimulus train delay
            case 12: {PulseTrainDuration[SelectedChannel-1] = ReturnUserValue(1, 72000000, 1, 1);} break; // stimulus train duration
            case 13: {byte Bit2Write = ReturnUserValue(0, 1, 1, 3);
                      byte Ch = SelectedChannel-1;
                      TriggerAddress[0][Ch] = Bit2Write;
                      } break; // Follow input 1 (on/off)
            case 14: {byte Bit2Write = ReturnUserValue(0, 1, 1, 3);
                      byte Ch = SelectedChannel-1;
                      TriggerAddress[1][Ch] = Bit2Write;
                      } break; // Follow input 2 (on/off)
            case 15: {CustomTrainID[SelectedChannel-1] = ReturnUserValue(0, 2, 1, 0);} break; // stimulus train duration
            case 16: {CustomTrainTarget[SelectedChannel-1] = ReturnUserValue(0,1,1,4);} break; // Custom stim target (Pulses / Bursts)
            case 17: {
                      RestingVoltage[SelectedChannel-1] = ReturnUserValue(0, 255, 1, 2); // Get user to input resting voltage
                      dacValue.uint16[SelectedChannel-1] = RestingVoltage[SelectedChannel-1]; 
                      DACFlags[SelectedChannel-1] = 1; 
                      dacWrite(); // Update DAC
                      } break; 
            case 18: {
              // Exit to channel menu
            inMenu = 1; RefreshChannelMenu(SelectedChannel);
            } break;
           }
           PulseDuration[SelectedChannel-1] = ComputePulseDuration(IsBiphasic[SelectedChannel-1], Phase1Duration[SelectedChannel-1], InterPhaseInterval[SelectedChannel-1], Phase2Duration[SelectedChannel-1]);
           if (BurstDuration[SelectedChannel-1] == 0) {UsesBursts[SelectedChannel-1] = false;} else {UsesBursts[SelectedChannel-1] = true;}
           if ((SelectedAction > 1) && (SelectedAction < 18)) {
            //SaveCurrentProgram2SD();             
           }
          } break;
          case 3: { // Trigger menu
          switch (SelectedStimMode) {
            case 1: {
              // Soft-trigger channel
              write2Screen("< Single Train >","      ZAP!");
              delayMicroseconds(100000);
              while (ClickerButtonState == 1) {
               ClickerButtonState = ReadDebouncedButton();
              }
              write2Screen("< Single Train >"," ");
              PreStimulusStatus[SelectedChannel-1] = 1;
              BurstStatus[SelectedChannel-1] = 1;
              if (StimulatingState == 0) {ResetSystemTime(); StimulatingState = 2;}
              MicrosTime = micros();
              PrePulseTrainTimestamps[SelectedChannel-1] = SystemTime;  
            } break;
            case 2: { // Single example pulse. Timing for the example pulse is done with micros() instead of the HW timer.
              write2Screen("< Single Pulse >","      ZAP!");
              delayMicroseconds(100000);
              write2Screen("< Single Pulse >"," ");
              SystemTime = 0;
              if (IsBiphasic[SelectedChannel-1] == 0) {
                dacValue.uint16[SelectedChannel-1] = Phase1Voltage[SelectedChannel-1];
                DACFlags[SelectedChannel-1] = 1;
                NextPulseTransitionTime[SelectedChannel-1] = SystemTime + Phase1Duration[SelectedChannel-1];
                MicrosTime = micros(); LastLoopTime = MicrosTime;
                dacWrite();
                while (NextPulseTransitionTime[SelectedChannel-1] > SystemTime) {
                  while ((MicrosTime-LastLoopTime) < CycleDuration) {  // Make sure loop runs once every 100us 
                    MicrosTime = micros();
                  }
                 LastLoopTime = MicrosTime;
                 SystemTime++; 
                }
                dacValue.uint16[SelectedChannel-1] = RestingVoltage[SelectedChannel-1];
                DACFlags[SelectedChannel-1] = 1;
                dacWrite();
              } else {
                dacValue.uint16[SelectedChannel-1] = Phase1Voltage[SelectedChannel-1];
                DACFlags[SelectedChannel-1] = 1;
                NextPulseTransitionTime[SelectedChannel-1] = SystemTime + Phase1Duration[SelectedChannel-1];
                MicrosTime = micros(); LastLoopTime = MicrosTime;
                dacWrite();
                while (NextPulseTransitionTime[SelectedChannel-1] > SystemTime) {
                  while ((MicrosTime-LastLoopTime) < CycleDuration) {  // Make sure loop runs once every 100us 
                    MicrosTime = micros();
                  }
                 LastLoopTime = MicrosTime;
                 SystemTime++; 
                }
                if (InterPhaseInterval[SelectedChannel-1] > 0) {
                dacValue.uint16[SelectedChannel-1] = RestingVoltage[SelectedChannel-1];
                DACFlags[SelectedChannel-1] = 1;
                NextPulseTransitionTime[SelectedChannel-1] = SystemTime + InterPhaseInterval[SelectedChannel-1];
                dacWrite();
                while (NextPulseTransitionTime[SelectedChannel-1] > SystemTime) {
                  while ((MicrosTime-LastLoopTime) < CycleDuration) {  // Make sure loop runs once every 100us 
                    MicrosTime = micros();
                  }
                 LastLoopTime = MicrosTime;
                 SystemTime++; 
                }
                }
                dacValue.uint16[SelectedChannel-1] = Phase2Voltage[SelectedChannel-1];
                DACFlags[SelectedChannel-1] = 1;
                NextPulseTransitionTime[SelectedChannel-1] = SystemTime + Phase2Duration[SelectedChannel-1];
                dacWrite();
                while (NextPulseTransitionTime[SelectedChannel-1] > SystemTime) {
                  while ((MicrosTime-LastLoopTime) < CycleDuration) {  // Make sure loop runs once every 100us 
                    MicrosTime = micros();
                  }
                 LastLoopTime = MicrosTime;
                 SystemTime++; 
                }
                dacValue.uint16[SelectedChannel-1] = RestingVoltage[SelectedChannel-1];
                DACFlags[SelectedChannel-1] = 1;
                dacWrite();
              }
            } break;
            case 3: {
              if (ContinuousLoopMode[SelectedChannel-1] == false) {
                 write2Screen("<  Continuous  >","      On");
                 ContinuousLoopMode[SelectedChannel-1] = true;
                 SoftTriggerScheduled[SelectedChannel-1] = 1;
                 delayMicroseconds(200000); // Debounce
             } else {
                 write2Screen("<  Continuous  >","      Off");
                 ContinuousLoopMode[SelectedChannel-1] = false;
                 PulseStatus[SelectedChannel-1] = 0;
                 BurstStatus[SelectedChannel-1] = 0;
                 StimulusStatus[SelectedChannel-1] = 0;
                 CustomPulseTimeIndex[SelectedChannel-1] = 0;
                 dacValue.uint16[SelectedChannel-1] = RestingVoltage[SelectedChannel-1];
                 dacWrite();
                 digitalWrite(OutputLEDLines[SelectedChannel-1], LOW);
               }
            } break;
            case 4: {
              inMenu = 2;
              SelectedAction = 1;
              write2Screen("< Trigger Now  >"," ");
            } break;
           }
         } break; 
         case 4: { // Trigger channel menu
          switch (SelectedInputAction) {
            case 1: {
              // Trigger linked output channels
              write2Screen("< Trigger Now >","      ZAP!");
              delayMicroseconds(100000);
              while (ClickerButtonState == 1) {
               ClickerButtonState = ReadDebouncedButton();
              }
              write2Screen("< Trigger Now >"," ");
              for (int x = 0; x < 4; x++) {
                if (TriggerAddress[SelectedChannel-1][x] == 1) {
                  PreStimulusStatus[x] = 1;
                  BurstStatus[x] = 1;
                  if (StimulatingState == 0) {StimulatingState = 2; ResetSystemTime();}
                  MicrosTime = micros();
                  PrePulseTrainTimestamps[x] = SystemTime;
                }
              }
            } break;
            case 2: {
              // Change mode of selected channel
              TriggerMode[SelectedChannel-1] = ReturnUserValue(0, 2, 1, 5); // Get user to input trigger mode
              //Store changes
              //SaveCurrentProgram2SD();
            } break;
            case 3: {
              inMenu = 1;
              SelectedAction = 1;
              write2Screen("Output Channels","<  Channel 1  >");
              NeedUpdate = 1;
              SelectedChannel = SelectedChannel + 4;
            } break;
          }
        } break;
        case 5: { // Handle click in file load/save menu
          if (myFilePos < 1) {
            inMenu = 1;
            SelectedChannel = 8;
            write2Screen(" LOAD SETTINGS  ","<Click to load >");
            NeedUpdate = 1;
            myFilePos = 1;
          } else {
            // Load settings file
            //currentSettingsFileName.toCharArray(currentSettingsFileNameChar, sizeof(currentSettingsFileName));
            
            settingsFile.close();
            settingsFile.open(candidateSettingsFileChar, O_READ);
            validProgram = RestoreParametersFromSD();
            if (validProgram != 252) {
              write2Screen("!ERROR! INVALID ","SETTINGS FILE.");
              delayMicroseconds(1000000);
              LoadDefaultParameters();
            } else {
              for (int i = 0; i < 16; i++) {
                currentSettingsFileNameChar[i] = candidateSettingsFileChar[i];
              }
              write2Screen("Settings loaded."," ");
              delayMicroseconds(1000000);
              inMenu = 1;
              SelectedChannel = 8;
              write2Screen(" LOAD SETTINGS  ","<Click to load >");
              NeedUpdate = 1;
            }
          }
        } break;
        case 6: { // handle click in save menu
          if (myFilePos < 1) {
            inMenu = 1;
            SelectedChannel = 7;
            write2Screen(" SAVE SETTINGS  ","<Click to save >");
            NeedUpdate = 1;
            myFilePos = 1;
          } else {
            // save selected or enter file name creation mode
            if (myFilePos == 1) {
              // Create file name
              CursorPos = 0;
              candidateSettingsFileChar[CursorPos] = 'A';
              candidateSettingsFileChar[CursorPos+1] = '.';
              candidateSettingsFileChar[CursorPos+2] = 'p';
              candidateSettingsFileChar[CursorPos+3] = 'p';
              candidateSettingsFileChar[CursorPos+4] = 's';
              for (int i = 5; i<16; i++) {
                candidateSettingsFileChar[i] = 32;
              }
              
              lcd.setCursor(0, 1); lcd.print("                ");
              delayMicroseconds(100000);
              write2Screen("<Click to save >", candidateSettingsFileChar);
              ChoiceMade = 0;
              CursorOn = 0;
              CursorToggleTimer = 0;
              while (ChoiceMade == 0) {
                 CursorToggleTimer++;
                 if (CursorToggleTimer == 20000) {
                   switch (CursorOn) {
                     case 0: {lcd.setCursor(CursorPos, 1); lcd.cursor(); CursorOn = 1;} break;
                     case 1: {lcd.noCursor(); CursorOn = 0;} break;
                   }
                   CursorToggleTimer = 0;
                 }
                 ClickerX = analogRead(ClickerXLine);
                 ClickerY = analogRead(ClickerYLine);
                 ClickerButtonState = digitalRead(ClickerButtonLine);
                 if (ClickerButtonState == 0) {
                   ChoiceMade = 1;
                   lcd.noCursor();
                   lcd.setCursor(0, 1); lcd.print("                ");
                 }
                 if (ClickerY > ClickerMaxThreshold) {
                     if (candidateSettingsFileChar[CursorPos]  == 65) { // Skip from ASCii A to 9
                      candidateSettingsFileChar[CursorPos] = 57;
                     } else if (candidateSettingsFileChar[CursorPos]  == 48){ // Wrap from ASCii 0 to underscore
                      candidateSettingsFileChar[CursorPos] = 95;
                     } else if (candidateSettingsFileChar[CursorPos]  == 95){ // Skip from ASCii underscore to Z
                      candidateSettingsFileChar[CursorPos] = 90;
                     } else {
                      candidateSettingsFileChar[CursorPos] = candidateSettingsFileChar[CursorPos] - 1;
                     }
                     lcd.noCursor();
                     write2Screen("<Click to save >", candidateSettingsFileChar);
                     delayMicroseconds(200000); 
                 } else if (ClickerY < ClickerMinThreshold) {
                    if (candidateSettingsFileChar[CursorPos]  == 57) { // Skip from ASCii 9 to A
                      candidateSettingsFileChar[CursorPos] = 65;
                    } else if (candidateSettingsFileChar[CursorPos]  == 90){ // Wrap from ASCii Z to underscore
                      candidateSettingsFileChar[CursorPos] = 95;
                    } else if (candidateSettingsFileChar[CursorPos]  == 95){ // Skip from ASCii underscore to 0
                      candidateSettingsFileChar[CursorPos] = 48;
                    } else {
                    candidateSettingsFileChar[CursorPos] = candidateSettingsFileChar[CursorPos] + 1;
                    }
                    lcd.noCursor();
                    write2Screen("<Click to save >", candidateSettingsFileChar);
                    delayMicroseconds(200000); 
                 } else if (ClickerX < ClickerMinThreshold) {
                    if (CursorPos > 0) {
                      for (int i = CursorPos; i < 16; i++) {
                         candidateSettingsFileChar[i] = candidateSettingsFileChar[i+1];
                      }
                      CursorPos--;
                      write2Screen("<Click to save >", candidateSettingsFileChar);
                      lcd.setCursor(CursorPos, 1); 
                      delayMicroseconds(300000); 
                    }
                 } else if (ClickerX > ClickerMaxThreshold) {
                    if (CursorPos < 11) {
                      for (int i = 16; i > CursorPos; i--) {
                        candidateSettingsFileChar[i] = candidateSettingsFileChar[i-1];
                      }
                      CursorPos++;
                      candidateSettingsFileChar[CursorPos] = 'A';
                      write2Screen("<Click to save >", candidateSettingsFileChar);
                      lcd.setCursor(CursorPos, 1); 
                      delayMicroseconds(300000); 
                    }
                 }
              }
              currentSettingsFileName = "";
              for (int i = 0; i < CursorPos+5; i++) {
                currentSettingsFileName = currentSettingsFileName + candidateSettingsFileChar[i];
              }
              settingsFile.close();
              currentSettingsFileName.toCharArray(currentSettingsFileNameChar, sizeof(currentSettingsFileName));
            } else {
              for (int i = 0; i < 16; i++) {
                currentSettingsFileNameChar[i] = candidateSettingsFileChar[i];
              }
            }
            SaveCurrentProgram2SD();
            write2Screen("Settings saved."," ");
            delayMicroseconds(1000000);
            inMenu = 1;
            SelectedChannel = 7;
            write2Screen(" SAVE SETTINGS  ","<Click to save >");
            NeedUpdate = 1;
            myFilePos = 2;
          }
        } break;
        case 7: { // Handle click in delete menu
          if (myFilePos < 2) {
            inMenu = 1;
            SelectedChannel = 9;
            write2Screen(" ERASE SETTINGS ","<Click to erase>");
            NeedUpdate = 1;
            myFilePos = 1;
          } else {
            sd.remove(candidateSettingsFileChar);
            write2Screen("Settings erased."," ");
            delayMicroseconds(1000000);
            inMenu = 1;
            SelectedChannel = 9;
            write2Screen(" ERASE SETTINGS ","<Click to erase>");
            NeedUpdate = 1;
          }
        } break;
      }
     }
    }
    if (ClickerButtonState == 0 && LastClickerButtonState == 1) {
      LastClickerButtonState = 0;
    }
    if (LastClickerXState != 1 && ClickerX < 200) {
      LastClickerXState = 1;
      NeedUpdate = 1;
      if (inMenu == 1) {SelectedChannel = SelectedChannel - 1;}
      if (inMenu == 2) {
        if ((IsBiphasic[SelectedChannel-1] == 0) && (SelectedAction == 8)) {
          SelectedAction = SelectedAction - 4;
        } else {  
          SelectedAction = SelectedAction - 1;
        }
      }
      if (inMenu == 3) {SelectedStimMode = SelectedStimMode - 1;}
      if (inMenu == 4) {SelectedInputAction = SelectedInputAction - 1;}
      if ((inMenu > 4) && (inMenu < 8)) {
        if (myFilePos > 0) {myFilePos = myFilePos - 1;}
      }
      if (SelectedInputAction == 0) {SelectedInputAction = 3;}
      if (SelectedChannel == 0) {SelectedChannel = 11;}
      if (SelectedAction == 0) {SelectedAction = 18;}
      if (SelectedStimMode == 0) {SelectedStimMode = 4;}
    }
    if (LastClickerXState != 2 && ClickerX > ClickerMaxThreshold) {
      LastClickerXState = 2;
      NeedUpdate = 1;
      if (inMenu == 1) {SelectedChannel++;}
      if (inMenu == 2) {
        if ((IsBiphasic[SelectedChannel-1] == 0) && (SelectedAction == 4)) {
          SelectedAction = SelectedAction + 4;
        } else {
          SelectedAction++;
        }
      }
      if (inMenu == 3) {SelectedStimMode++;}
      if (inMenu == 4) {SelectedInputAction++;}
      if (inMenu > 4 && inMenu < 8) {
        myFilePos++;
      }
      if (SelectedInputAction == 4) {SelectedInputAction = 1;}
      if (SelectedChannel == 12) {SelectedChannel = 1;}
      if (SelectedAction == 19) {SelectedAction = 1;}
      if (SelectedStimMode == 5) {SelectedStimMode = 1;}
    }
    if (LastClickerXState != 0 && ClickerX < ClickerMaxThreshold && ClickerX > ClickerMinThreshold) {
      LastClickerXState = 0;
    }
    if (NeedUpdate == 1) {
      switch (inMenu) {
        case 1: {
          RefreshChannelMenu(SelectedChannel);
        } break;
        case 2: {
          RefreshActionMenu(SelectedAction);
        } break; 
        case 3: {
          switch (SelectedStimMode) {
            case 1: {write2Screen("< Single Train >", " ");} break;
            case 2: {write2Screen("< Single Pulse >", " ");} break;
            case 3: {
            if (ContinuousLoopMode[SelectedChannel-1] == false) {
                 write2Screen("<  Continuous  >","      Off");
               } else {
                 write2Screen("<  Continuous  >","      On");
               }
            } break;
            case 4: {write2Screen("<     Exit     >"," ");} break;
          }
        } break;
        case 4: {
          RefreshTriggerMenu(SelectedInputAction); 
        } break;
        case 5: { // Load settings file menu
          if (myFilePos == 0) {
            write2Screen("<    Cancel    >", " ");
          } else {
            candidateSettingsFile.close();
            if (!skipToFile(myFilePos)) {
              if (myFilePos > 1) {
                skipToFile(myFilePos-1);
                myFilePos = myFilePos - 1;
              }
            }
            for (int i = 0; i < 16; i++) {candidateSettingsFileChar[i] = 0;}
            candidateSettingsFile.getName(candidateSettingsFileChar, 16);
            // Center settings file name
            centerText(candidateSettingsFileChar);
            for (int i = 0; i < 16; i++) {
              candidateSettingsFileChar[i] = centeredText[i];
            }
            write2Screen("<Click to load >", candidateSettingsFileChar);
            candidateSettingsFile.close();
          }
        } break;
        case 6: { // Save settings menu
          if (myFilePos == 0) {
            write2Screen("<    Cancel    >", " ");
          } else if (myFilePos == 1) {
            write2Screen("<   New File   >", "");
          } else {
            
            candidateSettingsFile.close();
            if (!skipToFile(myFilePos)) {
              if (myFilePos > 1) {
                skipToFile(myFilePos-1);
                myFilePos = myFilePos - 1;
              }
            }
            for (int i = 0; i < 16; i++) {candidateSettingsFileChar[i] = 0;}
            candidateSettingsFile.getName(candidateSettingsFileChar, 16);
            // Center settings file name
            centerText(candidateSettingsFileChar);
            for (int i = 0; i < 16; i++) {
              candidateSettingsFileChar[i] = centeredText[i];
            }
            write2Screen("<Click to save >", candidateSettingsFileChar);
            candidateSettingsFile.close();
          }
        } break;
        case 7: {
          if (myFilePos == 0) {
            write2Screen("<    Cancel    >", " ");
          } else {
            candidateSettingsFile.close();
            if (!skipToFile(myFilePos)) {
              if (myFilePos > 1) {
                skipToFile(myFilePos-1);
                myFilePos = myFilePos - 1;
              }
            }
            for (int i = 0; i < 16; i++) {candidateSettingsFileChar[i] = 0;}
            candidateSettingsFile.getName(candidateSettingsFileChar, 16);
            // Center settings file name
            centerText(candidateSettingsFileChar);
            for (int i = 0; i < 16; i++) {
              candidateSettingsFileChar[i] = centeredText[i];
            }
            write2Screen("<Click to erase>", candidateSettingsFileChar);
            candidateSettingsFile.close();
          }
        } break;
    }
    NeedUpdate = 0;
  }

}

void centerText(char myText[]) {
  byte spaceCounter = 0;
  for (int i = 0; i < 16; i++) {
    if (myText[i] == 0) {spaceCounter++;}
    tempText[i] = 32;
  }
  fileNameOffset = spaceCounter/2;
    for (int i = fileNameOffset; i < 16; i++) {
      tempText[i] = myText[i-fileNameOffset];
    }
    for (int i = 0; i < 16; i++) {
      centeredText[i] = tempText[i];
    }
}

byte skipToFile(unsigned int fileNumber) {
  byte ok = 0;
  sd.vwd()->rewind();
  for (int i = 0; i < fileNumber; i++) {
    candidateSettingsFile.close();
    ok = candidateSettingsFile.openNext(sd.vwd(), O_READ);
  }
  return ok;
}

void RefreshChannelMenu(int ThisChannel) {
  switch (SelectedChannel) {
        case 1: {write2Screen("Output Channels","<  Channel 1  >");} break;
        case 2: {write2Screen("Output Channels","<  Channel 2  >");} break;
        case 3: {write2Screen("Output Channels","<  Channel 3  >");} break;
        case 4: {write2Screen("Output Channels","<  Channel 4  >");} break;
        case 5: {write2Screen("Trigger Channels","<  Channel 1  >");} break;
        case 6: {write2Screen("Trigger Channels","<  Channel 2  >");} break;
        case 7: {write2Screen(" SAVE SETTINGS  ","< Select File >");} break;
        case 8: {write2Screen(" LOAD SETTINGS  ","< Select File >");} break;
        case 9: {write2Screen(" ERASE SETTINGS ","< Select File >");} break;
        case 10: {write2Screen("    -RESET-       ","<Click to reset>");} break;
        case 11: {write2Screen("<Click to exit>"," ");} break;
  }
}
void RefreshActionMenu(int ThisAction) {
    switch (SelectedAction) {
          case 1: {write2Screen("< Trigger Now  >"," ");} break;
          case 2: {write2Screen("<Biphasic Pulse>",FormatNumberForDisplay(IsBiphasic[SelectedChannel-1], 3));} break;
          case 3: {write2Screen("<Phase1 Voltage>",FormatNumberForDisplay(Phase1Voltage[SelectedChannel-1], 2));} break;
          case 4: {write2Screen("<Phase1Duration>",FormatNumberForDisplay(Phase1Duration[SelectedChannel-1], 1));} break;
          case 5: {write2Screen("<InterPhaseTime>",FormatNumberForDisplay(InterPhaseInterval[SelectedChannel-1], 1));} break;
          case 6: {write2Screen("<Phase2 Voltage>",FormatNumberForDisplay(Phase2Voltage[SelectedChannel-1], 2));} break;
          case 7: {write2Screen("<Phase2Duration>",FormatNumberForDisplay(Phase2Duration[SelectedChannel-1], 1));} break;
          case 8: {write2Screen("<Pulse Interval>",FormatNumberForDisplay(InterPulseInterval[SelectedChannel-1], 1));} break;
          case 9: {write2Screen("<Burst Duration>",FormatNumberForDisplay(BurstDuration[SelectedChannel-1], 1));} break;
          case 10: {write2Screen("<Burst Interval>",FormatNumberForDisplay(BurstInterval[SelectedChannel-1], 1));} break;
          case 11: {write2Screen("< Train Delay  >",FormatNumberForDisplay(PulseTrainDelay[SelectedChannel-1], 1));} break;
          case 12: {write2Screen("<Train Duration>",FormatNumberForDisplay(PulseTrainDuration[SelectedChannel-1], 1));} break;
          case 13: {write2Screen("<Link Trigger 1>",FormatNumberForDisplay(TriggerAddress[0][SelectedChannel-1], 3));} break;
          case 14: {write2Screen("<Link Trigger 2>",FormatNumberForDisplay(TriggerAddress[1][SelectedChannel-1], 3));} break; 
          case 15: {write2Screen("<Custom Train# >",FormatNumberForDisplay(CustomTrainID[SelectedChannel-1], 0));} break;
          case 16: {write2Screen("<Custom Target >",FormatNumberForDisplay(CustomTrainTarget[SelectedChannel-1], 4));} break;
          case 17: {write2Screen("<RestingVoltage>",FormatNumberForDisplay(RestingVoltage[SelectedChannel-1], 2));} break;
          case 18: {write2Screen("<     Exit     >"," ");} break;
     }
     isNegativeZero = 0;
}
void RefreshTriggerMenu(int ThisAction) {
    switch (SelectedInputAction) {
          case 1: {write2Screen("< Trigger Now  >"," ");} break;
          case 2: {write2Screen("< Trigger Mode >",FormatNumberForDisplay(TriggerMode[SelectedChannel-1], 5));} break;
          case 3: {write2Screen("<     Exit     >"," ");} break;
     }
}
const char* FormatNumberForDisplay(unsigned int InputNumber, int Units) {
  // Units are: 0 - none, 1 - s/ms, 2 - V
  // Clear var
  for (int x = 0; x < 17; x++) {
    Value2Display[x] = ' ';
  }
  // Figure out how many digits
unsigned int Bits2Display = InputNumber;
double InputNum = double(InputNumber);
  if (Units == 1) {
  InputNum = InputNum/CycleFrequency;
  }
if (Units == 2) {
  // Convert volts from bytes to volts
  InputNum = (((InputNum/65536)*10)*2 - 10);
}
  switch (Units) {
    case 0: {sprintf (Value2Display, "       %.0f", InputNum);} break;
    case 1: {
      if (inMenu == 3) {
        sprintf (Value2Display, "  %010.5f s ", InputNum);
      } else {
        if (InputNum < 100) {
          sprintf (Value2Display, "    %.5f s ", InputNum);
        } else {
          sprintf (Value2Display, "   %.5f s ", InputNum);
        }
      }
    } break;
    case 2: {
        if (InputNum == 0) {
          if (isNegativeZero) {
            InputNum = InputNum-0.000001;
          }
        }
        if (InputNum >= 0) {
          sprintf (Value2Display, "     %04.2f V ", InputNum);
        } else {
          sprintf (Value2Display, "    %05.2f V ", InputNum);
        }
    } break;
    case 3:{
      if (InputNum == 0) {
        sprintf(Value2Display, "      Off");
      } else if (InputNum == 1) {
        sprintf(Value2Display, "       On");
      } else {
        sprintf(Value2Display, "Error");
      }
    } break;
    case 4: {
      if (InputNum == 0) {
        sprintf(Value2Display, "     Pulses");
      } else if (InputNum == 1) {
        sprintf(Value2Display, "     Bursts");
      } else {
        sprintf(Value2Display, "     Error");
      }
    } break;
    case 5: {
      if (InputNum == 0) {
        sprintf(Value2Display, "     Normal   ");
      } else if (InputNum == 1) {
        sprintf(Value2Display, "     Toggle   ");
      } else if (InputNum == 2) {
        sprintf(Value2Display, "  Pulse Gated  ");
      } else {
        sprintf(Value2Display, "     Error   ");
      }
    } break;
  }
  return Value2Display;
}
boolean ReadDebouncedButton() {
  DebounceTime = millis();
  ClickerButtonState = digitalRead(ClickerButtonLine);
    if (ClickerButtonState != lastButtonState) {lastDebounceTime = DebounceTime;}
    lastButtonState = ClickerButtonState;
   if (((DebounceTime - lastDebounceTime) > 75) && (ClickerButtonState == 0)) {
      return 1;
   } else {
     return 0;
   }
}

unsigned int ReturnUserValue(unsigned long LowerLimit, unsigned long UpperLimit, unsigned long StepSize, byte Units) {
      // This function returns a value that the user chooses by scrolling up and down a number list with the joystick, and clicks to select the desired number.
      // LowerLimit and UpperLimit are the limits for this selection, StepSize is the smallest step size the system will scroll. Units (as for Write2Screen) codes none=0, time=1, volts=2 True/False=3
     unsigned long ValueToAdd = 0;
     CursorPos = 0;
     for (int i = 0; i < 9; i++) {
       Digits[i] = 0;
       ValidCursorPositions[i] = 0;
     }
      float CandidateVoltage = 0; // used to see if voltage will go over limits for DAC
      float FractionalVoltage = 0;
      
     switch (SelectedAction) {
       case 2:{UserValue = IsBiphasic[SelectedChannel-1];} break;
       case 3:{UserValue = Phase1Voltage[SelectedChannel-1];} break;
       case 4:{UserValue = Phase1Duration[SelectedChannel-1];} break;
       case 5:{UserValue = InterPhaseInterval[SelectedChannel-1];} break;
       case 6:{UserValue = Phase2Voltage[SelectedChannel-1];} break;
       case 7:{UserValue = Phase2Duration[SelectedChannel-1];} break;
       case 8:{UserValue = InterPulseInterval[SelectedChannel-1];} break;
       case 9:{UserValue = BurstDuration[SelectedChannel-1];} break;
       case 10:{UserValue = BurstInterval[SelectedChannel-1];} break;
       case 11:{UserValue = PulseTrainDelay[SelectedChannel-1];} break;
       case 12:{UserValue = PulseTrainDuration[SelectedChannel-1];} break;
       case 13:{UserValue = TriggerAddress[0][SelectedChannel-1];} break;
       case 14:{UserValue = TriggerAddress[1][SelectedChannel-1];} break;
       case 15:{UserValue = CustomTrainID[SelectedChannel-1];} break;
       case 16:{UserValue = CustomTrainTarget[SelectedChannel-1];} break;
       case 17:{UserValue = RestingVoltage[SelectedChannel-1];} break;        
     }
     if (Units == 5) {
       UserValue = TriggerMode[SelectedChannel-1];
     }
     long UVTemp = UserValue;
     inMenu = 3; // Temporarily goes a menu layer deeper so leading zeros are displayed by FormatNumberForDisplay
     lcd.setCursor(0, 1); lcd.print("                ");
     delayMicroseconds(100000);
     lcd.setCursor(0, 1); lcd.print(FormatNumberForDisplay(UserValue, Units));
     ChoiceMade = 0;
    // Read digits from User Value
    int x = 0;
    if (Units == 1) {
      UVTemp = UVTemp / 2;
      while (UVTemp > 0) {
        Digits[7-x] = (UVTemp % 10);
        UVTemp = UVTemp / 10;
        x++;
      }
    }
    if (Units == 2) {
      UVTemp = round(((((float)UVTemp/DACBits)*20) - 10)*100);
      if (UVTemp < 0) {isNegativeZero = 1;}
      Digits[2] = (UVTemp % 10);
      UVTemp = UVTemp/10;
      Digits[1] = (UVTemp % 10);
      UVTemp = UVTemp/10;
      Digits[0] = UVTemp;
      if (isNegativeZero && (Digits[0] == 0)) {Digits[0] = 255;} // 255 codes for -0, required because interface changes value by digit
      if (Digits[1] < 0) {Digits[1] = Digits[1]*-1;}
      if (Digits[2] < 0) {Digits[2] = Digits[2]*-1;}
    }
    
     // Assign valid cursor positions by unit type
     switch(Units) {
       case 0: {ValidCursorPositions[0] = 7;} break;
       case 1: {ValidCursorPositions[0] = 2; ValidCursorPositions[1] = 3; ValidCursorPositions[2] = 4; ValidCursorPositions[3] = 5; ValidCursorPositions[4] = 7; ValidCursorPositions[5] = 8; ValidCursorPositions[6] = 9; ValidCursorPositions[7] = 10; ValidCursorPositions[8] = 11;} break;
       case 2: {ValidCursorPositions[0] = 5; ValidCursorPositions[1] = 7; ValidCursorPositions[2] = 8;} break;
       case 3: {ValidCursorPositions[0] = 7;} break;
       case 4: {ValidCursorPositions[0] = 7;} break;
       case 5: {ValidCursorPositions[0] = 7;} break;
     }
     // Initialize cursor starting positions and limits by unit type
     switch (Units) {
       case 0: {CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;} break; // Format for Index
       case 1: {CursorPos = 3; CursorPosLeftLimit = 0; CursorPosRightLimit = 7;} break; // Format for seconds
       case 2: {
        if (abs(Digits[0]) == 10) {
          CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;
        } else {
          CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 2;
        }
        } break; // Format for volts
       case 3: {CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;} break; // Format for Off/On
       case 4: {CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;} break; // Format for Pulses/Bursts
       case 5: {CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;} break; // Format for trigger mode
       }
     CursorToggleTimer = 0; 
     CursorOn = 0;
     CursorToggleThreshold = 20000;
     delayMicroseconds(75000); // Acts as a debounce so user has released pushbutton before reading for an entry
     while (ChoiceMade == 0) {
       CursorToggleTimer++;
       if (CursorToggleTimer == CursorToggleThreshold) {
         switch (CursorOn) {
           case 0: {lcd.setCursor(ValidCursorPositions[CursorPos], 1); lcd.cursor(); CursorOn = 1;} break;
           case 1: {lcd.noCursor(); CursorOn = 0;} break;
         }
         CursorToggleTimer = 0;
       }
       ClickerX = analogRead(ClickerXLine);
       ClickerY = analogRead(ClickerYLine);
       ClickerButtonState = digitalRead(ClickerButtonLine);
       if (ClickerButtonState == 0) {
         ChoiceMade = 1;
       }       
       if (ClickerY < ClickerMinThreshold) {
          switch(Units) {
            case 0: {
              if (UserValue < UpperLimit) {
                UserValue = UserValue + 1;
              }
            } break;
            case 1: {
                ValueToAdd = 2*(pow(10, ((5-CursorPos)+2)));
                if ((Digits[CursorPos] < 9) && ((UserValue+ValueToAdd) <= UpperLimit)) {
                 UserValue = UserValue + ValueToAdd;
                 Digits[CursorPos] = Digits[CursorPos] + 1;
                }
            } break;
            case 2: {
                if (((CursorPos > 0) && (Digits[CursorPos] < 9)) || ((CursorPos == 0) && ((Digits[0] < 10) || (Digits[0] == 255)))) {
                    if (UserValue < DACBits) {
                      if (Digits[CursorPos] == 255) {Digits[CursorPos] = 0;}
                      else if (Digits[CursorPos] == -1) {Digits[CursorPos] = 255;}
                      else {Digits[CursorPos] = Digits[CursorPos] + 1;}
                      if (abs(Digits[0]) == 10) {
                        CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;
                      } else {
                        CursorPosLeftLimit = 0; CursorPosRightLimit = 2;
                      }
                      CandidateVoltage = 0;
                      if (Digits[0] != 255) {
                        CandidateVoltage = CandidateVoltage + ((float)Digits[0]);
                      }
                      if ((Digits[0] < 0) || (Digits[0] == 255)) {
                        CandidateVoltage = CandidateVoltage - ((float)Digits[1]*0.1);
                        CandidateVoltage = CandidateVoltage - ((float)Digits[2]*0.01);
                      } else {
                        CandidateVoltage = CandidateVoltage + ((float)Digits[1]*0.1);
                        CandidateVoltage = CandidateVoltage + ((float)Digits[2]*0.01);
                      }
                      if (((Digits[0] == 255) && (CandidateVoltage == 0)) || (CandidateVoltage < 0)) {
                        isNegativeZero = 1;
                      } else {
                        isNegativeZero = 0;
                      }
                      
                      if (CandidateVoltage > 10) {
                        Digits[CursorPos] = Digits[CursorPos] - 1;
                      } else if (CandidateVoltage == 10) {
                        UserValue = 65535; // Top of DAC range (0-65535; 65536 is out of range)
                      } else {
                        CandidateVoltage = ((CandidateVoltage+10)/20)*DACBits;
                        UserValue = (unsigned int)CandidateVoltage;
                      }
                      delayMicroseconds(1000);
                    }
                } 
            } break;
            default: {
              if (UserValue < UpperLimit) {
                UserValue = UserValue + 1;
              }
            } break;
          }
          ScrollSpeedDelay = 200000;
          lcd.noCursor();
          lcd.setCursor(0, 1); lcd.print(FormatNumberForDisplay(UserValue, Units));
       }
      else if (ClickerY > ClickerMaxThreshold) {
         switch(Units) {
            case 0: {
              if (UserValue > LowerLimit) {
                UserValue = UserValue - 1;
              }
            } break;
            case 1: {
                if (Digits[CursorPos] > 0)  {
                 UserValue = UserValue - 2*(pow(10, ((5-CursorPos)+2)));
                  Digits[CursorPos] = Digits[CursorPos] - 1;
                }
            } break;
            case 2: {
              if (((CursorPos > 0) && (Digits[CursorPos] > 0)) || ((CursorPos == 0) && ((Digits[0] > -10) || (Digits[0] == 255)))) {
                    if (UserValue > 0) {
                      if (Digits[CursorPos] == 255) {Digits[CursorPos] = -1;}
                      else if (Digits[CursorPos] == 0) {Digits[CursorPos] = 255;}
                      else {Digits[CursorPos] = Digits[CursorPos] - 1;}
                      if (abs(Digits[0]) == 10) {
                        CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;
                      } else {
                        CursorPosLeftLimit = 0; CursorPosRightLimit = 2;
                      }
                      CandidateVoltage = 0;
                      if (Digits[0] != 255) {
                        CandidateVoltage = CandidateVoltage + ((float)Digits[0]);
                      }
                      if ((Digits[0] < 0) || (Digits[0] == 255)) {
                        CandidateVoltage = CandidateVoltage - ((float)Digits[1]*0.1);
                        CandidateVoltage = CandidateVoltage - ((float)Digits[2]*0.01);
                      } else {
                        CandidateVoltage = CandidateVoltage + ((float)Digits[1]*0.1);
                        CandidateVoltage = CandidateVoltage + ((float)Digits[2]*0.01);
                      }
                      if (((Digits[0] == 255) && (CandidateVoltage == 0)) || (CandidateVoltage < 0)) {
                        isNegativeZero = 1;
                      } else {
                        isNegativeZero = 0;
                      }
                      CandidateVoltage = ((CandidateVoltage+10)/20)*DACBits;
                      UserValue = (unsigned int)CandidateVoltage;
                      delayMicroseconds(1000);
//                  }
                  }
                } 
            } break;
            default: {
              if (UserValue > LowerLimit) {
                UserValue = UserValue - 1;
              }
            } break;
          }
          ScrollSpeedDelay = 200000;
          lcd.noCursor();
          lcd.setCursor(0, 1); lcd.print(FormatNumberForDisplay(UserValue, Units));
       } else {
         ScrollSpeedDelay = 0;
       }
       if ((ClickerX > ClickerMaxThreshold) && (CursorPos < CursorPosRightLimit)) {
         CursorPos = CursorPos + 1;
         ScrollSpeedDelay = 200000;
         lcd.noCursor();
          lcd.setCursor(0, 1); lcd.print(FormatNumberForDisplay(UserValue, Units));
         lcd.setCursor(ValidCursorPositions[CursorPos], 1); lcd.cursor(); CursorOn = 1; 
       }
       if ((ClickerX < ClickerMinThreshold) && (CursorPos > CursorPosLeftLimit)) {
         CursorPos = CursorPos - 1;
         ScrollSpeedDelay = 200000;
         lcd.noCursor();
         lcd.setCursor(0, 1); lcd.print(FormatNumberForDisplay(UserValue, Units));
         lcd.setCursor(ValidCursorPositions[CursorPos], 1); lcd.cursor(); CursorOn = 1; 
       }
     delayMicroseconds(ScrollSpeedDelay);  
     }
     lcd.noCursor();
     lcd.setCursor(0, 1); lcd.print("                ");
     if (Units == 5) {
       inMenu = 4;
     } else {
       inMenu = 2;
     }
     delayMicroseconds(200000);
     lcd.setCursor(0, 1); lcd.print(FormatNumberForDisplay(UserValue, Units));
     //lcd.noCursor();
     return UserValue;
} 

void LoadDefaultParameters() {
  // This function is called on boot if the EEPROM has an invalid program (or no program).
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
      UsesBursts[x] = 0;
    }
    for (int y = 0; y < 4; y++) {
      TriggerAddress[0][y] = 1;
    }
    for (int y = 0; y < 4; y++) {
      TriggerAddress[1][y] = 0;
    }
   TriggerMode[0] = 0; 
   TriggerMode[1] = 0;
   // Store default parameters to SD card
   SaveCurrentProgram2SD();
}

byte SerialReadByte(){
  byte ReturnByte = 0;
  if (SerialReadTimedout == 0) {
    SerialReadStartTime = millis();
    while (SerialUSB.available() == 0) {
        SerialCurrentTime = millis();
        if ((SerialCurrentTime - SerialReadStartTime) > Timeout) {
          SerialReadTimedout = 1;
          return 0;
        }
    }
    ReturnByte = SerialUSB.read();
    return ReturnByte;
  } else {
    return 0;
  }
}

void HandleReadTimeout() {
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

void AbortAllPulseTrains() {
    for (int x = 0; x < 4; x++) {
      killChannel(x);
    }
    dacWrite();
    write2Screen("   PULSE TRAIN","     ABORTED");
    delayMicroseconds(1000000);
    if (inMenu == 0) {
      write2Screen(CommanderString," Click for menu");
    } else {
      inMenu = 1;
      RefreshChannelMenu(SelectedChannel);
    }
}

void ResetSystemTime() {
  SystemTime = 0;
}

unsigned long ComputePulseDuration(byte myBiphasic, unsigned long myPhase1, unsigned long myPhaseInterval, unsigned long myPhase2) {
    unsigned long Duration = 0;
    if (myBiphasic == 0) {
       Duration = myPhase1;
     } else {
       Duration = myPhase1 + myPhaseInterval + myPhase2;
     }
     return Duration;
}

void write2Screen(const char* Line1, const char* Line2) {
  lcd.clear(); lcd.home(); lcd.print(Line1); lcd.setCursor(0, 1); lcd.print(Line2);
}

void breakLong(unsigned long LongInt2Break) {
  //BrokenBytes is a global array for the output of long int break operations
  BrokenBytes[3] = (byte)(LongInt2Break >> 24);
  BrokenBytes[2] = (byte)(LongInt2Break >> 16);
  BrokenBytes[1] = (byte)(LongInt2Break >> 8);
  BrokenBytes[0] = (byte)LongInt2Break;
}

void breakShort(word Value2Break) {
  //BrokenBytes is a global array for the output of long int break operations
  BrokenBytes[1] = (byte)(Value2Break >> 8);
  BrokenBytes[0] = (byte)Value2Break;
}

void writeLong2SD() {
  settingsFile.write(BrokenBytes[0]);
  settingsFile.write(BrokenBytes[1]);
  settingsFile.write(BrokenBytes[2]);
  settingsFile.write(BrokenBytes[3]);
}
void writeShort2SD() {
  settingsFile.write(BrokenBytes[0]);
  settingsFile.write(BrokenBytes[1]);
}
unsigned long readLongFromSD() {
  unsigned long myLongInt = 0;
  settingsFile.read(buf4, sizeof(buf4));
  myLongInt = makeUnsignedLong(buf4[3], buf4[2], buf4[1], buf4[0]);
  return myLongInt;
}
word readShortFromSD() {
  word myWord = 0;
  settingsFile.read(buf2, sizeof(buf2));
  myWord = makeUnsignedShort(buf2[1], buf2[0]);
  return myWord;
}
byte readByteFromSD() {
  byte myByte = 0;
  settingsFile.read(buf, sizeof(buf));
  myByte = buf[0];
  return myByte;
}

void SaveCurrentProgram2SD() {
  settingsFile.close();
  settingsFile.open(currentSettingsFileNameChar, O_CREAT | O_TRUNC | O_RDWR);
  //settingsFile.rewind();
  // This function saves all parameters to the SD card. See the memory map on the PulsePal wiki for a table describing how parameters are organized in memory
  for (int chan = 0; chan < 4; chan++) {
    breakLong(Phase1Duration[chan]); writeLong2SD();
    breakLong(InterPhaseInterval[chan]); writeLong2SD();
    breakLong(Phase2Duration[chan]); writeLong2SD();
    breakLong(InterPulseInterval[chan]); writeLong2SD();
    breakLong(BurstDuration[chan]); writeLong2SD();
    breakLong(BurstInterval[chan]); writeLong2SD();
    breakLong(PulseTrainDuration[chan]); writeLong2SD();
    breakLong(PulseTrainDelay[chan]); writeLong2SD();
    settingsFile.write(IsBiphasic[chan]);
    breakShort(Phase1Voltage[chan]); writeShort2SD();
    breakShort(Phase2Voltage[chan]); writeShort2SD();
    breakShort(RestingVoltage[chan]); writeShort2SD();
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
  settingsFile.write(252);
  settingsFile.close();
  settingsFile.open(currentSettingsFileNameChar, O_READ);
}

byte RestoreParametersFromSD() {
  // This function is called on Pulse Pal boot, to make pulse pal auto-load parameters from the previous session.
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

void Software_Reset() {
  const int RSTC_KEY = 0xA5;
  RSTC->RSTC_CR = RSTC_CR_KEY(RSTC_KEY) | RSTC_CR_PROCRST | RSTC_CR_PERRST;
  while (true);
}

void SerialWriteLong(unsigned long num) {
  SerialUSB.write((byte)num); 
  SerialUSB.write((byte)(num >> 8)); 
  SerialUSB.write((byte)(num >> 16)); 
  SerialUSB.write((byte)(num >> 24));
}

void SerialWriteShort(word num) {
  SerialUSB.write((byte)num); 
  SerialUSB.write((byte)(num >> 8)); 
}
