#include "network.h"
#include "config_flash.h"
#include "dht22.h"
#include "history.h"
#include "lcd.h"
#include "lcd_display.h"
#include "zones.h"
#include "scheduler.h"
#include "ds3231.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/mdns.h"
#include "lwip/apps/sntp.h"
#include "lwip/ip4_addr.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/time.h"
#include "pico/types.h"
#include "hardware/watchdog.h"
#include "dhserver.h"
#include <lwip/def.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

char *ip4_addr = NULL;
static bool g_ap_mode = false;
static volatile bool g_reboot_pending = false;

// Helper: URL decode a string in place
static void url_decode(char *str) {
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], 0};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Helper: Find parameter value by name
static const char *find_param(int numParams, char *params[], char *values[], const char *name) {
    for (int i = 0; i < numParams; i++) {
        if (strcmp(params[i], name) == 0) {
            return values[i];
        }
    }
    return NULL;
}

// Check if WiFi is connected
static bool wifi_is_connected(void) {
    int status = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    return (status == CYW43_LINK_JOIN);
}

// Attempt to reconnect WiFi
static bool wifi_reconnect(void) {
    printf("Network: Attempting WiFi reconnection...\n");
    lcd_display_set_status("WiFi Reconnect");

    const char *ssid = config_get_ssid();
    const char *password = config_get_password();

    int result = cyw43_arch_wifi_connect_timeout_ms(
        ssid, password, CYW43_AUTH_WPA2_AES_PSK, 30000);

    if (result == 0) {
        printf("Network: WiFi reconnected!\n");
        ip4_addr = ip4addr_ntoa(netif_default ? &netif_default->ip_addr : NULL);
        printf("Network: IP address %s\n", ip4_addr);
        lcd_display_set_ip(ip4_addr);
        lcd_display_startup_complete();
        return true;
    } else {
        printf("Network: WiFi reconnection failed (error %d)\n", result);
        return false;
    }
}

static const char *cgi_handler_default(int index, int numParams, char *params[],
                                       char *value[]) {

    printf("cgi called\n");
    if (g_ap_mode) {
        return "/setup.html";
    }
    return "/dashboard.html";
}

static const char *cgi_handler_sensors(int index, int numParams, char *params[],
                                       char *value[]) {
    printf("sensors API called\n");
    return "/api/sensors.json";
}

static const char *cgi_handler_zones(int index, int numParams, char *params[],
                                     char *value[]) {
    printf("zones API called\n");
    return "/api/zones.json";
}

static const char *cgi_handler_zones_save(int index, int numParams, char *params[],
                                          char *value[]) {
    printf("zones save API called with %d params\n", numParams);

    // Parse zone parameters: z1n, z1e, z2n, z2e, etc.
    for (int zone_id = 1; zone_id <= 8; zone_id++) {
        char name_param[8], enabled_param[8];
        snprintf(name_param, sizeof(name_param), "z%dn", zone_id);
        snprintf(enabled_param, sizeof(enabled_param), "z%de", zone_id);

        const char *name_val = find_param(numParams, params, value, name_param);
        const char *enabled_val = find_param(numParams, params, value, enabled_param);

        if (name_val) {
            // URL decode the name (modifies in place, need a copy)
            char decoded_name[MAX_ZONE_NAME_LEN + 1];
            strncpy(decoded_name, name_val, MAX_ZONE_NAME_LEN);
            decoded_name[MAX_ZONE_NAME_LEN] = '\0';
            url_decode(decoded_name);

            // Get current zone config to preserve GPIO pin
            const zone_config_t *current = config_get_zone(zone_id);
            uint8_t gpio_pin = current ? current->gpio_pin : 0;

            // Determine enabled state
            bool enabled = true;
            if (enabled_val) {
                enabled = (enabled_val[0] == '1' || enabled_val[0] == 't');
            }

            printf("Zone %d: name='%s', enabled=%d\n", zone_id, decoded_name, enabled);
            config_set_zone(zone_id, decoded_name, gpio_pin, enabled);
        }
    }

    // Save to flash
    if (config_save()) {
        printf("Zone config saved to flash\n");
        net_cmd_push(NET_CMD_CONFIG_RELOAD);
        return "/api/success.json";
    } else {
        printf("Failed to save zone config\n");
        return "/api/error.json";
    }
}

