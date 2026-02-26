#include "scheduler.h"
#include "config_flash.h"
#include "fault_tolerance.h"
#include "pico/time.h"
#include "zones.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Current scheduler state
static scheduler_status_t current_status = {0};

// Program run state
typedef struct {
    uint8_t active_program_id;  // 0 = no program running
    uint8_t current_step;       // index into steps[] (0-based)
    uint8_t total_steps;        // step_count from program config
} program_run_state_t;

static program_run_state_t program_state = {0};

// NTP sync state
static bool ntp_synced = false;
static bool ntp_timeout_logged = false;
static uint32_t ntp_wait_start_ms = 0;

// Get current time info (using thread-safe localtime_r)
static void get_current_time(int *hour, int *minute, int *day_of_week,
                             int *day_of_year, int *year) {
    // Get current time from system
    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm_info = localtime_r(&now, &tm_buf);

    if (tm_info) {
        *hour = tm_info->tm_hour;
        *minute = tm_info->tm_min;
        *day_of_week = tm_info->tm_wday;     // 0 = Sunday
        *day_of_year = tm_info->tm_yday + 1; // tm_yday is 0-365, we want 1-366
        *year = tm_info->tm_year + 1900;
    } else {
        // Fallback if time not available
        *hour = 0;
        *minute = 0;
        *day_of_week = 0;
        *day_of_year = 1;
        *year = 2024;
    }
}

// Check if a weekly schedule should run now (accepts raw fields)
static bool should_run_weekly(uint8_t day_mask, uint8_t sched_hour,
                              uint8_t sched_minute, int hour, int minute,
                              int day_of_week) {
    if (!(day_mask & (1 << day_of_week))) {
        return false;
    }
    return (sched_hour == hour && sched_minute == minute);
}

// Check if an interval schedule should run now (accepts raw fields)
static bool should_run_interval(uint8_t day_mask, uint8_t sched_hour,
                                uint8_t sched_minute, uint16_t last_run_day,
                                uint16_t last_run_year, int hour, int minute,
                                int day_of_year, int year) {
    if (sched_hour != hour || sched_minute != minute) {
        return false;
    }

    if (last_run_year == 0) {
        return true;
    }

    int days_since_last = 0;
    if (year == last_run_year) {
        days_since_last = day_of_year - last_run_day;
    } else {
        int days_in_last_year = (last_run_year % 4 == 0) ? 366 : 365;
        days_since_last = (days_in_last_year - last_run_day) + day_of_year;
    }

    int interval = day_mask;
    if (interval < 1)
        interval = 1;

    return (days_since_last >= interval);
}

// Clear program state
static void clear_program_state(void) {
    program_state.active_program_id = 0;
    program_state.current_step = 0;
    program_state.total_steps = 0;
    current_status.active_program_id = 0;
    current_status.program_step = 0;
    current_status.program_total_steps = 0;
}

// Start running a zone - returns true if zone started successfully
static bool start_zone_run(uint8_t zone_id, uint8_t schedule_id,
                           uint16_t duration_mins) {
    if (zone_id == 0 || zone_id > MAX_ZONES) {
        return false;
    }

    if (duration_mins == 0 || duration_mins > 240) {
        return false;
    }

    // Stop any currently running zone
    if (current_status.active_zone > 0) {
        zone_off(current_status.active_zone);
    }

    // Start the new zone
    if (zone_on(zone_id)) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        current_status.active_zone = zone_id;
        current_status.active_schedule_id = schedule_id;
        current_status.remaining_mins = duration_mins;
        current_status.run_end_ms = now_ms + ((uint32_t)duration_mins * 60 * 1000);

        printf("Zone %d started for %d min\n", zone_id, duration_mins);
        return true;
    }
    return false;
}

// Start the next valid step in a program (from current_step onward)
// Returns true if a step was started
static bool program_start_next_step(void) {
    if (program_state.active_program_id == 0) {
        return false;
    }

    const program_config_t *prog = config_get_program(program_state.active_program_id);
    if (!prog) {
        clear_program_state();
        return false;
    }

    while (program_state.current_step < prog->step_count) {
        const program_step_t *step = &prog->steps[program_state.current_step];

        // Skip invalid/empty steps
        if (step->zone_id == 0 || step->zone_id > MAX_ZONES || step->duration_mins == 0) {
            program_state.current_step++;
            continue;
        }

        // Skip disabled zones
        const zone_config_t *zone = config_get_zone(step->zone_id);
        if (!zone || !zone->enabled) {
            program_state.current_step++;
            continue;
        }

        // Start this step
        if (start_zone_run(step->zone_id, 0, step->duration_mins)) {
            // Update status for display (1-based step number)
            current_status.active_program_id = program_state.active_program_id;
            current_status.program_step = program_state.current_step + 1;
            current_status.program_total_steps = program_state.total_steps;
            printf("Program %d: step %d/%d (zone %d, %d min)\n",
                   program_state.active_program_id,
                   program_state.current_step + 1,
                   program_state.total_steps,
                   step->zone_id, step->duration_mins);
            return true;
        }

        // zone_on failed, skip this step
        program_state.current_step++;
    }

    // No more valid steps
    printf("Program %d: complete\n", program_state.active_program_id);
    clear_program_state();
    return false;
}

// Check if the current run should end
static void check_run_complete(void) {
    if (current_status.active_zone == 0) {
        return;
    }

    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    if (now_ms >= current_status.run_end_ms) {
        printf("Zone %d complete\n", current_status.active_zone);
        zone_off(current_status.active_zone);
        current_status.active_zone = 0;
        current_status.active_schedule_id = 0;
        current_status.remaining_mins = 0;

        // If a program is active, advance to next step
        if (program_state.active_program_id > 0) {
            program_state.current_step++;
            program_start_next_step();
        }
    } else {
        // Update remaining time for display
        uint32_t remaining_ms = current_status.run_end_ms - now_ms;
        current_status.remaining_mins = (remaining_ms + 59999) / 60000; // Round up
    }
}

