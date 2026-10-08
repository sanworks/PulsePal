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

// SYNTH PAL FIRMWARE for Pulse Pal 3 hardware
//
// Synth Pal turns a Pulse Pal 3 into a four channel waveform synthesizer. Each output channel plays a sine, triangle,
// square or sawtooth wave, with its own amplitude (peak to peak), mean voltage, resting voltage (the voltage between
// playbacks) and play duration, when it is triggered: by a TTL edge on a trigger channel, by a command from the PC, or
// from the thumb joystick menu. A channel can instead play a fixed voltage, its amplitude, for its play duration. Linear
// on and off ramps can fade each channel in from its resting voltage, and back to it.
// One frequency, 1Hz to 20kHz in steps of 0.01Hz, applies to all four channels.
// Samples are computed as they play, at a sampling rate that is a multiple of the frequency, so that every cycle is
// rendered the same way. The DAC's output range is chosen for each channel, to give its waveform the finest steps.
// The USB protocol is documented in PROTOCOL.md in this folder, and AGENTS.md lists the rules for changing this code.
//
// ** DEPENDENCIES YOU NEED TO INSTALL FIRST **
// Board: Teensy 4.1, from Teensyduino. Library: U8g2_Arduino, developed by Oliver Kraus (Thanks Oliver!!), from the
// Arduino library manager (verified with v2.36.19).
//
// CODE MAP
// This sketch is split into tabs. Arduino joins them into a single file before compiling (this file first, then the
// others in alphabetical order), so all tabs share the constants and global variables defined in this file.
//   SynthPal.ino     Build configuration, pin map, named constants, global variables, setup() and loop()
//   Playback.ino     The sample clock interrupt handler(), waveform synthesis, the trigger inputs, and starting and
//                    stopping channels. Start here for timing questions: "How playback works" is at the top.
//   Settings.ino     The frequency and the output channel settings: limits, output ranges, and handing new settings
//                    to the playback interrupts
//   USBOps.ino       Commands from the PC, processUSBCommands()
//   Menu.ino         Thumb joystick menu, with a map of its options, and the value editors
//   Display.ino      Screen output, screen saver and splash screen
//   HardwareIO.ino   DAC writes, output ranges, the sample clock's timer and software reset
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
#error Synth Pal runs on Pulse Pal 3 hardware only. Select Tools > Board > Teensy 4.1.
#endif

#define HARDWARE_VERSION 3 // Pulse Pal hardware version. Synth Pal runs on Pulse Pal 3 only. ArCOM.h reads this.

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
// Named constants. The numeric values of the op codes, waveforms, trigger modes and output ranges are part of the USB
// protocol (PROTOCOL.md), so they must never be renumbered.
// ---------------------------------------------------------------------------------------------------------------

// USB op codes. Every command from the PC is: OpMenuByte (213), op code, then op-specific data. Ops 72, 81, 89 and 99
// are those of Pulse Pal firmware. The others are ASCII letters, as in Wave Pal.
enum OpCode {
  OP_HANDSHAKE = 72,                  // Returns HANDSHAKE_REPLY and the firmware version
  OP_DISCONNECT = 81,                 // The client is closing: show the device's own name on the screen again
  OP_SET_CLIENT_NAME = 89,            // Set the 6-character client name shown on the top screen, as "NAME Connected"
  OP_SET_SCREEN_SAVER = 99,           // Switch the screen saver on or off, and set its timeout (stored in EEPROM)
  OP_HARDWARE_INFO = 'N',             // 78. Returns the hardware properties and the limits of the settings
  OP_SET_FREQUENCY = 'F',             // 70. Frequency of all output channels, in hundredths of a Hz
  OP_SET_WAVEFORM = 'W',              // 87. Waveform of each output channel. See enum WaveformValue
  OP_SET_AMPLITUDE = 'A',             // 65. Amplitude of each output channel, in microvolts: peak to peak, or the voltage
                                      // of a fixed voltage
  OP_SET_RESTING_VOLTAGE = 'V',       // 86. Resting voltage of each output channel, in microvolts
  OP_SET_MEAN_VOLTAGE = 'M',          // 77. Mean voltage of each output channel's waveform, in microvolts
  OP_SET_PLAY_DURATION = 'D',         // 68. Play duration of each output channel, in microseconds. 0 = until stopped
  OP_SET_ON_RAMP_DURATION = 'B',      // 66. On ramp of each output channel, at the Beginning of playback, in microseconds
  OP_SET_OFF_RAMP_DURATION = 'E',     // 69. Off ramp of each output channel, at the End of playback, in microseconds
  OP_SET_TRIGGER_LINKS = 'I',         // 73. Links from the trigger channels to the output channels
  OP_SET_TRIGGER_MODE = 'T',          // 84. Trigger mode of each trigger channel. See enum TriggerModeValue
  OP_PLAY = 'P',                      // 80. Soft-trigger output channels (1 bit per channel)
  OP_STOP = 'X',                      // 88. Stop output channels (1 bit per channel)
  OP_GET_STATUS = 'G',                // 71. Returns the playback state
  OP_GET_PLAYBACK_CHECKSUMS = 'Z'     // 90. For testing: samples played since each channel started, and their sum
};
#define HANDSHAKE_REPLY 'S' // 83. Pulse Pal firmware replies 'K' (75) to op 72 and Wave Pal 'W' (87), so clients can tell
                            // the three apart