static const char *cgi_handler_schedules(int index, int numParams, char *params[],
                                         char *value[]) {
    printf("schedules API called\n");
    return "/api/schedules.json";
}

static const char *cgi_handler_schedules_save(int index, int numParams, char *params[],
                                              char *value[]) {
    printf("schedules save API called with %d params\n", numParams);

    // Parse schedule parameters: id, zone, type, days, hour, min, dur, en
    const char *id_val = find_param(numParams, params, value, "id");
    const char *zone_val = find_param(numParams, params, value, "zone");
    const char *type_val = find_param(numParams, params, value, "type");
    const char *days_val = find_param(numParams, params, value, "days");
    const char *hour_val = find_param(numParams, params, value, "hour");
    const char *min_val = find_param(numParams, params, value, "min");
    const char *dur_val = find_param(numParams, params, value, "dur");
    const char *en_val = find_param(numParams, params, value, "en");

    if (!id_val || !zone_val) {
        printf("Missing required params\n");
        return "/api/error.json";
    }

    int schedule_id = atoi(id_val);
    if (schedule_id < 1 || schedule_id > MAX_SCHEDULES) {
        printf("Invalid schedule id: %d\n", schedule_id);
        return "/api/error.json";
    }

    schedule_config_t sched = {0};
    sched.zone_id = (uint8_t)atoi(zone_val);
    sched.type = type_val ? (uint8_t)atoi(type_val) : 0;
    sched.day_mask = days_val ? (uint8_t)atoi(days_val) : 0;
    sched.hour = hour_val ? (uint8_t)atoi(hour_val) : 0;
    sched.minute = min_val ? (uint8_t)atoi(min_val) : 0;
    sched.duration_mins = dur_val ? (uint16_t)atoi(dur_val) : 0;
    sched.enabled = en_val ? (en_val[0] == '1' || en_val[0] == 't') : 1;

    printf("Schedule %d: zone=%d, type=%d, days=0x%02X, time=%02d:%02d, dur=%d, en=%d\n",
           schedule_id, sched.zone_id, sched.type, sched.day_mask,
           sched.hour, sched.minute, sched.duration_mins, sched.enabled);

    config_set_schedule(schedule_id, &sched);

    if (config_save()) {
        printf("Schedule saved to flash\n");
        net_cmd_push(NET_CMD_CONFIG_RELOAD);
        return "/api/success.json";
    } else {
        printf("Failed to save schedule\n");
        return "/api/error.json";
    }
}

static const char *cgi_handler_schedule_delete(int index, int numParams, char *params[],
                                               char *value[]) {
    printf("schedule delete API called\n");

    const char *id_val = find_param(numParams, params, value, "id");
    if (!id_val) {
        return "/api/error.json";
    }

    int schedule_id = atoi(id_val);
    if (schedule_id < 1 || schedule_id > MAX_SCHEDULES) {
        return "/api/error.json";
    }

    config_clear_schedule(schedule_id);

    if (config_save()) {
        printf("Schedule %d deleted\n", schedule_id);
        net_cmd_push(NET_CMD_CONFIG_RELOAD);
        return "/api/success.json";
    }
    return "/api/error.json";
}

static const char *cgi_handler_zone_on(int index, int numParams, char *params[],
                                       char *value[]) {
    printf("zone on API called\n");

    const char *id_val = find_param(numParams, params, value, "id");
    const char *dur_val = find_param(numParams, params, value, "dur");
    if (!id_val) {
        return "/api/error.json";
    }

    int zone_id = atoi(id_val);
    uint16_t duration = dur_val ? (uint16_t)atoi(dur_val) : 10;  // Default 10 minutes

    // Send command to Core 0 via FIFO
    net_cmd_push(NET_MAKE_ZONE_ON(zone_id, duration));
    printf("Zone %d start command sent (%d minutes)\n", zone_id, duration);
    return "/api/success.json";
}

