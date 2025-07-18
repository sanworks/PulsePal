#include "LiquidCrystal_U8G2.h"

// --- Font Selection Logic ---

// Struct to hold information about available fixed-width fonts
struct FontInfo {
  const uint8_t* font;
  uint8_t width;
  uint8_t height;
};

// A database of suitable, monospaced (fixed-width) fonts from the U8G2 library.
// The selection logic in begin() will pick the largest one that fits.
// They are ordered roughly from smallest to largest.
static const FontInfo fontDatabase[] = {
  { u8g2_font_4x6_tf, 4, 6 },
  { u8g2_font_5x7_tf, 5, 7 },
  { u8g2_font_5x8_tf, 5, 8 },
  { u8g2_font_6x10_tf, 6, 10 },
  { u8g2_font_6x12_tf, 6, 12 },
  { u8g2_font_6x13_tf, 6, 13 },
  { u8g2_font_7x13_tf, 7, 13 },
  { u8g2_font_7x14_tf, 7, 14 },
  { u8g2_font_8x13_tf, 8, 13 },
  { u8g2_font_9x15_tf, 9, 15 },
  { u8g2_font_9x18_tf, 9, 18 },
  { u8g2_font_10x20_tf, 10, 20 }
};
static const int fontDatabaseSize = sizeof(fontDatabase) / sizeof(fontDatabase[0]);

// --- Class Implementation ---

LiquidCrystal_U8G2::LiquidCrystal_U8G2(U8G2 &u8g2) : _u8g2(u8g2) {
  _cols = 0;
  _rows = 0;
  _char_width = 0;
  _char_height = 0;
  _font_ascent = 0;
  _cursor_col = 0;
  _cursor_row = 0;
  _show_cursor = false;
  _show_blink = false;
  _display_on = true;
  _blink_last_time = 0;
  _blink_state = true;
  _text_buffer = nullptr;
  memset(_custom_chars, 0, sizeof(_custom_chars));
}