// Values of waveform[]. The shape of one cycle, as it starts at a trigger: see unitWaveform() in Playback.ino.
enum WaveformValue {
  WAVEFORM_SINE = 0,                  // Starts at the resting voltage, rising
  WAVEFORM_TRIANGLE = 1,              // Starts at the resting voltage, rising
  WAVEFORM_SQUARE = 2,                // High for the first half of each cycle, low for the second
  WAVEFORM_SAWTOOTH = 3,              // Rises from its lowest voltage to its highest, then falls back at the cycle's end
  WAVEFORM_FIXED_VOLTAGE = 4          // Not periodic: the output steps to the amplitude, a voltage (-10V to 10V), for the
                                      // play duration. See isValidOutputLevel() in Settings.ino.
};
#define MAX_WAVEFORM WAVEFORM_FIXED_VOLTAGE

// Values of TriggerMode[], one per trigger channel. These are Pulse Pal's trigger modes, with the same values.
enum TriggerModeValue {
  TRIGGER_MODE_NORMAL = 0,            // A rising edge starts the linked output channels. It is ignored by channels that play.
  TRIGGER_MODE_TOGGLE = 1,            // A rising edge starts the linked output channels, or stops those that play
  TRIGGER_MODE_GATED = 2              // A rising edge starts the linked output channels, and a falling edge stops them
};
#define MAX_TRIGGER_MODE TRIGGER_MODE_GATED

// The AD5754R's output ranges that Synth Pal uses, one per output channel, chosen from the channel's settings by
// outputRangeFor() in Settings.ino: the first range in this order that holds the whole waveform. The 10.8V ranges are not
// used: every voltage within +/-10V fits one of these, with finer steps.
enum OutputRange {
  RANGE_0_TO_5V = 0,                  // 76uV steps
  RANGE_0_TO_10V = 1,                 // 153uV steps
  RANGE_PLUS_MINUS_5V = 2,            // 153uV steps
  RANGE_PLUS_MINUS_10V = 3            // 305uV steps. Pulse Pal's range, in which the zero code calibration was measured
};
#define N_OUTPUT_RANGES 4
const byte dacRangeCodes[N_OUTPUT_RANGES] = {0, 1, 3, 4}; // The AD5754R's output range select code for each range
const int32_t rangeMinMicrovolts[N_OUTPUT_RANGES] = {0, 0, -5000000, -10000000}; // Voltage of DAC code 0
const int32_t rangeMaxMicrovolts[N_OUTPUT_RANGES] = {5000000, 10000000, 5000000, 10000000}; // Voltage of code 65536

// Values of inMenu, the current level of the thumb joystick menu. See the menu map in Menu.ino.
enum MenuLevel {
  MENU_TOP = 0,                       // "Synth Pal v3.0 / Click for menu"
  MENU_CHANNEL_LIST = 1,              // Scroll through the channels, frequency, screen saver, device info, reset and exit
  MENU_OUTPUT_CHANNEL = 2,            // Settings of one output channel
  MENU_TRIGGER_CHANNEL = 3            // Settings of one trigger channel
};

// Values of SelectedChannel, the item selected in MENU_CHANNEL_LIST. Items 1-4 are the output channels, and 5-6 the
// trigger channels.
enum ChannelListItem {
  MENU_ITEM_FREQUENCY = 7,
  MENU_ITEM_SCREEN_SAVER = 8,
  MENU_ITEM_DEVICE_INFO = 9,
  MENU_ITEM_RESET = 10,
  MENU_ITEM_EXIT = 11                 // The last item
};