static const char *cgi_handler_zone_off(int index, int numParams, char *params[],
                                        char *value[]) {
    printf("zone off API called\n");

    // Send stop command to Core 0 via FIFO
    net_cmd_push(NET_CMD_ZONE_OFF);
    printf("Zone stop command sent\n");
    return "/api/success.json";
}

static const char *cgi_handler_zone_status(int index, int numParams, char *params[],
                                           char *value[]) {
    return "/api/zone_status.json";
}

static const char *cgi_handler_time(int index, int numParams, char *params[],
                                    char *value[]) {
    return "/api/time.json";
}

static const char *cgi_handler_programs(int index, int numParams, char *params[],
                                        char *value[]) {
    printf("programs API called\n");
    return "/api/programs.json";
}

static const char *cgi_handler_programs_save(int index, int numParams, char *params[],
                                             char *value[]) {
    printf("programs save API called with %d params\n", numParams);

    const char *id_val = find_param(numParams, params, value, "id");
    const char *name_val = find_param(numParams, params, value, "name");
    const char *en_val = find_param(numParams, params, value, "en");
    const char *type_val = find_param(numParams, params, value, "type");
    const char *days_val = find_param(numParams, params, value, "days");
    const char *hour_val = find_param(numParams, params, value, "hour");
    const char *min_val = find_param(numParams, params, value, "min");

    if (!id_val) {
        printf("Missing program id\n");
        return "/api/error.json";
    }

    int program_id = atoi(id_val);
    if (program_id < 1 || program_id > MAX_PROGRAMS) {
        printf("Invalid program id: %d\n", program_id);
        return "/api/error.json";
    }

    program_config_t prog = {0};

    if (name_val) {
        char decoded_name[MAX_PROGRAM_NAME_LEN + 1];
        strncpy(decoded_name, name_val, MAX_PROGRAM_NAME_LEN);
        decoded_name[MAX_PROGRAM_NAME_LEN] = '\0';
        url_decode(decoded_name);
        strncpy(prog.name, decoded_name, MAX_PROGRAM_NAME_LEN);
        prog.name[MAX_PROGRAM_NAME_LEN] = '\0';
    }

    prog.enabled = en_val ? (en_val[0] == '1' || en_val[0] == 't') : 1;
    prog.type = type_val ? (uint8_t)atoi(type_val) : 0;
    prog.day_mask = days_val ? (uint8_t)atoi(days_val) : 0;
    prog.hour = hour_val ? (uint8_t)atoi(hour_val) : 0;
    prog.minute = min_val ? (uint8_t)atoi(min_val) : 0;

    // Parse steps: s1z/s1d through s8z/s8d
    prog.step_count = 0;
    for (int s = 1; s <= MAX_PROGRAM_STEPS; s++) {
        char zone_param[8], dur_param[8];
        snprintf(zone_param, sizeof(zone_param), "s%dz", s);
        snprintf(dur_param, sizeof(dur_param), "s%dd", s);

        const char *sz = find_param(numParams, params, value, zone_param);
        const char *sd = find_param(numParams, params, value, dur_param);

        if (sz && sd) {
            uint8_t zone_id = (uint8_t)atoi(sz);
            uint16_t dur = (uint16_t)atoi(sd);
            if (zone_id > 0 && zone_id <= MAX_ZONES && dur > 0) {
                prog.steps[prog.step_count].zone_id = zone_id;
                prog.steps[prog.step_count].duration_mins = dur;
                prog.step_count++;
            }
        }
    }

    // Preserve last_run from existing config
    const program_config_t *existing = config_get_program(program_id);
    if (existing) {
        prog.last_run_day = existing->last_run_day;
        prog.last_run_year = existing->last_run_year;
    }

    printf("Program %d: name='%s', type=%d, days=%d, time=%02d:%02d, steps=%d, en=%d\n",
           program_id, prog.name, prog.type, prog.day_mask,
           prog.hour, prog.minute, prog.step_count, prog.enabled);

    config_set_program(program_id, &prog);

    if (config_save()) {
        printf("Program saved to flash\n");
        net_cmd_push(NET_CMD_CONFIG_RELOAD);
        return "/api/success.json";
    }
    return "/api/error.json";
}

