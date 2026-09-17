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


// Low level hardware access: DAC writes, fast digital I/O and software reset.
//
// Functions in this file:
//   dacWrite()
//   clampU16()
//   ProgramDAC()
//   digitalWriteDirect()
//   digitalReadDirect()
//   Software_Reset()

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