// Values of SelectedAction, the item selected in MENU_OUTPUT_CHANNEL
enum OutputChannelAction {
  MENU_ACTION_TRIGGER = 1,            // Play the channel, or stop it while it plays
  MENU_ACTION_WAVEFORM = 2,
  MENU_ACTION_AMPLITUDE = 3,
  MENU_ACTION_MEAN_VOLTAGE = 4,
  MENU_ACTION_RESTING_VOLTAGE = 5,
  MENU_ACTION_PLAY_DURATION = 6,
  MENU_ACTION_ON_RAMP = 7,
  MENU_ACTION_OFF_RAMP = 8,
  MENU_ACTION_LINK_TRIGGER1 = 9,
  MENU_ACTION_LINK_TRIGGER2 = 10,
  MENU_ACTION_EXIT = 11               // The last item
};

// Values of rampStage[], each playing channel's place in its envelope. See "Ramps" in Playback.ino.
enum RampStage {
  STAGE_ON_RAMP = 0,                  // Fading in from the resting voltage. Every start begins here, even with no on ramp
  STAGE_HOLD = 1,                     // Full amplitude around the mean voltage, for the play duration
  STAGE_OFF_RAMP = 2                  // Fading back to the resting voltage
};

// Values of SelectedInputAction, the item selected in MENU_TRIGGER_CHANNEL
enum TriggerChannelAction {
  MENU_INPUT_ACTION_TRIGGER = 1,      // Act as a rising edge on the trigger channel
  MENU_INPUT_ACTION_MODE = 2,
  MENU_INPUT_ACTION_EXIT = 3          // The last item
};

#define N_CHANNELS 4
#define ALL_CHANNELS 0x0F // Channel bits for output channels 1-4

// Limits of the settings. Op 78 reports them.
#define MIN_FREQUENCY_CENTIHZ 100 // 1Hz
#define MAX_FREQUENCY_CENTIHZ 2000000 // 20kHz
#define MAX_AMPLITUDE_MICROVOLTS 20000000 // 20V peak to peak
#define MAX_VOLTAGE_MICROVOLTS 10000000 // Every voltage on an output stays within +/-10V
#define MAX_PLAY_DURATION_MICROS 3600000000UL // 1 hour. 0 plays until stopped. Also the longest on and off ramp.

// Default settings, at startup and after a comm failure. The Python and MATLAB classes program the same ones.
#define DEFAULT_FREQUENCY_CENTIHZ 10000 // 100Hz
#define DEFAULT_AMPLITUDE_MICROVOLTS 5000000 // 5V peak to peak
#define DEFAULT_PLAY_DURATION_MICROS 1000000 // 1 second

// The sample clock. The sampling rate is samplesPerCycle times the frequency, where samplesPerCycle is the largest
// multiple of 4 that keeps the rate at or below MAX_SAMPLING_RATE: so a sample falls on every edge, peak and trough of
// every waveform (see "How playback works" in Playback.ino).
#define MAX_SAMPLING_RATE 100000 // Hz
#define TIMER_CLOCK_HZ 24000000 // The PIT timer behind IntervalTimer counts this clock
#define TIMER_TICKS_PER_SAMPLE_NUMERATOR 2400000000UL // TIMER_CLOCK_HZ * 100. Timer ticks per sample are this divided by
                                                      // (frequency in hundredths of a Hz * samplesPerCycle)
#define PLAYBACK_IRQ_PRIORITY 64 // Priority of the sample clock and trigger interrupts. Both must be equal, so that neither
                                 // interrupts the other in the middle of a DAC write. It is above the USB interrupt (128),
                                 // so a USB transfer cannot delay a sample.
static_assert((uint64_t)TIMER_CLOCK_HZ * 100 == TIMER_TICKS_PER_SAMPLE_NUMERATOR, "Ticks per sample numerator");
static_assert((uint64_t)MAX_FREQUENCY_CENTIHZ * 4 <= (uint64_t)MAX_SAMPLING_RATE * 100,
              "At the highest frequency, 4 samples per cycle must fit in the highest sampling rate");