static const char *cgi_handler_programs_delete(int index, int numParams, char *params[],
                                               char *value[]) {
    printf("program delete API called\n");

    const char *id_val = find_param(numParams, params, value, "id");
    if (!id_val) {
        return "/api/error.json";
    }

    int program_id = atoi(id_val);
    if (program_id < 1 || program_id > MAX_PROGRAMS) {
        return "/api/error.json";
    }

    config_clear_program(program_id);

    if (config_save()) {
        printf("Program %d deleted\n", program_id);
        net_cmd_push(NET_CMD_CONFIG_RELOAD);
        return "/api/success.json";
    }
    return "/api/error.json";
}

static const char *cgi_handler_programs_run(int index, int numParams, char *params[],
                                            char *value[]) {
    printf("program run API called\n");

    const char *id_val = find_param(numParams, params, value, "id");
    if (!id_val) {
        return "/api/error.json";
    }

    int program_id = atoi(id_val);
    if (program_id < 1 || program_id > MAX_PROGRAMS) {
        return "/api/error.json";
    }

    net_cmd_push(NET_MAKE_PROGRAM_RUN(program_id));
    printf("Program %d run command sent\n", program_id);
    return "/api/success.json";
}

static const char *cgi_handler_wifi_save(int index, int numParams, char *params[],
                                         char *value[]) {
    printf("wifi save API called\n");

    const char *ssid_val = find_param(numParams, params, value, "ssid");
    const char *pass_val = find_param(numParams, params, value, "pass");

    if (!ssid_val || ssid_val[0] == '\0') {
        printf("WiFi save: missing SSID\n");
        return "/api/error.json";
    }

    // URL decode the values
    char decoded_ssid[MAX_SSID_LEN + 1];
    strncpy(decoded_ssid, ssid_val, MAX_SSID_LEN);
    decoded_ssid[MAX_SSID_LEN] = '\0';
    url_decode(decoded_ssid);

    char decoded_pass[MAX_PASSWORD_LEN + 1];
    if (pass_val) {
        strncpy(decoded_pass, pass_val, MAX_PASSWORD_LEN);
        decoded_pass[MAX_PASSWORD_LEN] = '\0';
        url_decode(decoded_pass);
    } else {
        decoded_pass[0] = '\0';
    }

    printf("WiFi save: SSID='%s'\n", decoded_ssid);
    config_set_wifi(decoded_ssid, decoded_pass);

    if (config_save()) {
        printf("WiFi credentials saved to flash\n");
        return "/api/success.json";
    }
    printf("Failed to save WiFi credentials\n");
    return "/api/error.json";
}

static const char *cgi_handler_wifi_clear(int index, int numParams, char *params[],
                                           char *value[]) {
    printf("wifi clear API called\n");
    config_set_wifi("", "");
    if (config_save()) {
        printf("WiFi credentials cleared\n");
        return "/api/success.json";
    }
    return "/api/error.json";
}

static const char *cgi_handler_timezone_save(int index, int numParams, char *params[],
                                              char *value[]) {
    printf("timezone save API called\n");

    const char *tz_val = find_param(numParams, params, value, "tz");
    if (!tz_val || tz_val[0] == '\0') {
        return "/api/error.json";
    }

    char decoded_tz[MAX_TIMEZONE_LEN + 1];
    strncpy(decoded_tz, tz_val, MAX_TIMEZONE_LEN);
    decoded_tz[MAX_TIMEZONE_LEN] = '\0';
    url_decode(decoded_tz);

    printf("Timezone: '%s'\n", decoded_tz);
    config_set_timezone(decoded_tz);

    if (config_save()) {
        // Apply immediately
        setenv("TZ", decoded_tz, 1);
        tzset();
        printf("Timezone saved and applied\n");
        return "/api/success.json";
    }
    return "/api/error.json";
}

