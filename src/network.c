#include "FreeRTOS.h"
#include "task.h"
#include "network.h"
#include "config.h"
#include "lcd.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/types.h"
#include "wifi_credentials.h"
#include <stdio.h>

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
    char *ip_address =
        ip4addr_ntoa(netif_default ? &netif_default->ip_addr : NULL);
    printf("IP address %s\n", ip_address);

    // Display IP address on LCD
    snprintf(ip_str, sizeof(ip_str), "%s", ip_address);
    lcd_set_text(1, 0, ip_address);
  }
  return true;
}

bool run_server(void *pvParameters) { return true; }
