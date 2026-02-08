#include "fault_tolerance.h"
#include "config_flash.h"
#include "zones.h"
#include "hardware/watchdog.h"
#include "pico/time.h"
#include <stdio.h>

// Zone start time for safety timeout
static volatile uint32_t zone_start_time_ms = 0;
static volatile bool zone_safety_active = false;

// Track if watchdog caused last reset
static bool watchdog_caused_reset = false;

void fault_tolerance_init(void) {
    // Check if we're recovering from a watchdog reset
    watchdog_caused_reset = watchdog_caused_reboot();

    if (watchdog_caused_reset) {
        printf("\n!!! WATCHDOG RESET DETECTED !!!\n");
        printf("System recovered from a watchdog timeout.\n");
        printf("Forcing all zones OFF for safety.\n");

        // Safety: ensure all zones are off after watchdog reset
        zones_all_off();

        // Increment watchdog reset counter
        config_increment_watchdog_reset_count();
        printf("Watchdog reset count: %lu\n\n",
               (unsigned long)config_get_watchdog_reset_count());
    }

    printf("Fault tolerance initialized (watchdog will be enabled after startup)\n");
}

bool was_watchdog_reset(void) {
    return watchdog_caused_reset;
}

// Check zone safety timeout - call periodically from main loop (~1 second)
void fault_tolerance_check_zone_timeout(void) {
    uint8_t active_zone = zones_get_active();

    if (active_zone > 0) {
        if (!zone_safety_active) {
            // Zone just turned on - record start time
            zone_start_time_ms = to_ms_since_boot(get_absolute_time());
            zone_safety_active = true;
        } else {
            // Check if zone has been on too long
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());
            uint32_t runtime_ms = now_ms - zone_start_time_ms;

            if (runtime_ms > MAX_ZONE_RUNTIME_MS) {
                // Safety timeout - force zone off
                printf("SAFETY: Zone %d exceeded max runtime (%lu ms), forcing off\n",
                       active_zone, (unsigned long)runtime_ms);
                zones_all_off();
                zone_safety_active = false;
            }
        }
    } else {
        zone_safety_active = false;
    }
}

// Save boot statistics to flash
void fault_tolerance_save_boot_stats(void) {
    printf("Saving boot statistics to flash...\n");
    if (config_save()) {
        printf("Boot statistics saved (boot=%lu, watchdog=%lu)\n",
               (unsigned long)config_get_boot_count(),
               (unsigned long)config_get_watchdog_reset_count());
    } else {
        printf("Failed to save boot statistics!\n");
    }
}

// Enable watchdog with specified timeout
void fault_tolerance_enable_watchdog(void) {
    // Enable watchdog with 8 second timeout
    // pause_on_debug = true so debugging doesn't trigger reset
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);

    printf("Watchdog enabled (%dms timeout)\n", WATCHDOG_TIMEOUT_MS);
}

void fault_tolerance_log_memory_stats(void) {
    // In bare-metal mode, we can use mallinfo if available
    // For now, just note that FreeRTOS heap functions are not available
    printf("Memory stats: (bare-metal mode - use mallinfo if needed)\n");
}
