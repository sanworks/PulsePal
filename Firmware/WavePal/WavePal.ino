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
// Wave Pal turns a Pulse Pal 3 into a four channel waveform player. It stores four waveforms of up to 1 million
// samples each on the microSD card, and each output channel plays one of them (its current waveform) when triggered:
// by a TTL edge on a trigger channel, by a command from the PC, or from the thumb joystick menu. Several channels can
// play the same waveform. In param sync mode, a waveform loaded during one trial becomes current at the next trial's
// TTL edge. The sampling rate (up to 100kHz) and the output range are set for the whole device. It is the WavePlayer
// firmware for the Bpod Analog Output Module (BpodWavePlayer.ino), ported to Pulse Pal 3 hardware, with the trigger
// channels in place of the Bpod UART, and the double-buffered waveforms of its AudioPlayer Live firmware.
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
//   Display.ino      Screen output, screen saver and splash screen
//   HardwareIO.ino   DAC writes, the output range and software reset
// Supporting classes: ArCOM (USB serial data types), LiquidCrystal_U8G2 (screen). Both are copies of the ones in
// /Firmware/PulsePal.

#define FIRMWARE_VERSION 1

// SETUP MACRO TO COMPILE FOR TARGET DEVICE:
#ifndef PIN_MAP_VERSION
  #define PIN_MAP_VERSION 1 // Hardware pin map, as in Pulse Pal firmware: 1 for every Pulse Pal 3 in use. 0 is the
                            // pinout of a pre-release PCB variant that was scrapped, kept in case one is re-outfitted.
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
#include <usb_names.h> // usb_string_serial_number, which op 102 sends

#define STRINGIFY(x) #x // This and the following line enable conversion of macros to strings (e.g. for displaying firmware version)
#define TOSTRING(x) STRINGIFY(x)

// ---------------------------------------------------------------------------------------------------------------
// Named constants. The numeric values of the op codes, trigger modes and output ranges are part of the USB protocol
// (PROTOCOL.md), so they must never be renumbered.
// ---------------------------------------------------------------------------------------------------------------

// USB op codes. Every command from the PC is: OpMenuByte (213), op code, then op-specific data. Where WavePlayer has
// the same op, the op code is its ASCII letter. Ops 72, 81, 89, 99 and 100 are those of Pulse Pal firmware.
enum OpCode {
  OP_HANDSHAKE = 72,                  // Returns HANDSHAKE_REPLY and the firmware version
  OP_DISCONNECT = 81,                 // The client is closing: stop playback, and show the device's own name again
  OP_SET_CLIENT_NAME = 89,            // Set the 6-character client name shown on the top screen, as "NAME Connected"
  OP_SET_SCREEN_SAVER = 99,           // Switch the screen saver on or off, and set its timeout (stored in EEPROM)
  OP_SEND_HARDWARE_REVISION = 100,    // Returns the circuit board revision, read at startup
  OP_SEND_SERIAL_NUMBER = 102,        // 'f'. Returns the USB serial number, as Pulse Pal firmware's op 102 does
  OP_HARDWARE_INFO = 'N',             // 78. Returns the hardware properties
  OP_SET_SAMPLING_RATE = 'S',         // 83. Sampling rate of all output channels, in Hz
  OP_SET_OUTPUT_RANGE = 'R',          // 82. Output range of all output channels. See enum OutputRange
  OP_LOAD_WAVEFORM = 'L',             // 76. Load one of the waveforms (1-4). See "Waveforms" in Playback.ino
  OP_SET_CURRENT_WAVEFORMS = 'C',     // 67. The waveform each output channel plays (0 = none)
  OP_PLAY = 'P',                     // 80. Soft-trigger output channels (1 bit per channel): starts idle ones
  OP_STOP = 'X',                      // 88. Stop output channels (1 bit per channel)
  OP_SET_FIXED_VOLTAGE = '!',         // 33. Hold output channels at a fixed DAC code (1 bit per channel)
  OP_SET_LOOP_MODE = 'O',             // 79. Loop mode of each output channel
  OP_SET_LOOP_DURATION = 'D',         // 68. Loop duration of each output channel, in samples
  OP_SET_TRIGGER_MODE = 'T',          // 84. Trigger mode of each trigger channel. See enum TriggerModeValue
  OP_SET_TRIGGER_LINKS = 'I',         // 73. Links from the trigger channels to the output channels
  OP_GET_STATUS = 'G',                // 71. Returns the playback state
  OP_GET_PLAYBACK_CHECKSUMS = 'Z'     // 90. For testing: samples played since each channel started, and their sum
};
#define HANDSHAKE_REPLY 'W' // 87. Pulse Pal firmware replies 'K' (75) to op 72, so clients can tell the two apart

