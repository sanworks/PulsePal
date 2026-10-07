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


// Low level hardware access: DAC writes, output ranges, the sample clock's timer and software reset.
//
// Every function here that talks to the DAC uses the SPI bus, so after setup() it may only run in the playback
// interrupts, or in loop() with interrupts disabled. See "Interrupt rules" in Playback.ino.
//
// Functions in this file:
//   clampU16()
//   writeDACRegister()
//   dacLoad()
//   dacLatch()
//   cyclesSinceTick()
//   waitForCyclesSinceTick()
//   dacLoadTimed()
//   dacLatchTimed()
//   dacWriteTimed()
//   dacWriteNow()
//   dacSwitchRange()
//   ProgramDAC()
//   nextSamplePeriodTicks()
//   startSampleClock()
//   advanceSampleClock()
//   stopSampleClock()
//   Software_Reset()

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

// Writes one channel's input register. The output changes on the next falling edge of LDAC, which the caller has set
// high.
static inline void writeDACRegister(byte channel, uint16_t value) {
  value = clampU16(value, activeCalibration[channel]);
  digitalWriteFast(SyncPin, LOW);
  dacBuffer[0] = dacMap[channel];
  dacBuffer[1] = value >> 8;
  dacBuffer[2] = value & 0xFF;
  SPI.transfer(dacBuffer, 3);
  digitalWriteFast(SyncPin, HIGH);
}

// Writes the channels whose DACFlags are set into the DAC's input registers. LDAC is left high, so the outputs keep their
// voltages until dacLatch() updates them all at once.
static inline void dacLoad() {
  digitalWriteFast(LDACPin, HIGH);
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (DACFlags[i]) {
      writeDACRegister(i, dacValue[i]);
      dacOutput[i] = dacValue[i];
      DACFlags[i] = 0;
    }
  }
}

// Updates every output to the value last written for it, together, on the falling edge of LDAC
static inline void dacLatch() {
  digitalWrite(LDACPin, HIGH); // Teensy 4.1 is too fast! Wait for DAC register to update
  digitalWriteFast(LDACPin, LOW);
}

// CPU cycles since the tick that began the current sample period. See "The time base" above startSampleClock().
static inline uint32_t cyclesSinceTick() {
  return ARM_DWT_CYCCNT - tickCycles;
}

// Waits until the CPU's cycle counter is cycles past the tick that began the current sample period
static inline void waitForCyclesSinceTick(uint32_t cycles) {
  while (cyclesSinceTick() < cycles) {}
}

static const uint32_t DAC_LATCH_CYCLES = DAC_LATCH_US * CPU_CYCLES_PER_US;
static const uint32_t DAC_WRITE_OVERHEAD_CYCLES = DAC_WRITE_OVERHEAD_US * CPU_CYCLES_PER_US;
static const uint32_t DAC_CHANNEL_WRITE_CYCLES = DAC_CHANNEL_WRITE_US * CPU_CYCLES_PER_US;
static const uint32_t DAC_LATCH_GAP_CYCLES = DAC_LATCH_GAP_US * CPU_CYCLES_PER_US;
static const uint32_t DAC_LATE_CYCLES = (DAC_LATCH_US - DAC_LATCH_GAP_US + DAC_LATE_TOLERANCE_US) * CPU_CYCLES_PER_US;
static_assert(DAC_LATCH_US >= DAC_WRITE_OVERHEAD_US + (4 * DAC_CHANNEL_WRITE_US) + DAC_LATCH_GAP_US,
              "DAC_LATCH_US must allow 4 channel writes and the gap");
static_assert(DAC_LATCH_US * MAX_SAMPLING_RATE < 1000000, "The DAC update must come within the shortest sample period");

