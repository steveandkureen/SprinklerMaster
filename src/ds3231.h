#ifndef DS3231_H
#define DS3231_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DS3231_I2C_INST    i2c1
#define DS3231_I2C_SDA_PIN 6
#define DS3231_I2C_SCL_PIN 7
#define DS3231_I2C_ADDR    0x68
#define DS3231_I2C_BAUD    100000
#define DS3231_SYNC_THRESHOLD_SEC 2

bool ds3231_init(void);
void ds3231_sync_from_ntp(uint32_t ntp_seconds);
bool ds3231_is_available(void);
bool ds3231_battery_ok(void);
int ds3231_get_time_str(char *buf, size_t buflen);

#endif
