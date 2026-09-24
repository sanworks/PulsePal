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

// WAVE PAL FIRMWARE for Pulse Pal 3 hardware
//
// Wave Pal turns a Pulse Pal 3 into a four channel waveform player. Each output channel has one waveform of up to
// 1 million samples, stored on the microSD card, and plays it when triggered: by a TTL edge on a trigger channel, by
// a command from the PC, or from the thumb joystick menu. The sampling rate (up to 100kHz) and the output range are
// set for the whole device. It is the WavePlayer firmware for the Bpod Analog Output Module (BpodWavePlayer.ino),
// ported to Pulse Pal 3 hardware, with the trigger channels in place of the Bpod UART.
// The USB protocol is documented in PROTOCOL.md in this folder, and AGENTS.md lists the rules for changing this code.
//
// ** DEPENDENCIES YOU NEED TO INSTALL FIRST **
// Board: Teensy 4.1, from Teensyduino. Library: U8g2_Arduino, developed by Oliver Kraus (Thanks Oliver!!), from the
// Arduino library manager (verified with v2.36.19). SdFat comes with the Teensy core.
//
// CODE MAP
// This sketch is split into tabs. Arduino joins them into a single file before compiling (this file first, then the
// others in alphabetical order), so all tabs share the constants and global variables defined in this file.
//   WavePal.ino      Build configuration, pin map, named constants, global variables, setup() and loop()
//   Playback.ino     The sample clock interrupt handler(), the trigger inputs, and starting and stopping channels.
//                    Start here for timing questions: "How playback works" is at the top.
//   Storage.ino      The waveform data file on the microSD card, and refilling the playback buffers from it
//   USBOps.ino       Commands from the PC, processUSBCommands(), and loading waveforms
//   Menu.ino         Thumb joystick menu, with a map of its options
//   Display.ino      Screen output and splash screen
//   HardwareIO.ino   DAC writes, the output range and software reset
// Supporting classes: ArCOM (USB serial data types), LiquidCrystal_U8G2 (screen). Both are copies of the ones in
// /Firmware/PulsePal3.

#define FIRMWARE_VERSION 1

// SETUP MACRO TO COMPILE FOR TARGET DEVICE:
#ifndef PIN_MAP_VERSION
  #define PIN_MAP_VERSION 1 // Hardware pin map. Use 0 for Pulse Pal PCB version < 3.0.4 and 1 for 3.0.5+
#endif

#if (PIN_MAP_VERSION < 0) || (PIN_MAP_VERSION > 1)
#error Error! PIN_MAP_VERSION must be either 0 or 1
#endif

#if !defined(ARDUINO_TEENSY41)
#error Wave Pal runs on Pulse Pal 3 hardware only. Select Tools > Board > Teensy 4.1.
#endif

#define HARDWARE_VERSION 3 // Pulse Pal hardware version. Wave Pal runs on Pulse Pal 3 only. ArCOM.h reads this.

#include "SdFat.h"
#include <SPI.h>
#include <EEPROM.h>
#include <U8g2lib.h>
// The screen is on the Teensy's second SPI bus. u8g2 compiles its second-bus driver only when the core defines
// SPI_INTERFACES_COUNT > 1, which Teensyduino does. If that ever stops being true the driver becomes an empty stub:
// the sketch still builds, and the screen stays dark. Stop the build instead.
#if !defined(U8X8_HAVE_2ND_HW_SPI)
  #error u8g2 has no second hardware SPI. The Pulse Pal 3 screen will not work.
#endif
#include "ArCOM.h"
#include "LiquidCrystal_U8G2.h"
#include "GFXData.h"

#define STRINGIFY(x) #x // This and the following line enable conversion of macros to strings (e.g. for displaying firmware version)
#define TOSTRING(x) STRINGIFY(x)

// ---------------------------------------------------------------------------------------------------------------
// Named constants. The numeric values of the op codes, trigger modes and output ranges are part of the USB protocol
// (PROTOCOL.md), so they must never be renumbered.
// ---------------------------------------------------------------------------------------------------------------

