#ifndef FAULT_TOLERANCE_H
#define FAULT_TOLERANCE_H

#include <stdint.h>
#include <stdbool.h>

// Task IDs for heartbeat tracking
typedef enum {
    TASK_ID_NETWORK = 0,
    TASK_ID_SENSOR,
    TASK_ID_LED,
    TASK_ID_SCHEDULER,
    TASK_ID_LCD,
    TASK_ID_COUNT
} task_id_t;

// Timeout constants
#define WATCHDOG_TIMEOUT_MS 8000                    // 8 second hardware watchdog
#define TASK_HEARTBEAT_TIMEOUT_TICKS 10000          // 10 seconds at 1ms tick
#define MAX_ZONE_RUNTIME_MS (4UL * 60 * 60 * 1000)  // 4 hours max zone on time
#define NTP_SYNC_TIMEOUT_MS (5UL * 60 * 1000)       // 5 minutes to wait for NTP
#define WIFI_CHECK_INTERVAL_MS 30000                // Check WiFi every 30 seconds

// Initialize fault tolerance (watchdog, heartbeat tracking)
// Call this after zones_init() but before starting scheduler
void fault_tolerance_init(void);

// Record a heartbeat for a task (call regularly from each task)
void task_heartbeat(task_id_t task_id);

// Check if the system was reset by watchdog
// Returns true if last reset was caused by watchdog timeout
bool was_watchdog_reset(void);

// Called from FreeRTOS tick hook
// Checks task health and zone safety timeout
void fault_tolerance_tick_update(void);

// Called from FreeRTOS idle hook
// Feeds watchdog only if system is healthy
void fault_tolerance_idle_check(void);

// Save boot statistics to flash (call once after FreeRTOS starts)
void fault_tolerance_save_boot_stats(void);

// Enable watchdog (call after FreeRTOS starts and tasks are running)
void fault_tolerance_enable_watchdog(void);

#endif // FAULT_TOLERANCE_H
