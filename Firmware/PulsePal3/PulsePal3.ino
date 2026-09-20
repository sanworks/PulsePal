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

// PULSE PAL FIRMWARE for Hardware v2 and v3
//
// ** DEPENDENCIES YOU NEED TO INSTALL FIRST** 

// IF COMPILING FOR PULSE PAL v2 (Also see v3 Dependency Below)
// Pulse Pal v2 requires the sdFat library v1, developed by Bill Greiman. (Thanks Bill!!)
// Download it from here: https://github.com/greiman/SdFat/releases/tag/1.1.4
// and copy it to your /Arduino/libraries folder.

// IF COMPILING FOR PULSE PAL v3
// You need the U8g2_Arduino library, developed by Oliver Kraus. (Thanks Oliver!!)
// You can install it from within Arduino IDE by searching for u8g2 in the Library manager. 
// You can also download it from here: https://github.com/olikraus/U8g2_Arduino
// !!! To work on Teensy, a mod to u8g2/u8x8lib.cpp is required !!!
// In function u8x8_byte_arduino_2nd_hw_spi() approx. line 993, add: #define U8X8_HAVE_2ND_HW_SPI 1

// CODE MAP
// This sketch is split into tabs (the .ino files in this folder). Before compiling, Arduino joins them into a single
// file (this file first, then the others in alphabetical order), so all tabs share the constants and global
// variables defined in this file.
//   PulsePal3.ino    Build configuration, pin maps, named constants, global variables, setup() and loop()
//   Playback.ino     Pulse train playback in the hardware timer callback, handler(). Start here for timing questions.
//   USBOps.ino       Commands from the PC, processUSBCommands()
//   Menu.ino         Thumb joystick menu, UpdateSettingsMenu(), with a map of all menu options, and the editor
//                    for parameter values, ReturnUserValue()
//   SDSettings.ino   Settings files on the microSD card, with the file layout
//   Display.ino      Screen output and splash screen
//   HardwareIO.ino   DAC writes, the hardware timer, fast digital I/O and software reset
// Supporting classes: ArCOM (USB serial data types), LiquidCrystal_U8G2 (Pulse Pal 3 screen)

#define FIRMWARE_VERSION 22

// SETUP MACROS TO COMPILE FOR TARGET DEVICE:
// Both macros can also be set on the compiler command line, e.g. for automated builds:
// arduino-cli compile --build-property compiler.cpp.extra_flags=-DHARDWARE_VERSION=2 ...
#ifndef HARDWARE_VERSION
  #define HARDWARE_VERSION 3 // Use: 2 = Pulse Pal v2.X (as marked on PCB), 3 = Pulse Pal v3.X
#endif

#ifndef PIN_MAP_VERSION
  #define PIN_MAP_VERSION 1 // Hardware pin map. On hardware 3.X use 0 for PCB version < 3.0.4 and 1 for 3.0.5+
                            // PIN_MAP_VERSION Does not affect hardware v2.X.
#endif

// Validate setup macros
#if (HARDWARE_VERSION < 2) || (HARDWARE_VERSION > 3)
#error Error! HARDWARE_VERSION must be either 2 or 3
#endif

#if (PIN_MAP_VERSION < 0) || (PIN_MAP_VERSION > 1)
#error Error! PIN_MAP_VERSION must be either 0 or 1
#endif

#include "SdFat.h"
#include <stdio.h>
#include <stdint.h>
#include <SPI.h>
#include <ctype.h>
#include "ArCOM.h"


#if (HARDWARE_VERSION == 2)
  #include <LiquidCrystal.h>
#else
  #include <U8g2lib.h>
  #include "LiquidCrystal_U8G2.h"
  #include "GFXData.h"
  #include <EEPROM.h>
  #define SD_CONFIG SdioConfig(FIFO_SDIO)
#endif

// Define other macros
#define TIMER_PERIOD 50 // How often the hardware timer refreshes pulse pal. Units = μs
                        // Limited by ~15μs analog read speed for joystick x/y and precision of the joystick UI (0.0000).
                        // On HW3 this may be reduced in the future with the ADC library for fast analog reads + UI mods


#define TriggerLevel 0  // Trigger line level configuration. This defines the logic level when the trigger is activated.
                        // The optoisolator in Pulse Pal 2 is inverting, so its output is high by default, and becomes low 
                        // when voltage is applied to the trigger channel. Set this to 1 if using a non-inverting isolator.

#define STRINGIFY(x) #x // This and the following line enable conversion of macros to strings (e.g. for displaying firmware version)
#define TOSTRING(x) STRINGIFY(x)

// ---------------------------------------------------------------------------------------------------------------
// Named constants. The numeric values are part of the USB protocol, the settings file format or the menu logic,
// so they must never be renumbered. Numbers are listed explicitly so that e.g. "op 91" is easy to find.
// ---------------------------------------------------------------------------------------------------------------