// USB op codes. Every command from the PC is: OpMenuByte (213), op code, then op-specific data. Where WavePlayer has
// the same op, the op code is its ASCII letter. Ops 72, 81 and 89 are those of Pulse Pal firmware.
enum OpCode {
  OP_HANDSHAKE = 72,                  // Returns HANDSHAKE_REPLY and the firmware version
  OP_DISCONNECT = 81,                 // The client is closing: show the device's own name on the screen again
  OP_SET_CLIENT_NAME = 89,            // Set the 6-character client name shown on the top screen, as "NAME Connected"
  OP_HARDWARE_INFO = 'N',             // 78. Returns the hardware properties
  OP_SET_SAMPLING_RATE = 'S',         // 83. Sampling rate of all output channels, in Hz
  OP_SET_OUTPUT_RANGE = 'R',          // 82. Output range of all output channels. See enum OutputRange
  OP_LOAD_WAVEFORM = 'L',             // 76. Load one output channel's waveform
  OP_PLAY = 'P',                      // 80. Soft-trigger output channels (1 bit per channel)
  OP_STOP = 'X',                      // 88. Stop output channels (1 bit per channel)
  OP_SET_FIXED_VOLTAGE = '!',         // 33. Hold output channels at a fixed DAC code (1 bit per channel)
  OP_SET_LOOP_MODE = 'O',             // 79. Loop mode of each output channel
  OP_SET_LOOP_DURATION = 'D',         // 68. Loop duration of each output channel, in samples
  OP_SET_TRIGGER_MODE = 'T',          // 84. Trigger mode of each output channel. See enum TriggerModeValue
  OP_SET_TRIGGER_LINKS = 'I',         // 73. Links from the trigger channels to the output channels
  OP_GET_STATUS = 'G',                // 71. Returns the playback state
  OP_GET_PLAYBACK_CHECKSUMS = 'Z'     // 90. For testing: samples played since each channel started, and their sum
};
#define HANDSHAKE_REPLY 'W' // 87. Pulse Pal firmware replies 'K' (75) to op 72, so clients can tell the two apart

// Values of triggerMode[]. What a trigger does to an output channel: see triggerChannels() in Playback.ino.
enum TriggerModeValue {
  TRIGGER_MODE_NORMAL = 0,            // A trigger starts the waveform. Triggers during playback are ignored.
  TRIGGER_MODE_MASTER = 1,            // A trigger starts the waveform, or restarts it from the first sample
  TRIGGER_MODE_TOGGLE = 2,            // A trigger starts the waveform, or stops it if it is playing
  TRIGGER_MODE_GATED = 3              // A rising edge starts the waveform, and a falling edge stops it
};
#define MAX_TRIGGER_MODE TRIGGER_MODE_GATED

// Values of rangeIndex. These are WavePlayer's ranges without its 12V ones, which need the Analog Output Module's
// external 3V reference (Pulse Pal 3 has none), so the indices differ from WavePlayer's. The unipolar ranges must stay
// below the bipolar ones: setOutputRange() relies on it.
enum OutputRange {
  RANGE_0_TO_5V = 0,
  RANGE_0_TO_10V = 1,
  RANGE_PLUS_MINUS_5V = 2,
  RANGE_PLUS_MINUS_10V = 3            // Pulse Pal's range, in which the zero code calibration was measured
};
#define N_OUTPUT_RANGES 4
const byte dacRangeCodes[N_OUTPUT_RANGES] = {0, 1, 3, 4}; // The AD5754R's output range select code for each range

// Values of inMenu, the current level of the thumb joystick menu. See the menu map in Menu.ino.
enum MenuLevel {
  MENU_TOP = 0,                       // "Wave Pal v3.0 / Click for menu"
  MENU_LIST = 1,                      // Scroll through the output channels, device info, reboot and exit
  MENU_DEVICE_INFO = 2                // Hardware and firmware versions
};