// Timed DAC updates, see dacLoadTimed() in HardwareIO.ino. As in Pulse Pal 3 firmware (DAC_LATCH_US in
// /Firmware/PulsePal3/PulsePal3.ino), with the write times measured in Synth Pal's handler(): 1.38us for one channel
// and 4.67us for four, the longest of 60000 ticks each.
#define CPU_CYCLES_PER_US (F_CPU / 1000000) // DAC updates are timed in CPU cycles: see "The time base" in HardwareIO.ino
#define CPU_CYCLES_PER_TIMER_TICK (F_CPU / TIMER_CLOCK_HZ) // 25 at 600MHz
#define TICK_RESYNC_CYCLES 600 // tickCycles is set from the timer if they disagree by more than this (1us at 600MHz).
                               // At a clock start the PIT begins counting up to 259 cycles (measured) after
                               // startSampleClock() reads the cycle counter; a missed tick is ~6000 cycles off.
static_assert(F_CPU % TIMER_CLOCK_HZ == 0, "The CPU clock must be a whole multiple of the sample clock's timer clock");
#define DAC_WRITE_OVERHEAD_US 0.25 // Time to start writing, once per update
#define DAC_CHANNEL_WRITE_US 1.2 // SPI write of one DAC channel
#define DAC_LATCH_GAP_US 1.5 // Time from the last write to the latch. The DAC updates at once only some time after a write.
                             // handler() fetches the next samples in this time.
#define DAC_LATCH_US 7.5 // Outputs change this long after each tick of the sample clock: the interrupt's start (up to
                         // about 1us), the writes of 4 channels, and the gap
#define DAC_LATE_TOLERANCE_US 0.25 // An update whose writes end this much later than planned counts in lateUpdates
#define DAC_WRITE_TO_LATCH_NS 200 // Time from a write to the latch, for the writes outside the timed updates (dacWriteNow(),
                                  // ProgramDAC()). The DAC drops a write followed too soon by a latch: a channel keeps its
                                  // old output, and a control register write (e.g. power up) is lost. Measured on a Pulse
                                  // Pal 3, 30ns failed and 60ns worked. digitalWrite() alone, the delay in dacLatch(), is
                                  // shorter than that.

// The sine wave is read from a table of its first quarter cycle, with linear interpolation (see quarterSine() in
// Playback.ino). With 4096 steps the interpolation is within 2e-8 of the true sine, under a thousandth of a DAC code.
#define SINE_TABLE_SIZE 4096

// Screen saver, as in Pulse Pal firmware. Once the device has been left alone for screenSaverTimeout seconds (no command
// from the PC, no rising edge on a trigger channel, and no joystick click or push), it dims the screen. Any of these
// brings the screen back. Op 99 switches it on or off and sets the timeout; the joystick menu switches it on or off.
#define SCREEN_SAVER_DEFAULT_TIMEOUT 1800 // Seconds, until op 99 sets another
#define SCREEN_BRIGHTNESS 128 // Brightness of the oLED display (u8g2 contrast, 0-255). Use 128 max because:
                              // 1. Higher values can draw excess current from the USB supply. 2. To extend the lifetime of the display
#define SCREEN_SAVER_DIM_BRIGHTNESS 8 // Brightness while the screen saver dims the screen

// EEPROM layout. These are Pulse Pal firmware's addresses (/Firmware/PulsePal3/PulsePal3.ino), so the calibration and
// the screen saver settings carry over when a device changes firmware.
#define EEPROM_ZERO_CODE_CALIBRATION_ADDRESS 0 // ZeroCodeCalibration (int16 x 4), set by Pulse Pal firmware's op 96
#define EEPROM_SCREEN_SAVER_ADDRESS 16 // screenSaverEnabled (byte), then screenSaverTimeout (uint16)

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
IntervalTimer hardwareTimer; // Attaches handler() to the PIT timer interrupt. The firmware then starts, stops and loads the
                             // timer's PIT channel itself (see startSampleClock() in HardwareIO.ino).
IMXRT_PIT_CHANNEL_t *sampleClockChannel = nullptr; // The PIT channel hardwareTimer took, found in setup()
SPISettings DACSettings(30000000, MSBFIRST, SPI_MODE2); // The AD5754R's maximum SPI clock is 30MHz