// USB op codes. Every command from the PC is: OpMenuByte (213), op code, then op-specific data.
enum OpCode {
  OP_HANDSHAKE = 72,                  // Returns 'K' and the firmware version
  OP_PROGRAM_ALL_PARAMS_LEGACY = 73,  // Legacy. Replaced by op 92
  OP_PROGRAM_ONE_PARAM = 74,          // Program one parameter on one output or trigger channel
  OP_LOAD_CUSTOM_TRAIN1_LEGACY = 75,  // Legacy. Replaced by op 95
  OP_LOAD_CUSTOM_TRAIN2_LEGACY = 76,  // Legacy. Replaced by op 95
  OP_SOFT_TRIGGER = 77,               // Trigger output channels (1 bit per channel)
  OP_DISPLAY_MESSAGE = 78,            // Show text on the screen
  OP_SET_FIXED_VOLTAGE = 79,          // Set a static voltage on one output channel
  OP_ABORT_ALL = 80,                  // Stop playback on all channels
  OP_DISCONNECT = 81,                 // Disconnect from PC app
  OP_SET_CONTINUOUS_LOOP = 82,        // Set continuous loop mode on one output channel
  OP_SEND_SETTINGS_FILE = 85,         // Return the raw bytes of the current microSD settings file
  OP_DEBUG_WRITE_PIN = 86,            // Development and debugging only
  OP_DEBUG_READ_PIN = 87,             // Development and debugging only
  OP_SET_CLIENT_NAME = 89,            // Set the 6-character client name shown on the top screen
  OP_SD_SETTINGS_FILE = 90,           // Save, load or delete a microSD settings file
  OP_PROGRAM_PARAM_ALL_CHANNELS = 91, // Program one parameter on all 4 output channels
  OP_PROGRAM_ALL_PARAMS = 92,         // Program all parameters
  OP_SEND_CURRENT_PARAMS = 93,        // Return all current parameters
  OP_SEND_HARDWARE_INFO = 94,         // Return hardware version, timer period and custom train limits
  OP_LOAD_CUSTOM_TRAIN = 95,          // Load a custom pulse train
  OP_SET_ZERO_CODE_CALIBRATION = 96,  // Set DAC zero code calibration for one channel (stored in EEPROM on HW3)
  OP_FORMAT_SD_CARD = 97,             // Format the microSD card (HW3 only)
  OP_ABORT_CHANNELS = 98              // Stop playback on specific output channels (1 bit per channel)
};

// Parameter IDs used by ops 74 and 91. These match the parameter codes in the MATLAB and Python interfaces.
enum ParamID {
  PARAM_IS_BIPHASIC = 1,
  PARAM_PHASE1_VOLTAGE = 2,
  PARAM_PHASE2_VOLTAGE = 3,
  PARAM_PHASE1_DURATION = 4,
  PARAM_INTER_PHASE_INTERVAL = 5,
  PARAM_PHASE2_DURATION = 6,
  PARAM_INTER_PULSE_INTERVAL = 7,
  PARAM_BURST_DURATION = 8,
  PARAM_BURST_INTERVAL = 9,
  PARAM_PULSE_TRAIN_DURATION = 10,
  PARAM_PULSE_TRAIN_DELAY = 11,
  PARAM_LINK_TRIGGER1 = 12,
  PARAM_LINK_TRIGGER2 = 13,
  PARAM_CUSTOM_TRAIN_ID = 14,
  PARAM_CUSTOM_TRAIN_TARGET = 15,
  PARAM_CUSTOM_TRAIN_LOOP = 16,
  PARAM_RESTING_VOLTAGE = 17,
  PARAM_CONTINUOUS_LOOP = 18,
  PARAM_TRIGGER_MODE = 128           // Applies to trigger channels, not output channels
};

// Values of inMenu, the current level of the thumb joystick menu. See the menu map above UpdateSettingsMenu().
enum MenuLevel {
  MENU_TOP = 0,                      // Top screen ("Click for menu")
  MENU_CHANNEL_LIST = 1,             // Scroll through output channels, trigger channels, save/load/erase, info, reset, exit
  MENU_OUTPUT_CHANNEL = 2,           // Parameters of one output channel
  MENU_OUTPUT_TRIGGER = 3,           // Manual trigger options for one output channel (also see ReturnUserValue())
  MENU_TRIGGER_CHANNEL = 4,          // Options for one trigger channel
  MENU_FILE_LOAD = 5,
  MENU_FILE_SAVE = 6,
  MENU_FILE_DELETE = 7
};

