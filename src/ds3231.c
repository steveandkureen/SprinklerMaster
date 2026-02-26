#include "ds3231.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

// DS3231 register layout (registers 0x00-0x06)
// 0x00: Seconds (BCD, bit 7 unused)
// 0x01: Minutes (BCD)
// 0x02: Hours   (BCD, 24-hour mode: bit 6=0)
// 0x03: Day of week (1-7, unused by us)
// 0x04: Date    (BCD, 1-31)
// 0x05: Month   (BCD, 1-12, bit 7 = century)
// 0x06: Year    (BCD, 0-99)

#define DS3231_REG_SECONDS 0x00
#define DS3231_REG_STATUS  0x0F
#define DS3231_STATUS_OSF  0x80  // Oscillator Stop Flag (bit 7)
#define DS3231_NUM_TIME_REGS 7

// BCD conversion helpers
static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

static uint8_t bin_to_bcd(uint8_t bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

// Read consecutive registers from DS3231
static bool ds3231_read_registers(uint8_t reg, uint8_t *buf, size_t len) {
    int ret = i2c_write_blocking(DS3231_I2C_INST, DS3231_I2C_ADDR, &reg, 1, true);
    if (ret < 0) return false;

    ret = i2c_read_blocking(DS3231_I2C_INST, DS3231_I2C_ADDR, buf, len, false);
    return ret == (int)len;
}

// Write consecutive registers to DS3231
static bool ds3231_write_registers(uint8_t reg, const uint8_t *data, size_t len) {
    uint8_t buf[8];
    buf[0] = reg;
    memcpy(&buf[1], data, len);

    int ret = i2c_write_blocking(DS3231_I2C_INST, DS3231_I2C_ADDR, buf, len + 1, false);
    return ret == (int)(len + 1);
}

// Read time from DS3231 into struct tm (UTC)
static bool ds3231_read_time(struct tm *t) {
    uint8_t regs[DS3231_NUM_TIME_REGS];
    if (!ds3231_read_registers(DS3231_REG_SECONDS, regs, DS3231_NUM_TIME_REGS))
        return false;

    t->tm_sec  = bcd_to_bin(regs[0] & 0x7F);
    t->tm_min  = bcd_to_bin(regs[1] & 0x7F);
    t->tm_hour = bcd_to_bin(regs[2] & 0x3F); // 24-hour mode
    t->tm_mday = bcd_to_bin(regs[4] & 0x3F);
    t->tm_mon  = bcd_to_bin(regs[5] & 0x1F) - 1; // struct tm months are 0-11
    t->tm_year = bcd_to_bin(regs[6]) + 100;        // struct tm years since 1900; DS3231 year 0-99 = 2000-2099
    t->tm_wday = 0;
    t->tm_yday = 0;
    t->tm_isdst = 0;

    return true;
}

// Write struct tm (UTC) to DS3231
static bool ds3231_write_time(const struct tm *t) {
    uint8_t regs[DS3231_NUM_TIME_REGS];

    regs[0] = bin_to_bcd(t->tm_sec);
    regs[1] = bin_to_bcd(t->tm_min);
    regs[2] = bin_to_bcd(t->tm_hour);   // 24-hour mode (bit 6 = 0)
    regs[3] = bin_to_bcd(t->tm_wday + 1); // DS3231 day 1-7
    regs[4] = bin_to_bcd(t->tm_mday);
    regs[5] = bin_to_bcd(t->tm_mon + 1);  // DS3231 month 1-12
    regs[6] = bin_to_bcd(t->tm_year - 100); // DS3231 year 0-99 (2000-2099)

    return ds3231_write_registers(DS3231_REG_SECONDS, regs, DS3231_NUM_TIME_REGS);
}

// Convert struct tm (UTC) to epoch seconds without relying on mktime/timegm.
// Valid for dates 2000-01-01 through 2099-12-31.
static time_t tm_to_epoch_utc(const struct tm *t) {
    // Days in each month (non-leap)
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};

    int year = t->tm_year + 1900;
    int mon  = t->tm_mon; // 0-11

    // Count days from 1970-01-01 to start of this year
    int32_t days = 0;
    for (int y = 1970; y < year; y++) {
        days += 365;
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))
            days++;
    }

    // Add days for completed months this year
    for (int m = 0; m < mon; m++) {
        days += mdays[m];
        if (m == 1 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)))
            days++; // leap day
    }

    // Add days in current month
    days += t->tm_mday - 1;

    return (time_t)days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
}