// Values of triggerMode[], one per trigger channel: what an edge on it does to the output channels linked to it. See
// processTriggerEdge() in Playback.ino. The first four are Pulse Pal's trigger modes, with Pulse Pal's codes.
enum TriggerModeValue {
  TRIGGER_MODE_NORMAL = 0,            // A rising edge starts idle channels. Channels that are playing ignore it.
  TRIGGER_MODE_TOGGLE = 1,            // A rising edge starts idle channels, and stops those that are playing
  TRIGGER_MODE_GATED = 2,             // A rising edge starts idle channels, and a falling edge stops them
  TRIGGER_MODE_PARAM_SYNC = 3,        // A rising edge makes the waveforms and current waveforms stored for it current.
                                      // It starts and stops nothing. See "Param sync" in Playback.ino
  TRIGGER_MODE_MASTER = 4             // A rising edge starts idle channels, and restarts those that are playing
};
#define N_TRIGGER_CHANNELS 2

static inline bool isValidTriggerMode(byte mode) {
  return mode <= TRIGGER_MODE_MASTER;
}

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
  MENU_ITEM_SCREEN_SAVER = 5,
  MENU_ITEM_DEVICE_INFO = 6,
  MENU_ITEM_REBOOT = 7,
  MENU_ITEM_EXIT = 8                  // The last item
};

// Screen saver, as in Pulse Pal and Synth Pal firmware. Once the device has been left alone for screenSaverTimeout
// seconds (no command from the PC, no rising edge on a trigger channel, and no joystick click or push), it dims the
// screen. Any of these brings the screen back. Op 99 switches it on or off and sets the timeout; the joystick menu
// switches it on or off.
#define SCREEN_SAVER_DEFAULT_TIMEOUT 1800 // Seconds, until op 99 sets another
#define SCREEN_BRIGHTNESS 128 // Brightness of the oLED display (u8g2 contrast, 0-255). Use 128 max because:
                              // 1. Higher values can draw excess current from the USB supply. 2. To extend the lifetime of the display
#define SCREEN_SAVER_DIM_BRIGHTNESS 8 // Brightness while the screen saver dims the screen

// EEPROM layout. These are Pulse Pal firmware's addresses (/Firmware/PulsePal/PulsePal.ino), so the calibration and
// the screen saver settings carry over when a device changes firmware.
#define EEPROM_ZERO_CODE_CALIBRATION_ADDRESS 0 // ZeroCodeCalibration (int16 x 4), set by Pulse Pal firmware's op 96
#define EEPROM_SCREEN_SAVER_ADDRESS 16 // screenSaverEnabled (byte), then screenSaverTimeout (uint16)

#define N_CHANNELS 4
#define N_WAVEFORMS 4 // Waveforms the device stores, numbered 1-4 over USB. Any output channel can play any of them.
#define N_MEMORY_BLOCKS (N_WAVEFORMS * 2) // Two memory blocks per waveform: waveform w has blocks 2w and 2w+1. See
                                          // "Waveforms" in Playback.ino
#define NO_BLOCK 255 // A memory block number meaning "none"
#define MAX_WAVE_SAMPLES 1000000 // Samples per waveform
#define MAX_SAMPLING_RATE 100000 // Hz
#define DEFAULT_SAMPLING_RATE 10000 // Hz
#define BUFFER_SAMPLES 16384 // Samples per playback buffer: 32kB, 164ms at 100kHz. Each memory block has a pre-buffer of
                             // this size, and each channel two playback buffers. See "How playback works" in Playback.ino.
#define BUFFER_BYTES (BUFFER_SAMPLES * 2)
#define WAVE_REGION_BYTES 2097152UL // Each memory block's region of the data file (2 MiB). A whole number of buffers, so
                                    // every chunk read from the card starts on a 512 byte sector boundary.
#define WAVE_FILE_BYTES (N_MEMORY_BLOCKS * WAVE_REGION_BYTES)
#define WAVE_FOLDER "/Wave_Pal"
#define WAVE_FILE_NAME "/Wave_Pal/WaveData.wfm"
#define TIMER_CLOCK_HZ 24000000 // IntervalTimer counts cycles of this clock, so the sample period is a whole number of them
#define PLAYBACK_IRQ_PRIORITY 64 // Priority of the sample clock and trigger interrupts. Both must be equal, so that neither
                                 // interrupts the other in the middle of a DAC write. It is above the USB (128) and
                                 // microSD (96) interrupts, so a USB transfer cannot delay a sample.