// Display formats for FormatNumberForDisplay() and ReturnUserValue()
enum DisplayUnits {
  UNITS_INDEX = 0,                   // Plain integer (e.g. custom train number)
  UNITS_TIME = 1,                    // Hardware timer cycles, shown in seconds
  UNITS_VOLTS = 2,                   // 16-bit DAC code, shown in volts
  UNITS_OFF_ON = 3,
  UNITS_PULSES_BURSTS = 4,
  UNITS_TRIGGER_MODE = 5
};

// Values of PulseStatus[], the phase of the pulse currently playing on each output channel
enum PulseStatusValue {
  PULSE_IDLE = 0,                    // Not delivering a pulse (e.g. inter-pulse interval)
  PULSE_PHASE1 = 1,                  // Phase 1 (the only phase of a monophasic pulse)
  PULSE_INTER_PHASE = 2,             // Interval between phases of a biphasic pulse
  PULSE_PHASE2 = 3                   // Phase 2 of a biphasic pulse
};

// Values of TriggerMode[]
enum TriggerModeValue {
  TRIGGER_MODE_NORMAL = 0,           // Low to high transitions start playback, but do not stop it
  TRIGGER_MODE_TOGGLE = 1,           // Low to high transitions start playback, or stop ongoing playback
  TRIGGER_MODE_GATED = 2             // Low to high starts playback, high to low stops it
};

// Values of LineTriggerEvent[]
enum TriggerEventValue {
  TRIGGER_EVENT_NONE = 0,
  TRIGGER_EVENT_LOW_TO_HIGH = 1,
  TRIGGER_EVENT_HIGH_TO_LOW = 2
};

// microSD settings files. See the file layout above SaveCurrentProgram2SD().
#define SETTINGS_FILE_END_MARKER 252 // Last byte of a valid settings file
#define SETTINGS_FILE_N_PARAM_BYTES 178 // Bytes in a settings file before the end marker
#define DEFAULT_SETTINGS_FILE_NAME "default.pps" // Written with default parameters in setup(). The joystick menu lists it
                                                 // first in the load menu, and cannot overwrite or erase it.

#if (HARDWARE_VERSION == 2)
  ArCOM PPUSB(SerialUSB); // Initialize ArCOM USB serial wrapper
  // initialize Arduino LCD library with the numbers of the interface pins
  LiquidCrystal lcd(10, 9, 8, 7, 6, 5);
  byte TriggerLines[2] = {12,11}; // Trigger channels 1 and 2
  byte InputLEDLines[2] = {13, A0}; // LEDs above trigger channels 1-2. An = Arduino Analog Channel n.
  byte OutputLEDLines[4] = {A1,A7,A11,A10}; // LEDs above output channels 1-4
  byte ClickerXLine = A8; // Analog line that reports the thumb joystick x axis
  byte ClickerYLine = A9; // Analog line that reports the thumb joystick y axis
  byte ClickerButtonLine = 15; // Digital line that reports the thumb joystick click state
  byte SyncPin=44; // AD5724 Pin 7 (Sync)
  byte LDACPin=A2; // AD5724 Pin 10 (LDAC)
  byte SDChipSelect=14; // microSD CS Pin 
  byte dacMap[4] = {0,1,2,3}; // Mapping of DAC output pins to output BNC connectors from left to right
  #define N_CUSTOM_PULSE_TRAINS 2
  #define MAX_CUSTOM_PULSES 5000
  #define CURSOR_BLINK_CYCLES 20000 // Joystick menu loop iterations between cursor blinks while editing a value
#else
  ArCOM PPUSB(Serial); // Initialize ArCOM USB serial wrapper
  // initialize u8g2 graphics library with the numbers of the interface pins

  // --- Pin map, to define connections of IC pins ---
  #define CS 17
  #define DC 37
  #define RST 16
  byte TriggerLines[2] = {2,3}; // Trigger channels 1 and 2
  byte InputLEDLines[2] = {1, 4}; // LEDs above trigger channels 1-2.
  byte ClickerXLine = 41; // Analog line that reports the thumb joystick x axis
  byte ClickerYLine = 40; // Analog line that reports the thumb joystick y axis
  byte pcbVersionMap[5] = {18,19,20,21,22}; // Teensy pins that are grounded to encode the PCB minor version in binary
  byte dacMap[4] = {3,2,0,1}; // Mapping of DAC output pins to output BNC connectors from left to right
  
  #if (PIN_MAP_VERSION == 0) // PP3 PCB v 3.0.4 and older
    byte OutputLEDLines[4] = {24,28,29,30}; // LEDs above output channels 1-4
    byte ClickerButtonLine = 34; // Digital line that reports the thumb joystick click state
    byte SyncPin=14; // AD5724 Pin 7 (Sync)
    byte LDACPin=39; // AD5724 Pin 10 (LDAC)
  #elif (PIN_MAP_VERSION == 1) // PP3 PCB v 3.0.5 and newer
    byte OutputLEDLines[4] = {24,32,33,35}; // LEDs above output channels 1-4
    byte ClickerButtonLine = 36; // Digital line that reports the thumb joystick click state
    byte SyncPin=34; // AD5724 Pin 7 (Sync)
    byte LDACPin=28; // AD5724 Pin 10 (LDAC)
  #endif

  // Note: SDChipSelect not required for Pulse Pal v3
  // NOTE! To work on Teensy, this requires a mod to u8g2/u8x8lib.cpp! 
  // In function u8x8_byte_arduino_2nd_hw_spi() approx. line 993, add: #define U8X8_HAVE_2ND_HW_SPI 1
  U8G2_SSD1322_NHD_128X64_F_2ND_4W_HW_SPI u8g2(U8G2_R0, CS, DC, RST);
  LiquidCrystal_U8G2 lcd(u8g2);
  IntervalTimer hardwareTimer; // Built-in hardware timer to ensure even sampling
  #define N_CUSTOM_PULSE_TRAINS 4
  #define MAX_CUSTOM_PULSES 10000
  #define CURSOR_BLINK_CYCLES 10000 // Joystick menu loop iterations between cursor blinks while editing a value
