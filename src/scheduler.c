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

// Check if a weekly schedule should run now
static bool should_run_weekly(const schedule_config_t *sched, int hour,
                              int minute, int day_of_week) {
    // Check if current day is in the schedule's day mask
    if (!(sched->day_mask & (1 << day_of_week))) {
        return false;
    }

    // Check if current time matches
    return (sched->hour == hour && sched->minute == minute);
}

// Check if an interval schedule should run now
static bool should_run_interval(const schedule_config_t *sched, int hour,
                                int minute, int day_of_year, int year) {
    // Check if current time matches
    if (sched->hour != hour || sched->minute != minute) {
        return false;
    }

    // If never run, run now
    if (sched->last_run_year == 0) {
        return true;
    }

    // Calculate days since last run
    int days_since_last = 0;

    if (year == sched->last_run_year) {
        days_since_last = day_of_year - sched->last_run_day;
    } else {
        // Different year - calculate days remaining in last year + days in current year
        int days_in_last_year = (sched->last_run_year % 4 == 0) ? 366 : 365;
        days_since_last = (days_in_last_year - sched->last_run_day) + day_of_year;
    }

    // Interval is stored in day_mask for interval schedules
    int interval = sched->day_mask;
    if (interval < 1)
        interval = 1;

    return (days_since_last >= interval);
}

// Start running a zone - returns true if zone started successfully
static bool start_zone_run(uint8_t zone_id, uint8_t schedule_id,
                           uint16_t duration_mins) {
    // Validate parameters
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
            should_run = should_run_weekly(sched, hour, minute, day_of_week);
        } else if (sched->type == SCHEDULE_TYPE_INTERVAL) {
            should_run = should_run_interval(sched, hour, minute, day_of_year, year);
        }

        if (should_run) {
            // Start the zone - only update last_run if it actually started
            if (start_zone_run(sched->zone_id, i, sched->duration_mins)) {
                // Update last run time in RAM only
                config_set_schedule_last_run(i, day_of_year, year);
            }

            // Only run one schedule at a time
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

    // Check schedules
    check_schedules();
}

void scheduler_manual_run(uint8_t zone_id, uint16_t duration_mins) {
    start_zone_run(zone_id, 0, duration_mins);
}

void scheduler_stop_current(void) {
    if (current_status.active_zone > 0) {
        printf("Scheduler: Stopping zone %d\n", current_status.active_zone);
        zone_off(current_status.active_zone);
        current_status.active_zone = 0;
        current_status.active_schedule_id = 0;
        current_status.remaining_mins = 0;
    }
}

scheduler_status_t scheduler_get_status(void) {
    return current_status;
}
