#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

// Scheduler status
typedef struct {
    uint8_t active_zone;        // Currently running zone (0 = none)
    uint8_t active_schedule_id; // Schedule that triggered it (0 = manual)
    uint16_t remaining_mins;    // Minutes remaining (for display)
    uint32_t run_end_ms;        // When the current run should end
    uint8_t active_program_id;  // Currently running program (0 = none)
    uint8_t program_step;       // Current step index (1-based for display)
    uint8_t program_total_steps; // Total steps in running program
} scheduler_status_t;

// Initialize the scheduler (setup, no task creation)
void scheduler_init(void);

// Polling function - call periodically from main loop (~5 seconds)
void scheduler_poll(void);

// Manual run - start a zone for specified duration
// If another zone is running, it will be stopped first
void scheduler_manual_run(uint8_t zone_id, uint16_t duration_mins);

// Stop the currently running zone
void scheduler_stop_current(void);

// Run a program manually (starts first valid step)
void scheduler_run_program(uint8_t program_id);

// Get current scheduler status
scheduler_status_t scheduler_get_status(void);

// Returns true if freeze protection is active (temp <= 35F)
bool scheduler_is_freeze_active(void);

#endif // SCHEDULER_H