#endif

// Variables for SPI bus
#if (HARDWARE_VERSION == 2)
  SPISettings DACSettings(25000000, MSBFIRST, SPI_MODE2); // Settings for DAC
#else
  SPISettings DACSettings(30000000, MSBFIRST, SPI_MODE2);
#endif

// Parameters that define pulse trains currently loaded on the 4 output channels
// For a visual description of these parameters, see https://sites.google.com/site/pulsepalwiki/parameter-guide
// The following parameters are times in microseconds:
uint32_t Phase1Duration[4] = {0}; // Pulse Duration in monophasic mode, first phase in biphasic mode
uint32_t InterPhaseInterval[4] = {0}; // Interval between phases in biphasic mode (at resting voltage)
uint32_t Phase2Duration[4] = {0}; // Second phase duration in biphasic mode
uint32_t InterPulseInterval[4] = {0}; // Interval between pulses
uint32_t BurstDuration[4] = {0}; // Duration of sequential bursts of pulses (0 if not using bursts)
uint32_t BurstInterval[4] = {0}; // Interval between sequential bursts of pulses (0 if not using bursts)
uint32_t PulseTrainDuration[4] = {0}; // Duration of pulse train
uint32_t PulseTrainDelay[4] = {0}; // Delay between trigger and pulse train onset
// The following are volts in bits. 16 bits span -10V to +10V.
uint16_t Phase1Voltage[4] = {0}; // The pulse voltage in monophasic mode, and phase 1 voltage in biphasic mode
uint16_t Phase2Voltage[4] = {0}; // Phase 2 voltage in biphasic mode.
uint16_t RestingVoltage[4] = {32768}; // Voltage the system returns to between pulses (32768 bits = 0V)
// The following are single byte parameters
uint8_t CustomTrainID[4] = {0}; // If 0, uses above params. If 1 to N_CUSTOM_PULSE_TRAINS, pulse times and voltages are played back from that custom train
uint8_t CustomTrainTarget[4] = {0}; // If 0, custom times define start-times of pulses. If 1, custom times are start-times of bursts.
uint8_t CustomTrainLoop[4] = {0}; // if 0, custom stim plays once. If 1, custom stim loops until PulseTrainDuration.
byte IsBiphasic[4] = {0}; // If 0, the pulse has only phase 1. If 1, phase 1 is followed by the inter-phase interval and phase 2
byte ContinuousLoopMode[4] = {0}; // If true, the channel loops its programmed stimulus train continuously
uint8_t TriggerAddress[2][4] = {0}; // This specifies which output channels get triggered by trigger channel 1 (row 1) or trigger channel 2 (row 2)
uint8_t TriggerMode[2] = {0}; // Normal, toggle or pulse gated mode. See enum TriggerModeValue

// ---------------------------------------------------------------------------------------------------------------
// Output channel parameter table
//
// One row per output channel parameter, describing where the parameter is stored, how the thumb joystick menu
// shows and edits it, and (by its position) its USB parameter code. The joystick menu and paramValueBytes() in
// USBOps.ino both read this table, so a parameter is described in one place.
//
// To add a parameter: add its array and its code to enum ParamID above, add a row here in code order, and add the
// code to menuActionParams below if it should appear in the joystick menu. The USB ops that carry whole parameter
// sets (73, 92 and 93) and the settings file (SDSettings.ino) still list parameters explicitly, because their byte
// layouts are fixed by the protocol. See the menu map above UpdateSettingsMenu() and /Firmware/PROTOCOL.md.
// ---------------------------------------------------------------------------------------------------------------

