#include "network.h"
#include "FreeRTOS.h"
#include "config.h"
#include "config_flash.h"
#include "dht22.h"
#include "lcd.h"
#include "zones.h"
#include "scheduler.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/sntp.h"
#include "lwip/ip4_addr.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/time.h"
#include "pico/types.h"
#include "task.h"
#include "wifi_credentials.h"
#include <lwip/def.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

char *ip4_addr = NULL;

// Initialize NTP time synchronization
static void init_ntp(void) {
    printf("NTP: Initializing...\n");

    // Set timezone (adjust for your location)
    // UTC offset in seconds: e.g., PST = -8 hours = -28800
    // For Central Time: -6 hours = -21600 (CST) or -5 hours = -18000 (CDT)
    setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1);  // US Central with DST
    tzset();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.google.com");
    sntp_init();

    printf("NTP: Started, waiting for sync...\n");
}

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

bool init_wifi(void *pvParameters) {
  // Now continue with the actual application
  char ip_str[16];
  wifi_config_t wifi_config;

  if (flash_safe_execute_core_init()) {
    printf("Multi-core init failed\n");
  }
  // Initialise the Wi-Fi chip
  if (cyw43_arch_init()) {
    printf("Wi-Fi init failed\n");
  }

  // Initialize flash storage
  lcd_set_text(1, 0, "Loading config..");

  // First boot - load WiFi credentials from wifi_credentials.h
  strncpy(wifi_config.ssid, WIFI_SSID, MAX_SSID_LENGTH);
  wifi_config.ssid[MAX_SSID_LENGTH] = '\0';
  strncpy(wifi_config.password, WIFI_PASSWORD, MAX_PASSWORD_LENGTH);
  wifi_config.password[MAX_PASSWORD_LENGTH] = '\0';

  printf("WiFi config: SSID=%s\n", wifi_config.ssid);

  // Initialise the Wi-Fi chip
  lcd_set_text(1, 0, "Init WiFi chip ");

  // Enable wifi station
  cyw43_arch_enable_sta_mode();

  printf("Connecting to Wi-Fi...\n");
  lcd_set_text(1, 0, "Connecting WiFi");

  if (cyw43_arch_wifi_connect_timeout_ms(wifi_config.ssid, wifi_config.password,
                                         CYW43_AUTH_WPA2_AES_PSK, 30000)) {
    printf("failed to connect.\n");
    lcd_set_text(1, 0, "WiFi Failed    ");
    vTaskDelete(NULL);
  } else {
    printf("Connected.\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
    // Read the ip address in a human readable way
    ip4_addr = ip4addr_ntoa(netif_default ? &netif_default->ip_addr : NULL);
    printf("IP address %s\n", ip4_addr);

    // Display IP address on LCD
    snprintf(ip_str, sizeof(ip_str), "%s", ip4_addr);
    lcd_set_text(1, 0, ip4_addr);

    // Initialize NTP time synchronization
    init_ntp();
  }
  return true;
}

static const char *cgi_handler_default(int index, int numParams, char *params[],
                                       char *value[]) {

  printf("cgi called\n");
  return "/welcome.shtml";
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

  // Use scheduler for manual run to track duration
  scheduler_manual_run(zone_id, duration);
  printf("Zone %d started for %d minutes\n", zone_id, duration);
  return "/api/success.json";
}

static const char *cgi_handler_zone_off(int index, int numParams, char *params[],
                                        char *value[]) {
  printf("zone off API called\n");

  // Use scheduler to stop current zone (handles cleanup)
  scheduler_stop_current();
  printf("Zone stopped\n");
  return "/api/success.json";
}

static const char *cgi_handler_zone_status(int index, int numParams, char *params[],
                                           char *value[]) {
  return "/api/zone_status.json";
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
                              {"/api/schedules/delete", cgi_handler_schedule_delete}};

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
    "remain"
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

bool run_server(void *pvParameters) {
  cyw43_arch_lwip_begin();
  httpd_init();
  http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
  http_set_ssi_handler(ssi_handler, ssi_tags, LWIP_ARRAYSIZE(ssi_tags));
  cyw43_arch_lwip_end();

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  return true;
}