#define DAC_WRITE_TO_LATCH_NS 200 // Time from the last DAC write to the latch, in dacWrite(), dacWriteChannels() and
                                  // ProgramDAC(). The AD5754R drops the first write since the previous latch if the next
                                  // latch comes less than about 60ns after it: the outputs keep their old voltages, and a
                                  // control register write (power up, output range) is lost. Measured on a Pulse Pal 3, 47ns
                                  // failed and 63ns worked. That matters when one channel is written. The digitalWrite()
                                  // call used before took about 50ns, which only just worked.
#define CHUNK_NONE -1 // Value of bufferChunk[][] for a playback buffer that holds no usable chunk
#define ALL_CHANNELS 0x0F // Channel bits for output channels 1-4

static_assert(MAX_WAVE_SAMPLES * 2 <= WAVE_REGION_BYTES, "A memory block's region of the data file must hold a whole waveform");
static_assert(WAVE_REGION_BYTES % BUFFER_BYTES == 0, "Chunks must not straddle a memory block's region boundary");
static_assert(BUFFER_BYTES % 512 == 0, "Chunks must be whole microSD sectors");
static_assert(N_MEMORY_BLOCKS < NO_BLOCK, "Memory block numbers must fit in a byte, below NO_BLOCK");

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
constexpr byte pcbVersionMap[5] = {18, 19, 20, 21, 22}; // Pins grounded to encode the PCB revision in binary. See readHardwareRevision()
#if (PIN_MAP_VERSION == 0) // Scrapped pre-release PP3 PCB variant. Unused: kept in case one is re-outfitted
  constexpr byte OutputLEDLines[4] = {24, 28, 29, 30}; // LEDs above output channels 1-4
  constexpr byte ClickerButtonLine = 34; // Digital line that reports the thumb joystick click state
  constexpr byte SyncPin = 14; // AD5754R Pin 7 (Sync)
  constexpr byte LDACPin = 39; // AD5754R Pin 10 (LDAC)
#else // Every PP3 PCB in use
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
volatile byte triggerMode[N_TRIGGER_CHANNELS] = {0}; // Of trigger channels 1 and 2. See enum TriggerModeValue
volatile byte TriggerAddress[2][N_CHANNELS] = {0}; // Output channels triggered by trigger channel 1 (row 1) and 2 (row 2)
volatile byte currentWaveform[N_CHANNELS] = {0}; // The waveform (1-4) each output channel plays from its next start.
                                                 // 0 = none: the channel ignores triggers.

// ---------------------------------------------------------------------------------------------------------------
// Waveforms, memory blocks and playback buffers. See "Waveforms" and "How playback works" in Playback.ino.
// ---------------------------------------------------------------------------------------------------------------
volatile uint32_t nSamples[N_MEMORY_BLOCKS] = {0}; // Samples held in each memory block. 0 = empty.
uint16_t preBuffer[N_MEMORY_BLOCKS][BUFFER_SAMPLES]; // The first BUFFER_SAMPLES of each memory block's waveform, kept in RAM
                                                     // from the moment it is loaded, so that playback can start without
                                                     // waiting for the card
volatile byte currentBlock[N_WAVEFORMS] = {0, 2, 4, 6}; // The memory block each waveform plays from: the one channels start.
                                                        // The waveform's other block is its unused block, which op 76 loads.
// Param sync mode. Op 76 and op 67 store what they send for the next rising edge on a param sync trigger channel. See
// "Param sync" in Playback.ino
volatile boolean loadStored[N_WAVEFORMS] = {0}; // True if the waveform's unused block holds a load for the next edge
volatile boolean currentWaveformsStored = false; // True if storedCurrentWaveform holds op 67's values for the next edge
volatile byte storedCurrentWaveform[N_CHANNELS] = {0};
DMAMEM uint16_t transferBuffer[BUFFER_SAMPLES]; // Holds each chunk of a waveform arriving over USB (after the first, which
                                                // goes to its pre-buffer) until it is written to the card
// Two playback buffers per channel: while one plays, the next chunk of the waveform is read from the card into the
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
                                               // The chunk of a waveform each playback buffer holds, or CHUNK_NONE
volatile byte bufferBlock[N_CHANNELS][2] = {{NO_BLOCK, NO_BLOCK}, {NO_BLOCK, NO_BLOCK}, {NO_BLOCK, NO_BLOCK},
                                            {NO_BLOCK, NO_BLOCK}}; // The memory block that chunk was read from

