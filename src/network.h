#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>

// Initialize network (WiFi, NTP, HTTP server)
bool network_init(void);

// Check WiFi connection and reconnect if needed
// Call periodically from main loop (~30 seconds)
void network_check_wifi(void);

#endif // NETWORK_H
