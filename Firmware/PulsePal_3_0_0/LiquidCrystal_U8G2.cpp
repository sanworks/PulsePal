#include "LiquidCrystal_U8G2.h"

// --- Font Selection & Class Implementation ---
// (No changes to the top part of the file, including constructor, begin, etc.)
// ... (code from the previous version remains here) ...

struct FontInfo {
  const uint8_t* font; uint8_t width; uint8_t height;
};
static const FontInfo fontDatabase[] = {
  { u8g2_font_4x6_tf, 4, 6 }, { u8g2_font_5x7_tf, 5, 7 }, { u8g2_font_5x8_tf, 5, 8 },
  { u8g2_font_6x10_tf, 6, 10 }, { u8g2_font_6x12_tf, 6, 12 }, { u8g2_font_6x13_tf, 6, 13 },
  { u8g2_font_7x13_tf, 7, 13 }, { u8g2_font_7x14_tf, 7, 14 }, { u8g2_font_8x13_tf, 8, 13 },
  { u8g2_font_9x15_tf, 9, 15 }, { u8g2_font_9x18_tf, 9, 18 }, { u8g2_font_10x20_tf, 10, 20 }
};
static const int fontDatabaseSize = sizeof(fontDatabase) / sizeof(fontDatabase[0]);
LiquidCrystal_U8G2::LiquidCrystal_U8G2(U8G2 &u8g2) : _u8g2(u8g2) {
  _cols = 0; _rows = 0; _char_width = 0; _char_height = 0; _font_ascent = 0;
  _cursor_col = 0; _cursor_row = 0; _show_cursor = false; _show_blink = false;
  _display_on = true; _blink_last_time = 0; _blink_state = true;
  _text_buffer = nullptr; memset(_custom_chars, 0, sizeof(_custom_chars));
}
void LiquidCrystal_U8G2::begin(uint8_t cols, uint8_t rows) {
  _cols = cols; _rows = rows;
  const uint16_t display_width = _u8g2.getDisplayWidth();
  const uint16_t display_height = _u8g2.getDisplayHeight();
  const uint8_t max_char_w = display_width / _cols;
  const uint8_t max_char_h = display_height / _rows;
  const uint8_t* best_font = fontDatabase[0].font;
  for (int i = 0; i < fontDatabaseSize; i++) {
    if (fontDatabase[i].width <= max_char_w && fontDatabase[i].height <= max_char_h) { best_font = fontDatabase[i].font; }
  }
  setFont(best_font);
  if (_text_buffer) {
    for (int i = 0; i < _rows; ++i) delete[] _text_buffer[i];
    delete[] _text_buffer;
  }
  _text_buffer = new char*[_rows];
  for (int i = 0; i < _rows; ++i) {
    _text_buffer[i] = new char[_cols + 1];
    memset(_text_buffer[i], ' ', _cols);
    _text_buffer[i][_cols] = '\0';
  }
  home(); clear();
}
void LiquidCrystal_U8G2::setFont(const uint8_t *font) {
  _u8g2.setFont(font); _char_width = _u8g2.getMaxCharWidth();
  _char_height = _u8g2.getMaxCharHeight(); _font_ascent = _u8g2.getFontAscent();
}
void LiquidCrystal_U8G2::clear() {
  for (int i = 0; i < _rows; ++i) memset(_text_buffer[i], ' ', _cols);
  home(); // render(); // render on clear disabled for this firmware
}
void LiquidCrystal_U8G2::home() { _cursor_col = 0; _cursor_row = 0; }
void LiquidCrystal_U8G2::setCursor(uint8_t col, uint8_t row) {
  if (col >= _cols) col = _cols - 1; if (row >= _rows) row = _rows - 1;
  _cursor_col = col; _cursor_row = row;
}
size_t LiquidCrystal_U8G2::write(uint8_t c) {
  if (c == '\n') { _cursor_col = 0; _cursor_row++; } else if (c == '\r') { _cursor_col = 0; }
  else { if (_cursor_col < _cols && _cursor_row < _rows) { _text_buffer[_cursor_row][_cursor_col] = c; _cursor_col++; } }
  if (_cursor_col >= _cols) { _cursor_col = 0; _cursor_row++; } if (_cursor_row >= _rows) { _cursor_row = 0; }
  return 1;
}
void LiquidCrystal_U8G2::noDisplay() { _display_on = false; _u8g2.setPowerSave(1); }
void LiquidCrystal_U8G2::display() { _display_on = true; _u8g2.setPowerSave(0); }
void LiquidCrystal_U8G2::noCursor() { _show_cursor = false; }
void LiquidCrystal_U8G2::cursor() { _show_cursor = true; }
void LiquidCrystal_U8G2::noBlink() { _show_blink = false; }
void LiquidCrystal_U8G2::blink() { _show_blink = true; }
void LiquidCrystal_U8G2::scrollDisplayLeft() {
  for (int r = 0; r < _rows; ++r) { char c = _text_buffer[r][0]; memmove(&_text_buffer[r][0], &_text_buffer[r][1], _cols - 1); _text_buffer[r][_cols - 1] = c; }
  render();
}
void LiquidCrystal_U8G2::scrollDisplayRight() {
  for (int r = 0; r < _rows; ++r) { char c = _text_buffer[r][_cols - 1]; memmove(&_text_buffer[r][1], &_text_buffer[r][0], _cols - 1); _text_buffer[r][0] = c; }
  render();
}
void LiquidCrystal_U8G2::createChar(uint8_t location, uint8_t charmap[]) {
  if (location >= 8) return; memcpy(_custom_chars[location], charmap, 8);
}


