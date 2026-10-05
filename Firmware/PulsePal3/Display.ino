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


// Screen output. On Pulse Pal 2 the screen is a 16x2 character LCD. On Pulse Pal 3 it is an OLED, driven through
// LiquidCrystal_U8G2 which emulates the character LCD interface.
//
// Functions in this file:
//   write2Screen()
//   LCD_home()
//   LCD_clear()
//   LCD_print()
//   LCD_print_no_trim_no_render()
//   trimmedCopy()
//   LCD_setCursor()
//   LCD_cursor()
//   LCD_noCursor()
//   trimString()
//   updateScreenSaver()
//   startScreenSaver()
//   endScreenSaver()
//   loadScreenSaverSettings()
//   saveScreenSaverSettings()
//   runSplashScreen()

void write2Screen(const char* Line1, const char* Line2) {
    LCD_clear();
    #if (HARDWARE_VERSION == 3)
      lcd.print(trimmedCopy(Line1));
    #else
      // In-line LCD_print without render
      LCD_home();
      LCD_print(Line1);
    #endif
    LCD_setCursor(0, 1); 
    LCD_print(Line2);
}

void LCD_home() {
    lcd.home();
}

void LCD_clear() {
    lcd.clear();
}

void LCD_print(const char* value) {
  #if (HARDWARE_VERSION == 3)
    lcd.print(trimmedCopy(value)); // Pulse Pal 3 centers each line, so leading and trailing spaces are removed
    lcd.render();
  #else
    lcd.print(value);
  #endif
}

void LCD_print_no_trim_no_render(const char* value) {
  lcd.print(value);
}

void LCD_setCursor(uint8_t col, uint8_t row) {
    lcd.setCursor(col, row);
}

void LCD_cursor() {
  #if (HARDWARE_VERSION == 3)
    lcd.cursor();
    lcd.render();
  #else
    lcd.cursor();
  #endif
}

void LCD_noCursor() {
  #if (HARDWARE_VERSION == 3)
    lcd.noCursor();
    lcd.render();
  #else
    lcd.noCursor();
  #endif
}

// Returns a copy of text with leading and trailing spaces removed. Screen text is usually a string literal,
// which trimString() must not modify in place: literals are shared between call sites, so trimming one would
// change the others. The returned buffer is reused by the next call.
const char* trimmedCopy(const char* text) {
  static char trimmed[33];
  strncpy(trimmed, text, sizeof(trimmed) - 1);
  trimmed[sizeof(trimmed) - 1] = '\0';
  trimString(trimmed);
  return trimmed;
}

void trimString(char *str) {
  if (str == nullptr || *str == '\0') {
    return;
  }
  char *end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) {
    end--;
  }
  *(end + 1) = '\0';
  char *start = str;
  while (*start && isspace((unsigned char)*start)) {
    start++;
  }
  if (start != str) {
    memmove(str, start, strlen(start) + 1);
  }
}

// Screen saver, called by loop() on every pass, after any reply to the PC has been sent. Activity (screenSaverActivity:
// a command from the PC, a rising edge on a trigger channel, or a joystick click or push) restarts the idle time and
// brings the screen back. Once the device has been idle for screenSaverTimeout seconds, the screen dims, if the
// screen saver is on. Settings changed by op 99 or the joystick menu are saved here, once no channel is playing.
//
// The screen is written from loop() only (rule 3 in AGENTS.md): handler() just sets screenSaverActivity, so a trigger
// is handled in the cycle it arrives, and the screen wakes after that cycle. The screen is on the second SPI bus and
// the DAC on the first, so the timer interrupt runs on time during a screen write, however long the write takes.
void updateScreenSaver() {
  if (screenSaverActivity) {
    screenSaverActivity = false;
    lastActivityTime = millis();
    if (screenDimmed) {
      endScreenSaver();
    }
  } else if (!screenDimmed && screenSaverEnabled &&
             ((millis() - lastActivityTime) >= ((uint32_t)screenSaverTimeout * 1000))) {
    startScreenSaver();
  }
  if (screenSaverSavePending && (StimulatingState == 0)) {
    screenSaverSavePending = false;
    saveScreenSaverSettings();
  }
}

void startScreenSaver() {
  #if (HARDWARE_VERSION == 3)
    u8g2.setContrast(SCREEN_SAVER_DIM_BRIGHTNESS); // One short command: the menu stays on the screen, dimmed
  #endif
  screenDimmed = true;
}

// Returns the screen to the menu as it was left
void endScreenSaver() {
  #if (HARDWARE_VERSION == 3)
    u8g2.setContrast(SCREEN_BRIGHTNESS);
  #endif
  screenDimmed = false;
}