// The sample clock's DAC update, in two steps: dacLoadTimed() writes, then dacLatchTimed() updates the outputs, and
// handler() fetches the next samples in between. dacWriteTimed() does both.
// As Pulse Pal firmware's dacWriteTimed() (see rule 1 in /Firmware/PulsePal3/AGENTS.md), the channels whose code changes
// are written, and all outputs are updated together at DAC_LATCH_US after the tick, however many channels were written.
// The DAC updates every output when LDAC falls after the last write, so writing and latching at once would make the
// update time depend on how many channels changed (1.2us per channel): a square wave's edges would move as other
// channels' codes repeated or changed. Waiting also absorbs a late start of the interrupt.
// dacLoadTimed() waits before writing, so that the writes end just before the latch: SPI activity shows on the outputs
// as a few mV of digital feedthrough, and next to the update it is hidden in it. The writes end DAC_LATCH_GAP_US before
// the latch: the DAC updates at once only some time after a write, and a sooner latch takes effect late. Writes that
// end more than DAC_LATE_TOLERANCE_US later than that, or a latch that comes after DAC_LATCH_US, count in lateUpdates.
// Returns the number of channels written.
static boolean dacWritesEndedLate = false; // Set by dacLoadTimed() for dacLatchTimed()

static inline byte dacLoadTimed() {
  byte nChannels = DACFlags[0] + DACFlags[1] + DACFlags[2] + DACFlags[3];
  if (nChannels == 0) {
    return 0;
  }
  waitForCyclesSinceTick(DAC_LATCH_CYCLES - DAC_LATCH_GAP_CYCLES - DAC_WRITE_OVERHEAD_CYCLES -
                         (nChannels * DAC_CHANNEL_WRITE_CYCLES));
  dacLoad();
  dacWritesEndedLate = (cyclesSinceTick() > DAC_LATE_CYCLES);
  return nChannels;
}

// Updates the outputs written by dacLoadTimed(), DAC_LATCH_US after the tick
static inline void dacLatchTimed() {
  if (dacWritesEndedLate || (cyclesSinceTick() > DAC_LATCH_CYCLES)) {
    lateUpdates++;
  }
  waitForCyclesSinceTick(DAC_LATCH_CYCLES);
  dacLatch();
}

// Writes the channels whose code changes, and updates the outputs DAC_LATCH_US after the tick
void dacWriteTimed() {
  if (dacLoadTimed()) {
    dacLatchTimed();
  }
}

// Writes one channel and updates its output at once, outside the sample clock's timed updates: in setup(), and for
// takePendingOutput(), which runs just after a timed update or while the sample clock is stopped. The other channels'
// input registers then hold the codes already on their outputs, so the latch does not change them.
void dacWriteNow(byte channel, uint16_t code) {
  digitalWriteFast(LDACPin, HIGH);
  writeDACRegister(channel, code);
  dacOutput[channel] = code;
  DACFlags[channel] = 0;
  delayNanoseconds(DAC_WRITE_TO_LATCH_NS); // A sooner latch is ignored, and the output keeps its old code
  dacLatch();
}

// Programs a channel's output range on the DAC (activeOutput[channel].range), and sets its output to a code in that
// range. The code goes into the input register first and the latch follows the range write straight away, so the
// output shows its old code in the new range only between the two, for well under a microsecond. The range write
// gives the code write the time it needs before the latch (DAC_WRITE_TO_LATCH_NS): measured, the code is latched.
// Called as dacWriteNow() is, with activeCalibration[channel] already set for the new range.
void dacSwitchRange(byte channel, uint16_t code) {
  digitalWriteFast(LDACPin, HIGH);
  writeDACRegister(channel, code);
  digitalWriteFast(SyncPin, LOW);
  dacBuffer[0] = 8 + dacMap[channel]; // The output range select register of this DAC channel
  dacBuffer[1] = 0;
  dacBuffer[2] = dacRangeCodes[activeOutput[channel].range];
  SPI.transfer(dacBuffer, 3);
  digitalWriteFast(SyncPin, HIGH);
  dacOutput[channel] = code;
  DACFlags[channel] = 0;
  dacLatch();
}

void ProgramDAC(byte Data1, byte Data2, byte Data3) {
  digitalWriteFast(LDACPin, HIGH);
  digitalWriteFast(SyncPin, LOW);
  SPI.transfer(Data1);
  SPI.transfer(Data2);
  SPI.transfer(Data3);
  digitalWriteFast(SyncPin, HIGH);
  digitalWriteFast(LDACPin, LOW);
}