static const char *cgi_handler_wifi_reboot(int index, int numParams, char *params[],
                                            char *value[]) {
    printf("wifi reboot API called — rebooting soon\n");
    g_reboot_pending = true;
    return "/api/success.json";
}

static tCGI cgi_handlers[] = {{"/", cgi_handler_default},
                              {"/index.html", cgi_handler_default},
                              {"/api/sensors", cgi_handler_sensors},
                              {"/api/zones", cgi_handler_zones},
                              {"/api/zones/save", cgi_handler_zones_save},
                              {"/api/zones/on", cgi_handler_zone_on},
                              {"/api/zones/off", cgi_handler_zone_off},
                              {"/api/zones/status", cgi_handler_zone_status},
                              {"/api/schedules", cgi_handler_schedules},
                              {"/api/schedules/save", cgi_handler_schedules_save},
                              {"/api/schedules/delete", cgi_handler_schedule_delete},
                              {"/api/time", cgi_handler_time},
                              {"/api/programs", cgi_handler_programs},
                              {"/api/programs/save", cgi_handler_programs_save},
                              {"/api/programs/delete", cgi_handler_programs_delete},
                              {"/api/programs/run", cgi_handler_programs_run},
                              {"/api/wifi/save", cgi_handler_wifi_save},
                              {"/api/wifi/clear", cgi_handler_wifi_clear},
                              {"/api/wifi/reboot", cgi_handler_wifi_reboot},
                              {"/api/timezone/save", cgi_handler_timezone_save}};

// SSI tags - indices: 0=ip4_addr, 1=temp, 2=hum, 3=upd, 4-27=zone data, 28=scheds, 29=activez
static const char *ssi_tags[] = {
    "ip4_addr", "temp", "hum", "upd",
    // Zone 1-8: name, pin, enabled (3 tags per zone = 24 tags)
    "z1name", "z1pin", "z1en",
    "z2name", "z2pin", "z2en",
    "z3name", "z3pin", "z3en",
    "z4name", "z4pin", "z4en",
    "z5name", "z5pin", "z5en",
    "z6name", "z6pin", "z6en",
    "z7name", "z7pin", "z7en",
    "z8name", "z8pin", "z8en",
    // Schedules (outputs full JSON array)
    "scheds",
    // Active zone
    "activez",
    // Remaining minutes for active zone
    "remain",
    // Server time (from NTP)
    "stime",
    // DS3231 RTC time (UTC)
    "rtctime",
    // Programs (full JSON array)
    "progs",
    // Active program ID
    "prgact",
    // Current program step
    "prgstep",
    // Total program steps
    "prgsteps",
    // DS3231 battery status (true/false)
    "battok",
    // Current WiFi SSID from flash config
    "wfssid",
    // Current timezone (POSIX TZ string)
    "tz",
    // Watering history JSON array
    "hist",
    // Freeze protection active (true/false)
    "freeze"
};

