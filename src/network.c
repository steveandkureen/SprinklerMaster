#include "network.h"
#include "FreeRTOS.h"
#include "config.h"
#include "lcd.h"
#include "lwip/apps/httpd.h"
#include "lwip/ip4_addr.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
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

static tCGI cgi_handlers[] = {{"/", cgi_handler_default},
                              {"/index.html", cgi_handler_default}};

// SSI tags
static const char *ssi_tags[] = {"ip4_addr"};

// SSI handler
static u16_t ssi_handler(int index, char *insert, int insertlen) {
  size_t printed = 0;

  printf("SSI called\n");
  switch (index) {
  case 0: // ip4_addr
  {
    printf("IP replace address %s\n", ip4_addr);
    printed = snprintf(insert, insertlen, "%s", ip4_addr);
  } break;
  default:
    printed = 0;
    break;
  }

  printf("IP replace address2  %s\n", ip4_addr);
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
