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


// Low level hardware access: DAC writes, the hardware timer, fast digital I/O and software reset.
//
// Functions in this file:
//   setDAC()
//   dacLoad()
//   dacLatch()
//   dacWrite()
//   timerCountsSinceTick()
//   waitForTimerCount()
//   dacWriteTimed()
//   outputRestingVoltages()
//   setRestingVoltageIfIdle()
//   clampU16()
//   ProgramDAC()
//   startHardwareTimer()
//   stopHardwareTimer()
//   digitalWriteDirect()
//   digitalReadDirect()
//   Software_Reset()

// Stores a new 16-bit DAC value for an output channel (0-3). handler() writes it to the DAC at the start of its next
// cycle (DAC_LATCH_US after the cycle's timer tick, see dacWriteTimed()), whether or not a pulse train is playing.
// DACFlags[channel] must be set before DACFlag: if the timer interrupt runs between them, it clears DACFlag without
// writing this channel.
static inline void setDAC(byte channel, uint16_t value) {
  dacValue.uint16[channel] = value;
  DACFlags[channel] = 1;
  DACFlag = 1;
}

// Writes flagged channels into the DAC's input registers over SPI. LDAC is left high, so the outputs keep their
// voltages until dacLatch() updates them all at once.
// Once the hardware timer has started, call this only from handler(), or from loop() while stopHardwareTimer() has it
// stopped. If a timer interrupt wrote to the DAC during an SPI transfer started from loop(), the nested transfer would
// take the outer transfer's received bytes, and loop() would wait forever inside SPI.transfer(). Use setDAC() instead.
void dacLoad() {
  digitalWriteDirect(LDACPin,HIGH);
  for (int i = 0; i < 4; i++) {
    if (DACFlags[i]) {
      dacValue.uint16[i] = clampU16(dacValue.uint16[i], ZeroCodeCalibration[i]);
      digitalWriteDirect(SyncPin,LOW);
      dacBuffer[0] = dacMap[i];
      dacBuffer[1] = dacValue.byteArray[1+(i*2)];
      dacBuffer[2] = dacValue.byteArray[0+(i*2)];
      SPI.transfer(dacBuffer,3);
      digitalWriteDirect(SyncPin,HIGH);
      DACFlags[i] = 0;
    }
  }
}

// Updates every output to the value dacLoad() last wrote for it, together, on the falling edge of LDAC
static inline void dacLatch() {
  digitalWriteDirect(LDACPin,LOW);
}

// Writes flagged channels and updates the outputs at once: for setup(), loop()'s comm failure handling (while the timer
// is stopped) and AbortAllPulseTrains(). handler() uses dacWriteTimed(). The same rules as dacLoad() apply.
// The AD5754R (Pulse Pal 3) ignores a latch that comes less than about 60ns after the first channel write since the
// previous latch, and the outputs keep their old voltages. Here little more than the digitalWrite() below (about 50ns)
// separates the last write from the latch. That is enough because every caller writes all four channels, so the first
// write is microseconds old. To write a single channel and latch at once, wait longer first (Wave Pal and Synth Pal wait
// DAC_WRITE_TO_LATCH_NS, 200ns).
void dacWrite() {
  dacLoad();
  #if (HARDWARE_VERSION > 2)
    digitalWrite(LDACPin, HIGH); // Teensy 4.1 is too fast! A short wait before the latch (see above)
  #endif
  dacLatch();
}

#if (HARDWARE_VERSION > 2)
  static const uint32_t HARDWARE_TIMER_LOAD = (TIMER_PERIOD * TIMER_COUNTS_PER_US) - 1; // IntervalTimer's PIT load value
#endif

// Timer counts (TIMER_COUNTS_PER_US per microsecond) since the current cycle's timer tick
static inline uint32_t timerCountsSinceTick() {
  #if (HARDWARE_VERSION == 2)
    return TC1->TC_CHANNEL[0].TC_CV; // Counts up from 0 at each tick (see startHardwareTimer())
  #else
    // The PIT counts down from its load value, and ticks at 0. One register read: each takes about 0.1us, and reading
    // LDVAL too made the waits end up to 0.2us after their count.
    return HARDWARE_TIMER_LOAD - hardwareTimerChannel->CVAL;
  #endif
}