// ---------------------------------------------------------------------------------------------------------------
// Settings. Programmed from USB and the joystick menu. See PROTOCOL.md and Settings.ino.
// ---------------------------------------------------------------------------------------------------------------
uint32_t frequencyCentiHz = DEFAULT_FREQUENCY_CENTIHZ; // Frequency of all output channels, in hundredths of a Hz
byte waveform[N_CHANNELS] = {0}; // See enum WaveformValue
int32_t amplitudeMicrovolts[N_CHANNELS] = {0}; // Peak to peak (0 or more), or the voltage of WAVEFORM_FIXED_VOLTAGE
int32_t restingVoltageMicrovolts[N_CHANNELS] = {0}; // The output while the channel is idle
int32_t meanVoltageMicrovolts[N_CHANNELS] = {0}; // A periodic waveform's mean, while it plays at full amplitude
uint32_t playDurationMicros[N_CHANNELS] = {0}; // How long the channel plays at full amplitude after its on ramp. 0 = until
                                               // stopped.
uint32_t onRampMicros[N_CHANNELS] = {0}; // How long the channel fades in after each trigger. 0 = no on ramp.
uint32_t offRampMicros[N_CHANNELS] = {0}; // How long the channel fades out when it stops. 0 = no off ramp.
volatile byte TriggerAddress[2][N_CHANNELS] = {0}; // Output channels triggered by trigger channel 1 (row 1) and 2 (row 2)
volatile byte TriggerMode[2] = {0}; // One per trigger channel. See enum TriggerModeValue

// ---------------------------------------------------------------------------------------------------------------
// The sample clock, set from the frequency by setFrequency() in Settings.ino. Read by the playback interrupts, and
// changed by loop() with interrupts disabled. See "How playback works" in Playback.ino.
// ---------------------------------------------------------------------------------------------------------------
uint32_t samplesPerCycle = 1000; // Samples in one cycle of the waveform: a multiple of 4
uint32_t samplesPerQuarter = 250; // samplesPerCycle / 4
float samplesPerQuarterFloat = 250; // The same, for the floating point waveform calculations
float sawtoothDenominator = 999; // samplesPerCycle - 1, the steps of the sawtooth's rise
uint32_t samplePeriodTicks = 240; // Each sample period is this many timer ticks, or one more (dithering)...
uint32_t ditherRemainder = 0; // ...so that every ditherDenominator samples (one cycle per centiHz) take
uint32_t ditherDenominator = 1000000; // TIMER_TICKS_PER_SAMPLE_NUMERATOR ticks in all: samplePeriodTicks * ditherDenominator
                                      // + ditherRemainder. ditherDenominator is the frequency in centiHz * samplesPerCycle,
                                      // i.e. the sampling rate in centiHz.
uint32_t ditherAccumulator = 0; // Adds ditherRemainder per sample. Each time it passes ditherDenominator, a period is one tick longer.
uint32_t currentPeriodLoad = 0; // The PIT load value (ticks - 1) of the sample period in progress
uint32_t nextPeriodLoad = 0; // The load value of the period after it, already in the PIT's LDVAL register
uint32_t tickCycles = 0; // ARM_DWT_CYCCNT at the tick that began the current sample period. See "The time base" in
                         // HardwareIO.ino.
volatile boolean timerRunning = false; // True while the sample clock runs, i.e. while any channel is playing

