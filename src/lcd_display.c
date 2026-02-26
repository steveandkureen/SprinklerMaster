#include "lcd_display.h"
#include "config_flash.h"
#include "lcd.h"
#include "network.h"
#include "scheduler.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// How long to show IP address after connection (1 minute)
#define IP_DISPLAY_DURATION_MS 60000

// LCD width
#define LCD_WIDTH 16

// Shared state (no mutex needed in single-threaded mode)
static char startup_status[LCD_WIDTH + 1] = "";
static char ip_address[LCD_WIDTH + 1] = "";
static uint32_t ip_set_time_ms = 0;
static bool startup_complete = false;

// Format a string to exactly LCD_WIDTH chars (pad or truncate)
static void format_line(char *dest, const char *src) {
    int len = strlen(src);
    if (len >= LCD_WIDTH) {
        memcpy(dest, src, LCD_WIDTH);
        dest[LCD_WIDTH] = '\0';
    } else {
        strcpy(dest, src);
        memset(dest + len, ' ', LCD_WIDTH - len);
        dest[LCD_WIDTH] = '\0';
    }
}

// Get next scheduled run info
// Returns true if a schedule was found, fills zone_id, hour, minute
static bool get_next_schedule(uint8_t *next_zone, uint8_t *next_hour, uint8_t *next_minute) {
    time_t now = time(NULL);
    if (now < 1000000000) {
        return false;  // NTP not synced yet
    }

    struct tm tm_buf;
    struct tm *tm_now = localtime_r(&now, &tm_buf);
    if (!tm_now) {
        return false;
    }

    int current_day = tm_now->tm_wday;  // 0 = Sunday
    int current_hour = tm_now->tm_hour;
    int current_minute = tm_now->tm_min;
    int current_day_of_year = tm_now->tm_yday + 1;
    int current_year = tm_now->tm_year + 1900;

    // Convert current time to minutes since midnight
    int current_mins = current_hour * 60 + current_minute;

    int best_mins_away = 999999;  // Large number
    uint8_t best_zone = 0;
    uint8_t best_hour = 0;
    uint8_t best_minute = 0;

    for (int i = 1; i <= MAX_SCHEDULES; i++) {
        const schedule_config_t *sched = config_get_schedule(i);
        if (!sched || sched->zone_id == 0 || !sched->enabled) {
            continue;
        }

        int sched_mins = sched->hour * 60 + sched->minute;

        if (sched->type == SCHEDULE_TYPE_PERMANENT) {
            // Weekly schedule - find next day in day_mask
            for (int d = 0; d < 7; d++) {
                int check_day = (current_day + d) % 7;
                if (sched->day_mask & (1 << check_day)) {
                    int mins_away;
                    if (d == 0) {
                        // Today - check if time has passed
                        if (sched_mins > current_mins) {
                            mins_away = sched_mins - current_mins;
                        } else {
                            continue;  // Already passed today, check next occurrence
                        }
                    } else {
                        // Future day
                        mins_away = (d * 24 * 60) + sched_mins - current_mins;
                    }

                    if (mins_away < best_mins_away) {
                        best_mins_away = mins_away;
                        best_zone = sched->zone_id;
                        best_hour = sched->hour;
                        best_minute = sched->minute;
                    }
                    break;  // Found next occurrence for this schedule
                }
            }
        } else if (sched->type == SCHEDULE_TYPE_INTERVAL) {
            // Interval schedule
            int interval_days = sched->day_mask;
            if (interval_days < 1) interval_days = 1;

            int days_until_run = 0;
            if (sched->last_run_year == 0) {
                // Never run - will run today if time hasn't passed
                if (sched_mins > current_mins) {
                    days_until_run = 0;
                } else {
                    days_until_run = interval_days;
                }
            } else {
                // Calculate days since last run
                int days_since_last;
                if (current_year == sched->last_run_year) {
                    days_since_last = current_day_of_year - sched->last_run_day;
                } else {
                    int days_in_last_year = (sched->last_run_year % 4 == 0) ? 366 : 365;
                    days_since_last = (days_in_last_year - sched->last_run_day) + current_day_of_year;
                }

                if (days_since_last >= interval_days) {
                    // Due today
                    if (sched_mins > current_mins) {
                        days_until_run = 0;
                    } else {
                        days_until_run = interval_days;
                    }
                } else {
                    days_until_run = interval_days - days_since_last;
                }
            }

            int mins_away;
            if (days_until_run == 0) {
                mins_away = sched_mins - current_mins;
            } else {
                mins_away = (days_until_run * 24 * 60) + sched_mins - current_mins;
            }

            if (mins_away > 0 && mins_away < best_mins_away) {
                best_mins_away = mins_away;
                best_zone = sched->zone_id;
                best_hour = sched->hour;
                best_minute = sched->minute;
            }
        }
    }

    // Also check programs
    for (int p = 1; p <= MAX_PROGRAMS; p++) {
        const program_config_t *prog = config_get_program(p);
        if (!prog || !prog->enabled || prog->step_count == 0) {
            continue;
        }

        int prog_sched_mins = prog->hour * 60 + prog->minute;
        int prog_mins_away = -1;

        if (prog->type == SCHEDULE_TYPE_PERMANENT) {
            for (int d = 0; d < 7; d++) {
                int check_day = (current_day + d) % 7;
                if (prog->day_mask & (1 << check_day)) {
                    if (d == 0) {
                        if (prog_sched_mins > current_mins) {
                            prog_mins_away = prog_sched_mins - current_mins;
                        } else {
                            continue;
                        }
                    } else {
                        prog_mins_away = (d * 24 * 60) + prog_sched_mins - current_mins;
                    }
                    break;
                }
            }
        } else if (prog->type == SCHEDULE_TYPE_INTERVAL) {
            int interval_days = prog->day_mask;
            if (interval_days < 1) interval_days = 1;

            int days_until_run = 0;
            if (prog->last_run_year == 0) {
                if (prog_sched_mins > current_mins) {
                    days_until_run = 0;
                } else {
                    days_until_run = interval_days;
                }
            } else {
                int days_since_last;
                if (current_year == prog->last_run_year) {
                    days_since_last = current_day_of_year - prog->last_run_day;
                } else {
                    int days_in_last_year = (prog->last_run_year % 4 == 0) ? 366 : 365;
                    days_since_last = (days_in_last_year - prog->last_run_day) + current_day_of_year;
                }

                if (days_since_last >= interval_days) {
                    if (prog_sched_mins > current_mins) {
                        days_until_run = 0;
                    } else {
                        days_until_run = interval_days;
                    }
                } else {
                    days_until_run = interval_days - days_since_last;
                }
            }

            if (days_until_run == 0) {
                prog_mins_away = prog_sched_mins - current_mins;
            } else {
                prog_mins_away = (days_until_run * 24 * 60) + prog_sched_mins - current_mins;
            }
        }

        if (prog_mins_away <= 0) continue;

        // Check each step — the first step's zone starts at program time,
        // subsequent steps are offset by preceding durations
        int step_offset = 0;
        for (int s = 0; s < prog->step_count && s < MAX_PROGRAM_STEPS; s++) {
            const program_step_t *step = &prog->steps[s];
            if (step->zone_id == 0) break;

            int total_mins_away = prog_mins_away + step_offset;
            if (total_mins_away > 0 && total_mins_away < best_mins_away) {
                best_mins_away = total_mins_away;
                best_zone = step->zone_id;
                int effective_mins = prog_sched_mins + step_offset;
                best_hour = (effective_mins / 60) % 24;
                best_minute = effective_mins % 60;
            }

            step_offset += step->duration_mins;
        }
    }

    if (best_zone > 0) {
        *next_zone = best_zone;
        *next_hour = best_hour;
        *next_minute = best_minute;
        return true;
    }
    return false;
}

