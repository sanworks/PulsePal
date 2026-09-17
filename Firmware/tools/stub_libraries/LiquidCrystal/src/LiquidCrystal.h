/*
  Compile-only stub of the Arduino LiquidCrystal library.

  The Pulse Pal 2 firmware uses LiquidCrystal for its 16x2 character LCD. This stub lets
  the Pulse Pal 2 build be compiled and compared on a machine without that library, e.g.
  by Firmware/tools/build_check.py or in continuous integration.

  Every method does nothing, so a binary built with this stub shows nothing on the screen.
  NEVER flash a binary built with this stub. To build a real Pulse Pal 2 binary, install
  the LiquidCrystal library with the Arduino IDE Library Manager.
*/
#ifndef LIQUIDCRYSTAL_STUB_H
#define LIQUIDCRYSTAL_STUB_H

#include <Arduino.h>

class LiquidCrystal : public Print {
public:
  LiquidCrystal(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) {}
  void begin(uint8_t, uint8_t) {}
  void clear() {}
  void home() {}
  void setCursor(uint8_t, uint8_t) {}
  void cursor() {}
  void noCursor() {}
  void blink() {}
  void noBlink() {}
  void display() {}
  void noDisplay() {}
  virtual size_t write(uint8_t) { return 1; }
  using Print::write;
};

#endif