// =========================================================================
// === THIS IS THE MODIFIED METHOD with horizontal text centering ===
// =========================================================================
void LiquidCrystal_U8G2::_draw_screen() {
  uint16_t display_w = _u8g2.getDisplayWidth();
  uint16_t display_h = _u8g2.getDisplayHeight();

  // Vertical Centering Logic (from previous step)
  uint16_t total_visual_text_height = _rows * _font_ascent;
  uint16_t vertical_gap_size = 0;
  if (display_h > total_visual_text_height) {
    vertical_gap_size = (display_h - total_visual_text_height) / (_rows + 1);
  }

  // Update blinking state once per render
  if (millis() - _blink_last_time > 500) { _blink_state = !_blink_state; _blink_last_time = millis(); }

  // Draw each row
  for (int r = 0; r < _rows; ++r) {
    // --- NEW: Horizontal Centering Logic for each row ---

    // 1. Find the length of the actual content by trimming trailing spaces
    int trimmed_len = 0;
    int last_char_idx = -1;
    for (int i = 0; i < _cols; i++) {
      if (_text_buffer[r][i] != ' ') {
        last_char_idx = i;
      }
    }
    trimmed_len = last_char_idx + 1;

    // 2. Calculate the pixel width of the trimmed text and its starting X
    uint16_t text_pixel_width = trimmed_len * _char_width;
    uint16_t start_x = 0;
    if (display_w > text_pixel_width) {
      start_x = (display_w - text_pixel_width) / 2;
    }
    
    // 3. Calculate the Y position for this row (same as before)
    uint16_t char_visual_top_y = (r + 1) * vertical_gap_size + r * _font_ascent;

    // 4. Draw the trimmed string character by character
    for (int c = 0; c < trimmed_len; c++) {
      uint8_t character = _text_buffer[r][c];
      uint16_t char_x = start_x + (c * _char_width);

      if (character < 8) { // Custom character
        uint16_t custom_char_offset_y = (_font_ascent > 8) ? (_font_ascent - 8) / 2 : 0;
        uint16_t char_start_y = char_visual_top_y + custom_char_offset_y;
        for (int i = 0; i < 8; i++) { 
          for (int j = 0; j < 8; j++) {
            if ((_custom_chars[character][i] >> (7-j)) & 1) {
               _u8g2.drawPixel(char_x + j, char_start_y + i);
            }
          }
        }
      } else { // Standard character
        uint16_t baseline_y = char_visual_top_y + _font_ascent;
        _u8g2.drawGlyph(char_x, baseline_y, character);
      }
    }

    // 5. Draw cursor/blink if it belongs on this row
    if (_cursor_row == r) {
      // The cursor can be positioned after the last character
      uint16_t cursor_char_x = start_x + (_cursor_col * _char_width);

      if (_show_cursor) {
        uint16_t cursor_y = char_visual_top_y + _char_height; 
        _u8g2.drawHLine(cursor_char_x, cursor_y, _char_width);
      }
      
      if (_show_blink && _blink_state) {
        _u8g2.setDrawColor(2);
        _u8g2.drawBox(cursor_char_x, char_visual_top_y, _char_width, _font_ascent);
        _u8g2.setDrawColor(1);
      }
    }
  }
}

void LiquidCrystal_U8G2::render() {
  if (!_display_on) return;
  _u8g2.clearBuffer();
  _draw_screen();
  _u8g2.sendBuffer();
}