// ---------------------------------------------------------------------------------------------------------------
// What each output channel plays, worked out from its settings by updateChannelOutput() in Settings.ino. handler()
// plays activeOutput[]. loop() never changes it while the sample clock runs: it leaves new values in pendingOutput[],
// and handler() takes them (takePendingOutput() in Playback.ino), because a change of output range needs DAC writes.
// ---------------------------------------------------------------------------------------------------------------
struct ChannelOutput {
  byte waveform;              // See enum WaveformValue
  byte range;                 // See enum OutputRange
  uint16_t restCode;          // The DAC code nearest the resting voltage: the code output while the channel is idle
  float restCodeFraction;     // The resting voltage's exact code minus restCode, -0.5 to 0.5
  uint16_t meanCode;          // The DAC code nearest the mean voltage, which a periodic waveform swings around
  float meanCodeFraction;     // The mean voltage's exact code minus meanCode, -0.5 to 0.5
  float halfAmplitudeCodes;   // Half the peak to peak amplitude, in DAC codes
  uint16_t fixedCode;         // WAVEFORM_FIXED_VOLTAGE: the DAC code nearest the amplitude, played on every sample
  float rampOffsetCodes;      // Where the ramps lead from the resting voltage, in DAC codes: the mean voltage's exact
                              // code minus the resting voltage's, or for a fixed voltage the fixed voltage's
};
ChannelOutput activeOutput[N_CHANNELS]; // What the channel plays now, and the range the DAC has for it
ChannelOutput pendingOutput[N_CHANNELS]; // New values from loop(), waiting for handler() (see pendingOutputChannels)
volatile byte pendingOutputChannels = 0; // One bit per channel whose pendingOutput[] handler() has not taken yet
volatile uint32_t playDurationSamples[N_CHANNELS] = {0}; // playDurationMicros in samples. 0 = until stopped.
volatile uint32_t onRampSamples[N_CHANNELS] = {0}; // onRampMicros in samples. 0 = no on ramp.
volatile uint32_t offRampSamples[N_CHANNELS] = {0}; // offRampMicros in samples. 0 = no off ramp.
volatile float onRampReciprocal[N_CHANNELS] = {0}; // 1 / onRampSamples, which the envelope is multiplied by
volatile float offRampReciprocal[N_CHANNELS] = {0}; // 1 / offRampSamples

// Playback state of each output channel. Changed by the playback interrupts, and by loop() with interrupts disabled.
volatile boolean playing[N_CHANNELS] = {0}; // True while the channel plays, off ramp included
volatile boolean stopAfterWrite[N_CHANNELS] = {0}; // The channel has stopped: its resting voltage is waiting to be written
volatile uint32_t phase[N_CHANNELS] = {0}; // Position in the cycle of the next sample to fetch, 0 to samplesPerCycle - 1
volatile byte rampStage[N_CHANNELS] = {0}; // See enum RampStage
volatile uint32_t rampPosition[N_CHANNELS] = {0}; // In a ramp, the step of the next sample: its envelope is rampPosition
                                                  // times the ramp's reciprocal. Counts up in the on ramp, down in the off.
volatile uint32_t holdSamples[N_CHANNELS] = {0}; // Samples fetched at full amplitude since the on ramp (for the play duration)
volatile float fetchedEnvelope[N_CHANNELS] = {0}; // The envelope of the sample fetched last, 0 to 1 (takePendingOutput())
volatile uint32_t samplesPlayed[N_CHANNELS] = {0}; // Samples fetched since the channel started from rest, ramps included.
                                                   // stopChannels() takes out the one it fetched but did not play.
volatile uint32_t sampleSum[N_CHANNELS] = {0}; // Sum of the DAC codes counted in samplesPlayed, wrapping.
                                               // Op 90 reports it, so that a test can check every sample played.
volatile uint32_t longestHandlerCycles = 0; // Longest run of handler(), in CPU cycles, since the last status request (op 71)
volatile uint32_t lateUpdates = 0; // DAC updates that may have come later than DAC_LATCH_US after their tick, since the
                                   // last status request (op 71). See dacLoadTimed().
volatile boolean triggerLineActive[2] = {0}; // Level of each trigger channel after its last edge. True = TTL high.
volatile boolean triggersEnabled = false; // False while the device cannot play (startup, comm failure)

// DAC variables
uint16_t dacValue[N_CHANNELS] = {0}; // Next DAC code of each channel. Written by dacWriteTimed() if DACFlags is set.
boolean DACFlags[N_CHANNELS] = {0}; // True for each channel whose dacValue differs from the code on its output
uint16_t dacOutput[N_CHANNELS] = {0}; // The DAC code on each output now, before the zero code calibration
byte dacBuffer[3] = {0}; // Holds bytes about to be written via SPI
int16_t ZeroCodeCalibration[N_CHANNELS] = {0}; // Zero code calibration, stored in EEPROM by Pulse Pal firmware (its op 96)
int16_t activeCalibration[N_CHANNELS] = {0}; // The calibration applied in each channel's range: see takePendingOutput()
float sineTable[SINE_TABLE_SIZE + 1]; // sin(pi/2 * i / SINE_TABLE_SIZE): the first quarter of a sine wave, filled in setup()

// USB command parsing
byte OpMenuByte = 213; // This byte must be the first byte in any serial transmission to the device. Reduces the probability of interference from port-scanning software
byte CommandByte = 0;

