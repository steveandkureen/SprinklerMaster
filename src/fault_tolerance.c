#include "fault_tolerance.h"
#include "config_flash.h"
#include "zones.h"
#include "FreeRTOS.h"
#include "task.h"
#include "hardware/watchdog.h"
#include "pico/time.h"
#include <stdio.h>

// Task heartbeat timestamps (in ticks)
static volatile TickType_t task_last_heartbeat[TASK_ID_COUNT] = {0};

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

        // Increment watchdog reset counter (will be saved later after FreeRTOS starts)
        config_increment_watchdog_reset_count();
        printf("Watchdog reset count: %lu\n\n",
               (unsigned long)config_get_watchdog_reset_count());
    }

    // Note: Watchdog will be enabled later by fault_tolerance_enable_watchdog()
    // after FreeRTOS is running and tasks have started
    printf("Fault tolerance initialized (watchdog will be enabled after startup)\n");
}

void task_heartbeat(task_id_t task_id) {
    if (task_id < TASK_ID_COUNT) {
        task_last_heartbeat[task_id] = xTaskGetTickCount();
    }
}

bool was_watchdog_reset(void) {
    return watchdog_caused_reset;
}

// Task names for debug output
static const char *task_names[] = {
    "NETWORK", "SENSOR", "LED", "SCHEDULER", "LCD"
};

// Check if all monitored tasks are healthy
static bool all_tasks_healthy(void) {
    TickType_t now = xTaskGetTickCount();
    static TickType_t last_warning = 0;

    for (int i = 0; i < TASK_ID_COUNT; i++) {
        // Skip LED task - it uses cyw43 which can block on network operations
        if (i == TASK_ID_LED) {
            continue;
        }

        TickType_t elapsed = now - task_last_heartbeat[i];
        if (elapsed > TASK_HEARTBEAT_TIMEOUT_TICKS) {
            // Only print warning once per second to avoid flooding
            if ((now - last_warning) > 1000) {
                printf("WATCHDOG: Task %s missed heartbeat (%lu ticks)\n",
                       task_names[i], (unsigned long)elapsed);
                last_warning = now;
            }
            return false;
        }
    }
    return true;
}

// Called from tick hook - runs in interrupt context
void fault_tolerance_tick_update(void) {
    // Track zone runtime for safety timeout
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
                // Note: This is in interrupt context, so we can't print here
                // The zone_off will be picked up by the scheduler
                zones_all_off();
                zone_safety_active = false;
            }
        }
    } else {
        zone_safety_active = false;
    }
}

// Called from idle hook - runs when no other tasks need CPU
void fault_tolerance_idle_check(void) {
    // Only feed watchdog if all tasks are healthy
    if (all_tasks_healthy()) {
        watchdog_update();
    }
    // If tasks are unhealthy, watchdog will eventually timeout and reset
}

// Save boot statistics to flash (call once after FreeRTOS starts)
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

// Enable watchdog (call after FreeRTOS starts and tasks are running)
void fault_tolerance_enable_watchdog(void) {
    // Initialize heartbeat timestamps to now
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < TASK_ID_COUNT; i++) {
        task_last_heartbeat[i] = now;
    }

    // Enable watchdog with 8 second timeout
    // pause_on_debug = true so debugging doesn't trigger reset
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);

    printf("Watchdog enabled (%dms timeout)\n", WATCHDOG_TIMEOUT_MS);
}

void fault_tolerance_log_memory_stats(void) {
    size_t free_heap = xPortGetFreeHeapSize();
    size_t min_ever_free = xPortGetMinimumEverFreeHeapSize();
    printf("Heap: %u free, %u min ever\n",
           (unsigned)free_heap, (unsigned)min_ever_free);
}