void LiquidCrystal_U8G2::begin(uint8_t cols, uint8_t rows) {
  _cols = cols;
  _rows = rows;
  
  const uint16_t display_width = _u8g2.getDisplayWidth();
  const uint16_t display_height = _u8g2.getDisplayHeight();
  
  const uint8_t max_char_w = display_width / _cols;
  const uint8_t max_char_h = display_height / _rows;

  const uint8_t* best_font = fontDatabase[0].font;

  for (int i = 0; i < fontDatabaseSize; i++) {
    if (fontDatabase[i].width <= max_char_w && fontDatabase[i].height <= max_char_h) {
      best_font = fontDatabase[i].font;
    }
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

  home();
  clear();
}

void LiquidCrystal_U8G2::setFont(const uint8_t *font) {
  _u8g2.setFont(font);
  _char_width = _u8g2.getMaxCharWidth();
  _char_height = _u8g2.getMaxCharHeight();
  _font_ascent = _u8g2.getFontAscent();
}

void LiquidCrystal_U8G2::clear() {
  for (int i = 0; i < _rows; ++i) {
    memset(_text_buffer[i], ' ', _cols);
  }
  home();
  render();
}

void LiquidCrystal_U8G2::home() {
  _cursor_col = 0;
  _cursor_row = 0;
}

void LiquidCrystal_U8G2::setCursor(uint8_t col, uint8_t row) {
  if (col >= _cols) col = _cols - 1;
  if (row >= _rows) row = _rows - 1;
  _cursor_col = col;
  _cursor_row = row;
}

size_t LiquidCrystal_U8G2::write(uint8_t c) {
  if (c == '\n') {
    _cursor_col = 0;
    _cursor_row++;
  } else if (c == '\r') {
    _cursor_col = 0;
  } else {
    if (_cursor_col < _cols && _cursor_row < _rows) {
      _text_buffer[_cursor_row][_cursor_col] = c;
      _cursor_col++;
    }
  }

  if (_cursor_col >= _cols) {
    _cursor_col = 0;
    _cursor_row++;
  }
  if (_cursor_row >= _rows) {
    _cursor_row = 0;
  }
  return 1;
}

void LiquidCrystal_U8G2::noDisplay() {
  _display_on = false;
  _u8g2.setPowerSave(1);
}

void LiquidCrystal_U8G2::display() {
  _display_on = true;
  _u8g2.setPowerSave(0);
}

void LiquidCrystal_U8G2::noCursor() {
  _show_cursor = false;
}

void LiquidCrystal_U8G2::cursor() {
  _show_cursor = true;
}

void LiquidCrystal_U8G2::noBlink() {
  _show_blink = false;
}

void LiquidCrystal_U8G2::blink() {
  _show_blink = true;
}

void LiquidCrystal_U8G2::scrollDisplayLeft() {
  for (int r = 0; r < _rows; ++r) {
    char first_char = _text_buffer[r][0];
    memmove(&_text_buffer[r][0], &_text_buffer[r][1], _cols - 1);
    _text_buffer[r][_cols - 1] = first_char;
  }
  render();
}

void LiquidCrystal_U8G2::scrollDisplayRight() {
  for (int r = 0; r < _rows; ++r) {
    char last_char = _text_buffer[r][_cols - 1];
    memmove(&_text_buffer[r][1], &_text_buffer[r][0], _cols - 1);
    _text_buffer[r][0] = last_char;
  }
  render();
}

void LiquidCrystal_U8G2::createChar(uint8_t location, uint8_t charmap[]) {
  if (location >= 8) return;
  memcpy(_custom_chars[location], charmap, 8);
}

void LiquidCrystal_U8G2::_draw_screen() {
  // === NEW: Calculate cell dimensions and centering offsets ===
  uint16_t display_w = _u8g2.getDisplayWidth();
  uint16_t display_h = _u8g2.getDisplayHeight();
  
  uint16_t cell_w = display_w / _cols;
  uint16_t cell_h = display_h / _rows;
  
  uint16_t x_offset = (cell_w - _char_width) / 2;
  uint16_t y_offset = (cell_h - _char_height) / 2;

  // Draw the text from the buffer
  for (int r = 0; r < _rows; ++r) {
    for (int c = 0; c < _cols; ++c) {
      uint8_t character = _text_buffer[r][c];
      
      // Calculate the top-left corner of the cell
      uint16_t cell_x = c * cell_w;
      uint16_t cell_y = r * cell_h;

      if (character < 8) { // Is it a custom character?
        // Custom chars are drawn pixel by pixel from the top-left of the char box
        uint16_t char_start_x = cell_x + x_offset;
        uint16_t char_start_y = cell_y + y_offset;
        for (int i = 0; i < 8; i++) { // row of the custom char bitmap
          for (int j = 0; j < 8; j++) { // col of the custom char bitmap
            if ((_custom_chars[character][i] >> (7-j)) & 1) {
               _u8g2.drawPixel(char_start_x + j, char_start_y + i);
            }
          }
        }
      } else { // Standard character
        // Standard chars are drawn from the baseline
        uint16_t baseline_x = cell_x + x_offset;
        uint16_t baseline_y = cell_y + y_offset + _font_ascent;
        _u8g2.drawGlyph(baseline_x, baseline_y, character);
      }
    }
  }

  // Update blinking state
  if (millis() - _blink_last_time > 500) {
    _blink_state = !_blink_state;
    _blink_last_time = millis();
  }
  
  // Calculate cursor/blink position based on the centered cell
  uint16_t cursor_cell_x = _cursor_col * cell_w;
  uint16_t cursor_cell_y = _cursor_row * cell_h;

  // Draw cursor (underline)
  if (_show_cursor) {
    uint16_t cursor_x = cursor_cell_x + x_offset;
    uint16_t cursor_y = cursor_cell_y + y_offset + _char_height; // Position below the char
    _u8g2.drawHLine(cursor_x, cursor_y, _char_width);
  }
  
  // Draw blink (block)
  if (_show_blink && _blink_state) {
    uint16_t blink_x = cursor_cell_x + x_offset;
    uint16_t blink_y = cursor_cell_y + y_offset;
    // To make it a true block, we draw over the character area
    _u8g2.setDrawColor(2); // Use XOR mode to invert colors under the block
    _u8g2.drawBox(blink_x, blink_y, _char_width, _char_height);
    _u8g2.setDrawColor(1); // Set color back to default
  }
}


void LiquidCrystal_U8G2::render() {
  if (!_display_on) return;
  
  _u8g2.clearBuffer();
  _draw_screen();
  _u8g2.sendBuffer();
}