// Waits until the timer has counted to count since the tick. Stops waiting if the next cycle starts first.
static inline void waitForTimerCount(uint32_t count) {
  uint32_t last = timerCountsSinceTick();
  while (last < count) {
    uint32_t now = timerCountsSinceTick();
    if (now < last) {break;} // The next tick came: the counter restarted
    last = now;
  }
}

static const uint32_t DAC_LATCH_COUNT = DAC_LATCH_US * TIMER_COUNTS_PER_US;
static const uint32_t DAC_CHANNEL_WRITE_COUNT = DAC_CHANNEL_WRITE_US * TIMER_COUNTS_PER_US;
static const uint32_t DAC_LATCH_GAP_COUNT = DAC_LATCH_GAP_US * TIMER_COUNTS_PER_US;
static_assert(DAC_LATCH_US >= (4 * DAC_CHANNEL_WRITE_US) + DAC_LATCH_GAP_US, "DAC_LATCH_US must allow 4 channel writes and the gap");

// handler()'s DAC write. The outputs change DAC_LATCH_US after the timer tick, however many channels changed.
// The DAC updates every output when LDAC falls after the last channel's SPI write, so writing and latching at once
// delayed the update by the write time of every channel written in that cycle (1.13us each on Pulse Pal 3, 3.7us on
// Pulse Pal 2), and a pulse's width depended on what the other channels were doing. So this waits, writes, and latches
// at a fixed time. It waits before writing, so that the writes end just before the latch: SPI activity shows on the
// outputs as a few mV of digital feedthrough, and next to the edge it is hidden in it. (Writing at the end of one cycle
// and latching at the start of the next fixed the delay too, but put that feedthrough about 48us before every edge.)
// The writes end at least DAC_LATCH_GAP_US before the latch: the DAC updates at once only about 1.4us after a write
// (measured on Pulse Pal 2), and a sooner latch took effect late by the difference, so edges moved by 0.3-0.4us per
// channel written again.
// The cost: in a cycle that changes any output, the interrupt runs for at least DAC_LATCH_US.
void dacWriteTimed() {
  byte nChannels = DACFlags[0] + DACFlags[1] + DACFlags[2] + DACFlags[3];
  if (nChannels == 0) {
    return;
  }
  #if (HARDWARE_VERSION > 2)
    if (hardwareTimerChannel == nullptr) { // startHardwareTimer() did not find the timer: write at once
      dacWrite();
      return;
    }
  #endif
  waitForTimerCount(DAC_LATCH_COUNT - DAC_LATCH_GAP_COUNT - (nChannels * DAC_CHANNEL_WRITE_COUNT));
  dacLoad();
  waitForTimerCount(DAC_LATCH_COUNT);
  #if (HARDWARE_VERSION > 2)
    digitalWrite(LDACPin, HIGH); // Teensy 4.1 is too fast! Wait for DAC register to update
  #endif
  dacLatch();
}

// Sets all idle output channels to their resting voltage (written by handler() on its next cycle). Call after loading new parameters.
void outputRestingVoltages() {
  for (int i = 0; i < 4; i++) {
    setRestingVoltageIfIdle(i);
  }
}

// Sends an output channel's (0-3) resting voltage to the DAC if the channel is idle. A channel playing a pulse train
// takes the new resting voltage at its next transition to rest: setting it at once cut short the pulse it was playing.
// Call from loop() only. Interrupts are off for the check and setDAC(), so that handler() cannot start the channel
// between them and have its first pulse replaced by the resting voltage.
void setRestingVoltageIfIdle(byte channel) {
  noInterrupts();
  if ((StimulusStatus[channel] == 0) && (PreStimulusStatus[channel] == 0)) {
    setDAC(channel, RestingVoltage[channel]);
  }
  interrupts();
}

