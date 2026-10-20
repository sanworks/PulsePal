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


// Low level hardware access: DAC writes, the hardware timer, fast digital I/O, the board revision and software reset.
//
// Functions in this file:
//   setDAC()
//   dacLoad()
//   dacLatch()
//   dacWrite()
//   latchNextWrite() (FLEXPWM_TIMER only)
//   maskLatchPulse() (FLEXPWM_TIMER only)
//   advanceTickCycles() (HW3 pin map 0 only)
//   timerCountsSinceTick()
//   waitForTimerCount()
//   dacWriteTimed()
//   outputRestingVoltages()
//   setRestingVoltageIfIdle()
//   clampU16()
//   ProgramDAC()
//   flexpwmTick() (FLEXPWM_TIMER only)
//   startHardwareTimer()
//   stopHardwareTimer()
//   pauseTimerForSD()
//   resumeTimerAfterSD()
//   digitalWriteDirect()
//   digitalReadDirect()
//   setupTriggerEdgeLatches() (HW3 only)
//   clearTriggerEdgeLatches() (HW3 only)
//   triggerEdgeLatched() (HW3 only)
//   readHardwareRevision()
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

#if FLEXPWM_TIMER
  // FlexPWM3 submodule 1 ticks every TIMER_PERIOD and, on its B output (pin 28, LDAC), pulses LDAC low for
  // FLEXPWM_LATCH_PULSE_US at DAC_LATCH_US after each tick: the falling edge updates every output to what was written
  // since the last one. The pulse is masked in every cycle that writes nothing (flexpwmTick() masks it as the cycle
  // starts, and latchNextWrite() unmasks it): an LDAC edge every 10us coupled into the outputs as a 100kHz comb of up to
  // 160uV (LDAC low for 2.5us of each cycle) or 17uV (this narrow pulse), and outputs at rest should carry nothing of
  // the timer. Both take effect at once (UPDATE_MASK). LDAC is high from the unmasking on, so the DAC holds the writes
  // that follow in its input registers until the pulse.
  static const uint16_t FLEXPWM_SM1 = 1 << 1;
  static uint16_t flexpwmLateWriteCount = 0; // Writes that end after this count of the cycle may miss its latch (set by startHardwareTimer())
  static bool latchNextCycle = false; // The last writes ended too late for their cycle's latch: flexpwmTick() leaves the next one unmasked
  static inline void latchNextWrite() {
    IMXRT_FLEXPWM3.MASK = (IMXRT_FLEXPWM3.MASK & ~FLEXPWM_MASK_MASKB(FLEXPWM_SM1)) | FLEXPWM_MASK_UPDATE_MASK(FLEXPWM_SM1);
  }
  static inline void maskLatchPulse() {
    IMXRT_FLEXPWM3.MASK = IMXRT_FLEXPWM3.MASK | FLEXPWM_MASK_MASKB(FLEXPWM_SM1) | FLEXPWM_MASK_UPDATE_MASK(FLEXPWM_SM1);
  }
  // Writes flagged channels for the timer to latch, from handler(). If the writes end within DAC_LATCH_GAP_US of the
  // latch, or after it (handler() started late, behind a higher priority interrupt, or ran past the next tick: the reload
  // flag is set again), the latch can miss some or all of them, so the next cycle latches too. Without that, the pulse
  // would be masked in a next cycle that writes nothing, and the outputs would keep the old voltages until some later write.
  static inline void dacLoadForTimerLatch() {
    latchNextWrite();
    dacLoad();
    if ((IMXRT_FLEXPWM3.SM[1].CNT >= flexpwmLateWriteCount) || (IMXRT_FLEXPWM3.SM[1].STS & FLEXPWM_SMSTS_RF)) {
      latchNextCycle = true;
    }
  }
#endif