// Values of menuItem, the option selected in MENU_LIST. Items 1-4 are the output channels.
enum MenuItem {
  MENU_ITEM_DEVICE_INFO = 5,
  MENU_ITEM_REBOOT = 6,
  MENU_ITEM_EXIT = 7
};

#define N_CHANNELS 4
#define MAX_WAVE_SAMPLES 1000000 // Samples per waveform
#define MAX_SAMPLING_RATE 100000 // Hz
#define DEFAULT_SAMPLING_RATE 10000 // Hz
#define BUFFER_SAMPLES 16384 // Samples per playback buffer: 32kB, 164ms at 100kHz. Each channel has a pre-buffer and
                             // two playback buffers of this size. See "How playback works" in Playback.ino.
#define BUFFER_BYTES (BUFFER_SAMPLES * 2)
#define WAVE_REGION_BYTES 2097152UL // Each channel's region of the data file (2 MiB). A whole number of buffers, so every
                                    // block read from the card starts on a 512 byte sector boundary.
#define WAVE_FILE_BYTES (N_CHANNELS * WAVE_REGION_BYTES)
#define WAVE_FOLDER "/Wave_Pal"
#define WAVE_FILE_NAME "/Wave_Pal/WaveData.wfm"
#define TIMER_CLOCK_HZ 24000000 // IntervalTimer counts cycles of this clock, so the sample period is a whole number of them
#define PLAYBACK_IRQ_PRIORITY 64 // Priority of the sample clock and trigger interrupts. Both must be equal, so that neither
                                 // interrupts the other in the middle of a DAC write. It is above the USB (128) and
                                 // microSD (96) interrupts, so a USB transfer cannot delay a sample.
#define CHUNK_NONE -1 // Value of bufferChunk[][] for a playback buffer that holds no usable block
#define ALL_CHANNELS 0x0F // Channel bits for output channels 1-4

static_assert(MAX_WAVE_SAMPLES * 2 <= WAVE_REGION_BYTES, "A channel's region of the data file must hold a whole waveform");
static_assert(WAVE_REGION_BYTES % BUFFER_BYTES == 0, "Blocks must not straddle a channel region boundary");
static_assert(BUFFER_BYTES % 512 == 0, "Blocks must be whole microSD sectors");

#define TriggerLevel 0  // Trigger line level when the trigger is active. The optoisolators are inverting: their output is
                        // high by default, and goes low when voltage is applied to the trigger channel.

// --- Pin map, to define connections of IC pins (as in Pulse Pal 3 firmware) ---
#define CS 17
#define DC 37
#define RST 16
constexpr byte TriggerLines[2] = {2, 3}; // Trigger channels 1 and 2
constexpr byte InputLEDLines[2] = {1, 4}; // LEDs above trigger channels 1-2
constexpr byte ClickerXLine = 41; // Analog line that reports the thumb joystick x axis
constexpr byte ClickerYLine = 40; // Analog line that reports the thumb joystick y axis
constexpr byte dacMap[4] = {3, 2, 0, 1}; // Mapping of DAC output pins to output BNC connectors from left to right
#if (PIN_MAP_VERSION == 0) // PP3 PCB v 3.0.4 and older
  constexpr byte OutputLEDLines[4] = {24, 28, 29, 30}; // LEDs above output channels 1-4
  constexpr byte ClickerButtonLine = 34; // Digital line that reports the thumb joystick click state
  constexpr byte SyncPin = 14; // AD5754R Pin 7 (Sync)
  constexpr byte LDACPin = 39; // AD5754R Pin 10 (LDAC)
#else // PP3 PCB v 3.0.5 and newer
  constexpr byte OutputLEDLines[4] = {24, 32, 33, 35};
  constexpr byte ClickerButtonLine = 36;
  constexpr byte SyncPin = 34;
  constexpr byte LDACPin = 28;
#endif

