#include "config_flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include <string.h>
#include <stdio.h>

// Flash storage location - last 4KB sector of 4MB flash
// Flash starts at XIP_BASE (0x10000000), 4MB = 0x400000
// Last sector at offset 0x3FF000
#define CONFIG_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CONFIG_FLASH_ADDR (XIP_BASE + CONFIG_FLASH_OFFSET)

// Current configuration in RAM
static sprinkler_config_t g_config;

// Default zone names
static const char* default_zone_names[MAX_ZONES] = {
    "Zone 1", "Zone 2", "Zone 3", "Zone 4",
    "Zone 5", "Zone 6", "Zone 7", "Zone 8"
};

// Default GPIO pins for each zone
static const uint8_t default_zone_pins[MAX_ZONES] = {
    ZONE_1_PIN, ZONE_2_PIN, ZONE_3_PIN, ZONE_4_PIN,
    ZONE_5_PIN, ZONE_6_PIN, ZONE_7_PIN, ZONE_8_PIN
};

// CRC32 lookup table (polynomial 0xEDB88320)
static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

// Initialize CRC32 lookup table
static void crc32_init_table(void) {
    if (crc32_table_initialized) return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

// Calculate CRC32 of data
static uint32_t crc32_calculate(const void* data, size_t length) {
    crc32_init_table();

    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

// Calculate CRC32 of config (excluding the crc32 field itself)
static uint32_t config_calculate_crc(const sprinkler_config_t* config) {
    // CRC covers everything except the last 4 bytes (the crc32 field)
    size_t crc_length = sizeof(sprinkler_config_t) - sizeof(uint32_t);
    return crc32_calculate(config, crc_length);
}

// Set default configuration values
static void config_set_defaults(void) {
    memset(&g_config, 0, sizeof(g_config));

    g_config.magic = CONFIG_MAGIC;
    g_config.version = CONFIG_VERSION;

    // Default WiFi (empty)
    g_config.ssid[0] = '\0';
    g_config.password[0] = '\0';

    // Default timezone (Arizona MST, no DST)
    strncpy(g_config.timezone, "MST7", MAX_TIMEZONE_LEN);
    g_config.timezone[MAX_TIMEZONE_LEN] = '\0';

    // Default zone configuration
    for (int i = 0; i < MAX_ZONES; i++) {
        strncpy(g_config.zones[i].name, default_zone_names[i], MAX_ZONE_NAME_LEN);
        g_config.zones[i].name[MAX_ZONE_NAME_LEN] = '\0';
        g_config.zones[i].gpio_pin = default_zone_pins[i];
        g_config.zones[i].enabled = 1;  // All zones enabled by default
    }

    // Default schedules (all empty/disabled)
    for (int i = 0; i < MAX_SCHEDULES; i++) {
        g_config.schedules[i].zone_id = 0;  // 0 = unused
        g_config.schedules[i].enabled = 0;
    }

    // Boot statistics start at 0
    g_config.boot_count = 0;
    g_config.watchdog_reset_count = 0;

    // Calculate CRC
    g_config.crc32 = config_calculate_crc(&g_config);
}

// Verify configuration is valid
static bool config_verify(const sprinkler_config_t* config) {
    // Check magic number
    if (config->magic != CONFIG_MAGIC) {
        printf("Config: Invalid magic (0x%04X != 0x%04X)\n", config->magic, CONFIG_MAGIC);
        return false;
    }

    // Check version (allow loading older versions in future)
    if (config->version > CONFIG_VERSION) {
        printf("Config: Unknown version (%d > %d)\n", config->version, CONFIG_VERSION);
        return false;
    }

    // Verify CRC
    uint32_t calculated_crc = config_calculate_crc(config);
    if (config->crc32 != calculated_crc) {
        printf("Config: CRC mismatch (0x%08lX != 0x%08lX)\n",
               (unsigned long)config->crc32, (unsigned long)calculated_crc);
        return false;
    }

    return true;
}

// Context for flash write operation
typedef struct {
    const uint8_t* data;
    size_t size;
} flash_write_context_t;

// Flash write callback (called with interrupts disabled)
static void flash_write_callback(void* param) {
    flash_write_context_t* ctx = (flash_write_context_t*)param;

    // Erase the sector
    flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);

    // Write the data (must be multiple of FLASH_PAGE_SIZE)
    // Round up size to next page boundary
    size_t write_size = (ctx->size + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
    flash_range_program(CONFIG_FLASH_OFFSET, ctx->data, write_size);
}

// Initialize configuration
void config_init(void) {
    printf("Config: Initializing flash storage...\n");
    printf("Config: Flash offset=0x%X, addr=0x%X, sector_size=%d\n",
           CONFIG_FLASH_OFFSET, CONFIG_FLASH_ADDR, FLASH_SECTOR_SIZE);
    printf("Config: Structure size=%d bytes\n", sizeof(sprinkler_config_t));

    // Read configuration from flash
    const sprinkler_config_t* flash_config = (const sprinkler_config_t*)CONFIG_FLASH_ADDR;

    // Verify and load
    if (config_verify(flash_config)) {
        printf("Config: Loaded valid configuration from flash\n");
        memcpy(&g_config, flash_config, sizeof(sprinkler_config_t));
    } else {
        printf("Config: No valid configuration found, using defaults\n");
        config_set_defaults();
    }

    // Increment boot count (will be saved later after FreeRTOS starts)
    g_config.boot_count++;
    printf("Config: Boot count = %lu (watchdog resets = %lu)\n",
           (unsigned long)g_config.boot_count,
           (unsigned long)g_config.watchdog_reset_count);

    // Print zone info
    for (int i = 0; i < MAX_ZONES; i++) {
        printf("Config: Zone %d: '%s' (GPIO %d, %s)\n",
               i + 1,
               g_config.zones[i].name,
               g_config.zones[i].gpio_pin,
               g_config.zones[i].enabled ? "enabled" : "disabled");
    }
}

// Save configuration to flash
bool config_save(void) {
    printf("Config: Saving to flash...\n");

    // Update CRC before saving
    g_config.crc32 = config_calculate_crc(&g_config);

    // Prepare write context
    // Need page-aligned buffer for flash write
    static uint8_t write_buffer[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));
    memset(write_buffer, 0xFF, FLASH_SECTOR_SIZE);  // Flash erases to 0xFF
    memcpy(write_buffer, &g_config, sizeof(sprinkler_config_t));

    flash_write_context_t ctx = {
        .data = write_buffer,
        .size = sizeof(sprinkler_config_t)
    };

    // Feed watchdog before flash operation since it blocks all interrupts
    watchdog_update();

    // Try to lock out the other core (with timeout) - still needed for dual-core safety
    bool lockout_ok = multicore_lockout_start_timeout_us(1000000);  // 1 second timeout
    if (!lockout_ok) {
        printf("Config: Failed to lock out other core\n");
        return false;
    }

    // Disable interrupts and do flash operation
    uint32_t ints = save_and_disable_interrupts();

    // Erase and program flash directly
    flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    size_t write_size = (ctx.size + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
    flash_range_program(CONFIG_FLASH_OFFSET, ctx.data, write_size);

    restore_interrupts(ints);

    // Release other core
    multicore_lockout_end_blocking();

    // Verify the write
    const sprinkler_config_t* flash_config = (const sprinkler_config_t*)CONFIG_FLASH_ADDR;
    if (!config_verify(flash_config)) {
        printf("Config: Verification after write failed!\n");
        return false;
    }

    printf("Config: Saved successfully (CRC=0x%08lX)\n", (unsigned long)g_config.crc32);
    return true;
}

// Get pointer to current configuration
const sprinkler_config_t* config_get(void) {
    return &g_config;
}

// Set WiFi credentials
void config_set_wifi(const char* ssid, const char* password) {
    if (ssid) {
        strncpy(g_config.ssid, ssid, MAX_SSID_LEN);
        g_config.ssid[MAX_SSID_LEN] = '\0';
    }
    if (password) {
        strncpy(g_config.password, password, MAX_PASSWORD_LEN);
        g_config.password[MAX_PASSWORD_LEN] = '\0';
    }
}

const char* config_get_ssid(void) {
    return g_config.ssid;
}

const char* config_get_password(void) {
    return g_config.password;
}

// Set timezone (POSIX TZ format)
void config_set_timezone(const char* tz) {
    if (tz) {
        strncpy(g_config.timezone, tz, MAX_TIMEZONE_LEN);
        g_config.timezone[MAX_TIMEZONE_LEN] = '\0';
    }
}

const char* config_get_timezone(void) {
    // Return default if empty
    if (g_config.timezone[0] == '\0') {
        return "MST7";
    }
    return g_config.timezone;
}

// Set zone configuration
void config_set_zone(uint8_t zone_id, const char* name, uint8_t gpio_pin, bool enabled) {
    if (zone_id < 1 || zone_id > MAX_ZONES) {
        printf("Config: Invalid zone_id %d\n", zone_id);
        return;
    }

    uint8_t idx = zone_id - 1;

    if (name) {
        strncpy(g_config.zones[idx].name, name, MAX_ZONE_NAME_LEN);
        g_config.zones[idx].name[MAX_ZONE_NAME_LEN] = '\0';
    }
    g_config.zones[idx].gpio_pin = gpio_pin;
    g_config.zones[idx].enabled = enabled ? 1 : 0;
}

const zone_config_t* config_get_zone(uint8_t zone_id) {
    if (zone_id < 1 || zone_id > MAX_ZONES) {
        return NULL;
    }
    return &g_config.zones[zone_id - 1];
}

void config_set_zone_enabled(uint8_t zone_id, bool enabled) {
    if (zone_id < 1 || zone_id > MAX_ZONES) {
        return;
    }
    g_config.zones[zone_id - 1].enabled = enabled ? 1 : 0;
}

// Set schedule configuration
void config_set_schedule(uint8_t schedule_id, const schedule_config_t* schedule) {
    if (schedule_id < 1 || schedule_id > MAX_SCHEDULES || !schedule) {
        return;
    }
    memcpy(&g_config.schedules[schedule_id - 1], schedule, sizeof(schedule_config_t));
}

const schedule_config_t* config_get_schedule(uint8_t schedule_id) {
    if (schedule_id < 1 || schedule_id > MAX_SCHEDULES) {
        return NULL;
    }
    return &g_config.schedules[schedule_id - 1];
}

void config_clear_schedule(uint8_t schedule_id) {
    if (schedule_id < 1 || schedule_id > MAX_SCHEDULES) {
        return;
    }
    memset(&g_config.schedules[schedule_id - 1], 0, sizeof(schedule_config_t));
}

void config_set_schedule_last_run(uint8_t schedule_id, uint16_t day_of_year, uint16_t year) {
    if (schedule_id < 1 || schedule_id > MAX_SCHEDULES) {
        return;
    }
    g_config.schedules[schedule_id - 1].last_run_day = day_of_year;
    g_config.schedules[schedule_id - 1].last_run_year = year;
}

// Boot statistics
uint32_t config_get_boot_count(void) {
    return g_config.boot_count;
}

uint32_t config_get_watchdog_reset_count(void) {
    return g_config.watchdog_reset_count;
}

void config_increment_boot_count(void) {
    g_config.boot_count++;
}

void config_increment_watchdog_reset_count(void) {
    g_config.watchdog_reset_count++;
}

// Reset to factory defaults
void config_reset_to_defaults(void) {
    printf("Config: Resetting to defaults\n");
    config_set_defaults();
}

// Check if current configuration is valid
bool config_is_valid(void) {
    return g_config.magic == CONFIG_MAGIC;
}
