#ifndef CONFIG_FLASH_H
#define CONFIG_FLASH_H

#include <stdbool.h>
#include <stdint.h>

// Configuration constants
#define CONFIG_MAGIC 0xCAFE
#define CONFIG_VERSION 4  // v4: Added boot counters
#define MAX_ZONES 8
#define MAX_SCHEDULES 20
#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64
#define MAX_ZONE_NAME_LEN 31
#define MAX_TIMEZONE_LEN 63

// Zone GPIO pin mapping (from Plan.md)
#define ZONE_1_PIN 10
#define ZONE_2_PIN 11
#define ZONE_3_PIN 12
#define ZONE_4_PIN 13
#define ZONE_5_PIN 18
#define ZONE_6_PIN 19
#define ZONE_7_PIN 20
#define ZONE_8_PIN 21

// Schedule types
typedef enum {
    SCHEDULE_TYPE_PERMANENT = 0,
    SCHEDULE_TYPE_INTERVAL = 1,
    SCHEDULE_TYPE_MANUAL = 2
} schedule_type_t;

// Zone configuration (36 bytes each)
typedef struct {
    char name[MAX_ZONE_NAME_LEN + 1];  // 32 bytes (null-terminated)
    uint8_t gpio_pin;                   // GPIO pin number
    uint8_t enabled;                    // 0 = disabled, 1 = enabled
    uint8_t reserved[2];                // Padding for alignment
} zone_config_t;

// Schedule configuration (12 bytes each)
typedef struct {
    uint8_t zone_id;        // 1-8, 0 = unused
    uint8_t type;           // schedule_type_t
    uint8_t day_mask;       // For weekly: bits Sun=0..Sat=6; For interval: N days
    uint8_t hour;           // 0-23
    uint8_t minute;         // 0-59
    uint8_t enabled;        // 0 = disabled, 1 = enabled
    uint16_t duration_mins; // Duration in minutes
    uint16_t last_run_day;  // Day of year (1-366) when last run, 0 = never
    uint16_t last_run_year; // Year of last run (e.g., 2024), 0 = never
} schedule_config_t;

// Main configuration structure (~900 bytes)
typedef struct {
    uint16_t magic;                         // CONFIG_MAGIC for validity
    uint16_t version;                       // Config format version

    // WiFi credentials (98 bytes)
    char ssid[MAX_SSID_LEN + 1];            // 33 bytes
    char password[MAX_PASSWORD_LEN + 1];    // 65 bytes

    // Timezone (64 bytes) - POSIX TZ format, e.g., "MST7" or "EST5EDT,M3.2.0,M11.1.0"
    char timezone[MAX_TIMEZONE_LEN + 1];    // 64 bytes

    // Zone configurations (8 * 36 = 288 bytes)
    zone_config_t zones[MAX_ZONES];

    // Schedule configurations (20 * 12 = 240 bytes)
    schedule_config_t schedules[MAX_SCHEDULES];

    // Boot statistics (8 bytes)
    uint32_t boot_count;                    // Total number of boots
    uint32_t watchdog_reset_count;          // Number of watchdog-triggered resets

    uint32_t crc32;                         // Data integrity check
} sprinkler_config_t;

// Initialize configuration (load from flash or use defaults)
void config_init(void);

// Save current configuration to flash
// Returns true on success, false on failure
bool config_save(void);

// Get pointer to current configuration (read-only access recommended)
const sprinkler_config_t* config_get(void);

// WiFi configuration
void config_set_wifi(const char* ssid, const char* password);
const char* config_get_ssid(void);
const char* config_get_password(void);

// Timezone configuration (POSIX TZ format)
void config_set_timezone(const char* tz);
const char* config_get_timezone(void);

// Zone configuration
void config_set_zone(uint8_t zone_id, const char* name, uint8_t gpio_pin, bool enabled);
const zone_config_t* config_get_zone(uint8_t zone_id);
void config_set_zone_enabled(uint8_t zone_id, bool enabled);

// Schedule configuration
void config_set_schedule(uint8_t schedule_id, const schedule_config_t* schedule);
const schedule_config_t* config_get_schedule(uint8_t schedule_id);
void config_clear_schedule(uint8_t schedule_id);
void config_set_schedule_last_run(uint8_t schedule_id, uint16_t day_of_year, uint16_t year);

// Boot statistics
uint32_t config_get_boot_count(void);
uint32_t config_get_watchdog_reset_count(void);
void config_increment_boot_count(void);
void config_increment_watchdog_reset_count(void);

// Utility functions
void config_reset_to_defaults(void);
bool config_is_valid(void);

#endif // CONFIG_FLASH_H
