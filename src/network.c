#include "network.h"
#include "FreeRTOS.h"
#include "config.h"
#include "dht22.h"
#include "lcd.h"
#include "lwip/apps/httpd.h"
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

char *ip4_addr = NULL;

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

static tCGI cgi_handlers[] = {{"/", cgi_handler_default},
                              {"/index.html", cgi_handler_default},
                              {"/api/sensors", cgi_handler_sensors}};

// SSI tags - indices: 0=ip4_addr, 1=temp, 2=hum, 3=upd
static const char *ssi_tags[] = {"ip4_addr", "temp", "hum", "upd"};

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
  default:
    printed = 0;
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