// Values of OutputParam.type. Each value is the number of bytes the parameter occupies in a USB command.
enum ParamType {
  PARAM_TYPE_BYTE = 1,
  PARAM_TYPE_UINT16 = 2,
  PARAM_TYPE_UINT32 = 4
};

struct OutputParam {
  const char* label;   // Shown on the top line of the screen while the parameter is selected (16 characters)
  void* values;        // The parameter array, with one element per output channel
  uint8_t type;        // See enum ParamType
  uint8_t units;       // Display format, see enum DisplayUnits
  uint32_t minValue;   // Limits used while editing the parameter with the joystick
  uint32_t maxValue;
  bool biphasicOnly;   // The menu skips this parameter when the selected channel is monophasic
};

// Indexed by USB parameter code - 1, so these rows must stay in the order of enum ParamID
const OutputParam outputParams[] = {
  {"<Biphasic Pulse>", IsBiphasic,         PARAM_TYPE_BYTE,   UNITS_OFF_ON,         0, 1,        false},
  {"<Phase1 Voltage>", Phase1Voltage,      PARAM_TYPE_UINT16, UNITS_VOLTS,          0, 65535,    false},
  {"<Phase2 Voltage>", Phase2Voltage,      PARAM_TYPE_UINT16, UNITS_VOLTS,          0, 65535,    true},
  {"<Phase1Duration>", Phase1Duration,     PARAM_TYPE_UINT32, UNITS_TIME,           1, 72000000, false},
  {"<InterPhaseTime>", InterPhaseInterval, PARAM_TYPE_UINT32, UNITS_TIME,           1, 72000000, true},
  {"<Phase2Duration>", Phase2Duration,     PARAM_TYPE_UINT32, UNITS_TIME,           1, 72000000, true},
  {"<Pulse Interval>", InterPulseInterval, PARAM_TYPE_UINT32, UNITS_TIME,           1, 72000000, false},
  {"<Burst Duration>", BurstDuration,      PARAM_TYPE_UINT32, UNITS_TIME,           1, 72000000, false},
  {"<Burst Interval>", BurstInterval,      PARAM_TYPE_UINT32, UNITS_TIME,           1, 72000000, false},
  {"<Train Duration>", PulseTrainDuration, PARAM_TYPE_UINT32, UNITS_TIME,           1, 72000000, false},
  {"< Train Delay  >", PulseTrainDelay,    PARAM_TYPE_UINT32, UNITS_TIME,           1, 72000000, false},
  {"<Link Trigger 1>", TriggerAddress[0],  PARAM_TYPE_BYTE,   UNITS_OFF_ON,         0, 1,        false},
  {"<Link Trigger 2>", TriggerAddress[1],  PARAM_TYPE_BYTE,   UNITS_OFF_ON,         0, 1,        false},
  {"<Custom Train# >", CustomTrainID,      PARAM_TYPE_BYTE,   UNITS_INDEX,          0, N_CUSTOM_PULSE_TRAINS, false},
  {"<Custom Target >", CustomTrainTarget,  PARAM_TYPE_BYTE,   UNITS_PULSES_BURSTS,  0, 1,        false},
  {"< Custom Loop  >", CustomTrainLoop,    PARAM_TYPE_BYTE,   UNITS_OFF_ON,         0, 1,        false},
  {"<RestingVoltage>", RestingVoltage,     PARAM_TYPE_UINT16, UNITS_VOLTS,          0, 65535,    false},
  {"<Playback Mode >", ContinuousLoopMode, PARAM_TYPE_BYTE,   UNITS_OFF_ON,         0, 1,        false}
};
static_assert(sizeof(outputParams) / sizeof(outputParams[0]) == PARAM_CONTINUOUS_LOOP,
              "outputParams needs one row per output parameter code, in the order of enum ParamID");

// The parameters the joystick menu offers, in the order they are scrolled through. Menu action 1 triggers the
// channel and the last action exits, so these are actions 2 to MENU_ACTION_EXIT - 1. Custom train loop and
// playback mode are left out: they are set from the PC only.
const byte menuActionParams[] = {
  PARAM_IS_BIPHASIC,
  PARAM_PHASE1_VOLTAGE,
  PARAM_PHASE1_DURATION,
  PARAM_INTER_PHASE_INTERVAL,
  PARAM_PHASE2_VOLTAGE,
  PARAM_PHASE2_DURATION,
  PARAM_INTER_PULSE_INTERVAL,
  PARAM_BURST_DURATION,
  PARAM_BURST_INTERVAL,
  PARAM_PULSE_TRAIN_DELAY,
  PARAM_PULSE_TRAIN_DURATION,
  PARAM_LINK_TRIGGER1,
  PARAM_LINK_TRIGGER2,
  PARAM_CUSTOM_TRAIN_ID,
  PARAM_CUSTOM_TRAIN_TARGET,
  PARAM_RESTING_VOLTAGE
};

