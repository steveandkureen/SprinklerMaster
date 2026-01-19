#ifndef LCD_H
#define LCD_H

#include <stdint.h>

// I2C Configuration for SprinklerMaster
#define LCD_I2C_SDA_PIN 0  // GP0
#define LCD_I2C_SCL_PIN 1  // GP1
#define LCD_I2C_ADDR 0x27

// Public API Functions
void lcd_init(void);
void lcd_clear(void);
void lcd_set_text(uint8_t row, uint8_t col, const char* text);

#endif // LCD_H
