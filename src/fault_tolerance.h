#ifndef FAULT_TOLERANCE_H
#define FAULT_TOLERANCE_H

#include <stdint.h>
#include <stdbool.h>

// Timeout constants
#define WATCHDOG_TIMEOUT_MS 8000                    // 8 second hardware watchdog
#define MAX_ZONE_RUNTIME_MS (4UL * 60 * 60 * 1000)  // 4 hours max zone on time
#define NTP_SYNC_TIMEOUT_MS (5UL * 60 * 1000)       // 5 minutes to wait for NTP

// Initialize fault tolerance (watchdog, reset detection)
// Call this after zones_init() but before main loop
void fault_tolerance_init(void);

// Check if the system was reset by watchdog
// Returns true if last reset was caused by watchdog timeout
bool was_watchdog_reset(void);

// Check zone safety timeout - call periodically from main loop (~1 second)
void fault_tolerance_check_zone_timeout(void);

// Save boot statistics to flash
void fault_tolerance_save_boot_stats(void);

// Enable watchdog (call after startup stabilization)
void fault_tolerance_enable_watchdog(void);

// Log memory statistics (call periodically to monitor)
void fault_tolerance_log_memory_stats(void);

#endif // FAULT_TOLERANCE_H