const byte MENU_ACTION_TRIGGER = 1; // Output channel menu: trigger this channel
const byte MENU_ACTION_FIRST_PARAM = 2; // First action that edits a parameter from menuActionParams
const byte MENU_ACTION_EXIT = MENU_ACTION_FIRST_PARAM + sizeof(menuActionParams); // Last action: back to the channel list

// Variables used in programming
byte OpMenuByte = 213; // This byte must be the first byte in any serial transmission to Pulse Pal. Reduces the probability of interference from port-scanning software
unsigned long CustomTrainNpulses[N_CUSTOM_PULSE_TRAINS] = {0}; // Stores the total number of pulses in the custom pulse train
int SerialCurrentTime = 0; // Current time (millis), used while flashing the LEDs of a comm failure
int SerialReadStartTime = 0; // Time the comm failure message started

// Variables used to parse USB commands
byte inByte; byte inByte2; byte inByte3; byte inByte4; byte CommandByte;
byte LogicLevel = 0;

// Variables used in stimulus playback
unsigned long SystemTime = 0; // Number of cycles since stimulation start
unsigned long PrePulseTrainTimestamps[4] = {0};
unsigned long PulseTrainTimestamps[4] = {0};
unsigned long NextPulseTransitionTime[4] = {0}; // Stores next pulse-high or pulse-low timestamp for each channel
unsigned long NextBurstTransitionTime[4] = {0}; // Stores next burst-on or burst-off timestamp for each channel
unsigned long PulseTrainEndTime[4] = {0}; // Stores time the stimulus train is supposed to end
#if (HARDWARE_VERSION == 2)
  uint32_t CustomPulseTimes[N_CUSTOM_PULSE_TRAINS][MAX_CUSTOM_PULSES+1] = {0};
  uint16_t CustomVoltages[N_CUSTOM_PULSE_TRAINS][MAX_CUSTOM_PULSES+1] = {0};
#else
  DMAMEM uint32_t CustomPulseTimes[N_CUSTOM_PULSE_TRAINS][MAX_CUSTOM_PULSES+1] = {0};
  DMAMEM uint16_t CustomVoltages[N_CUSTOM_PULSE_TRAINS][MAX_CUSTOM_PULSES+1] = {0};
#endif
int CustomPulseTimeIndex[4] = {0}; // Keeps track of the pulse number of the custom train currently being played on each channel
byte PulseStatus[4] = {0}; // Phase of the current pulse on each channel. See enum PulseStatusValue
boolean BurstStatus[4] = {0}; // This is "true" during bursts and false during inter-burst intervals.
boolean StimulusStatus[4] = {0}; // This is "true" for a channel when the stimulus train is actively being delivered
boolean PreStimulusStatus[4] = {0}; // This is "true" for a channel during the pre-stimulus delay
boolean InputValues[2] = {0}; // The values read directly from the two inputs (for analog, digital equiv. after thresholding)
boolean InputValuesLastCycle[2] = {0}; // The values on the last cycle. Used to detect low to high transitions.
byte LineTriggerEvent[2] = {0}; // Trigger line transition detected this cycle. See enum TriggerEventValue
boolean UsesBursts[4] = {0};
byte ContinuousLoopModeOriginal[4] = {0}; // Memory for previous continuous loop mode state
byte StimulatingState = 0; // 1 if ANY channel is stimulating, 2 if this is the first cycle after the system was triggered. 
byte LastStimulatingState = 0;
boolean DACFlag = 0; // true if any DAC channel needs to be updated
byte DefaultInputLevel = 0; // 0 for PulsePal 0.3, 1 for 0.2 and 0.1. Logic is inverted by optoisolator

// microSD and file management variables
uint8_t buf[1];
uint8_t buf4[4];
#if (HARDWARE_VERSION < 3)
  SdFat sd;
  SdFile settingsFile;
  SdFile candidateSettingsFile;
#else
  SdFs sd;
  FsFile root;
  FsFile settingsFile;
  FsFile candidateSettingsFile;
  uint8_t mountOK = 0;
#endif
String currentSettingsFileName = DEFAULT_SETTINGS_FILE_NAME; // Used to build file names received from USB or entered with the joystick
byte settingsFileNameLength = 0; // Set when a new file name is entered
char currentSettingsFileNameChar[100]; // Filename must be converted from string to character array for use with sdFAT
char candidateSettingsFileChar[17];
byte settingsOp = 0; // Reports whether to load an existing settings file, or create/overwrite, or delete
byte validProgram = 0; // Reports whether the program just loaded from the SD card is valid 
uint16_t myFilePos = 2; // Selected position in the file load, save and erase menus. See the menu map above UpdateSettingsMenu()

