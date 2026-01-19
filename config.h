#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_SSID_LENGTH 32
#define MAX_PASSWORD_LENGTH 64

// WiFi configuration structure
typedef struct {
    char ssid[MAX_SSID_LENGTH + 1];
    char password[MAX_PASSWORD_LENGTH + 1];
} wifi_config_t;

#endif // CONFIG_H