// Check all schedules
static void check_schedules(void) {
    int hour, minute, day_of_week, day_of_year, year;
    get_current_time(&hour, &minute, &day_of_week, &day_of_year, &year);

    // Don't start new schedules if one is already running
    if (current_status.active_zone > 0) {
        return;
    }

    for (int i = 1; i <= MAX_SCHEDULES; i++) {
        const schedule_config_t *sched = config_get_schedule(i);
        if (!sched) {
            continue;
        }

        // Skip unused or disabled schedules
        if (sched->zone_id == 0 || sched->zone_id > MAX_ZONES || !sched->enabled) {
            continue;
        }

        bool should_run = false;

        if (sched->type == SCHEDULE_TYPE_PERMANENT) {
            should_run = should_run_weekly(sched->day_mask, sched->hour,
                                           sched->minute, hour, minute,
                                           day_of_week);
        } else if (sched->type == SCHEDULE_TYPE_INTERVAL) {
            should_run = should_run_interval(sched->day_mask, sched->hour,
                                             sched->minute, sched->last_run_day,
                                             sched->last_run_year, hour, minute,
                                             day_of_year, year);
        }

        if (should_run) {
            if (start_zone_run(sched->zone_id, i, sched->duration_mins)) {
                config_set_schedule_last_run(i, day_of_year, year);
            }
            break;
        }
    }
}

// Check all programs
static void check_programs(void) {
    int hour, minute, day_of_week, day_of_year, year;
    get_current_time(&hour, &minute, &day_of_week, &day_of_year, &year);

    // Don't start if something is already running
    if (current_status.active_zone > 0) {
        return;
    }

    for (int i = 1; i <= MAX_PROGRAMS; i++) {
        const program_config_t *prog = config_get_program(i);
        if (!prog || !prog->enabled || prog->step_count == 0) {
            continue;
        }

        bool should_run = false;

        if (prog->type == SCHEDULE_TYPE_PERMANENT) {
            should_run = should_run_weekly(prog->day_mask, prog->hour,
                                           prog->minute, hour, minute,
                                           day_of_week);
        } else if (prog->type == SCHEDULE_TYPE_INTERVAL) {
            should_run = should_run_interval(prog->day_mask, prog->hour,
                                             prog->minute, prog->last_run_day,
                                             prog->last_run_year, hour, minute,
                                             day_of_year, year);
        }

        if (should_run) {
            printf("Program %d '%s' triggered by schedule\n", i, prog->name);
            config_set_program_last_run(i, day_of_year, year);
            scheduler_run_program(i);
            break;
        }
    }
}

// Initialize scheduler (no task creation, just setup)
void scheduler_init(void) {
    printf("Scheduler initialized\n");

    // Set timezone from config
    const char* tz = config_get_timezone();
    setenv("TZ", tz, 1);
    tzset();

    // Record when we started waiting for NTP
    ntp_wait_start_ms = to_ms_since_boot(get_absolute_time());
}

// Polling function - called periodically from main loop
void scheduler_poll(void) {
    // Check NTP sync status
    if (!ntp_synced) {
        if (time(NULL) >= 1000000000) {
            ntp_synced = true;
            printf("Scheduler: NTP synced, time is valid\n");
        } else {
            // Check for timeout
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());
            if (!ntp_timeout_logged && (now_ms - ntp_wait_start_ms) > NTP_SYNC_TIMEOUT_MS) {
                printf("Scheduler: NTP sync timeout\n");
                printf("Scheduler: Continuing without schedules until NTP syncs\n");
                ntp_timeout_logged = true;
            }
            // Don't run schedules without valid time
            return;
        }
    }

    // Check if current run is complete
    check_run_complete();

    // Check schedules (priority over programs)
    check_schedules();

    // Check programs
    check_programs();
}

void scheduler_manual_run(uint8_t zone_id, uint16_t duration_mins) {
    // Manual control aborts any running program
    clear_program_state();
    start_zone_run(zone_id, 0, duration_mins);
}

void scheduler_stop_current(void) {
    // Stop aborts any running program
    clear_program_state();
    if (current_status.active_zone > 0) {
        printf("Scheduler: Stopping zone %d\n", current_status.active_zone);
        zone_off(current_status.active_zone);
        current_status.active_zone = 0;
        current_status.active_schedule_id = 0;
        current_status.remaining_mins = 0;
    }
}

void scheduler_run_program(uint8_t program_id) {
    const program_config_t *prog = config_get_program(program_id);
    if (!prog || prog->step_count == 0) {
        printf("Scheduler: Invalid program %d\n", program_id);
        return;
    }

    // Stop anything currently running
    if (current_status.active_zone > 0) {
        zone_off(current_status.active_zone);
        current_status.active_zone = 0;
        current_status.active_schedule_id = 0;
        current_status.remaining_mins = 0;
    }

    // Set up program state
    program_state.active_program_id = program_id;
    program_state.current_step = 0;
    program_state.total_steps = prog->step_count;

    printf("Scheduler: Starting program %d '%s' (%d steps)\n",
           program_id, prog->name, prog->step_count);

    // Start the first valid step
    if (!program_start_next_step()) {
        printf("Scheduler: Program %d has no valid steps\n", program_id);
    }
}

scheduler_status_t scheduler_get_status(void) {
    return current_status;
}