// Writes flagged channels and updates the outputs at once: for setup(), loop()'s comm failure handling (while the timer
// is stopped) and AbortAllPulseTrains(). handler() uses dacWriteTimed(). The same rules as dacLoad() apply.
// The AD5754R (Pulse Pal 3) ignores a latch that comes less than about 60ns after the first channel write since the
// previous latch, and the outputs keep their old voltages. Here little more than the digitalWrite() below (about 50ns)
// separates the last write from the latch. That is enough because every caller writes all four channels, so the first
// write is microseconds old. To write a single channel and latch at once, wait longer first (Wave Pal and Synth Pal wait
// DAC_WRITE_TO_LATCH_NS, 200ns).
//
// While FlexPWM3 drives LDAC (FLEXPWM_TIMER, once startHardwareTimer() has run), the pin is not a GPIO and dacLatch()
// does nothing: the timer latches the writes instead, DAC_LATCH_US after the tick of the cycle they are written in (or
// after the next tick, if they end too close to that latch). AbortAllPulseTrains() is the only caller then.
void dacWrite() {
  #if FLEXPWM_TIMER
    if (flexpwmTimerRunning) {
      dacLoadForTimerLatch();
      return;
    }
  #endif
  dacLoad();
  #if (HARDWARE_VERSION > 2)
    digitalWrite(LDACPin, HIGH); // Teensy 4.1 is too fast! A short wait before the latch (see above)
  #endif
  dacLatch();
}

#if (HARDWARE_VERSION == 2)
  #define DAC_TIMING_COUNTS_PER_US TIMER_COUNTS_PER_US // dacWriteTimed() reads the timer's own counter
#elif !FLEXPWM_TIMER // Pulse Pal 3, pin map 0
  static const uint32_t HARDWARE_TIMER_LOAD = (TIMER_PERIOD * TIMER_COUNTS_PER_US) - 1; // IntervalTimer's PIT load value
  static const uint32_t CPU_CYCLES_PER_TIMER_PERIOD = TIMER_PERIOD * CPU_CYCLES_PER_US; // 30000 at 600MHz
  static_assert(F_CPU % 1000000 == 0, "A timer cycle must be a whole number of CPU cycles");
  #define DAC_TIMING_COUNTS_PER_US CPU_CYCLES_PER_US // dacWriteTimed() counts CPU cycles (see timerCountsSinceTick())

  // Called by handler() as it starts: moves tickCycles on to the tick that began this cycle. The CPU's clock and the
  // PIT's come from the same crystal, and a timer cycle is exactly CPU_CYCLES_PER_TIMER_PERIOD CPU cycles, so each tick
  // comes that many cycles after the one before, and this needs no read of the timer. A tick that comes while
  // interrupts are off for longer than a cycle (an EEPROM write, rule 10 in AGENTS.md) does not run handler() again,
  // so handler() then finds more than a cycle passed since tickCycles, and moves on to the last tick.
  static inline void advanceTickCycles() {
    tickCycles += CPU_CYCLES_PER_TIMER_PERIOD;
    int32_t cyclesSinceTick = (int32_t)(ARM_DWT_CYCCNT - tickCycles);
    if (cyclesSinceTick >= (int32_t)CPU_CYCLES_PER_TIMER_PERIOD) { // Ticks were missed
      tickCycles += (cyclesSinceTick / CPU_CYCLES_PER_TIMER_PERIOD) * CPU_CYCLES_PER_TIMER_PERIOD;
    }
  }
#endif

#if !FLEXPWM_TIMER
// Counts since the current cycle's timer tick, of the clock dacWriteTimed() is timed with (DAC_TIMING_COUNTS_PER_US per
// microsecond). Pulse Pal 2 reads the timer's own counter. Pulse Pal 3 reads the CPU's cycle counter, from tickCycles,
// its count at the tick (see advanceTickCycles()): its PIT counts at 24MHz (41.7ns per count), and each read of the PIT
// takes about 0.1us, longer while USB transfers use the bus. Waits that polled the PIT ended up to a read after their
// count, so an edge moved by about 40ns when other channels changed in the same cycle (their writes moved the reads),
// and by up to 80ns while the PC sent commands. The cycle counter counts at 600MHz and takes about one cycle to read.
static inline uint32_t timerCountsSinceTick() {
  #if (HARDWARE_VERSION == 2)
    return TC1->TC_CHANNEL[0].TC_CV; // Counts up from 0 at each tick (see startHardwareTimer())
  #else
    return ARM_DWT_CYCCNT - tickCycles;
  #endif
}