// Hardware objects
ArCOM PPUSB(Serial); // ArCOM USB serial wrapper. The name is kept from Pulse Pal firmware, so that ArCOM.h is shared.
// 2ND in the constructor name selects the second SPI bus, checked for above with U8X8_HAVE_2ND_HW_SPI
U8G2_SSD1322_NHD_128X64_F_2ND_4W_HW_SPI u8g2(U8G2_R0, CS, DC, RST);
LiquidCrystal_U8G2 lcd(u8g2);
IntervalTimer hardwareTimer; // The sample clock. It runs only while a channel is playing. See Playback.ino.
SPISettings DACSettings(30000000, MSBFIRST, SPI_MODE2); // The AD5754R's maximum SPI clock is 30MHz
SdFs sd;
FsFile waveFile; // The waveform data file. See Storage.ino.

// ---------------------------------------------------------------------------------------------------------------
// Settings. Programmed from USB, and read by the playback interrupts. See PROTOCOL.md.
// ---------------------------------------------------------------------------------------------------------------
uint32_t samplingRate = DEFAULT_SAMPLING_RATE; // Hz
double samplePeriodMicros = 100; // The sample period for IntervalTimer. Set with samplingRate by setSamplingRate().
byte rangeIndex = RANGE_PLUS_MINUS_10V; // See enum OutputRange
uint16_t DACBits_ZeroVolts = 32768; // DAC code for 0V in the current range: 32768 in the bipolar ranges, 0 in the unipolar ones
volatile byte loopMode[N_CHANNELS] = {0}; // If 1, the channel loops its waveform
volatile uint32_t loopDuration[N_CHANNELS] = {0}; // In loop mode, samples to play before stopping. 0 = loop until stopped.
volatile byte triggerMode[N_CHANNELS] = {0}; // See enum TriggerModeValue
volatile byte TriggerAddress[2][N_CHANNELS] = {0}; // Output channels triggered by trigger channel 1 (row 1) and 2 (row 2)

// ---------------------------------------------------------------------------------------------------------------
// Waveforms and playback buffers. See "How playback works" in Playback.ino.
// ---------------------------------------------------------------------------------------------------------------
volatile uint32_t nSamples[N_CHANNELS] = {0}; // Samples in each channel's waveform. 0 = no waveform: triggers are ignored.
uint16_t preBuffer[N_CHANNELS][BUFFER_SAMPLES]; // The first BUFFER_SAMPLES of each waveform, kept in RAM from the moment it
                                                // is loaded, so that playback can start without waiting for the card
// Two playback buffers per channel: while one plays, the next block of the waveform is read from the card into the
// other. They are in RAM2 (DMAMEM), which is not zeroed at startup; nothing is played from a buffer before it is filled.
DMAMEM uint16_t channel1BufferA[BUFFER_SAMPLES];
DMAMEM uint16_t channel1BufferB[BUFFER_SAMPLES];
DMAMEM uint16_t channel2BufferA[BUFFER_SAMPLES];
DMAMEM uint16_t channel2BufferB[BUFFER_SAMPLES];
DMAMEM uint16_t channel3BufferA[BUFFER_SAMPLES];
DMAMEM uint16_t channel3BufferB[BUFFER_SAMPLES];
DMAMEM uint16_t channel4BufferA[BUFFER_SAMPLES];
DMAMEM uint16_t channel4BufferB[BUFFER_SAMPLES];
uint16_t* const playbackBuffers[N_CHANNELS][2] = {
  {channel1BufferA, channel1BufferB},
  {channel2BufferA, channel2BufferB},
  {channel3BufferA, channel3BufferB},
  {channel4BufferA, channel4BufferB}
};
volatile int32_t bufferChunk[N_CHANNELS][2] = {{CHUNK_NONE, CHUNK_NONE}, {CHUNK_NONE, CHUNK_NONE},
                                               {CHUNK_NONE, CHUNK_NONE}, {CHUNK_NONE, CHUNK_NONE}};
                                               // The block ("chunk") of the waveform each playback buffer holds, or CHUNK_NONE

