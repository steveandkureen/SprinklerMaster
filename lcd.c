#include "lcd.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include <stdio.h>

// Helper to safely delay - use busy_wait to avoid FreeRTOS conflicts
static inline void lcd_delay_ms(uint32_t ms) { busy_wait_us_32(ms * 1000); }

// PCF8574 Pin Mapping - Standard config
#define LCD_BACKLIGHT 0x08 // Bit 3
#define LCD_ENABLE 0x04    // Bit 2
#define LCD_RW 0x02        // Bit 1
#define LCD_RS 0x01        // Bit 0

// HD44780 Commands
#define LCD_CMD_CLEAR 0x01
#define LCD_CMD_HOME 0x02
#define LCD_CMD_ENTRY_MODE 0x06
#define LCD_CMD_DISPLAY_ON 0x0C // Display on, cursor off, blink off
#define LCD_CMD_FUNCTION_SET 0x28
#define LCD_CMD_SET_DDRAM 0x80

// DDRAM addresses for each row
static const uint8_t row_offsets[] = {0x00, 0x40};

// I2C Communication Helper
static void i2c_write_byte(uint8_t data) {
  i2c_write_blocking(i2c0, LCD_I2C_ADDR, &data, 1, false);
}

// Send 4-bit nibble to LCD with enable pulse (matches LiquidCrystal_I2C
// library)
static void lcd_write_nibble(uint8_t nibble, bool rs) {
  // Keep data in upper 4 bits, control signals in lower 4 bits
  uint8_t data = (nibble & 0xF0) | LCD_BACKLIGHT;
  if (rs)
    data |= LCD_RS;
  data &= ~LCD_RW; // Explicitly ensure RW=0 (write mode)

  // First send data
  i2c_write_byte(data);
  if (rs)
    sleep_us(50); // Extra settling time for RS in data mode

  // Pulse Enable high
  i2c_write_byte(data | LCD_ENABLE);
  sleep_us(1); // Enable pulse must be >450ns

  // Pulse Enable low
  i2c_write_byte(data & ~LCD_ENABLE);
  sleep_us(50); // Commands need >37us to settle
}

// Send command byte to LCD
static void lcd_write_command(uint8_t cmd) {
  lcd_write_nibble(cmd & 0xF0, false);
  lcd_write_nibble((cmd << 4) & 0xF0, false);
  lcd_delay_ms(1);
}

// Send character data to LCD
static void lcd_write_char(uint8_t ch) {
  sleep_us(200); // Additional setup time before character data
  lcd_write_nibble(ch & 0xF0, true);
  lcd_write_nibble((ch << 4) & 0xF0, true);
  lcd_delay_ms(1);
}

// Set cursor position
static void lcd_set_cursor(uint8_t row, uint8_t col) {
  if (row > 1)
    row = 1;
  if (col > 15)
    col = 15;

  uint8_t addr = row_offsets[row] + col;
  lcd_write_command(LCD_CMD_SET_DDRAM | addr);
  lcd_delay_ms(2); // Longer delay after cursor position
}

// Initialize I2C and LCD
void lcd_init(void) {
  // Initialize I2C0 at 50kHz (slower for reliability)
  i2c_init(i2c0, 50 * 1000);
  gpio_set_function(LCD_I2C_SDA_PIN, GPIO_FUNC_I2C);
  gpio_set_function(LCD_I2C_SCL_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(LCD_I2C_SDA_PIN);
  gpio_pull_up(LCD_I2C_SCL_PIN);

  // Wait for LCD power-up
  lcd_delay_ms(50);

  // Reset expander - turn backlight off initially
  uint8_t reset_val = 0x00;
  i2c_write_blocking(i2c0, LCD_I2C_ADDR, &reset_val, 1, false);
  lcd_delay_ms(1000); // Library does 1 second delay here

  // HD44780 initialization sequence (matches LiquidCrystal_I2C exactly)
  // We start in 8bit mode, try to set 4 bit mode
  lcd_write_nibble(0x30, false); // 0x03 << 4
  sleep_us(4500);                // wait min 4.1ms

  // second try
  lcd_write_nibble(0x30, false);
  sleep_us(4500);

  // third go
  lcd_write_nibble(0x30, false);
  sleep_us(150);

  // finally, set to 4-bit interface
  lcd_write_nibble(0x20, false); // 0x02 << 4

  // Now we can use 4-bit commands
  // Function set: 4-bit, 2-line, 5x8 font
  lcd_write_command(LCD_CMD_FUNCTION_SET);
  sleep_us(50);

  // Display on, cursor off, blink off
  lcd_write_command(LCD_CMD_DISPLAY_ON);
  sleep_us(50);

  // Clear display
  lcd_write_command(LCD_CMD_CLEAR);
  lcd_delay_ms(2);

  // Entry mode: increment cursor, no shift
  lcd_write_command(LCD_CMD_ENTRY_MODE);
  sleep_us(50);

  // Return home (library does this at end of init)
  lcd_write_command(LCD_CMD_HOME);
  sleep_us(2000); // Home command takes 2ms
}

// Clear LCD display
void lcd_clear(void) {
  lcd_write_command(LCD_CMD_CLEAR);
  lcd_delay_ms(2);
}

// Set text at specified row and column
void lcd_set_text(uint8_t row, uint8_t col, const char *text) {
  lcd_set_cursor(row, col);

  while (*text) {
    lcd_write_char(*text);
    text++;
  }
}