// Initialize LCD display (no task creation)
void lcd_display_init(void) {
    // Show initial startup message
    lcd_set_text(0, 0, "Starting up....");
}

// Polling function - called periodically from main loop
void lcd_display_poll(void) {
    char line0[LCD_WIDTH + 1];
    char line1[LCD_WIDTH + 1];
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    // AP setup mode: fixed display
    if (network_is_ap_mode()) {
        format_line(line0, "Setup Mode");
        format_line(line1, "192.168.4.1");
        lcd_set_text(0, 0, line0);
        lcd_set_text(1, 0, line1);
        return;
    }

    // Line 0: "Starting up...." during startup, then zone status or "Idle"
    if (!startup_complete) {
        // Keep showing "Starting up...."
    } else {
        scheduler_status_t status = scheduler_get_status();

        if (status.active_zone > 0) {
            // Zone is running
            const zone_config_t *zone = config_get_zone(status.active_zone);
            char zone_line[LCD_WIDTH + 1];
            if (zone && zone->name[0] != '\0') {
                char name_buf[11];  // Max 10 chars for zone name
                strncpy(name_buf, zone->name, 10);
                name_buf[10] = '\0';
                snprintf(zone_line, sizeof(zone_line), "%-10s%3dmin", name_buf, status.remaining_mins);
            } else {
                snprintf(zone_line, sizeof(zone_line), "Zone %d    %3dmin", status.active_zone, status.remaining_mins);
            }
            format_line(line0, zone_line);
        } else {
            format_line(line0, "Idle");
        }
        lcd_set_text(0, 0, line0);
    }

    // Line 1: Status during startup, IP for 1 min after connection, then next schedule
    bool show_ip = false;

    if (ip_address[0] != '\0' && ip_set_time_ms > 0) {
        if ((now_ms - ip_set_time_ms) < IP_DISPLAY_DURATION_MS) {
            format_line(line1, ip_address);
            show_ip = true;
        }
    }

    if (!show_ip && !startup_complete && startup_status[0] != '\0') {
        format_line(line1, startup_status);
        show_ip = true;  // Reuse flag to indicate we have content
    }

    if (!show_ip && startup_complete) {
        // Show next scheduled run
        uint8_t next_zone, next_hour, next_minute;
        if (get_next_schedule(&next_zone, &next_hour, &next_minute)) {
            char next_line[LCD_WIDTH + 1];
            snprintf(next_line, sizeof(next_line), "Next:Z%d %02d:%02d", next_zone, next_hour, next_minute);
            format_line(line1, next_line);
        } else {
            format_line(line1, "No schedules");
        }
    } else if (!show_ip && !startup_complete) {
        format_line(line1, "");
    }

    lcd_set_text(1, 0, line1);
}

void lcd_display_set_status(const char *status) {
    strncpy(startup_status, status, LCD_WIDTH);
    startup_status[LCD_WIDTH] = '\0';
}

void lcd_display_set_ip(const char *ip) {
    strncpy(ip_address, ip, LCD_WIDTH);
    ip_address[LCD_WIDTH] = '\0';
    ip_set_time_ms = to_ms_since_boot(get_absolute_time());
}

void lcd_display_startup_complete(void) {
    startup_complete = true;
}
