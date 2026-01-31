#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

// Scheduler status
typedef struct {
    uint8_t active_zone;        // Currently running zone (0 = none)
    uint8_t active_schedule_id; // Schedule that triggered it (0 = manual)
    uint16_t remaining_mins;    // Minutes remaining
    uint32_t run_start_ms;      // When the current run started
} scheduler_status_t;

// Initialize and start the scheduler task
void scheduler_init(void);

// Manual run - start a zone for specified duration
// If another zone is running, it will be stopped first
void scheduler_manual_run(uint8_t zone_id, uint16_t duration_mins);

// Stop the currently running zone
void scheduler_stop_current(void);

// Get current scheduler status
scheduler_status_t scheduler_get_status(void);

// The FreeRTOS task function (called internally by scheduler_init)
void scheduler_task(void *pvParameters);

#endif // SCHEDULER_H