// Screen and thumb joystick menu. See the menu map in Menu.ino.
const char DefaultCommanderString[] = "Synth Pal v3.0"; // Top line of the top screen while no client is connected
char CommanderString[17] = "Synth Pal v3.0"; // Top line of the top screen: the device name, or "NAME Connected" (op 89).
                                             // 16 characters and a terminator.
const char ClientStringSuffix[] = " Connected"; // Follows the 6-character client name, e.g. "PYTHON Connected"
const char* TopScreenLine2 = "Click for menu";
int inMenu = MENU_TOP; // Current menu level. See enum MenuLevel
byte SelectedChannel = 1; // Item selected in MENU_CHANNEL_LIST. See enum ChannelListItem
byte SelectedAction = MENU_ACTION_TRIGGER; // Item selected in MENU_OUTPUT_CHANNEL. See enum OutputChannelAction
byte SelectedInputAction = MENU_INPUT_ACTION_TRIGGER; // Item selected in MENU_TRIGGER_CHANNEL. See enum TriggerChannelAction
boolean viewingInfo = false; // True while the device info is on the screen
int32_t shownPlayState = -1; // Play state of the channel shown in MENU_OUTPUT_CHANNEL when it was drawn, so that a change redraws it
int ClickerX = 0; // Value of analog reads from X line of joystick input device
int ClickerY = 0; // Value of analog reads from Y line of joystick input device
int ClickerMinThreshold = 300; // Joystick position to consider an upwards or leftwards movement (on Y and X lines respectively)
int ClickerMaxThreshold = 700; // Joystick position to consider a downwards or rightwards movement
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
  // The DAC. Every channel starts in the -10V to 10V range at 0V; LoadDefaultSettings() below sets each channel's range.
  pinMode(SyncPin, OUTPUT); // Configure SPI bus pins as outputs
  pinMode(LDACPin, OUTPUT);
  SPI.begin();
  SPI.beginTransaction(DACSettings); // The DAC is the only device on this SPI bus, so the transaction is never ended
  digitalWriteFast(LDACPin, LOW);
  digitalWriteFast(SyncPin, HIGH);
  EEPROM.get(EEPROM_ZERO_CODE_CALIBRATION_ADDRESS, ZeroCodeCalibration); // Read the zero code calibration from the EEPROM
  loadScreenSaverSettings();
  ProgramDAC(28, 0, 0); // Clear DAC register
  ProgramDAC(12, 0, dacRangeCodes[RANGE_PLUS_MINUS_10V]); // Output range select register, all DACs
  ProgramDAC(16, 0, 31); // Power up DACs
  for (byte i = 0; i < N_CHANNELS; i++) {
    activeOutput[i].range = RANGE_PLUS_MINUS_10V;
    activeCalibration[i] = ZeroCodeCalibration[i];
    dacWriteNow(i, 32768); // 0V. The playback interrupts are not attached yet, so this cannot be interrupted.
  }
  fillSineTable();

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

  // The sample clock. IntervalTimer attaches handler() to the PIT interrupt and takes a PIT channel, which is then
  // stopped until a channel plays. The long period means it cannot tick before it is stopped.
  hardwareTimer.priority(PLAYBACK_IRQ_PRIORITY);
  hardwareTimer.begin(handler, 100000.0f);
  for (int i = 0; i < 4; i++) {
    if (IMXRT_PIT_CHANNELS[i].TCTRL & PIT_TCTRL_TEN) { // No other code in this sketch uses a PIT channel
      sampleClockChannel = &IMXRT_PIT_CHANNELS[i];
      break;
    }
  }
  if (sampleClockChannel == nullptr) {
    haltWithMessage("Startup Failed:", "No timer");
  }
  stopSampleClock();

  LoadDefaultSettings();

  // The trigger channels
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

// loop() handles commands from the PC and runs the joystick menu. All sample output is done by the playback interrupts,
// in Playback.ino, so loop() may wait (e.g. while a value is edited with the joystick) without disturbing playback.
void loop() {
  processUSBCommands(); // Read and execute a command from the PC, if one is available
  PPUSB.flush(); // Send any reply from this pass as a single USB packet
  updateScreenSaver(); // After the reply is sent, so that waking the screen never delays it
  takePendingOutputIfIdle(); // Settings that handler() had no time to take before the last channel stopped
  UpdateMenu();
  if (PPUSB.timedOut()) { // A serial USB message started, but didn't finish as expected
    HandleReadTimeout();
  }
}