// Playback state of each output channel. Changed by the playback interrupts, and by loop() with interrupts disabled.
volatile boolean playing[N_CHANNELS] = {0}; // True while the channel plays a waveform
volatile boolean stopAfterWrite[N_CHANNELS] = {0}; // The waveform has ended: 0V is waiting to be written, then the channel stops
volatile byte playBlock[N_CHANNELS] = {0}; // The memory block the channel plays, chosen when it started
volatile uint32_t wavePos[N_CHANNELS] = {0}; // Index in the waveform of the next sample to fetch
volatile uint32_t playChunk[N_CHANNELS] = {0}; // Chunk of the waveform that holds the next sample. 0 = the pre-buffer.
volatile uint32_t chunkPos[N_CHANNELS] = {0}; // Position of the next sample in its chunk
volatile uint32_t samplesPlayed[N_CHANNELS] = {0}; // Samples fetched since the channel was triggered (for the loop duration)
volatile uint32_t sampleSum[N_CHANNELS] = {0}; // Sum of the DAC codes fetched since the channel was triggered, wrapping.
                                               // Op 90 reports it, so that a test can check every sample played.
volatile uint32_t underrunCount[N_CHANNELS] = {0}; // Chunks that were not read from the card in time, since startup
volatile boolean timerRunning = false; // True while the sample clock runs, i.e. while any channel is playing
volatile uint32_t longestHandlerCycles = 0; // Longest run of handler(), in CPU cycles, since the last status request (op 71)
volatile boolean triggerLineActive[2] = {0}; // Level of each trigger channel after its last edge. True = TTL high.
volatile boolean edgeHandledEarly[2] = {0}; // The other trigger channel's interrupt handled this channel's rising edge
                                            // first, as a param sync edge (see triggerInterrupt())
volatile boolean triggersEnabled = false; // False while the device cannot play (startup, comm failure)

// DAC variables
uint16_t dacValue[N_CHANNELS] = {0}; // Next DAC code of each channel. Written to the DAC by dacWrite() if DACFlags is set.
boolean DACFlags[N_CHANNELS] = {0}; // True for each channel with a new value in dacValue
byte dacBuffer[3] = {0}; // Holds bytes about to be written via SPI
int16_t ZeroCodeCalibration[N_CHANNELS] = {0}; // Zero code calibration, stored in EEPROM by Pulse Pal firmware (its op 96)
int16_t activeCalibration[N_CHANNELS] = {0}; // The calibration applied in the current range: see setOutputRange()

byte hardwareRevision = 0; // Circuit board (PCB) revision, read from pcbVersionMap by readHardwareRevision() in setup(). Op 100
                           // returns it.

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

// Screen saver. See updateScreenSaver() in Display.ino.
byte screenSaverEnabled = 1; // 1 if the screen saver is on. Set by op 99 and the joystick menu
uint16_t screenSaverTimeout = SCREEN_SAVER_DEFAULT_TIMEOUT; // Seconds without activity before the screen dims. Set by op 99
boolean screenSaverSavePending = false; // The settings above changed, and are written to the EEPROM once no channel is playing
boolean screenDimmed = false; // True while the screen saver has dimmed the screen
uint32_t lastActivityTime = 0; // millis() when updateScreenSaver() last found screenSaverActivity set
volatile boolean screenSaverActivity = false; // Set by the trigger interrupts on a rising edge, and by loop() for a command
                                              // from the PC or a joystick click or push. updateScreenSaver() clears it.
static_assert(EEPROM_SCREEN_SAVER_ADDRESS >= EEPROM_ZERO_CODE_CALIBRATION_ADDRESS + sizeof(ZeroCodeCalibration),
              "The screen saver settings must not overlap the calibration in the EEPROM");

void handler(void);

void setup() {
  // The DAC
  pinMode(SyncPin, OUTPUT); // Configure SPI bus pins as outputs
  pinMode(LDACPin, OUTPUT);
  SPI.begin();
  SPI.beginTransaction(DACSettings); // The DAC is the only device on this SPI bus, so the transaction is never ended
  digitalWriteFast(LDACPin, LOW);
  digitalWriteFast(SyncPin, HIGH);
  EEPROM.get(EEPROM_ZERO_CODE_CALIBRATION_ADDRESS, ZeroCodeCalibration); // Read the zero code calibration from the EEPROM
  loadScreenSaverSettings();
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
  u8g2.setContrast(SCREEN_BRIGHTNESS);
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
  readHardwareRevision(); // Leaves the revision pins high impedance

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
  updateScreenSaver(); // After the reply is sent, so that waking the screen never delays it
  UpdateMenu();
  if (PPUSB.timedOut()) { // A serial USB message started, but didn't finish as expected
    HandleReadTimeout();
  }
}