// Playback state of each output channel. Changed by the playback interrupts, and by loop() with interrupts disabled.
volatile boolean playing[N_CHANNELS] = {0}; // True while the channel plays its waveform
volatile boolean stopAfterWrite[N_CHANNELS] = {0}; // The waveform has ended: 0V is waiting to be written, then the channel stops
volatile uint32_t wavePos[N_CHANNELS] = {0}; // Index in the waveform of the next sample to fetch
volatile uint32_t playChunk[N_CHANNELS] = {0}; // Block of the waveform that holds the next sample. 0 = the pre-buffer.
volatile uint32_t chunkPos[N_CHANNELS] = {0}; // Position of the next sample in its block
volatile uint32_t samplesPlayed[N_CHANNELS] = {0}; // Samples fetched since the channel was triggered (for the loop duration)
volatile uint32_t sampleSum[N_CHANNELS] = {0}; // Sum of the DAC codes fetched since the channel was triggered, wrapping.
                                               // Op 90 reports it, so that a test can check every sample played.
volatile uint32_t underrunCount[N_CHANNELS] = {0}; // Blocks that were not read from the card in time, since startup
volatile boolean timerRunning = false; // True while the sample clock runs, i.e. while any channel is playing
volatile uint32_t longestHandlerCycles = 0; // Longest run of handler(), in CPU cycles, since the last status request (op 71)
volatile boolean triggerLineActive[2] = {0}; // Level of each trigger channel after its last edge. True = TTL high.
volatile boolean triggersEnabled = false; // False while the device cannot play (startup, comm failure)

// DAC variables
uint16_t dacValue[N_CHANNELS] = {0}; // Next DAC code of each channel. Written to the DAC by dacWrite() if DACFlags is set.
boolean DACFlags[N_CHANNELS] = {0}; // True for each channel with a new value in dacValue
byte dacBuffer[3] = {0}; // Holds bytes about to be written via SPI
int16_t ZeroCodeCalibration[N_CHANNELS] = {0}; // Zero code calibration, stored in EEPROM by Pulse Pal firmware (its op 96)
int16_t activeCalibration[N_CHANNELS] = {0}; // The calibration applied in the current range: see setOutputRange()

// USB command parsing
byte OpMenuByte = 213; // This byte must be the first byte in any serial transmission to the device. Reduces the probability of interference from port-scanning software
byte CommandByte = 0;

// Screen and thumb joystick menu. See the menu map in Menu.ino.
const char DefaultCommanderString[] = "Wave Pal v3.0"; // Top line of the top screen while no client is connected
char CommanderString[17] = "Wave Pal v3.0"; // Top line of the top screen: the device name, or "NAME Connected" (op 89).
                                            // 16 characters and a terminator.
const char ClientStringSuffix[] = " Connected"; // Follows the 6-character client name, e.g. "PYTHON Connected"
const char* TopScreenLine2 = "Click for menu";
int inMenu = MENU_TOP; // Current menu level. See enum MenuLevel
byte menuItem = 1; // Option selected in MENU_LIST. See enum MenuItem
int32_t shownChannelState = -1; // State of the channel shown in MENU_LIST when it was drawn, so that a change redraws it
int ClickerX = 0; // Value of analog reads from X line of joystick input device
int ClickerMinThreshold = 300; // Joystick position to consider a leftwards movement
int ClickerMaxThreshold = 700; // Joystick position to consider a rightwards movement
boolean ClickerButtonState = 0; // Debounced click state of the joystick button (1 = pressed)
boolean LastClickerButtonState = 0;
int LastClickerXState = 0; // 0 for neutral, 1 for left, 2 for right.
uint32_t lastDebounceTime = 0; // to debounce the joystick button
boolean lastButtonState = 1; // last logic state of joystick button (1 = released)

void handler(void);

