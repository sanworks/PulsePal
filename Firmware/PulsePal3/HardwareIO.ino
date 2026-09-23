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
//   dacWrite()
//   outputRestingVoltages()
//   clampU16()
//   ProgramDAC()
//   startHardwareTimer()
//   stopHardwareTimer()
//   digitalWriteDirect()
//   digitalReadDirect()
//   Software_Reset()

// Stores a new 16-bit DAC value for an output channel (0-3). handler() writes it to the DAC at the start of its next
// cycle (within TIMER_PERIOD microseconds), whether or not a pulse train is playing.
// DACFlags[channel] must be set before DACFlag: if the timer interrupt runs between them, it clears DACFlag without
// writing this channel.
static inline void setDAC(byte channel, uint16_t value) {
  dacValue.uint16[channel] = value;
  DACFlags[channel] = 1;
  DACFlag = 1;
}

// Writes flagged channels to the DAC over SPI. Once the hardware timer has started, call this only from handler(),
// or from loop() while stopHardwareTimer() has it stopped.
// If a timer interrupt called dacWrite() during an SPI transfer started from loop(), the nested transfer would take the
// outer transfer's received bytes, and loop() would wait forever inside SPI.transfer(). Use setDAC() instead.
void dacWrite() {
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
  #if (HARDWARE_VERSION > 2)
    digitalWrite(LDACPin, HIGH); // Teensy 4.1 is too fast! Wait for DAC register to update
  #endif
  digitalWriteDirect(LDACPin,LOW);
}

// Sets all output channels to their resting voltage (written by handler() on its next cycle). Call after loading new parameters.
void outputRestingVoltages() {
  for (int i = 0; i < 4; i++) {
    setDAC(i, RestingVoltage[i]);
  }
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
    NVIC_ClearPendingIRQ(TC3_IRQn);
    NVIC_EnableIRQ(TC3_IRQn);
    TC_Start(TC1, 0);
  #else
    hardwareTimer.begin(handler, TIMER_PERIOD);
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