// SSI handler
static u16_t ssi_handler(int index, char *insert, int insertlen) {
    size_t printed = 0;

    switch (index) {
    case 0: // ip4_addr
        printed = snprintf(insert, insertlen, "%s", ip4_addr);
        break;
    case 1: // temp - temperature in Fahrenheit
        printed = snprintf(insert, insertlen, "%d", g_sensor_data.temperature_f);
        break;
    case 2: // hum - humidity percentage
        printed = snprintf(insert, insertlen, "%d", g_sensor_data.humidity);
        break;
    case 3: // upd - time since last update
    {
        if (!g_sensor_data.valid) {
            printed = snprintf(insert, insertlen, "No data");
        } else {
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());
            uint32_t elapsed_s = (now_ms - g_sensor_data.last_read_ms) / 1000;
            if (elapsed_s < 60) {
                printed = snprintf(insert, insertlen, "%lu seconds ago", elapsed_s);
            } else {
                printed = snprintf(insert, insertlen, "%lu minutes ago", elapsed_s / 60);
            }
        }
    } break;
    case 28: // scheds - output full schedules JSON array
    {
        char *p = insert;
        int remaining = insertlen;
        int n;

        n = snprintf(p, remaining, "[");
        p += n; remaining -= n;

        bool first = true;
        for (int i = 1; i <= MAX_SCHEDULES && remaining > 150; i++) {
            const schedule_config_t *sched = config_get_schedule(i);
            if (sched && sched->zone_id > 0) {
                n = snprintf(p, remaining, "%s{\"id\":%d,\"zone\":%d,\"type\":%d,\"days\":%d,\"hour\":%d,\"minute\":%d,\"duration\":%d,\"enabled\":%s,\"lastDay\":%d,\"lastYear\":%d}",
                             first ? "" : ",",
                             i, sched->zone_id, sched->type, sched->day_mask,
                             sched->hour, sched->minute, sched->duration_mins,
                             sched->enabled ? "true" : "false",
                             sched->last_run_day, sched->last_run_year);
                p += n; remaining -= n;
                first = false;
            }
        }

        n = snprintf(p, remaining, "]");
        p += n;
        printed = p - insert;
    } break;
    case 29: // activez - currently active zone
        printed = snprintf(insert, insertlen, "%d", zones_get_active());
        break;
    case 30: // remain - remaining minutes for active zone
    {
        scheduler_status_t status = scheduler_get_status();
        printed = snprintf(insert, insertlen, "%d", status.remaining_mins);
    } break;
    case 31: // stime - server time from NTP
    {
        time_t now = time(NULL);
        struct tm tm_buf;
        struct tm *tm_info = localtime_r(&now, &tm_buf);
        if (tm_info && now > 1000000000) {  // Valid time (after year 2001)
            printed = snprintf(insert, insertlen, "%04d-%02d-%02d %02d:%02d:%02d",
                               tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                               tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
        } else {
            printed = snprintf(insert, insertlen, "Syncing...");
        }
    } break;
    case 32: // rtctime - DS3231 RTC time (UTC)
        printed = ds3231_get_time_str(insert, insertlen);
        break;
    case 33: // progs - output full programs JSON array
    {
        char *p = insert;
        int remaining = insertlen;
        int n;

        n = snprintf(p, remaining, "[");
        p += n; remaining -= n;

        bool first_prog = true;
        for (int i = 1; i <= MAX_PROGRAMS && remaining > 200; i++) {
            const program_config_t *prog = config_get_program(i);
            if (prog && prog->name[0] != '\0') {
                n = snprintf(p, remaining, "%s{\"id\":%d,\"name\":\"%s\",\"enabled\":%s,\"type\":%d,\"days\":%d,\"hour\":%d,\"minute\":%d,\"stepCount\":%d,\"lastDay\":%d,\"lastYear\":%d,\"steps\":[",
                             first_prog ? "" : ",",
                             i, prog->name, prog->enabled ? "true" : "false",
                             prog->type, prog->day_mask, prog->hour, prog->minute,
                             prog->step_count, prog->last_run_day, prog->last_run_year);
                p += n; remaining -= n;

                for (int s = 0; s < prog->step_count && remaining > 40; s++) {
                    n = snprintf(p, remaining, "%s{\"zone\":%d,\"duration\":%d}",
                                 s > 0 ? "," : "",
                                 prog->steps[s].zone_id, prog->steps[s].duration_mins);
                    p += n; remaining -= n;
                }

                n = snprintf(p, remaining, "]}");
                p += n; remaining -= n;
                first_prog = false;
            }
        }

        n = snprintf(p, remaining, "]");
        p += n;
        printed = p - insert;
    } break;
    case 34: // prgact - active program ID
    {
        scheduler_status_t status = scheduler_get_status();
        printed = snprintf(insert, insertlen, "%d", status.active_program_id);
    } break;
    case 35: // prgstep - current program step
    {
        scheduler_status_t status = scheduler_get_status();
        printed = snprintf(insert, insertlen, "%d", status.program_step);
    } break;
    case 36: // prgsteps - total program steps
    {
        scheduler_status_t status = scheduler_get_status();
        printed = snprintf(insert, insertlen, "%d", status.program_total_steps);
    } break;
    case 37: // battok - DS3231 battery status
        printed = snprintf(insert, insertlen, "%s", ds3231_battery_ok() ? "true" : "false");
        break;
    case 38: // wfssid - current WiFi SSID
        printed = snprintf(insert, insertlen, "%s", config_get_ssid());
        break;
    case 39: // tz - current timezone
        printed = snprintf(insert, insertlen, "%s", config_get_timezone());
        break;
    case 40: // hist - watering history JSON array
        printed = history_get_json(insert, insertlen);
        break;
    case 41: // freeze - freeze protection active
        printed = snprintf(insert, insertlen, "%s", scheduler_is_freeze_active() ? "true" : "false");
        break;
    default:
        // Zone data: indices 4-27 (8 zones * 3 fields each)
        if (index >= 4 && index < 28) {
            int zone_index = (index - 4) / 3;  // 0-7
            int field = (index - 4) % 3;        // 0=name, 1=pin, 2=enabled
            const zone_config_t *zone = config_get_zone(zone_index + 1);
            if (zone) {
                switch (field) {
                case 0: // name
                    printed = snprintf(insert, insertlen, "%s", zone->name);
                    break;
                case 1: // pin
                    printed = snprintf(insert, insertlen, "%d", zone->gpio_pin);
                    break;
                case 2: // enabled
                    printed = snprintf(insert, insertlen, "%s", zone->enabled ? "true" : "false");
                    break;
                }
            }
        }
        break;
    }

    return (u16_t)printed;
}