// THE TIME BASE. The sample clock is a PIT timer channel, counting a 24MHz clock, and the DAC updates are timed from
// the CPU's cycle counter, ARM_DWT_CYCCNT. Both clocks come from the same crystal, at exactly CPU_CYCLES_PER_TIMER_TICK
// CPU cycles per timer tick, so the cycle count of each tick follows from the lengths of the periods loaded into the PIT:
// tickCycles is set when the clock starts, and advanceSampleClock() adds each period as it ends. Polling the PIT's own
// counter instead, as Pulse Pal firmware does, placed updates up to 0.14us late, depending on where the polls fell: a PIT
// register read takes about 0.1us, and a read of the cycle counter one CPU cycle.

// The length of the next sample period in timer ticks: samplePeriodTicks, or one tick more often enough to make up the
// remainder. See "How playback works" in Playback.ino.
static inline uint32_t nextSamplePeriodTicks() {
  ditherAccumulator += ditherRemainder;
  if (ditherAccumulator >= ditherDenominator) {
    ditherAccumulator -= ditherDenominator;
    return samplePeriodTicks + 1;
  }
  return samplePeriodTicks;
}

// Starts the sample clock from this moment: handler() runs at the end of each sample period. The PIT loads LDVAL as it
// starts, and loads it again each time a period ends, so the first period's length goes in before the start and the
// second's straight after. The firmware drives the PIT channel directly, rather than through IntervalTimer's begin(),
// so that a trigger starts it in a few register writes. Call with interrupts disabled, or from a playback interrupt.
void startSampleClock() {
  ditherAccumulator = 0;
  currentPeriodLoad = nextSamplePeriodTicks() - 1; // A load value of n gives a period of n + 1 ticks
  nextPeriodLoad = nextSamplePeriodTicks() - 1;
  sampleClockChannel->LDVAL = currentPeriodLoad;
  sampleClockChannel->TCTRL = PIT_TCTRL_TIE | PIT_TCTRL_TEN;
  tickCycles = ARM_DWT_CYCCNT; // The first period begins
  sampleClockChannel->LDVAL = nextPeriodLoad;
  timerRunning = true;
}

// Called by handler() as it starts: a tick has ended the period whose load value is currentPeriodLoad, and the PIT has
// begun the next one, with the load value it took from nextPeriodLoad. Moves tickCycles on to this tick, and loads the
// length of the period after the one that has begun (see "How playback works" in Playback.ino).
// tickCycles is checked against the PIT's own counter, which it matches to within the time of the register read, unless
// a tick was missed (interrupts disabled for a whole sample period). It is then set from the counter, and the tick is
// counted in lateUpdates.
static inline void advanceSampleClock() {
  tickCycles += CPU_CYCLES_PER_TIMER_TICK * (currentPeriodLoad + 1);
  currentPeriodLoad = nextPeriodLoad;
  nextPeriodLoad = nextSamplePeriodTicks() - 1;
  sampleClockChannel->LDVAL = nextPeriodLoad;
  uint32_t timerCycles = (currentPeriodLoad - sampleClockChannel->CVAL) * CPU_CYCLES_PER_TIMER_TICK;
  int32_t error = (int32_t)(cyclesSinceTick() - timerCycles);
  if ((error > TICK_RESYNC_CYCLES) || (error < -TICK_RESYNC_CYCLES)) {
    tickCycles = ARM_DWT_CYCCNT - timerCycles;
    lateUpdates++;
  }
}

// Stops the sample clock. handler() calls it when no channel is playing. A tick that has already raised the interrupt
// is cleared, so handler() does not run again.
void stopSampleClock() {
  sampleClockChannel->TCTRL = 0;
  sampleClockChannel->TFLG = 1;
  NVIC_CLEAR_PENDING(IRQ_PIT);
  timerRunning = false;
}

void Software_Reset() {
  SCB_AIRCR = 0x05FA0004;
  while (true);  // Wait for reset
}
