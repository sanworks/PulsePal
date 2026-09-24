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


// Screen output. The screen is an OLED, driven through LiquidCrystal_U8G2, which emulates a 16x2 character LCD and
// centers each line. Never write to the screen from the playback interrupts: see "Interrupt rules" in Playback.ino.
//
// Functions in this file:
//   write2Screen()
//   showTopScreen()
//   trimmedCopy()
//   trimString()
//   runSplashScreen()

// Shows two lines of up to 16 characters. Each line is centered, so leading and trailing spaces are removed.
void write2Screen(const char* Line1, const char* Line2) {
  lcd.clear();
  lcd.print(trimmedCopy(Line1));
  lcd.setCursor(0, 1);
  lcd.print(trimmedCopy(Line2));
  lcd.render();
}

// Returns to the top of the joystick menu
void showTopScreen() {
  inMenu = MENU_TOP;
  write2Screen(CommanderString, TopScreenLine2);
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

// The Sanworks logo with twinkling stars, then the Wave Pal logo with a loading bar. As in Pulse Pal firmware.
void runSplashScreen() {
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
  u8g2.drawXBMP(0, 0, GFX_logo_width, GFX_logo_height, GFX_WPlogo);
  u8g2.sendBuffer();

  // Loading bar at the bottom of the screen: a fixed outline, filled left to right during the 2s logo display
  const uint32_t logoDuration = 2000;
  const uint8_t barHeight = 8;
  const uint8_t barWidth = 117; // Spans the logo text (x = 5 to 121)
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
}