// DHCP server entries for AP mode (pool of assignable IPs: 192.168.4.2 - 192.168.4.5)
#define AP_ADDR_0 192
#define AP_ADDR_1 168
#define AP_ADDR_2 4
#define AP_ADDR_3 1
#define NUM_DHCP_ENTRIES 4

static dhcp_entry_t dhcp_entries[NUM_DHCP_ENTRIES] = {
    {{0}, {AP_ADDR_0, AP_ADDR_1, AP_ADDR_2, 2}, {255, 255, 255, 0}, 24 * 60 * 60},
    {{0}, {AP_ADDR_0, AP_ADDR_1, AP_ADDR_2, 3}, {255, 255, 255, 0}, 24 * 60 * 60},
    {{0}, {AP_ADDR_0, AP_ADDR_1, AP_ADDR_2, 4}, {255, 255, 255, 0}, 24 * 60 * 60},
    {{0}, {AP_ADDR_0, AP_ADDR_1, AP_ADDR_2, 5}, {255, 255, 255, 0}, 24 * 60 * 60},
};

static dhcp_config_t dhcp_config = {
    .addr = {AP_ADDR_0, AP_ADDR_1, AP_ADDR_2, AP_ADDR_3},
    .port = 67,
    .dns = {AP_ADDR_0, AP_ADDR_1, AP_ADDR_2, AP_ADDR_3},
    .domain = "sprinkler",
    .num_entry = NUM_DHCP_ENTRIES,
    .entries = dhcp_entries,
};

