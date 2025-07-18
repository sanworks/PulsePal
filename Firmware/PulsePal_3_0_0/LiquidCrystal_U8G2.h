#ifndef LIQUIDCRYSTAL_U8G2_H
#define LIQUIDCRYSTAL_U8G2_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <Print.h>

class LiquidCrystal_U8G2 : public Print {
public:
  // Constructor: Takes a reference to an already-initialized u8g2 object
  LiquidCrystal_U8G2(U8G2 &u8g2);

  // LiquidCrystal API Methods
  void begin(uint8_t cols, uint8_t rows);
  void clear();
  void home();
  void setCursor(uint8_t col, uint8_t row);
  
  virtual size_t write(uint8_t);
  
  void noDisplay();
  void display();
  
  void noCursor();
  void cursor();
  
  void noBlink();
  void blink();
  
  void scrollDisplayLeft();
  void scrollDisplayRight();
  
  void createChar(uint8_t location, uint8_t charmap[]);

  // --- U8G2 Specific Extensions ---
  // Call this method after your drawing commands to update the physical display
  void render(); 
  
  // Optionally set a different u8g2 font
  void setFont(const uint8_t *font);

private:
  U8G2 &_u8g2; // Reference to the u8g2 object

  // Display dimensions in characters
  uint8_t _cols;
  uint8_t _rows;

  // Character dimensions in pixels
  uint8_t _char_width;
  uint8_t _char_height;
  int8_t _font_ascent;

  // Current cursor position in characters
  uint8_t _cursor_col;
  uint8_t _cursor_row;

  // Internal state flags
  bool _show_cursor;
  bool _show_blink;
  bool _display_on;

  // Blinking state
  unsigned long _blink_last_time;
  bool _blink_state;
  
  // Internal buffer for text content to support scrolling
  char **_text_buffer; 

  // Storage for custom characters
  uint8_t _custom_chars[8][8];

  // Helper method to draw the entire screen from the text buffer
  void _draw_screen();
};

#endif // LIQUIDCRYSTAL_U8G2_H