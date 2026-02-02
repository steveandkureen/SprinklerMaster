#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

// Initialize the LCD display task
void lcd_display_init(void);

// Set status message during startup (shown on line 1)
void lcd_display_set_status(const char* status);

// Set IP address to display (shown on line 1 for 1 minute after connection)
void lcd_display_set_ip(const char* ip);

// Signal that startup is complete - switches line 0 to "Idle"
void lcd_display_startup_complete(void);

#endif // LCD_DISPLAY_H