void setup() {
  // The DAC
  pinMode(SyncPin, OUTPUT); // Configure SPI bus pins as outputs
  pinMode(LDACPin, OUTPUT);
  SPI.begin();
  SPI.beginTransaction(DACSettings); // The DAC is the only device on this SPI bus, so the transaction is never ended
  digitalWriteFast(LDACPin, LOW);
  digitalWriteFast(SyncPin, HIGH);
  EEPROM.get(0, ZeroCodeCalibration); // Read the zero code calibration from the EEPROM
  ProgramDAC(28, 0, 0); // Clear DAC register
  setOutputRange(RANGE_PLUS_MINUS_10V);
  ProgramDAC(16, 0, 31); // Power up DACs
  for (int i = 0; i < N_CHANNELS; i++) {
    dacValue[i] = DACBits_ZeroVolts;
  }
  dacWriteChannels(ALL_CHANNELS); // The playback interrupts are not attached yet, so this cannot be interrupted

  // The screen
  u8g2.begin();
  // Use the SSD1322's internal VSL. u8g2's init selects external VSL (0xB4, 0xA0), which this module doesn't support.
  // With external VSL, every lit row leaves a dim ghost on the row below it.
  u8g2.sendF("caa", 0xB4, 0xA2, 0xFD);
  runSplashScreen();
  u8g2.setContrast(128); // Brightness of oLED display. Use 128 max (of 256) because:
                         // 1. Higher values can draw excess current from the USB supply. 2. To extend the lifetime of the display
  lcd.begin(16, 2);
  lcd.clear();
  lcd.home();
  lcd.noDisplay();
  delay(100);
  lcd.display();

  // Pin modes. The LEDs are set after the screen: input LED 1 shares its pin with the second SPI bus's MISO line,
  // which u8g2.begin() claims and the write-only screen does not need.
  pinMode(TriggerLines[0], INPUT); // Configure trigger pins as digital inputs
  pinMode(TriggerLines[1], INPUT);
  pinMode(ClickerButtonLine, INPUT_PULLUP); // Configure clicker button as digital input with an internal pullup resistor
  for (int i = 0; i < 2; i++) {
    pinMode(InputLEDLines[i], OUTPUT);
    digitalWrite(InputLEDLines[i], LOW);
  }
  for (int i = 0; i < N_CHANNELS; i++) {
    pinMode(OutputLEDLines[i], OUTPUT); // Configure channel LED pins as outputs
    digitalWrite(OutputLEDLines[i], LOW); // Initialize channel LEDs to low (off)
  }

  // The microSD card
  if (!sd.begin(SdioConfig(FIFO_SDIO))) {
    haltWithMessage("Startup Failed:", "SD Card ERROR");
  }
  setupWaveFile(); // Halts with a message if the card cannot hold the waveforms

  LoadDefaultSettings();

  // The playback interrupts: the sample clock, and edges on the trigger channels
  hardwareTimer.priority(PLAYBACK_IRQ_PRIORITY);
  for (int i = 0; i < 2; i++) {
    triggerLineActive[i] = (digitalRead(TriggerLines[i]) == TriggerLevel);
    digitalWrite(InputLEDLines[i], triggerLineActive[i]);
  }
  attachInterrupt(digitalPinToInterrupt(TriggerLines[0]), trigger1ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(TriggerLines[1]), trigger2ISR, CHANGE);
  NVIC_SET_PRIORITY(IRQ_GPIO6789, PLAYBACK_IRQ_PRIORITY); // After attachInterrupt(), which leaves the default priority
  triggersEnabled = true;

  showTopScreen();
}

// loop() refills the playback buffers from the microSD card, handles commands from the PC and runs the joystick menu.
// It must never wait for long: a channel whose next block is not read in time holds its output (an underrun). All
// sample output is done by the playback interrupts, in Playback.ino.
void loop() {
  refillPlaybackBuffers();
  processUSBCommands(); // Read and execute a command from the PC, if one is available
  PPUSB.flush(); // Send any reply from this pass as a single USB packet
  UpdateMenu();
  if (PPUSB.timedOut()) { // A serial USB message started, but didn't finish as expected
    HandleReadTimeout();
  }
}
