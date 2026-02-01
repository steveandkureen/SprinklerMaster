#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>

// Initialize network (WiFi, mDNS, NTP) - call before FreeRTOS starts
bool network_init(void);

// Start the HTTP server task
void httpd_task(void *pvParameters);

#endif // NETWORK_H