static inline uint16_t clampU16(uint16_t value, int16_t offset)
{
    int32_t corrected = (int32_t)value + offset;

    if (corrected < 0) {
        return 0;
    }

    if (corrected > UINT16_MAX) {
        return UINT16_MAX;
    }

    return (uint16_t)corrected;
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

// Starts the hardware timer that calls handler() every TIMER_PERIOD microseconds
void startHardwareTimer() {
  #if (HARDWARE_VERSION == 2)
    // The following hardware timer setup for Pulse Pal v2 is adapted from the DueTimer library by Ivan Seidel. (Thanks Ivan!!)
    // https://github.com/ivanseidel/DueTimer
    // Configure timer counter TC3 (TC1 channel 0) to interrupt every TIMER_PERIOD us.
    // The interrupt runs TC3_Handler() in Playback.ino, which calls handler().
    pmc_set_writeprotect(false); // Allow writes to the power management and timer registers
    pmc_enable_periph_clk(ID_TC3); // Enable the timer's peripheral clock
    TC_Configure(TC1, 0, TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC | TC_CMR_TCCLKS_TIMER_CLOCK2); // Count up from 0 to RC, then reset. Clocked at MCK/8 (10.5MHz)
    TC_SetRC(TC1, 0, (uint32_t)round(VARIANT_MCK / 8.0 * TIMER_PERIOD / 1000000.0)); // RC = ticks per period (525 for 50us)
    TC1->TC_CHANNEL[0].TC_IER = TC_IER_CPCS; // Enable the interrupt on RC compare...
    TC1->TC_CHANNEL[0].TC_IDR = ~TC_IER_CPCS; // ...and disable all other timer interrupts
    // The timer interrupt must be able to preempt the USB interrupt (0 is the highest priority). The Due core's USB
    // interrupt copies each received byte into SerialUSB's buffer itself, about 3.9us per byte and up to 512 bytes, and
    // at equal priority every timer cycle in that time was lost: op 92 (178 bytes) during playback stretched every
    // playing channel by 0.7ms, and a custom train upload by 2ms. The core sets the USB priority once, before setup().
    NVIC_SetPriority(UOTGHS_IRQn, 1);
    NVIC_SetPriority(TC3_IRQn, 0);
    NVIC_ClearPendingIRQ(TC3_IRQn);
    NVIC_EnableIRQ(TC3_IRQn);
    TC_Start(TC1, 0);
  #else
    hardwareTimer.begin(handler, TIMER_PERIOD);
    // dacWriteTimed() reads the timer's counter. IntervalTimer does not say which PIT channel it took, so find the
    // running channel loaded with this period.
    hardwareTimerChannel = nullptr;
    for (int i = 0; i < 4; i++) {
      if ((IMXRT_PIT_CHANNELS[i].TCTRL & PIT_TCTRL_TEN) && (IMXRT_PIT_CHANNELS[i].LDVAL == HARDWARE_TIMER_LOAD)) {
        hardwareTimerChannel = &IMXRT_PIT_CHANNELS[i];
        break;
      }
    }
  #endif
}

// Stops the hardware timer, so handler() no longer runs
void stopHardwareTimer() {
  #if (HARDWARE_VERSION == 2)
    NVIC_DisableIRQ(TC3_IRQn);
    TC_Stop(TC1, 0);
  #else
    hardwareTimer.end();
  #endif
}

void digitalWriteDirect(int pin, boolean val){
  #if (HARDWARE_VERSION == 2)
    if(val) g_APinDescription[pin].pPort -> PIO_SODR = g_APinDescription[pin].ulPin;
    else    g_APinDescription[pin].pPort -> PIO_CODR = g_APinDescription[pin].ulPin;
  #else
    digitalWriteFast(pin, val);
  #endif
}

byte digitalReadDirect(int pin){
  #if (HARDWARE_VERSION == 2)
    return !!(g_APinDescription[pin].pPort -> PIO_PDSR & g_APinDescription[pin].ulPin);
  #else
    return digitalReadFast(pin);
  #endif
}

void Software_Reset() {
  #if (HARDWARE_VERSION < 3)
    const int RSTC_KEY = 0xA5;
    RSTC->RSTC_CR = RSTC_CR_KEY(RSTC_KEY) | RSTC_CR_PROCRST | RSTC_CR_PERRST;
  #else
      SCB_AIRCR = 0x05FA0004;
  #endif
  while (true);  // Wait for reset
}
