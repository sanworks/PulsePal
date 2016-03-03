/*
----------------------------------------------------------------------------

This file is part of the PulsePal Project
Copyright (C) 2016 Sanworks LLC, NY, USA

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

// PULSE PAL v1.1 firmware 
// Josh Sanders, January 2016

#include <LiquidCrystal.h>
#include <stdio.h>
#include <gpio.h>

// Define a macro for compressing sequential bytes read from the serial port into long ints
#define makeLong(msb, byte2, byte3, lsb) ((msb << 24) | (byte2 << 16) | (byte3 << 8) | (lsb))
#define LED_PIN_PORT GPIOA
#define INPUT_PIN_PORT GPIOC
// EEPROM constants
#define PAGE_SIZE 32
#define SPI_MODE 0
#define CS 37 // chip select pin
// EEPROM opcodes
#define WREN 6
#define WRDI 4
#define RDSR 5
#define WRSR 1
#define READ 3
#define WRITE 2

// Reset macros
#define SCB_AIRCR ((volatile uint32*) (0xE000ED00 + 0x0C))
#define SCB_AIRCR_SYSRESETREQ (1 << 2)
#define SCB_AIRCR_RESET ((0x05FA0000) | SCB_AIRCR_SYSRESETREQ)

// Trigger line level configuration (0 = default high, trigger low (versions with optocoupler). 1 = default low, trigger high.)
#define TriggerLevel 0
#define ClickerButtonLogicHigh 0

// Firmware build number
unsigned long FirmwareVersion = 5;

// initialize LCD library with the numbers of the interface pins
// Pins matched with hello world LCD sketch
//LiquidCrystal lcd(12, 13, 28, 29, 30, 31);
LiquidCrystal lcd(14, 20, 9, 8, 7, 6);

// Variables that define system parameters
byte InputLines[2] = {15,16};
byte InputLineBits[2] = {0,1};
byte OutputLEDLines[4] = {3,2,1,0}; // Output lines
byte OutputLEDLineBits[4] = {1, 0, 2, 3 }; // for faster write times, "Bits" address the pins directly - low level ARM commands.
byte InputLEDLines[2] = {35, 36};
byte InputLEDLineBits[2] = {6,7}; 
byte ClickerXLine = 19;
byte ClickerYLine = 18;
byte ClickerButtonLine = 17;
byte ClickerButtonBit = 2;
//int ClickerButtonSupplyPin = 6;
//int HbridgeEnableLine = 12; // tie to vcc in future editions
byte LEDLine = 22;
byte DACLoadPin=4;
byte DACLatchPin=5;
byte USBPacketCorrectionByte = 0; // If messages sent over USB in Windows XP are 64 bytes, the system crashes - so this variable keeps track of whether to chop off a junk byte at the end of the message. Used for custom stimuli.
HardwareSPI spi(1);
HardwareSPI EEPROM(2);
byte CycleDuration = 50; unsigned int CycleFrequency = 20000;

// Variables related to EEPROM
byte PageBytes[32] = {0}; // Stores page to be written
int EEPROM_address = 0;
byte EEPROM_OutputValue = 0;
byte nBytesToWrite = 0;
byte nBytesToRead = 0;
byte BrokenBytes[4] = {0};

// Variables that define pulse trains currently loaded on the 4 output channels
unsigned long Phase1Duration[4] = {0};
unsigned long InterPhaseInterval[4] = {0};
unsigned long Phase2Duration[4] = {0};
unsigned long InterPulseInterval[4] = {0};
unsigned long BurstDuration[4] = {0};
unsigned long BurstInterval[4] = {0};
unsigned long PulseTrainDuration[4] = {0};
unsigned long PulseTrainDelay[4] = {0};
byte Phase1Voltage[4] = {0};
byte Phase2Voltage[4] = {0};
byte RestingVoltage[4] = {128}; // Voltage the system returns to between pulses (128 bits = 0V)
int CustomTrainID[4] = {0}; // If 0, uses above params. If 1 or 2, triggering plays back timestamps in CustomTrain1 or CustomTrain2 with pulsewidth defined as usual
int CustomTrainTarget[4] = {0}; // If 0, custom stim timestamps are start-times of pulses. If 1, custom stim timestamps are start-times of bursts.
int CustomTrainLoop[4] = {0}; // if 0, custom stim plays once. If 1, custom stim loops until PulseTrainDuration.
int ConnectedToApp = 0; // 0 for none, 1 for MATLAB client, 2 for Labview client, 3 for Python client

// Variables used in programming
byte OpMenuByte = 213; // This byte must be the first byte in any serial transmission to Pulse Pal. Reduces the probability of interference from port-scanning software
byte TriggerAddress[2][4] = {0}; // This specifies which output channels get triggered by trigger channel 1 (row 1) or trigger channel 2 (row 2)
byte TriggerMode[2] = {0}; // if 0, "Normal mode", triggers on low to high transitions and ignores triggers until end of stimulus train. if 1, "Toggle mode", triggers on low to high and shuts off stimulus
//train on next high to low. If 2, "Button mode", triggers on low to high and shuts off on high to low.
unsigned long TriggerButtonDebounce[2] = {0}; // In button mode, number of microseconds the line must be low before stopping the pulse train.
int CustomPulseTimeIndex[4] = {0}; // Keeps track of the pulse number being played in custom stim condition
unsigned long CustomTrainNpulses[2] = {0}; // Number of pulses in the stimulus
int ClickerX = 0; // Value of analog reads from X line of joystick input device
int ClickerY = 0; // Value of analog reads from Y line of joystick input device
boolean ClickerButtonState = 0; // Value of digital reads from button line of joystick input device
boolean LastClickerButtonState = 1;
int LastClickerYState = 0; // 0 for neutral, 1 for up, 2 for down.
int LastClickerXState = 0; // 0 for neutral, 1 for left, 2 for right.
int inMenu = 0; // Menu id: 0 for top, 1 for channel menu, 2 for action menu
int SelectedChannel = 0;
int SelectedAction = 1;
byte SelectedInputAction = 1;
int SelectedStimMode = 1;
boolean NeedUpdate = 0; // If a new menu item is selected, the screen must be updated
boolean SerialReadTimedout = 0; // Goes to 1 if a serial read timed out, causing all subsequent serial reads to skip until next main loop iteration.
int SerialCurrentTime = 0; // Current time (millis) for serial read timeout
int SerialReadStartTime = 0; // Time the serial read was started
int Timeout = 500; // Times out after 500ms

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
unsigned long StimulusTrainEndTime[4] = {0}; // Stores time the stimulus train is supposed to end
unsigned long CustomPulseTimes[2][1001] = {0};
byte CustomVoltages[2][1001] = {0};
unsigned long LastLoopTime = 0;
boolean PulseStatus[4] = {0}; // This is 0 if not delivering a pulse, 1 if delivering.
boolean BurstStatus[4] = {0}; // This is "true" during bursts and false during inter-burst intervals.
boolean StimulusStatus[4] = {0}; // This is "true" for a channel when the stimulus train is actively being delivered
boolean PreStimulusStatus[4] = {0}; // This is "true" for a channel during the pre-stimulus delay
boolean InputValues[2] = {0}; // The values read directly from the two inputs (for analog, digital equiv. after thresholding)
boolean InputValuesLastCycle[2] = {0}; // The values on the last cycle. Used to detect low to high transitions.
boolean LineTriggerEvent[2] = {0}; // 0 if no line trigger event detected, 1 if present.
unsigned long InputLineDebounceTimestamp[2] = {0}; // Last time the line went from high to low
boolean UsesBursts[4] = {0};
unsigned long PulseDuration[4] = {0}; // Duration of a pulse (sum of 3 phases for biphasic pulse)
boolean IsBiphasic[4] = {0};
boolean IsCustomBurstTrain[4] = {0};
boolean ContinuousLoopMode[4] = {0}; // If true, the channel loops its programmed stimulus train continuously
int AnalogValues[2] = {0};
int SensorValue = 0;
byte StimulatingState = 0; // 1 if ANY channel is stimulating, 2 if this is the first cycle after the system was triggered. 
byte LastStimulatingState = 0;
boolean WasStimulating = 0; // true if any channel was stimulating on the previous loop. Used to force a DAC write after all channels end their stimulation, to return lines to 0
int nStimulatingChannels = 0; // number of actively stimulating channels
boolean DACFlags[4] = {0}; // true if an individual DAC needs to be updated
byte DACValues[4] = {0};
byte DefaultInputLevel = 0; // 0 for PulsePal 0.3, 1 for 0.2 and 0.1. Logic is inverted by optoisolator
byte DACBuffer0[2] = {0}; // Buffers already containing address for faster SPIwriting
byte DACBuffer1[2] = {1};
byte DACBuffer2[2] = {2};
byte DACBuffer3[2] = {3};

// Screen saver variables
boolean useScreenSaver = 0; // Disabled by default
boolean SSactive = 0; // Bit indicating whether screen saver is currently active
unsigned long SSdelay = 60000; // Idle cycles until screen saver is activated
unsigned long SScount = 0; // Counter of idle cycles

// Other variables
char Value2Display[18] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0'};
int lastDebounceTime = 0; // to debounce the joystick button
boolean lastButtonState = 0;
boolean ChoiceMade = 0; // determines whether user has chosen a value from a list
unsigned int UserValue = 0; // The current value displayed on a list of values (written to LCD when choosing parameters)
char CommanderString[16] = " PULSE PAL v1.1";
char ClientStringSuffix[11] = " Connected";
char DefaultCommanderString[16] = " PULSE PAL v1.1";
byte ValidEEPROMProgram = 0; // A byte read from EEPROM. This is always 1 if the EEPROM has been written to. Used to load defaults on first-time use.
void handler(void);

void setup() {
  // Enable EEPROM
  pinMode(CS, OUTPUT);
  digitalWrite(CS, HIGH); // disable writes
  EEPROM.begin(SPI_9MHZ, MSBFIRST, SPI_MODE);
  // set up the LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.home();
  lcd.noDisplay() ;
  delay(100);
  lcd.display() ;
  
  // Pin modes
  pinMode(InputLines[0], INPUT);
  pinMode(InputLines[1], INPUT);
  pinMode(ClickerButtonLine, INPUT_PULLUP);
  pinMode(ClickerXLine, INPUT_ANALOG);
  pinMode(ClickerYLine, INPUT_ANALOG);
  
    for (int x = 0; x < 4; x++) {
    pinMode(OutputLEDLines[x], OUTPUT);
  }
    spi.begin(SPI_18MHZ, MSBFIRST, 0);
    pinMode(DACLoadPin, OUTPUT);
    pinMode(DACLatchPin, OUTPUT);
    pinMode(InputLEDLines[0], OUTPUT);
    pinMode(InputLEDLines[1], OUTPUT);
  
    RestoreParametersFromEEPROM();
    if (ValidEEPROMProgram != 1) {
      LoadDefaultParameters();
    }
    // Set DAC to resting voltage on all channels
    for (int x = 0; x < 4; x++) {
      DACValues[x] = RestingVoltage[x];
    }
    dacWrite(DACValues);
    
    write2Screen(CommanderString," Click for menu");
    SystemTime = micros();
    LastLoopTime = SystemTime;
    DefaultInputLevel = 1 - TriggerLevel;
    Timer2.setChannel1Mode(TIMER_OUTPUTCOMPARE);
    //Timer2.setPeriod(CycleDuration); // in microseconds
    Timer2.setPrescaleFactor(1800);
    Timer2.setOverflow(1);
    Timer2.setCompare1(1);      // overflow might be small
    Timer2.attachCompare1Interrupt(handler);
}

void loop() {

}

void handler(void) {
  if (SerialReadTimedout == 1) {
    Timer2.pause();
    HandleReadTimeout(); // Notifies user of error, then prompts to click and restores DEFAULT channel settings.
    SerialReadTimedout = 0;
    Timer2.resume();
  }
  if (StimulatingState == 0) {
      if (LastStimulatingState == 1) {
        dacWrite(DACValues); // Update DAC
      }
      UpdateSettingsMenu();
      SystemTime = 0;
      if (!inMenu) {
        if (useScreenSaver) {
          SScount++;
          if (SScount > SSdelay) {
            if (!SSactive && !ConnectedToApp) {
              SSactive = 1;
              lcd.clear();
            }
          }
        }
      }
   } else {
//     if (StimulatingState == 2) {
//                // Place to include first-cycle operations
//     }
     StimulatingState = 1;
     dacWrite(DACValues); // Update DAC
     SystemTime++; // Increment system time (# of cycles since stim start)
     ClickerButtonState = digitalRead(ClickerButtonLine);
     if (ClickerButtonState == ClickerButtonLogicHigh){    // A button click ends ongoing stimulation on all channels.
       AbortAllPulseTrains();
     }
  }
  LastStimulatingState = StimulatingState;
      
  if (SerialUSB.available() > 0) {
    CommandByte = SerialUSB.read();
    if (CommandByte == OpMenuByte) {
      CommandByte = SerialReadByte();
      switch (CommandByte) {
        case 72: { // Handshake
          SerialUSB.write(75); // Send 'K' (as in ok)
          breakLong(FirmwareVersion); // Send 32-bit firmware version
          SerialUSB.write(BrokenBytes[0]);
          SerialUSB.write(BrokenBytes[1]);
          SerialUSB.write(BrokenBytes[2]);
          SerialUSB.write(BrokenBytes[3]); 
          ConnectedToApp = 1;
        } break;
        case 73: { // Program the module - total program (faster than item-wise in some instances)
          for (int x = 0; x < 4; x++) {
            Phase1Duration[x] = SerialReadLong();
            InterPhaseInterval[x] = SerialReadLong();
            Phase2Duration[x] = SerialReadLong();
            InterPulseInterval[x] = SerialReadLong();
            BurstDuration[x] = SerialReadLong();
            BurstInterval[x] = SerialReadLong();
            PulseTrainDuration[x] = SerialReadLong();
            PulseTrainDelay[x] = SerialReadLong();
          }
          for (int x = 0; x < 4; x++) {
            IsBiphasic[x] = SerialReadByte();
            Phase1Voltage[x] = SerialReadByte();
            Phase2Voltage[x] = SerialReadByte();
            CustomTrainID[x] = SerialReadByte();
            CustomTrainTarget[x] = SerialReadByte();
            CustomTrainLoop[x] = SerialReadByte();
            RestingVoltage[x] = SerialReadByte();
          }
         for (int x = 0; x < 2; x++) { // Read 8 trigger address bytes
           for (int y = 0; y < 4; y++) {
             TriggerAddress[x][y] = SerialReadByte();
           }
         }
         TriggerMode[0] = SerialReadByte(); 
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
           DACValues[x] = RestingVoltage[x];
         }
         dacWrite(DACValues);
        } break;
        
        // Program the module - one parameter
        case 74: {
          inByte2 = SerialReadByte();
          inByte3 = SerialReadByte(); // inByte3 = channel (1-4)
          inByte3 = inByte3 - 1; // Convert channel for zero-indexing
          switch (inByte2) { 
             case 1: {IsBiphasic[inByte3] = SerialReadByte();} break;
             case 2: {Phase1Voltage[inByte3] = SerialReadByte();} break;
             case 3: {Phase2Voltage[inByte3] = SerialReadByte();} break;
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
             case 17: {RestingVoltage[inByte3] = SerialReadByte();} break;
             case 128: {TriggerMode[inByte3] = SerialReadByte();} break;
          }
          if (inByte2 < 14) {
            if ((BurstDuration[inByte3] == 0) || (BurstInterval[inByte3] == 0)) {UsesBursts[inByte3] = false;} else {UsesBursts[inByte3] = true;}
            if (CustomTrainTarget[inByte3] == 1) {UsesBursts[inByte3] = true;}
            if ((CustomTrainID[inByte3] > 0) && (CustomTrainTarget[inByte3] == 0)) {UsesBursts[inByte3] = false;}
          }
          if (inByte2 == 17) {
            DACValues[inByte3] = RestingVoltage[inByte3];
            dacWrite(DACValues);
          }
          PulseDuration[inByte3] = ComputePulseDuration(IsBiphasic[inByte3], Phase1Duration[inByte3], InterPhaseInterval[inByte3], Phase2Duration[inByte3]);
          if ((CustomTrainID[inByte3] > 0) && (CustomTrainTarget[inByte3] == 1)) {
            IsCustomBurstTrain[inByte3] = 1;
          } else {
            IsCustomBurstTrain[inByte3] = 0;
          }
          SerialUSB.write(1); // Send confirm byte
        } break;
  
        // Program custom stimulus 1
        case 75: {
          USBPacketCorrectionByte = SerialReadByte();
          CustomTrainNpulses[0] = SerialReadLong();
          for (int x = 0; x < CustomTrainNpulses[0]; x++) {
            CustomPulseTimes[0][x] = SerialReadLong();
          }
          for (int x = 0; x < CustomTrainNpulses[0]; x++) {
            CustomVoltages[0][x] = SerialReadByte();
          }
          if (USBPacketCorrectionByte == 1) {
            USBPacketCorrectionByte = 0;
            CustomTrainNpulses[0] = CustomTrainNpulses[0]  - 1;
          }
          SerialUSB.write(1); // Send confirm byte
        } break;
        // Program custom stimulus 2
        case 76: {
          USBPacketCorrectionByte = SerialReadByte();
          CustomTrainNpulses[1] = SerialReadLong();
          for (int x = 0; x < CustomTrainNpulses[1]; x++) {
            CustomPulseTimes[1][x] = SerialReadLong();
          }
          for (int x = 0; x < CustomTrainNpulses[1]; x++) {
            CustomVoltages[1][x] = SerialReadByte();
          }
          if (USBPacketCorrectionByte == 1) {
            USBPacketCorrectionByte = 0;
            CustomTrainNpulses[1] = CustomTrainNpulses[1]  - 1;
          }
          SerialUSB.write(1); // Send confirm byte
        } break;      
        // Soft-trigger the module
        case 77: {
          inByte2 = SerialReadByte();
          for (int x = 0; x < 4; x++) {
            PreStimulusStatus[x] = bitRead(inByte2, x);
            if (PreStimulusStatus[x] == 1) {
              if ((CustomTrainID[x] != 0) && (CustomTrainTarget[x] == 1)) {BurstStatus[x] = 0;} else {
                   BurstStatus[x] = 1; 
              }
            if (StimulatingState == 0) {ResetSystemTime(); StimulatingState = 2;}
              PrePulseTrainTimestamps[x] = SystemTime;
            }
          }
        } break;
        case 78: { 
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
        case 79: {
          // Write specific voltage to output channel (not a pulse train) 
          inByte = SerialReadByte();
          inByte = inByte - 1; // Convert for zero-indexing
          inByte2 = SerialReadByte();
          DACValues[inByte] = inByte2;
          dacWrite(DACValues);
          SerialUSB.write(1); // Send confirm byte
        } break;
        case 80: { // Soft-abort ongoing stimulation without disconnecting from client
         for (int x = 0; x < 4; x++) {
          killChannel(x);
        }
        dacWrite(DACValues);
       } break;
       case 81: { // Disconnect from client and store params to EEPROM
          ConnectedToApp = 0;
          inMenu = 0;
          for (int x = 0; x < 4; x++) {
            killChannel(x);
          }
          dacWrite(DACValues);
          // Store last program to EEPROM
          write2Screen("Saving Settings",".");
          EEPROM_address = 0;
          for (int x = 0; x < 4; x++) {
            PrepareOutputChannelMemoryPage1(x);
            WriteEEPROMPage(PageBytes, 32, EEPROM_address);
            EEPROM_address = EEPROM_address + 32;
            PrepareOutputChannelMemoryPage2(x);
            WriteEEPROMPage(PageBytes, 32, EEPROM_address);
            EEPROM_address = EEPROM_address + 32;
            switch (x) {
              case 0: { write2Screen("Saving Settings",". .");} break;
              case 1: { write2Screen("Saving Settings",". . .");} break;
              case 2: { write2Screen("Saving Settings",". . . .");} break;
              case 3: { write2Screen("Saving Settings",". . . . .");} break;
            }
            delay(100);
          }
          write2Screen("Saving Settings",". . . . . Done!");
          delay(700);
          SSactive = 0;
          SScount = 0;
          for (int x = 0; x < 16; x++) {
           CommanderString[x] = DefaultCommanderString[x];
         } 
          write2Screen(CommanderString," Click for menu");
         } break;
         // Set free-run mode
        case 82:{
          inByte2 = SerialReadByte();
          inByte2 = inByte2 - 1; // Convert for zero-indexing
          inByte3 = SerialReadByte();
          ContinuousLoopMode[inByte2] = inByte3;
          SerialUSB.write(1);
        } break;
        case 83: { // Clear stored parameters from EEPROM
         WipeEEPROM();
         if (inMenu == 0) {
          write2Screen(CommanderString," Click for menu");
        } else {
          inMenu = 1;
          RefreshChannelMenu(SelectedChannel);
        }
       } break;
       case 84: {
          inByte2 = SerialReadByte();
          EEPROM_address = inByte2;
          nBytesToWrite = SerialReadByte();
          for (int i = 0; i < nBytesToWrite; i++) {
          PageBytes[i] = SerialReadByte();
          }
          WriteEEPROMPage(PageBytes, nBytesToWrite, EEPROM_address);
          SerialUSB.write(1);
        } break; 
      case 85: {
          inByte2 = SerialReadByte();
          EEPROM_address = inByte2;
          nBytesToRead = SerialReadByte();
          for (int i = 0; i < nBytesToRead; i++) {
           EEPROM_OutputValue = ReadEEPROM(EEPROM_address+i);
           SerialUSB.write(EEPROM_OutputValue);
          }
        } break;
        
        case 86: { // Override IO Lines
          inByte2 = SerialReadByte();
          inByte3 = SerialReadByte();
          pinMode(inByte2, OUTPUT); digitalWrite(inByte2, inByte3);
        } break; 
        
        case 87: { // Direct Read IO Lines
          inByte2 = SerialReadByte();
          pinMode(inByte2, INPUT);
          delayMicroseconds(10);
          LogicLevel = digitalRead(inByte2);
          SerialUSB.write(LogicLevel);
        } break; 
        case 88: { // Direct Read IO Lines as analog
          inByte2 = SerialReadByte();
          pinMode(inByte2, INPUT_ANALOG);
          delay(10);
          SensorValue = analogRead(inByte2);
          SerialUSB.println(SensorValue);
          pinMode(inByte2, OUTPUT);
        } break;
        case 89: { // Receive new CommanderString (displayed on top line of OLED, i.e. "MATLAB connected"
          for (int x = 0; x < 6; x++) {
            CommanderString[x] = SerialReadByte();
          }
          for (int x = 6; x < 16; x++) {
            CommanderString[x] = ClientStringSuffix[x-6];
          }
          write2Screen(CommanderString," Click for menu");
        } break;
        case 94: {
          useScreenSaver = SerialReadByte();
          if (useScreenSaver == 1) {
            SSdelay = SerialReadLong(); // Screensaver onset delay in 50us cycles (i.e. 20000 = 1 second)
          }
          SerialUSB.write(1);
        } break;
       }
     }
  }

    // Read values of trigger pins
    LineTriggerEvent[0] = 0; LineTriggerEvent[1] = 0;
    for (int x = 0; x < 2; x++) {
         //InputValues[x] = gpio_read_bit(INPUT_PIN_PORT, InputLineBits[x]);
         InputValues[x] = digitalRead(InputLines[x]);
         if (InputValues[x] == TriggerLevel) {
           gpio_write_bit(INPUT_PIN_PORT, InputLEDLineBits[x], HIGH);
         } else {
           gpio_write_bit(INPUT_PIN_PORT, InputLEDLineBits[x], LOW);
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
                    if (TriggerMode[1-y] == 2) {
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
       if (TriggerAddress[0][x] && (LineTriggerEvent[0] == 1)) {
         if (StimulatingState == 0) {ResetSystemTime(); StimulatingState = 2;}
         PreStimulusStatus[x] = 1; BurstStatus[x] = 1; PrePulseTrainTimestamps[x] = SystemTime; PulseStatus[x] = 0; 
       }
       if (TriggerAddress[1][x] && (LineTriggerEvent[1] == 1)) {
         if (StimulatingState == 0) {ResetSystemTime(); StimulatingState = 2;}
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
          StimulusTrainEndTime[x] = SystemTime + PulseTrainDuration[x];
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
            DACValues[x] = Phase1Voltage[x];
          } else {
            NextPulseTransitionTime[x] = SystemTime + CustomPulseTimes[thisTrainIDIndex][0]; 
            CustomPulseTimeIndex[x] = 0;
          }
        }
      }
      if (StimulusStatus[x] == 1) { // if this output line has been triggered and is delivering a stimulus
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
                if ((IsCustomBurstTrain[x] == 1) || (PulseDuration[x] + SystemTime) <= StimulusTrainEndTime[x]) { // so that it doesn't start a pulse it can't finish due to pulse train end
                    if (!((UsesBursts[x] == 1) && (NextPulseTransitionTime[x] >= NextBurstTransitionTime[x]))){ // so that it doesn't start a pulse it can't finish due to burst end
                      PulseStatus[x] = 1;
                      gpio_write_bit(LED_PIN_PORT, OutputLEDLineBits[x], HIGH);
                      if ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 1)) {
                        DACValues[x] = CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]];
                      } else {
                        DACValues[x] = Phase1Voltage[x]; 
                      }
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
                     DACValues[x] = CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]];
                     gpio_write_bit(LED_PIN_PORT, OutputLEDLineBits[x], HIGH);
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
                      gpio_write_bit(LED_PIN_PORT, OutputLEDLineBits[x], LOW);
                      DACValues[x] = RestingVoltage[x]; 
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
                              DACValues[x] = CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]];
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
                      gpio_write_bit(LED_PIN_PORT, OutputLEDLineBits[x], LOW);
                      DACValues[x] = RestingVoltage[x]; 
                    }
                  }
     
                } else {
                  if (InterPhaseInterval[x] == 0) {
                    NextPulseTransitionTime[x] = SystemTime + Phase2Duration[x];
                    PulseStatus[x] = 3;
                    if (CustomTrainID[x] == 0) {
                      DACValues[x] = Phase2Voltage[x]; 
                    } else {
                      
                     if (CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]] < 128) {
                       DACValues[x] = 128 + (128 - CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]]); 
                     } else {
                       DACValues[x] = 128 - (CustomVoltages[thisTrainIDIndex][CustomPulseTimeIndex[x]] - 128);
                     }
                   if (CustomTrainTarget[x] == 0) {
                       CustomPulseTimeIndex[x] = CustomPulseTimeIndex[x] + 1;
                   }
                    } 
                  } else {
                    NextPulseTransitionTime[x] = SystemTime + InterPhaseInterval[x];
                    PulseStatus[x] = 2;
                    DACValues[x] = RestingVoltage[x]; 
                  }
                }
              }
            } break;
            case 2: {
               if (SystemTime == NextPulseTransitionTime[x]) {
                 NextPulseTransitionTime[x] = SystemTime + Phase2Duration[x];
                 PulseStatus[x] = 3;
                 if (CustomTrainID[x] == 0) {
                 DACValues[x] = Phase2Voltage[x]; 
                 } else {
                   if (CustomTrainID[x] == 1) {
                     if (CustomVoltages[0][CustomPulseTimeIndex[x]] < 128) {
                       DACValues[x] = 128 + (128 - CustomVoltages[0][CustomPulseTimeIndex[x]]); 
                     } else {
                       DACValues[x] = 128 - (CustomVoltages[0][CustomPulseTimeIndex[x]] - 128);
                     }
                   } else {
                     if (CustomVoltages[1][CustomPulseTimeIndex[x]] < 128) {
                       DACValues[x] = 128 + (128 - CustomVoltages[1][CustomPulseTimeIndex[x]]); 
                     } else {
                       DACValues[x] = 128 - (CustomVoltages[1][CustomPulseTimeIndex[x]]-128);
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
                   gpio_write_bit(LED_PIN_PORT, OutputLEDLineBits[x], LOW);
                   DACValues[x] = RestingVoltage[x]; 
                 } else {
                   PulseStatus[x] = 1;
                   NextPulseTransitionTime[x] = (NextPulseTransitionTime[x] - InterPulseInterval[x]) + (Phase1Duration[x]);
                   DACValues[x] = Phase1Voltage[x]; 
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
              DACValues[x] = RestingVoltage[x]; 
          } else {
          // Determine if burst status should go to 1 now
            NextBurstTransitionTime[x] = SystemTime + BurstDuration[x];
            NextPulseTransitionTime[x] = SystemTime + Phase1Duration[x];
            PulseStatus[x] = 1;
            if ((CustomTrainID[x] > 0) && (CustomTrainTarget[x] == 1)) {              
              if (CustomTrainID[x] == 1) {
                 if (CustomPulseTimeIndex[x] < CustomTrainNpulses[0]){
                    DACValues[x] = CustomVoltages[0][CustomPulseTimeIndex[x]];
                 }
              } else {
                if (CustomPulseTimeIndex[x] < CustomTrainNpulses[1]){
                    DACValues[x] = CustomVoltages[1][CustomPulseTimeIndex[x]];
                 }
              }
            } else {
                 DACValues[x] = Phase1Voltage[x]; 
            }
            BurstStatus[x] = 1;
         }
        }
       } 
        // Determine if Stimulus Status should go to 0 now
        if ((SystemTime == StimulusTrainEndTime[x]) && (StimulusStatus[x] == 1)) {
          if (((CustomTrainID[x] > 0) && (CustomTrainLoop[x] == 1)) || (CustomTrainID[x] == 0)) {
            if (ContinuousLoopMode[x] == false) {
                killChannel(x);
            }
          }
        }
     }
   }
}
// Convenience Functions


unsigned long SerialReadLong() {
   // Generic routine for getting a 4-byte long int over the serial port
   unsigned long OutputLong = 0;
          inByte = SerialReadByte();
          inByte2 = SerialReadByte();
          inByte3 = SerialReadByte();
          inByte4 = SerialReadByte();
          OutputLong =  makeLong(inByte4, inByte3, inByte2, inByte);
  return OutputLong;
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
  DACValues[outputChannel] = RestingVoltage[outputChannel]; 
  gpio_write_bit(LED_PIN_PORT, OutputLEDLineBits[outputChannel], LOW);
}

void dacWrite(byte DACVal[]) {
      DACBuffer0[1] = DACVal[0];
      spi.write(DACBuffer0,2);
      GPIOB_BASE->BRR = 1<<5; // DAC load pin low
      GPIOB_BASE->BSRR = 1<<5; // DAC load pin high
      DACBuffer1[1] = DACVal[1];
      spi.write(DACBuffer1,2);
      GPIOB_BASE->BRR = 1<<5; // DAC load pin low
      GPIOB_BASE->BSRR = 1<<5; // DAC load pin high
      DACBuffer2[1] = DACVal[2];
      spi.write(DACBuffer2,2);
      GPIOB_BASE->BRR = 1<<5; // DAC load pin low
      GPIOB_BASE->BSRR = 1<<5; // DAC load pin high
      DACBuffer3[1] = DACVal[3];
      spi.write(DACBuffer3,2);
      GPIOB_BASE->BRR = 1<<5; // DAC load pin low
      GPIOB_BASE->BSRR = 1<<5; // DAC load pin high

      GPIOB_BASE->BRR = 1<<6; // DAC latch pin low
      GPIOB_BASE->BRR = 1<<6; // DAC latch pin low (stall for time)
      GPIOB_BASE->BSRR = 1<<6; // DAC latch pin high
}

void UpdateSettingsMenu() {
    ClickerX = analogRead(ClickerXLine);
    ClickerY = analogRead(ClickerYLine);
    ClickerButtonState = ReadDebouncedButton();
    if (ClickerButtonState == 1 && LastClickerButtonState == 0) {
      LastClickerButtonState = 1;
      if (SSactive) {
        SSactive = 0;
        SScount = 0;
        write2Screen(CommanderString," Click for menu");
        delayMicroseconds(100000);
       } else {
        switch(inMenu) {
          case 0: {
            inMenu = 1;
            SelectedChannel = 1;
            write2Screen("Output Channels","<  Channel 1  >");
            NeedUpdate = 1;
          } break;
          case 1: {
            switch(SelectedChannel) {
              case 7: { // Reset
              write2Screen(" "," ");
              delay(1);
                *(SCB_AIRCR) = SCB_AIRCR_RESET;
              } break;
              case 8: {
                inMenu = 0;
                switch (ConnectedToApp) {
                  case 0: {write2Screen(CommanderString," Click for menu");} break;
                  case 1: {write2Screen("MATLAB Connected"," Click for menu");} break;
                }
              } break;
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
            default: {
              inMenu = 2; // output menu
              SelectedAction = 1;
              write2Screen("< Trigger Now  >"," ");
            } break;
           }
         } break;
         case 2: {
          switch (SelectedAction) {
            case 1: {
              inMenu = 3; // soft-trigger menu
              write2Screen("< Single Train >"," ");
              SelectedStimMode = 1;
            } break;
            case 2: {IsBiphasic[SelectedChannel-1] = ReturnUserValue(0, 1, 1, 3);} break; // biphasic (on /off)
            case 3: {Phase1Voltage[SelectedChannel-1] = ReturnUserValue(0, 255, 1, 2);} break; // Get user to input phase 1 voltage
            case 4: {Phase1Duration[SelectedChannel-1] = ReturnUserValue(1, 72000000, 1, 1);} break; // phase 1 duration
            case 5: {InterPhaseInterval[SelectedChannel-1] = ReturnUserValue(1, 72000000, 1, 1);} break; // inter-phase interval
            case 6: {Phase2Voltage[SelectedChannel-1] = ReturnUserValue(0, 255, 1, 2);} break; // Get user to input phase 2 voltage
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
                      DACValues[SelectedChannel-1] = RestingVoltage[SelectedChannel-1]; dacWrite(DACValues); // Update DAC
                      } break; 
            case 18: {
              // Exit to channel menu
            inMenu = 1; RefreshChannelMenu(SelectedChannel);
            } break;
           }
           PulseDuration[SelectedChannel-1] = ComputePulseDuration(IsBiphasic[SelectedChannel-1], Phase1Duration[SelectedChannel-1], InterPhaseInterval[SelectedChannel-1], Phase2Duration[SelectedChannel-1]);
           if (BurstDuration[SelectedChannel-1] == 0) {UsesBursts[SelectedChannel-1] = false;} else {UsesBursts[SelectedChannel-1] = true;}
           if ((SelectedAction > 1) && (SelectedAction < 9)) {
            //EEPROM update channel timer values
            PrepareOutputChannelMemoryPage1(SelectedChannel-1);
            WriteEEPROMPage(PageBytes, 32, ((SelectedChannel-1)*64));
            PrepareOutputChannelMemoryPage2(SelectedChannel-1);
            WriteEEPROMPage(PageBytes, 32, (((SelectedChannel-1)*64)+32));              
           }
          } break;
          case 3: {
          switch (SelectedStimMode) {
            case 1: {
              // Soft-trigger channel
              write2Screen("< Single Train >","      ZAP!");
              delay(100);
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
            case 2: {
              write2Screen("< Single Pulse >","      ZAP!");
              delay(100);
              write2Screen("< Single Pulse >"," ");
              SystemTime = 0;
              if (IsBiphasic[SelectedChannel-1] == 0) {
                DACValues[SelectedChannel-1] = Phase1Voltage[SelectedChannel-1];
                NextPulseTransitionTime[SelectedChannel-1] = SystemTime + Phase1Duration[SelectedChannel-1];
                MicrosTime = micros(); LastLoopTime = MicrosTime;
                dacWrite(DACValues);
                while (NextPulseTransitionTime[SelectedChannel-1] > SystemTime) {
                  while ((MicrosTime-LastLoopTime) < CycleDuration) {  // Make sure loop runs once every 100us 
                    MicrosTime = micros();
                  }
                 LastLoopTime = MicrosTime;
                 SystemTime++; 
                }
                DACValues[SelectedChannel-1] = RestingVoltage[SelectedChannel-1];
                dacWrite(DACValues);
              } else {
                DACValues[SelectedChannel-1] = Phase1Voltage[SelectedChannel-1];
                NextPulseTransitionTime[SelectedChannel-1] = SystemTime + Phase1Duration[SelectedChannel-1];
                MicrosTime = micros(); LastLoopTime = MicrosTime;
                dacWrite(DACValues);
                while (NextPulseTransitionTime[SelectedChannel-1] > SystemTime) {
                  while ((MicrosTime-LastLoopTime) < CycleDuration) {  // Make sure loop runs once every 100us 
                    MicrosTime = micros();
                  }
                 LastLoopTime = MicrosTime;
                 SystemTime++; 
                }
                if (InterPhaseInterval[SelectedChannel-1] > 0) {
                DACValues[SelectedChannel-1] = RestingVoltage[SelectedChannel-1];
                NextPulseTransitionTime[SelectedChannel-1] = SystemTime + InterPhaseInterval[SelectedChannel-1];
                dacWrite(DACValues);
                while (NextPulseTransitionTime[SelectedChannel-1] > SystemTime) {
                  while ((MicrosTime-LastLoopTime) < CycleDuration) {  // Make sure loop runs once every 100us 
                    MicrosTime = micros();
                  }
                 LastLoopTime = MicrosTime;
                 SystemTime++; 
                }
                }
                DACValues[SelectedChannel-1] = Phase2Voltage[SelectedChannel-1];
                NextPulseTransitionTime[SelectedChannel-1] = SystemTime + Phase2Duration[SelectedChannel-1];
                dacWrite(DACValues);
                while (NextPulseTransitionTime[SelectedChannel-1] > SystemTime) {
                  while ((MicrosTime-LastLoopTime) < CycleDuration) {  // Make sure loop runs once every 100us 
                    MicrosTime = micros();
                  }
                 LastLoopTime = MicrosTime;
                 SystemTime++; 
                }
                DACValues[SelectedChannel-1] = RestingVoltage[SelectedChannel-1];
                dacWrite(DACValues);
              }
            } break;
            case 3: {
              if (ContinuousLoopMode[SelectedChannel-1] == false) {
                 write2Screen("<  Continuous  >","      On");
                 ContinuousLoopMode[SelectedChannel-1] = true;
             } else {
                 write2Screen("<  Continuous  >","      Off");
                 ContinuousLoopMode[SelectedChannel-1] = false;
                 PulseStatus[SelectedChannel-1] = 0;
                 BurstStatus[SelectedChannel-1] = 0;
                 StimulusStatus[SelectedChannel-1] = 0;
                 CustomPulseTimeIndex[SelectedChannel-1] = 0;
                 DACValues[SelectedChannel-1] = RestingVoltage[SelectedChannel-1];
                 dacWrite(DACValues);
                 gpio_write_bit(LED_PIN_PORT, OutputLEDLineBits[SelectedChannel-1], LOW);
               }
            } break;
            case 4: {
              inMenu = 2;
              SelectedAction = 1;
              write2Screen("< Trigger Now  >"," ");
            } break;
           }
         } break; 
         case 4: {
          switch (SelectedInputAction) {
            case 1: {
              // Trigger linked output channels
              write2Screen("< Trigger Now >","      ZAP!");
              delay(100);
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
              EEPROM_address = 32;
              for (int x = 0; x < 4; x++) {
                PrepareOutputChannelMemoryPage2(x);
                WriteEEPROMPage(PageBytes, 32, EEPROM_address);
                EEPROM_address = EEPROM_address + 64;
              }
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
      }
      }
    }
      if (ClickerButtonState == 0 && LastClickerButtonState == 1) {
        LastClickerButtonState = 0;
      }
      if (LastClickerXState != 1 && ClickerX < 800) {
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
        if (SelectedInputAction == 0) {SelectedInputAction = 3;}
        if (SelectedChannel == 0) {SelectedChannel = 8;}
        if (SelectedAction == 0) {SelectedAction = 18;}
        if (SelectedStimMode == 0) {SelectedStimMode = 4;}
      }
      if (LastClickerXState != 2 && ClickerX > 3200) {
        LastClickerXState = 2;
        NeedUpdate = 1;
        if (inMenu == 1) {SelectedChannel = SelectedChannel + 1;}
        if (inMenu == 2) {
          if ((IsBiphasic[SelectedChannel-1] == 0) && (SelectedAction == 4)) {
            SelectedAction = SelectedAction + 4;
          } else {
            SelectedAction = SelectedAction + 1;
          }
        }
        if (inMenu == 3) {SelectedStimMode = SelectedStimMode + 1;}
        if (inMenu == 4) {SelectedInputAction = SelectedInputAction + 1;}
        if (SelectedInputAction == 4) {SelectedInputAction = 1;}
        if (SelectedChannel == 9) {SelectedChannel = 1;}
        if (SelectedAction == 19) {SelectedAction = 1;}
        if (SelectedStimMode == 5) {SelectedStimMode = 1;}
      }
      if (LastClickerXState != 0 && ClickerX < 2800 && ClickerX > 1200) {
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
      }
    NeedUpdate = 0;
  }
}
void RefreshChannelMenu(int ThisChannel) {
  switch (SelectedChannel) {
        case 1: {write2Screen("Output Channels","<  Channel 1  >");} break;
        case 2: {write2Screen("Output Channels","<  Channel 2  >");} break;
        case 3: {write2Screen("Output Channels","<  Channel 3  >");} break;
        case 4: {write2Screen("Output Channels","<  Channel 4  >");} break;
        case 5: {write2Screen("Trigger Channels","<  Channel 1  >");} break;
        case 6: {write2Screen("Trigger Channels","<  Channel 2  >");} break;
        case 7: {write2Screen("    -RESET-       ","<Click to reset>");} break;
        case 8: {write2Screen("<Click to exit>"," ");} break;
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
}
void RefreshTriggerMenu(int ThisAction) {
    switch (SelectedInputAction) {
          case 1: {write2Screen("< Trigger Now  >"," ");} break;
          case 2: {write2Screen("< Trigger Mode >",FormatNumberForDisplay(TriggerMode[SelectedChannel-1], 5));} break;
          case 3: {write2Screen("<     Exit     >"," ");} break;
     }
}

void write2Screen(const char* Line1, const char* Line2) {
  lcd.clear(); lcd.home(); lcd.print(Line1); lcd.setCursor(0, 1); lcd.print(Line2);
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
  InputNum = (((InputNum/256)*10)*2 - 10);
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
      if (inMenu == 3) {
        if (InputNum >= 0) {
          if (Bits2Display == 256) {
            sprintf (Value2Display, "%03d bits= +%04.1fV ", Bits2Display, InputNum);
          } else {
            sprintf (Value2Display, "%03d bits= +%04.2fV ", Bits2Display, InputNum);
          }
        } else {
          if (Bits2Display > 0) {
            sprintf (Value2Display, "%03d bits= %4.2fV", Bits2Display, InputNum);
          } else {
            sprintf (Value2Display, "%03d bits= %04.1fV", Bits2Display, InputNum);
          }
        }
      } else {
        if (InputNum >= 0) {
          sprintf (Value2Display, "     %04.2f V", InputNum);
        } else {
          sprintf (Value2Display, "    %05.2f V", InputNum);
        }
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
  unsigned int DebounceTime = millis();
  ClickerButtonState = digitalRead(ClickerButtonLine);
  //ClickerButtonState = gpio_read_bit(INPUT_PIN_PORT, ClickerButtonBit);
    if (ClickerButtonState != lastButtonState) {lastDebounceTime = DebounceTime;}
    lastButtonState = ClickerButtonState;
   if (((DebounceTime - lastDebounceTime) > 75) && (ClickerButtonState == ClickerButtonLogicHigh)) {
      return 1;
   } else {
     return 0;
   }
   
}

unsigned int ReturnUserValue(unsigned long LowerLimit, unsigned long UpperLimit, unsigned long StepSize, byte Units) {
      // This function returns a value that the user chooses by scrolling up and down a number list with the joystick, and clicks to select the desired number.
      // LowerLimit and UpperLimit are the limits for this selection, StepSize is the smallest step size the system will scroll. Units (as for Write2Screen) codes none=0, time=1, volts=2 True/False=3
     unsigned long ValueToAdd = 0;
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
     inMenu = 3; // Temporarily goes a menu layer deeper so leading zeros are displayed by FormatNumberForDisplay
     lcd.setCursor(0, 1); lcd.print("                ");
     delay(100);
     lcd.setCursor(0, 1); lcd.print(FormatNumberForDisplay(UserValue, Units));
     ChoiceMade = 0;
     int ScrollSpeedDelay = 500;
     int Place = 0; 
     byte CursorPos = 0;
     byte CursorPosRightLimit = 0;
     byte CursorPosLeftLimit = 0;
     byte ValidCursorPositions[9] = {0};
     byte Digits[9] = {0};
     int DACBits = pow(2,8);
     int CandidateVoltage = 0; // used to see if voltage will go over limits for DAC
    unsigned int UVTemp = UserValue;
    float FractionalVoltage = 0;
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
      Digits[2] = (UVTemp % 10);
      UVTemp = UVTemp/10;
      Digits[1] = (UVTemp % 10);
      UVTemp = UVTemp/10;
      Digits[0] = (UVTemp % 10);
    }
    
     // Assign valid cursor positions by unit type
     switch(Units) {
       case 0: {ValidCursorPositions[0] = 7;} break;
       case 1: {ValidCursorPositions[0] = 2; ValidCursorPositions[1] = 3; ValidCursorPositions[2] = 4; ValidCursorPositions[3] = 5; ValidCursorPositions[4] = 7; ValidCursorPositions[5] = 8; ValidCursorPositions[6] = 9; ValidCursorPositions[7] = 10; ValidCursorPositions[8] = 11;} break;
       case 2: {ValidCursorPositions[0] = 0; ValidCursorPositions[1] = 1; ValidCursorPositions[2] = 2;} break;
       case 3: {ValidCursorPositions[0] = 7;} break;
       case 4: {ValidCursorPositions[0] = 7;} break;
       case 5: {ValidCursorPositions[0] = 7;} break;
     }
     // Initialize cursor starting positions and limits by unit type
     switch (Units) {
       case 0: {CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;} break; // Format for Index
       case 1: {CursorPos = 3; CursorPosLeftLimit = 0; CursorPosRightLimit = 7;} break; // Format for seconds
       case 2: {CursorPos = 2; CursorPosLeftLimit = 0; CursorPosRightLimit = 2;} break; // Format for volts
       case 3: {CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;} break; // Format for Off/On
       case 4: {CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;} break; // Format for Pulses/Bursts
       case 5: {CursorPos = 0; CursorPosLeftLimit = 0; CursorPosRightLimit = 0;} break; // Format for trigger mode
       }
     unsigned long CursorToggleTimer = 0; 
     unsigned long CursorToggleThreshold = 20000;
     boolean CursorOn = 0;
     delay(75); // Acts as a debounce so user has released pushbutton before reading for an entry
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
       if (ClickerButtonState == ClickerButtonLogicHigh) {
         ChoiceMade = 1;
       }       
       if (ClickerY < 1500) {
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
                if (((CursorPos > 0) && (Digits[CursorPos] < 9)) || (((CursorPos == 0) && (Digits[CursorPos] < 2)))) {
                    if (UserValue < 255) {
                      Digits[CursorPos] = Digits[CursorPos] + 1;
                      CandidateVoltage = 0;
                      CandidateVoltage = CandidateVoltage + (Digits[0]*100);
                      CandidateVoltage = CandidateVoltage + (Digits[1]*10);
                      CandidateVoltage = CandidateVoltage + (Digits[2]*1);
                      
                      if (CandidateVoltage > DACBits) {
                        Digits[CursorPos] = Digits[CursorPos] - 1;
                      } else {
                        UserValue = CandidateVoltage;
                        //dacWrite(SelectedChannel-1, UserValue);
                        delay(1);
                      }
                    }
                } 
            } break;
            default: {
              if (UserValue < UpperLimit) {
                UserValue = UserValue + 1;
              }
            } break;
          }
          ScrollSpeedDelay = 300;
          lcd.noCursor();
          lcd.setCursor(0, 1); lcd.print(FormatNumberForDisplay(UserValue, Units));
       }
      else if (ClickerY > 2500) {
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
              if (Digits[CursorPos] > 0) {
                    if (UserValue > 0) {
                      Digits[CursorPos] = Digits[CursorPos] - 1;
                      CandidateVoltage = 0;
                      CandidateVoltage = CandidateVoltage + (Digits[0]*100);
                      CandidateVoltage = CandidateVoltage + (Digits[1]*10);
                      CandidateVoltage = CandidateVoltage + (Digits[2]*1);

                      if (CandidateVoltage < 0) {
                        Digits[CursorPos] = Digits[CursorPos] + 1;
                      } else {
                        UserValue = CandidateVoltage;
                        //dacWrite(SelectedChannel-1, UserValue);
                        delay(1);
                      }
                    }
                } 
            } break;
            default: {
              if (UserValue > LowerLimit) {
                UserValue = UserValue - 1;
              }
            } break;
          }
          ScrollSpeedDelay = 300;
          lcd.noCursor();
          lcd.setCursor(0, 1); lcd.print(FormatNumberForDisplay(UserValue, Units));
       } else {
         ScrollSpeedDelay = 0;
       }
       if ((ClickerX > 2500) && (CursorPos < CursorPosRightLimit)) {
         CursorPos = CursorPos + 1;
         ScrollSpeedDelay = 300;
         lcd.noCursor();
          lcd.setCursor(0, 1); lcd.print(FormatNumberForDisplay(UserValue, Units));
         lcd.setCursor(ValidCursorPositions[CursorPos], 1); lcd.cursor(); CursorOn = 1; 
       }
       if ((ClickerX < 1500) && (CursorPos > CursorPosLeftLimit)) {
         CursorPos = CursorPos - 1;
         ScrollSpeedDelay = 300;
         lcd.noCursor();
         lcd.setCursor(0, 1); lcd.print(FormatNumberForDisplay(UserValue, Units));
         lcd.setCursor(ValidCursorPositions[CursorPos], 1); lcd.cursor(); CursorOn = 1; 
       }
     delay(ScrollSpeedDelay);  
     }
     lcd.noCursor();
     lcd.setCursor(0, 1); lcd.print("                ");
     if (Units == 5) {
       inMenu = 4;
     } else {
       inMenu = 2;
     }
     delay(200);
     lcd.setCursor(0, 1); lcd.print(FormatNumberForDisplay(UserValue, Units));
     //lcd.noCursor();
     return UserValue;
} 

byte  ReadEEPROM(int EEPROM_address) {
 int data;
 digitalWrite(CS, LOW); // EEPROM enable
 EEPROM.send(READ); //transmit read opcode
 EEPROM.send((uint8)(EEPROM_address>>8)); //send MSByte address first
 EEPROM.send((uint8)(EEPROM_address)); //send LSByte address
 data = EEPROM.send(0xFF); //get data byte
 digitalWrite(CS, HIGH); // EEPROM disable
 return data;
}
void WriteEEPROMPage(byte Content[], byte nBytes, int address) {
 digitalWrite(CS, LOW); // EEPROM enable
 EEPROM.send(WREN); // EEPROM write enable instruction
 digitalWrite(CS, HIGH); // EEPROM disable
 delay(10);
 digitalWrite(CS, LOW); // EEPROM enable
 EEPROM.send(WRITE); // EEPROM write instruction
 EEPROM.send((uint8)(address>>8)); //send MSByte address first
 EEPROM.send((uint8)(address)); //send LSByte address
 for (int i = 0; i < nBytes; i++) {
 EEPROM.send(Content[i]); // EEPROM write data byte
 }
 digitalWrite(CS, HIGH); // EEPROM disable
 delay(5);
}

void PrepareOutputChannelMemoryPage1(byte ChannelNum) {
  // This function organizes a single output channel's parameters into an array in preparation for an EEPROM memory write operation, according to the
  // PulsePal EEPROM Map (see documentation). Each channel is stored in two pages of memory. This function prepares page 1.
  breakLong(Phase1Duration[ChannelNum]);
  PageBytes[0] = BrokenBytes[0]; PageBytes[1] = BrokenBytes[1]; PageBytes[2] = BrokenBytes[2]; PageBytes[3] = BrokenBytes[3]; 
  breakLong(InterPhaseInterval[ChannelNum]);
  PageBytes[4] = BrokenBytes[0]; PageBytes[5] = BrokenBytes[1]; PageBytes[6] = BrokenBytes[2]; PageBytes[7] = BrokenBytes[3]; 
  breakLong(Phase2Duration[ChannelNum]);
  PageBytes[8] = BrokenBytes[0]; PageBytes[9] = BrokenBytes[1]; PageBytes[10] = BrokenBytes[2]; PageBytes[11] = BrokenBytes[3]; 
  breakLong(InterPulseInterval[ChannelNum]);
  PageBytes[12] = BrokenBytes[0]; PageBytes[13] = BrokenBytes[1]; PageBytes[14] = BrokenBytes[2]; PageBytes[15] = BrokenBytes[3]; 
  breakLong(BurstDuration[ChannelNum]);
  PageBytes[16] = BrokenBytes[0]; PageBytes[17] = BrokenBytes[1]; PageBytes[18] = BrokenBytes[2]; PageBytes[19] = BrokenBytes[3]; 
  breakLong(BurstInterval[ChannelNum]);
  PageBytes[20] = BrokenBytes[0]; PageBytes[21] = BrokenBytes[1]; PageBytes[22] = BrokenBytes[2]; PageBytes[23] = BrokenBytes[3]; 
  breakLong(PulseTrainDuration[ChannelNum]);
  PageBytes[24] = BrokenBytes[0]; PageBytes[25] = BrokenBytes[1]; PageBytes[26] = BrokenBytes[2]; PageBytes[27] = BrokenBytes[3]; 
  breakLong(PulseTrainDelay[ChannelNum]);
  PageBytes[28] = BrokenBytes[0]; PageBytes[29] = BrokenBytes[1]; PageBytes[30] = BrokenBytes[2]; PageBytes[31] = BrokenBytes[3]; 
}

void PrepareOutputChannelMemoryPage2(byte ChannelNum) {
  // This function organizes a single output channel's parameters into an array in preparation for an EEPROM memory write operation, according to the
  // PulsePal EEPROM Map (see documentation). Each channel is stored in two pages of memory. This function prepares page 2.
  PageBytes[0] = IsBiphasic[ChannelNum];
  PageBytes[1] = Phase1Voltage[ChannelNum];
  // PageBytes[2] reserved for >8-bit DAC upgrade 
  PageBytes[3] = Phase2Voltage[ChannelNum];
  // PageBytes[4] reserved for >8-bit DAC upgrade
  PageBytes[5] = CustomTrainID[ChannelNum];
  PageBytes[6] = CustomTrainTarget[ChannelNum];
  PageBytes[7] = CustomTrainLoop[ChannelNum];
  PageBytes[8] = TriggerAddress[0][0]; 
  PageBytes[9] = TriggerAddress[0][1];
  PageBytes[10] = TriggerAddress[0][2];
  PageBytes[11] = TriggerAddress[0][3];
  PageBytes[12] = TriggerAddress[1][0];
  PageBytes[13] = TriggerAddress[1][1];
  PageBytes[14] = TriggerAddress[1][2];
  PageBytes[15] = TriggerAddress[1][3];
  PageBytes[16] = TriggerMode[0];
  PageBytes[17] = TriggerMode[1];
  PageBytes[18] = RestingVoltage[ChannelNum]; 
  PageBytes[19] = 0; // To be used in future...
  PageBytes[20] = 0;
  PageBytes[21] = 0;
  PageBytes[22] = 0;
  PageBytes[23] = 0;
  PageBytes[24] = 0;
  PageBytes[25] = 0;
  PageBytes[26] = 0;
  PageBytes[27] = 0;
  PageBytes[28] = 0;
  PageBytes[29] = 0;
  PageBytes[30] = 0;
  PageBytes[31] = 1;
}
void breakLong(unsigned long LongInt2Break) {
  //BrokenBytes is a global array for the output of long int break operations
  BrokenBytes[3] = (byte)(LongInt2Break >> 24);
  BrokenBytes[2] = (byte)(LongInt2Break >> 16);
  BrokenBytes[1] = (byte)(LongInt2Break >> 8);
  BrokenBytes[0] = (byte)LongInt2Break;
}
void RestoreParametersFromEEPROM() {
  // This function is called on Pulse Pal boot, to make pulse pal parameters invariant to power cycles.
  int ChannelMemoryOffset = 0;
   byte PB = 0;
  for (int Chan = 0; Chan < 4; Chan++) {
    ChannelMemoryOffset = 64*Chan;
    PB = 0;
    for (int i = ChannelMemoryOffset; i < (32+ChannelMemoryOffset); i++) {
      PageBytes[PB] = ReadEEPROM(i);
      PB++;
    }
    // Set Channel time parameters
    Phase1Duration[Chan] =  makeLong(PageBytes[3], PageBytes[2], PageBytes[1], PageBytes[0]);
    InterPhaseInterval[Chan] = makeLong(PageBytes[7], PageBytes[6], PageBytes[5], PageBytes[4]);
    Phase2Duration[Chan] = makeLong(PageBytes[11], PageBytes[10], PageBytes[9], PageBytes[8]);
    InterPulseInterval[Chan] = makeLong(PageBytes[15], PageBytes[14], PageBytes[13], PageBytes[12]);
    BurstDuration[Chan] = makeLong(PageBytes[19], PageBytes[18], PageBytes[17], PageBytes[16]);
    BurstInterval[Chan] = makeLong(PageBytes[23], PageBytes[22], PageBytes[21], PageBytes[20]);
    PulseTrainDuration[Chan] = makeLong(PageBytes[27], PageBytes[26], PageBytes[25], PageBytes[24]);
    PulseTrainDelay[Chan] = makeLong(PageBytes[31], PageBytes[30], PageBytes[29], PageBytes[28]);
    PB = 0;
    for (int i = (32+ChannelMemoryOffset); i < (64+ChannelMemoryOffset); i++) {
      PageBytes[PB] = ReadEEPROM(i);
      PB++;
    }
    // Set Channel non-time parameters
    IsBiphasic[Chan] = PageBytes[0];
    Phase1Voltage[Chan] = PageBytes[1];
    Phase2Voltage[Chan] = PageBytes[3];
    CustomTrainID[Chan] = PageBytes[5];
    CustomTrainTarget[Chan] = PageBytes[6];
    CustomTrainLoop[Chan] = PageBytes[7];
    TriggerAddress[0][0] = PageBytes[8]; // TriggerAddress and TriggerMode are stored on every channel and over-written 4 times for programming convenience 
    TriggerAddress[0][1] = PageBytes[9];
    TriggerAddress[0][2] = PageBytes[10];
    TriggerAddress[0][3] = PageBytes[11];
    TriggerAddress[1][0] = PageBytes[12];
    TriggerAddress[1][1] = PageBytes[13];
    TriggerAddress[1][2] = PageBytes[14];
    TriggerAddress[1][3] = PageBytes[15];
    TriggerMode[0] = PageBytes[16];
    TriggerMode[1] = PageBytes[17];
    RestingVoltage[Chan] = PageBytes[18];
  }
  ValidEEPROMProgram = PageBytes[31];
}

void WipeEEPROM() {
  write2Screen("Clearing Memory"," ");
  for (int i = 0; i < 32; i++) {
  PageBytes[i] = 0;
  }
  int WritePagePosition = 0;
  for (int i = 0; i < 512; i++) {
    WriteEEPROMPage(PageBytes, 32, WritePagePosition);
    WritePagePosition = WritePagePosition + 32;
  }
  write2Screen("Clearing Memory","     DONE! ");
  delay(1000);
  write2Screen(CommanderString," Click for menu");
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
      Phase1Voltage[x] = 192;
      Phase2Voltage[x] = 192;
      RestingVoltage[x] = 128;
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
   useScreenSaver = 0;
   SSdelay = 60000;
   // Store default parameters to EEPROM
    EEPROM_address = 0;
    for (int x = 0; x < 4; x++) {
      PrepareOutputChannelMemoryPage1(x);
      WriteEEPROMPage(PageBytes, 32, EEPROM_address);
      EEPROM_address = EEPROM_address + 32;
      PrepareOutputChannelMemoryPage2(x);
      WriteEEPROMPage(PageBytes, 32, EEPROM_address);
      EEPROM_address = EEPROM_address + 32;
    }
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
  while (ClickerButtonState != ClickerButtonLogicHigh) {
    ClickerButtonState = digitalRead(ClickerButtonLine);
    SerialCurrentTime = millis();
    if ((SerialCurrentTime - SerialReadStartTime) > 100) { // Time to flash
      if (FlashState == 0) {
        gpio_write_bit(INPUT_PIN_PORT, InputLEDLineBits[0], LOW);
        gpio_write_bit(INPUT_PIN_PORT, InputLEDLineBits[1], LOW);
        FlashState = 1;
        SerialReadStartTime = millis();
      } else {
        gpio_write_bit(INPUT_PIN_PORT, InputLEDLineBits[0], HIGH);
        gpio_write_bit(INPUT_PIN_PORT, InputLEDLineBits[1], HIGH);
        FlashState = 0;
        SerialReadStartTime = millis();
      }
    }
  }
  gpio_write_bit(INPUT_PIN_PORT, InputLEDLineBits[0], LOW);
  gpio_write_bit(INPUT_PIN_PORT, InputLEDLineBits[1], LOW);
  write2Screen("Loading default","parameters...");
  LoadDefaultParameters();
  delay(2000);
  write2Screen(CommanderString," Click for menu");
}

void AbortAllPulseTrains() {
    for (int x = 0; x < 4; x++) {
      killChannel(x);
    }
    dacWrite(DACValues);
    write2Screen("   PULSE TRAIN","     ABORTED");
    delay(1000);
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