// variables used in thumb joystick menus
char Value2Display[18] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0'}; // Holds text for sprintf
int ScrollSpeedDelay = 200000; // Microseconds before scrolling values while joystick is held in one direction
byte CursorPos = 0;
byte CursorPosRightLimit = 0;
byte CursorPosLeftLimit = 0;
byte ValidCursorPositions[9] = {0};
int Digits[9] = {0};
unsigned int DACBits = pow(2,16);
unsigned long CursorToggleTimer = 0; 
unsigned long CursorToggleThreshold = CURSOR_BLINK_CYCLES;
boolean CursorOn = 0;
int ClickerX = 0; // Value of analog reads from X line of joystick input device
int ClickerY = 0; // Value of analog reads from Y line of joystick input device
int ClickerMinThreshold = 300; // Joystick position to consider an upwards or leftwards movement (on Y and X lines respectively)
int ClickerMaxThreshold = 700;
boolean ClickerButtonState = 0; // Value of digital reads from button line of joystick input device
boolean LastClickerButtonState = 1;
unsigned int DebounceTime = 0; // Time since the joystick button changed states
int LastClickerXState = 0; // 0 for neutral, 1 for left, 2 for right.
int inMenu = MENU_TOP; // Current menu level. See enum MenuLevel
int SelectedChannel = 0; // Channel the user has selected
int SelectedAction = 1; // Action the user has selected
byte isNegativeZero = 0; // Keeps track of negative zero in digit-wise voltage adjustment menu
byte SelectedInputAction = 1; // Trigger channel action the user has selected
int SelectedStimMode = 1; // Manual trigger from joystick menu. 1 = Single train, 2 = Single pulse, 3 = continuous stimulation
int lastDebounceTime = 0; // to debounce the joystick button
boolean lastButtonState = 0; // last logic state of joystick button
boolean ChoiceMade = 0; // determines whether user has chosen a value from a list
boolean viewingInfo = false; // True if viewing system info
unsigned int UserValue = 0; // The current value displayed on a list of values (written to LCD when choosing parameters)
#if (HARDWARE_VERSION == 2)
  char CommanderString[16] = " PULSE PAL v2.0"; // Displayed at the menu top when disconnected from software
  char DefaultCommanderString[16] = " PULSE PAL v2.0"; // The CommanderString can be overwritten. This stores the original.
#else
  char CommanderString[16] = " PULSE PAL v3.0"; // Displayed at the menu top when disconnected from software
  char DefaultCommanderString[16] = " PULSE PAL v3.0"; // The CommanderString can be overwritten. This stores the original.
#endif
char ClientStringSuffix[11] = " Connected"; // Displayed after 6-character client ID string (as in, "MATLAB Connected")
char centeredText[17] = {0}; // Global for returning centered text to display on a 16-char screen line
byte fileNameOffset = 0; // Offset of centered string (for display on 16-char screen)
char tempText[17] = {0}; // Temporary buffer for holding a file name or other text
boolean NeedUpdate = 0; // If a new menu item is selected, the screen must be updated
uint32_t PulseTrainDuration_ExamplePulse[4] = {0};

// DAC variables
boolean DACFlags[4] = {0}; // Flag to indicate whether each output channel needs to be updated in a call to dacWrite()
byte dacBuffer[3] = {0}; // Holds bytes about to be written via SPI (for improved transfer speed with array writes)
int16_t ZeroCodeCalibration[4] = {0}; // Calibration for zero-code error of the DAC
union { // dacValue contains a single sample of raw 16-bit data to be written on each DAC channel
    byte byteArray[8];
    uint16_t uint16[4];
} dacValue; // Union allows faster type conversion between 16-bit DAC values and bytes to write via SPI

// Other variables
unsigned int CycleFrequency = 20000; // in Hz, derived in the setup from TIMER_PERIOD
void handler(void);
boolean SoftTriggered[4] = {0}; // If a software trigger occurred this cycle (for timing reasons, it is scheduled to occur on the next cycle)
boolean SoftTriggerScheduled[4] = {0}; // If a software trigger is scheduled for the next cycle
volatile byte usbLoadTarget = 0;
volatile boolean usbLoadFlag = 0;
volatile boolean abortRequested = 0; // Set by handler() when the joystick button stops playback. loop() then shows the message.
union { // typeCast converts bytes read from the microSD card to 16 and 32-bit integers
    byte byteArray[4];
    uint16_t uint16;
    uint32_t uint32;
} typeCast; // Union allows faster type conversion than a bit-shift macro