// Reads the screen saver settings from the EEPROM (Pulse Pal 3), at setup. Bytes never written read 0xFF, which is not
// a valid state, so a new device keeps the defaults: on, with SCREEN_SAVER_DEFAULT_TIMEOUT.
FLASHMEM void loadScreenSaverSettings() { // Startup only, so it runs from flash (see FLASHMEM in PulsePal3.ino)
  #if (HARDWARE_VERSION > 2)
    byte enabled = 0;
    uint16_t timeout = 0;
    EEPROM.get(EEPROM_SCREEN_SAVER_ADDRESS, enabled);
    EEPROM.get(EEPROM_SCREEN_SAVER_ADDRESS + sizeof(enabled), timeout);
    if ((enabled <= MAX_SCREEN_SAVER_STATE) && (timeout > 0)) {
      screenSaverEnabled = enabled;
      screenSaverTimeout = timeout;
    }
  #endif
}

// Writes the screen saver settings to the EEPROM (Pulse Pal 3). Only bytes that changed are written. Call it only while
// no channel is playing, as updateScreenSaver() does: on Teensy 4.1 the EEPROM is emulated in flash, and interrupts are
// off while a byte is programmed. That is about 20us (measured: during playback, an output edge moved by up to 12.5us),
// and, once in about 2000 changes, a flash sector erase of tens of ms (datasheet), which loses timer cycles.
void saveScreenSaverSettings() {
  #if (HARDWARE_VERSION > 2)
    EEPROM.put(EEPROM_SCREEN_SAVER_ADDRESS, screenSaverEnabled);
    EEPROM.put(EEPROM_SCREEN_SAVER_ADDRESS + sizeof(screenSaverEnabled), screenSaverTimeout);
  #endif
}

FLASHMEM void runSplashScreen() { // Startup only, so it runs from flash (see FLASHMEM in PulsePal3.ino)
  #if (HARDWARE_VERSION == 3)
    // SplashScreen
      u8g2.clearBuffer();
      u8g2.drawXBMP(0, 0, GFX_logo_width, GFX_logo_height, GFX_SWlogo);
      u8g2.sendBuffer();
      // Twinkling stars
      uint8_t StarPixelX[25] = {10, 3,  40, 22, 49, 17, 33, 9,  53, 25, 26,  40, 79, 108, 125, 98, 84, 120, 90, 103, 115, 128, 125, 95, 108};
      uint8_t StarPixelY[25] = {3,  30, 9,  14, 1,  26, 22, 17, 11, 5,  19,  27, 7,  25,  16,  29, 12, 23,  3,  8,   4,   0,   32, 20, 16};

      for (int i = 0; i < 150; i++) {
        for (int j = 0; j < 25; j++) {
          if (random(100) < 5) {
            u8g2.setDrawColor(0);
          } else {
            u8g2.setDrawColor(1);
          }
          u8g2.drawPixel(StarPixelX[j], StarPixelY[j]);
        }
        u8g2.sendBuffer();
        delay(10);
      }
      u8g2.setDrawColor(1);

      u8g2.clearBuffer();
      u8g2.drawXBMP(0, 0, GFX_logo_width, GFX_logo_height, GFX_PPlogo);
      u8g2.sendBuffer();

      // Loading bar at the bottom of the screen: a fixed outline, filled left to right during the 2s logo display
      const uint32_t logoDuration = 2000;
      const uint8_t barHeight = 8;
      const uint8_t barWidth = 122; // Widest centered bar that stays within the logo text (x = 3 to 125)
      const uint8_t barX = (u8g2.getDisplayWidth() - barWidth) / 2;
      const uint8_t barY = u8g2.getDisplayHeight() - barHeight - 6;
      // Fill sits inside the 1px outline with a 1px blank gap on all sides
      const uint8_t fillX = barX + 2;
      const uint8_t fillY = barY + 2;
      const uint8_t fillWidth = barWidth - 4;
      const uint8_t fillHeight = barHeight - 4;

      u8g2.drawFrame(barX, barY, barWidth, barHeight);
      u8g2.sendBuffer();

      uint32_t logoStartTime = millis();
      uint8_t fillLength = 0;
      while (fillLength < fillWidth) {
        uint32_t elapsed = millis() - logoStartTime;
        uint8_t targetLength = (elapsed < logoDuration) ? (fillWidth * elapsed) / logoDuration : fillWidth;
        if (targetLength > fillLength) {
          while (fillLength < targetLength) {
            u8g2.drawVLine(fillX + fillLength, fillY, fillHeight);
            fillLength++;
          }
          u8g2.sendBuffer();
        } else {
          delay(5);
        }
      }
    #endif
}