// Waits until count since the tick
static inline void waitForTimerCount(uint32_t count) {
  #if (HARDWARE_VERSION == 2)
    uint32_t last = timerCountsSinceTick();
    while (last < count) {
      uint32_t now = timerCountsSinceTick();
      if (now < last) {break;} // The next tick came: the counter restarted. Stop waiting
      last = now;
    }
  #else
    while (timerCountsSinceTick() < count) {} // Only handler() moves tickCycles on, so the count never restarts here
  #endif
}

static const uint32_t DAC_LATCH_COUNT = DAC_LATCH_US * DAC_TIMING_COUNTS_PER_US;
static const uint32_t DAC_CHANNEL_WRITE_COUNT = DAC_CHANNEL_WRITE_US * DAC_TIMING_COUNTS_PER_US;
static const uint32_t DAC_LATCH_GAP_COUNT = DAC_LATCH_GAP_US * DAC_TIMING_COUNTS_PER_US;
#endif
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
//
// With FLEXPWM_TIMER (Pulse Pal 3 at 10us), the timer itself latches the DAC DAC_LATCH_US after the tick, so the writes
// go out at once and the interrupt does not wait: at 10us, waiting took nearly all of the CPU (94-96% with all four
// outputs changing every cycle, and ticks were lost), and the hardware latch leaves 69-72%. The writes must end
// DAC_LATCH_GAP_US before the latch; four of them end about 5.2us after the tick. Their digital feedthrough comes up to
// about 7us before the edge, instead of just before it.
void dacWriteTimed() {
  byte nChannels = DACFlags[0] + DACFlags[1] + DACFlags[2] + DACFlags[3];
  if (nChannels == 0) {
    return;
  }
  #if FLEXPWM_TIMER
    dacLoadForTimerLatch();
  #else
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
  #endif
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

#if FLEXPWM_TIMER
  // FlexPWM3 submodule 1's reload interrupt, every TIMER_PERIOD: the timer tick. This cycle's LDAC pulse is masked first,
  // and dacWriteTimed() unmasks it if handler() writes the DAC (see latchNextWrite()). It stays unmasked after writes
  // that ended too late for the last cycle's latch (see dacLoadForTimerLatch()).
  void flexpwmTick() {
    IMXRT_FLEXPWM3.SM[1].STS = FLEXPWM_SMSTS_RF;
    if (latchNextCycle) {
      latchNextCycle = false;
    } else {
      maskLatchPulse();
    }
    handler();
    asm volatile("dsb"); // The flag's clear must reach the peripheral before the interrupt returns, or it runs again
  }
#endif

// Starts the hardware timer that calls handler() every TIMER_PERIOD microseconds. Runs at startup and after a comm
// failure only, so it runs from flash (see FLASHMEM in PulsePal.ino)
FLASHMEM void startHardwareTimer() {
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
    // interrupt copied each received byte into SerialUSB's buffer itself, about 3.9us per byte and up to 512 bytes, and
    // at equal priority every timer cycle in that time was lost: op 92 (178 bytes) during playback stretched every
    // playing channel by 0.7ms, and a custom train upload by 2ms. ArCOM now takes received bytes from loop() instead
    // (see ArCOM.h), but the USB interrupt still runs for the PC's other requests. The core sets the USB priority once,
    // before setup().
    NVIC_SetPriority(UOTGHS_IRQn, 1);
    NVIC_SetPriority(TC3_IRQn, 0);
    NVIC_ClearPendingIRQ(TC3_IRQn);
    NVIC_EnableIRQ(TC3_IRQn);
    TC_Start(TC1, 0);
  #else
    clearTriggerEdgeLatches();
  #if FLEXPWM_TIMER
    // FlexPWM3 submodule 1 counts the bus clock (F_BUS_ACTUAL, 150MHz) from 0 to a period's counts - 1, and its reload
    // interrupt runs handler() through flexpwmTick(). Its B output drives LDAC (pin 28): inverted (POLB), so that LDAC
    // stays high and falls for FLEXPWM_LATCH_PULSE_US at DAC_LATCH_US after each tick. The DAC updates on that falling
    // edge. The pulse is masked in cycles that write nothing (see latchNextWrite()). The timer's clock comes from the
    // CPU's crystal, as the PIT's did. Priority 64, above USB's 128, for the reasons given for the PIT below.
    {
      IMXRT_FLEXPWM_t *p = &IMXRT_FLEXPWM3;
      uint32_t countsPerUs = F_BUS_ACTUAL / 1000000;
      uint32_t latchCount = (uint32_t)(DAC_LATCH_US * countsPerUs);
      flexpwmLateWriteCount = latchCount - (uint32_t)(DAC_LATCH_GAP_US * countsPerUs);
      latchNextCycle = false;
      NVIC_DISABLE_IRQ(IRQ_FLEXPWM3_1);
      p->MCTRL &= ~FLEXPWM_MCTRL_RUN(FLEXPWM_SM1);
      p->MCTRL |= FLEXPWM_MCTRL_CLDOK(FLEXPWM_SM1);
      p->SM[1].CTRL2 = FLEXPWM_SMCTRL2_INDEP | FLEXPWM_SMCTRL2_WAITEN | FLEXPWM_SMCTRL2_DBGEN | FLEXPWM_SMCTRL2_FRCEN;
      p->SM[1].CTRL = FLEXPWM_SMCTRL_FULL | FLEXPWM_SMCTRL_PRSC(0);
      p->SM[1].OCTRL = FLEXPWM_SMOCTRL_POLB;
      p->SM[1].DTCNT1 = 0;
      p->SM[1].INIT = 0;
      p->SM[1].VAL0 = 0;
      p->SM[1].VAL1 = TIMER_PERIOD * countsPerUs - 1;
      p->SM[1].VAL4 = latchCount;
      p->SM[1].VAL5 = latchCount + (uint32_t)(FLEXPWM_LATCH_PULSE_US * countsPerUs);
      p->MCTRL |= FLEXPWM_MCTRL_LDOK(FLEXPWM_SM1);
      p->SM[1].CTRL2 |= FLEXPWM_SMCTRL2_FORCE; // Start the count at INIT
      maskLatchPulse();
      p->SM[1].STS = 0xFFFF;
      p->SM[1].INTEN = FLEXPWM_SMINTEN_RIE;
      p->OUTEN |= FLEXPWM_OUTEN_PWMB_EN(FLEXPWM_SM1);
      p->MCTRL |= FLEXPWM_MCTRL_RUN(FLEXPWM_SM1);
      *(portConfigRegister(LDACPin)) = 1; // Pin 28's ALT1: FLEXPWM3_PWMB01
      flexpwmTimerRunning = true;
      attachInterruptVector(IRQ_FLEXPWM3_1, flexpwmTick);
      NVIC_SET_PRIORITY(IRQ_FLEXPWM3_1, 64);
      NVIC_ENABLE_IRQ(IRQ_FLEXPWM3_1);
    }
  #else
    // The timer interrupt must be able to preempt the USB interrupt (lower numbers are higher priorities). The Teensy
    // core starts every interrupt at priority 128, the timer's and USB's included, so a USB interrupt running at a
    // timer tick held handler() back until it returned: up to 5.5us while the PC opened the port, and dacWriteTimed()
    // then finished its writes, and the outputs changed, up to 2us late. USB loses nothing by waiting: its transfers
    // are DMA, and the controller holds off the PC until the interrupt has queued the next buffer. A USB interrupt
    // already waited for handler() whenever the tick came first: 9.9us at most, measured with all four outputs
    // changing every cycle and param sync edges arriving. 64 is also above SdFat's microSD interrupt (96), which
    // FIFO_SDIO uses only in sd.begin(). The core's SysTick (32) stays above it.
    hardwareTimer.priority(64);
    hardwareTimer.begin(handler, TIMER_PERIOD);
    // dacWriteTimed() needs the timer's counter, to find when it ticked (below). IntervalTimer does not say which PIT
    // channel it took, so find the running channel loaded with this period.
    hardwareTimerChannel = nullptr;
    for (int i = 0; i < 4; i++) {
      if ((IMXRT_PIT_CHANNELS[i].TCTRL & PIT_TCTRL_TEN) && (IMXRT_PIT_CHANNELS[i].LDVAL == HARDWARE_TIMER_LOAD)) {
        hardwareTimerChannel = &IMXRT_PIT_CHANNELS[i];
        break;
      }
    }
    // tickCycles starts at the last tick the counter passed (its start, or a tick since), as the time of one register
    // read places it, and advanceTickCycles() moves it on by whole cycles: so the outputs change at a constant time after
    // the tick until the timer is started again. Interrupts are off so that handler() cannot run between the reads and
    // the write.
    if (hardwareTimerChannel != nullptr) {
      noInterrupts();
      uint32_t cycles = ARM_DWT_CYCCNT;
      uint32_t countsSinceTick = HARDWARE_TIMER_LOAD - hardwareTimerChannel->CVAL; // The PIT counts down, and ticks at 0
      tickCycles = cycles - ((countsSinceTick * CPU_CYCLES_PER_US) / TIMER_COUNTS_PER_US);
      interrupts();
    }
  #endif
  #endif
}

// Stops the hardware timer, so handler() no longer runs
void stopHardwareTimer() {
  #if (HARDWARE_VERSION == 2)
    NVIC_DisableIRQ(TC3_IRQn);
    TC_Stop(TC1, 0);
  #elif FLEXPWM_TIMER
    NVIC_DISABLE_IRQ(IRQ_FLEXPWM3_1);
    IMXRT_FLEXPWM3.SM[1].INTEN = 0;
    IMXRT_FLEXPWM3.MCTRL &= ~FLEXPWM_MCTRL_RUN(FLEXPWM_SM1);
    flexpwmTimerRunning = false;
    pinMode(LDACPin, OUTPUT); // LDAC back to a GPIO, low, so dacWrite() can latch while the timer is stopped
    digitalWriteDirect(LDACPin, LOW);
  #else
    hardwareTimer.end();
  #endif
}

// Pulse Pal 2's microSD card shares the SPI bus with the DAC, and handler() writes the DAC from the timer interrupt:
// in any cycle that changes an output, and when loop() has asked for a write with setDAC(), playing or not. A write in
// the middle of one of the card's transfers left loop() waiting for ever (see the note above dacLoad()), and the card
// so stuck that the device did not start again until it was unplugged. So loop() holds the timer interrupt off while
// it uses the card: call pauseTimerForSD() before the card's functions and resumeTimerAfterSD() after them, around
// those calls only, not around screen messages or waits. The pair may nest, and leaves the interrupt as it found it.
// Meanwhile handler() does not run: a trigger is seen once it runs again (a pulse that ends first is missed), and a
// channel playing would pause, so op 90 refuses to use the card while a channel plays (see playbackOngoing()).
// Pulse Pal 3's card has a bus of its own (SDIO), and there these do nothing.
#if (HARDWARE_VERSION == 2)
  static byte sdTimerPauseDepth = 0; // Calls of pauseTimerForSD() not yet matched by resumeTimerAfterSD()
  static bool sdTimerWasEnabled = false; // Whether the timer interrupt was on when the outermost pause began
#endif

static inline void pauseTimerForSD() {
  #if (HARDWARE_VERSION == 2)
    if (sdTimerPauseDepth++ == 0) {
      sdTimerWasEnabled = (NVIC->ISER[((uint32_t)TC3_IRQn) >> 5] & (1UL << (((uint32_t)TC3_IRQn) & 0x1F))) != 0;
      NVIC_DisableIRQ(TC3_IRQn); // A tick meanwhile stays pending, and runs handler() once the interrupt is back on
    }
  #endif
}

static inline void resumeTimerAfterSD() {
  #if (HARDWARE_VERSION == 2)
    if ((sdTimerPauseDepth > 0) && (--sdTimerPauseDepth == 0) && sdTimerWasEnabled) {
      NVIC_EnableIRQ(TC3_IRQn);
    }
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

#if (HARDWARE_VERSION > 2)
  // handler() reads each trigger line once per cycle, and a pulse shorter than a cycle can come and go between two
  // reads. A 1 cycle pulse from another Pulse Pal (50us, before Pulse Pal 3 ran at 10us) is one: it reached the trigger pin about 0.4us short of a cycle,
  // so at some phases of the two devices' timers every pulse fell between reads, and a cycle that writes the DAC reads
  // the lines DAC_LATCH_US late, so reads can be 57.5us apart. Measured on a Pulse Pal 3: 0.6% of 50us pulses missed
  // with no channel playing, 9% with channels playing.
  // So each trigger line's GPIO port also latches every edge on it, rising or falling, in its interrupt status register
  // (ISR). The register latches whether or not the edge's interrupt is enabled, and here it is not: no interrupt runs.
  // handler() reads and clears the latch each cycle (triggerEdgeLatched()), and sees any pulse longer than
  // TRIGGER_CHATTER_NS, wherever it falls in the cycle. Call once from setup(), after pinMode().
  FLASHMEM void setupTriggerEdgeLatches() {
    for (int x = 0; x < 2; x++) {
      triggerGPIO[x] = (IMXRT_GPIO_t *)portOutputRegister(TriggerLines[x]); // DR, the port's first register
      triggerPinMask[x] = digitalPinToBitMask(TriggerLines[x]);
      triggerGPIO[x]->IMR &= ~triggerPinMask[x]; // No interrupt
      triggerGPIO[x]->EDGE_SEL |= triggerPinMask[x]; // Latch both edges. Without it, ICR's default latches while the line is low
    }
  }

  // Forgets the edges latched so far. Called by handler() TRIGGER_CHATTER_NS after it reads the trigger lines (see
  // there), and by startHardwareTimer(): edges that arrived while the timer was stopped (a comm failure's message) would
  // otherwise reach handler() as a pulse in its first cycle.
  static inline void clearTriggerEdgeLatches() {
    for (int x = 0; x < 2; x++) {
      triggerGPIO[x]->ISR = triggerPinMask[x]; // Write 1 to clear
    }
  }

  // True if trigger line 0 or 1 changed since its latch was last cleared, and clears it. Call it after reading the
  // line's level. The latch holds an edge a few ns before the level shows it (measured: never later), so an edge
  // between the two reads is in this latch, or in neither read. Read before the level, the latch could miss an edge
  // the level showed, and hold it at the next cycle: a pulse that had come and gone, to handler().
  static inline bool triggerEdgeLatched(byte line) {
    if (triggerGPIO[line]->ISR & triggerPinMask[line]) {
      triggerGPIO[line]->ISR = triggerPinMask[line]; // Write 1 to clear
      return true;
    }
    return false;
  }

  // Reads the circuit board revision into hardwareRevision (op 100). The board encodes it in binary by grounding some
  // of the pins in pcbVersionMap: a grounded pin is a 1 bit, pcbVersionMap[0] the lowest, so a board that grounds none
  // reads 0. The Bpod state machine reads its board revision the same way. The pull-ups are on only for the read: the
  // pins are left high impedance (INPUT_DISABLE: no pull resistor and no keeper), so no current flows into the grounded
  // ones. Startup only, so it runs from flash (FLASHMEM).
  FLASHMEM void readHardwareRevision() {
    for (unsigned int i = 0; i < sizeof(pcbVersionMap); i++) {
      pinMode(pcbVersionMap[i], INPUT_PULLUP);
    }
    // Time for the pull-ups (about 22k) to raise the open pins: one read before its pull-up had raised it would be
    // taken for a grounded pin. A margin: a Pulse Pal 3 read the same revision without it, as Bpod reads, 50 times out
    // of 50.
    delayMicroseconds(10);
    hardwareRevision = 0;
    for (unsigned int i = 0; i < sizeof(pcbVersionMap); i++) {
      if (digitalRead(pcbVersionMap[i]) == LOW) {
        bitSet(hardwareRevision, i);
      }
      pinMode(pcbVersionMap[i], INPUT_DISABLE);
    }
  }
#endif

void Software_Reset() {
  #if (HARDWARE_VERSION < 3)
    const int RSTC_KEY = 0xA5;
    RSTC->RSTC_CR = RSTC_CR_KEY(RSTC_KEY) | RSTC_CR_PROCRST | RSTC_CR_PERRST;
  #else
      SCB_AIRCR = 0x05FA0004;
  #endif
  while (true);  // Wait for reset
}