void setup() {
  pinMode(SyncPin, OUTPUT); // Configure SPI bus pins as outputs
  pinMode(LDACPin, OUTPUT);
  SPI.begin();
  SPI.beginTransaction(DACSettings);
  digitalWriteDirect(LDACPin, LOW);
  digitalWriteDirect(SyncPin, HIGH);
  #if (HARDWARE_VERSION > 2)
    EEPROM.get(0, ZeroCodeCalibration); //Read the Zero code calibration from the EEPROM
  #endif
  ProgramDAC(28, 0, 0); // Clear DAC register
  ProgramDAC(12, 0, 4); // Set DAC output range to +/- 10V
  // Set DAC to resting voltage on all channels
  for (int i = 0; i < 4; i++) {
    RestingVoltage[i] = 32768; // 16-bit code for 0, in the range of -10 to +10
    setDAC(i, RestingVoltage[i]);
  }
  ProgramDAC(16, 0, 31); // Power up DACs
  dacWrite(); // Update the DAC. This is the only dacWrite() call outside handler(), because the hardware timer has not started.

  #if (HARDWARE_VERSION == 2)
    SerialUSB.begin(115200); // Initialize Serial USB interface at 115.2kbps
  #endif

  // set up the LCD
  #if (HARDWARE_VERSION == 3)
    u8g2.begin();
    // Use the SSD1322's internal VSL. u8g2's init selects external VSL (0xB4, 0xA0), which this module doesn't support.
    // With external VSL, every lit row leaves a dim ghost on the row below it.
    u8g2.sendF("caa", 0xB4, 0xA2, 0xFD);
    runSplashScreen();
    u8g2.setContrast(128); // Brightness of oLED display. Use 128 max (of 256) because:
                          // 1. Higher values can draw excess current from the USB supply. 2. To extend the lifetime of the display
  #endif
  lcd.begin(16, 2);
  lcd.clear();
  lcd.home();
  lcd.noDisplay();
  delay(100);
  lcd.display();

  // Pin modes
  pinMode(TriggerLines[0], INPUT); // Configure trigger pins as digital inputs
  pinMode(TriggerLines[1], INPUT);
  pinMode(ClickerButtonLine, INPUT_PULLUP); // Configure clicker button as digital input with an internal pullup resistor
 
  for (int i = 0; i < 2; i++) {
    pinMode(InputLEDLines[i], OUTPUT);
    digitalWrite(InputLEDLines[i], LOW);
  }

  for (int i = 0; i < 4; i++) {
    pinMode(OutputLEDLines[i], OUTPUT); // Configure channel LED pins as outputs
    digitalWrite(OutputLEDLines[i], LOW); // Initialize channel LEDs to low (off)
  }

  // microSD setup
  #if (HARDWARE_VERSION == 2)
    delay(100);
    pinMode(SDChipSelect, OUTPUT);
    if (!sd.begin(SDChipSelect, SPI_FULL_SPEED)) {
      sd.initErrorHalt();
    }
  #else
    if (!sd.begin(SD_CONFIG)) {
      write2Screen("Startup Failed:"," SD Card ERROR");
      sd.initErrorHalt();
    }
  #endif
  if (!sd.chdir("Pulse_Pal")) { // Create/enter a unique folder for Pulse Pal
    sd.mkdir("Pulse_Pal");
    sd.chdir("Pulse_Pal");
  }
  #if (HARDWARE_VERSION > 2)
    if (!root.open("/Pulse_Pal")) {
      write2Screen("Startup Error:"," Need SD Format");
    }
  #endif
  // Start with default parameters, and write them to the default settings file. The joystick menu cannot overwrite
  // or erase this file, so the default parameters can always be loaded from it.
  LoadDefaultParameters();
  SaveCurrentProgram2SD();

  write2Screen(CommanderString," Click for menu");
  DefaultInputLevel = 1 - TriggerLevel;
  InputValuesLastCycle[0] = digitalRead(TriggerLines[0]); // Pre-read trigger channels
  InputValuesLastCycle[1] = digitalRead(TriggerLines[1]);
  SystemTime = 0;
  CycleFrequency = 1.0/(TIMER_PERIOD/1000000.0); // Given as decimals to force floating point arithmetic
  startHardwareTimer();
}

// loop() runs the joystick menu (only while no channel is playing), and handles commands from the PC.
// All pulse train playback is done in handler().
void loop() {
    if (StimulatingState == 0) {
      UpdateSettingsMenu();
    }
    processUSBCommands(); // Read and execute a command from the PC, if one is available
  if (usbLoadFlag) {
    loadCustomPulseTrain(usbLoadTarget);
  }
  usbLoadFlag = false;
  PPUSB.flush(); // Send any reply from this pass as a single USB packet
  if (abortRequested) { // handler() stopped playback because the joystick button was pressed
    abortRequested = false;
    ShowAbortMessage();
  }
  if (PPUSB.timedOut()) { // A serial USB message started, but didn't finish as expected
    stopHardwareTimer();
    HandleReadTimeout(); // Notifies user of error, then prompts to click and restores DEFAULT channel settings.
    PPUSB.clearTimedOut();
    startHardwareTimer();
  }
}