static bool g_ds3231_available = false;

bool ds3231_is_available(void) {
    return g_ds3231_available;
}

bool ds3231_battery_ok(void) {
    if (!g_ds3231_available) return false;

    uint8_t status;
    if (!ds3231_read_registers(DS3231_REG_STATUS, &status, 1))
        return false;

    // OSF bit 7 = 1 means oscillator stopped (battery dead or removed)
    return (status & DS3231_STATUS_OSF) == 0;
}

int ds3231_get_time_str(char *buf, size_t buflen) {
    if (!g_ds3231_available)
        return snprintf(buf, buflen, "Not connected");

    struct tm t;
    if (!ds3231_read_time(&t))
        return snprintf(buf, buflen, "Read error");

    // Convert UTC from DS3231 to local time using the TZ env var
    time_t epoch = tm_to_epoch_utc(&t);
    struct tm local;
    localtime_r(&epoch, &local);

    return snprintf(buf, buflen, "%04d-%02d-%02d %02d:%02d:%02d",
                    local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                    local.tm_hour, local.tm_min, local.tm_sec);
}

bool ds3231_init(void) {
    // Initialize I2C1 hardware
    i2c_init(DS3231_I2C_INST, DS3231_I2C_BAUD);
    gpio_set_function(DS3231_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DS3231_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DS3231_I2C_SDA_PIN);
    gpio_pull_up(DS3231_I2C_SCL_PIN);

    // Try to read time from DS3231
    struct tm rtc_time;
    if (!ds3231_read_time(&rtc_time)) {
        printf("RTC: DS3231 I2C read failed\n");
        return false;
    }

    time_t epoch = tm_to_epoch_utc(&rtc_time);

    // Validate: reject times before 2020-01-01 (epoch 1577836800)
    if (epoch < 1577836800) {
        printf("RTC: DS3231 time invalid (epoch=%ld, before 2020)\n", (long)epoch);
        return false;
    }

    // Set system clock from RTC
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    printf("RTC: DS3231 time set to %04d-%02d-%02d %02d:%02d:%02d UTC (epoch=%ld)\n",
           rtc_time.tm_year + 1900, rtc_time.tm_mon + 1, rtc_time.tm_mday,
           rtc_time.tm_hour, rtc_time.tm_min, rtc_time.tm_sec, (long)epoch);

    // Log OSF status
    uint8_t status;
    if (ds3231_read_registers(DS3231_REG_STATUS, &status, 1)) {
        printf("RTC: Status register = 0x%02X (OSF=%d)\n", status, (status & DS3231_STATUS_OSF) ? 1 : 0);
    }

    g_ds3231_available = true;
    return true;
}

void ds3231_sync_from_ntp(uint32_t ntp_seconds) {
    // First: set system clock from NTP (preserves original SNTP behavior)
    struct timeval tv = { .tv_sec = ntp_seconds, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    printf("NTP: System clock set to %lu\n", (unsigned long)ntp_seconds);

    // Read current DS3231 time
    struct tm rtc_time;
    if (!ds3231_read_time(&rtc_time)) {
        printf("NTP: Could not read DS3231 to compare\n");
        return;
    }

    time_t rtc_epoch = tm_to_epoch_utc(&rtc_time);
    int32_t delta = (int32_t)ntp_seconds - (int32_t)rtc_epoch;
    int32_t abs_delta = delta < 0 ? -delta : delta;

    if (abs_delta > DS3231_SYNC_THRESHOLD_SEC) {
        // Write NTP time to DS3231
        time_t ntp_time = (time_t)ntp_seconds;
        struct tm ntp_tm;
        gmtime_r(&ntp_time, &ntp_tm);

        if (ds3231_write_time(&ntp_tm)) {
            printf("NTP: Updated RTC (delta was %lds)\n", (long)delta);
        } else {
            printf("NTP: Failed to write updated time to DS3231\n");
        }
    } else {
        printf("NTP: RTC in sync (delta=%lds)\n", (long)delta);
    }

    // Clear OSF flag on every successful NTP sync — the oscillator is clearly
    // running if we got here, and OSF is sticky from any past power loss
    uint8_t status;
    if (ds3231_read_registers(DS3231_REG_STATUS, &status, 1) &&
        (status & DS3231_STATUS_OSF)) {
        printf("NTP: Clearing OSF flag (was set from previous power loss)\n");
        status &= ~DS3231_STATUS_OSF;
        ds3231_write_registers(DS3231_REG_STATUS, &status, 1);
    }
}
