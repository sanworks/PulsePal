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


// Low level hardware access: DAC writes, the output range and software reset.
//
// Every function here that talks to the DAC uses the SPI bus, so after setup() it may only run in the playback
// interrupts, or in loop() with interrupts disabled. See "Interrupt rules" in Playback.ino.
//
// Functions in this file:
//   clampU16()
//   writeDACRegister()
//   dacWrite()
//   dacWriteChannels()
//   ProgramDAC()
//   setOutputRange()
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

// Writes one channel's input register. The output changes on the next falling edge of LDAC.
static inline void writeDACRegister(byte channel, uint16_t value) {
  value = clampU16(value, activeCalibration[channel]);
  digitalWriteFast(SyncPin, LOW);
  dacBuffer[0] = dacMap[channel];
  dacBuffer[1] = value >> 8;
  dacBuffer[2] = value & 0xFF;
  SPI.transfer(dacBuffer, 3);
  digitalWriteFast(SyncPin, HIGH);
}

// Writes the channels whose DACFlags are set, and updates their outputs together. Called by handler() on every sample.
void dacWrite() {
  digitalWriteFast(LDACPin, HIGH);
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (DACFlags[i]) {
      writeDACRegister(i, dacValue[i]);
      DACFlags[i] = 0;
    }
  }
  digitalWrite(LDACPin, HIGH); // Teensy 4.1 is too fast! Wait for DAC register to update
  digitalWriteFast(LDACPin, LOW);
}

// Writes the selected channels (one bit per channel) now, from dacValue[], and clears their DACFlags. The other
// channels' outputs do not change: their input registers still hold the values already on their outputs.
void dacWriteChannels(byte channelBits) {
  digitalWriteFast(LDACPin, HIGH);
  for (byte i = 0; i < N_CHANNELS; i++) {
    if (bitRead(channelBits, i)) {
      writeDACRegister(i, dacValue[i]);
      DACFlags[i] = 0;
    }
  }
  digitalWrite(LDACPin, HIGH); // Teensy 4.1 is too fast! Wait for DAC register to update
  digitalWriteFast(LDACPin, LOW);
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

// Programs the output range of all four DAC channels (see enum OutputRange), and the values that depend on it.
// The outputs keep their DAC codes, which now mean other voltages: the caller writes new ones straight after.
// The zero code calibration was measured in the -10V to 10V range, so it is only applied there.
void setOutputRange(byte newRange) {
  rangeIndex = newRange;
  ProgramDAC(12, 0, dacRangeCodes[newRange]); // Output range select register, all DACs
  ProgramDAC(28, 0, 0); // Clear DAC register. With the AD5754R's power-on settings, this is 0V in every range, which
                        // shortens the moment the old codes are on the outputs in the new range
  DACBits_ZeroVolts = (newRange >= RANGE_PLUS_MINUS_5V) ? 32768 : 0;
  for (byte i = 0; i < N_CHANNELS; i++) {
    activeCalibration[i] = (newRange == RANGE_PLUS_MINUS_10V) ? ZeroCodeCalibration[i] : 0;
  }
}

void Software_Reset() {
  SCB_AIRCR = 0x05FA0004;
  while (true);  // Wait for reset
}