// Initialize AP mode for WiFi setup
static bool network_init_ap(void) {
    printf("Network: No WiFi credentials — starting AP mode\n");
    lcd_display_set_status("AP Setup Mode");
    g_ap_mode = true;

    // Start AP with open network
    cyw43_arch_enable_ap_mode("SprinklerSetup", NULL, CYW43_AUTH_OPEN);
    printf("Network: AP 'SprinklerSetup' started (open)\n");

    // Set static IP on the AP interface
    struct netif *n = &cyw43_state.netif[CYW43_ITF_AP];
    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip,   AP_ADDR_0, AP_ADDR_1, AP_ADDR_2, AP_ADDR_3);
    IP4_ADDR(&mask,  255, 255, 255, 0);
    IP4_ADDR(&gw,   AP_ADDR_0, AP_ADDR_1, AP_ADDR_2, AP_ADDR_3);
    netif_set_addr(n, &ip, &mask, &gw);
    netif_set_up(n);
    netif_set_default(n);

    ip4_addr = "192.168.4.1";

    // Start DHCP server
    err_t err = dhserv_init(&dhcp_config);
    if (err != ERR_OK) {
        printf("Network: DHCP server failed to start (err %d)\n", err);
    } else {
        printf("Network: DHCP server started\n");
    }

    // Start HTTP server with CGI handlers
    printf("Network: Starting HTTP server (AP mode)...\n");
    httpd_init();
    http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
    http_set_ssi_handler(ssi_handler, ssi_tags, LWIP_ARRAYSIZE(ssi_tags));
    printf("Network: HTTP server running on 192.168.4.1:80\n");

    lcd_display_set_ip("192.168.4.1");
    lcd_display_startup_complete();
    return true;
}

// Initialize all network services
bool network_init(void) {
    // Initialize the Wi-Fi chip
    printf("Network: Initializing WiFi chip...\n");
    lcd_display_set_status("Init WiFi chip");
    if (cyw43_arch_init()) {
        printf("Network: Wi-Fi chip init failed\n");
        lcd_display_set_status("WiFi Failed");
        return false;
    }

    // Check flash for WiFi credentials
    const char *ssid = config_get_ssid();
    if (ssid[0] == '\0') {
        return network_init_ap();
    }

    const char *password = config_get_password();
    printf("Network: Connecting to %s...\n", ssid);
    lcd_display_set_status("Connecting WiFi");

    // Enable wifi station mode
    cyw43_arch_enable_sta_mode();

    // Connect to WiFi — fall back to AP setup mode on failure
    if (cyw43_arch_wifi_connect_timeout_ms(ssid, password,
                                           CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Network: Failed to connect to WiFi, falling back to AP setup mode\n");
        lcd_display_set_status("WiFi Failed");
        cyw43_arch_deinit();
        if (cyw43_arch_init()) {
            printf("Network: Wi-Fi chip re-init failed\n");
            return false;
        }
        return network_init_ap();
    }

    printf("Network: Connected!\n");
    sleep_ms(1000);

    // Get IP address
    ip4_addr = ip4addr_ntoa(netif_default ? &netif_default->ip_addr : NULL);
    printf("Network: IP address %s\n", ip4_addr);
    lcd_display_set_ip(ip4_addr);

    // Initialize NTP
    printf("Network: Starting NTP...\n");
    const char* tz = config_get_timezone();
    printf("Network: Timezone = %s\n", tz);
    setenv("TZ", tz, 1);
    tzset();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.google.com");
    sntp_init();
    printf("Network: NTP started\n");

    // Initialize HTTP server (in polling mode, this just sets up handlers)
    printf("Network: Starting HTTP server...\n");
    httpd_init();
    http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
    http_set_ssi_handler(ssi_handler, ssi_tags, LWIP_ARRAYSIZE(ssi_tags));
    printf("Network: HTTP server running on port 80\n");

    // Initialize mDNS responder (sprinkler.local)
    mdns_resp_init();
    mdns_resp_add_netif(netif_default, "sprinkler");
    printf("Network: mDNS responder started (sprinkler.local)\n");

    printf("Network: Initialization complete\n");
    lcd_display_startup_complete();
    return true;
}

// Check WiFi connection status and reconnect if needed
// Call periodically from main loop (~30 seconds)
void network_check_wifi(void) {
    if (!wifi_is_connected()) {
        printf("Network: WiFi disconnected, attempting reconnection\n");
        wifi_reconnect();
    }
}

// Returns true if device is in AP setup mode
bool network_is_ap_mode(void) {
    return g_ap_mode;
}

// Returns true if a reboot has been requested (stops watchdog feeding)
bool network_reboot_pending(void) {
    return g_reboot_pending;
}
