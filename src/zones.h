#ifndef ZONES_H
#define ZONES_H

#include <stdbool.h>
#include <stdint.h>

// Initialize zone GPIOs
void zones_init(void);

// Turn on a zone (turns off any other active zone first)
// Returns false if zone_id is invalid or zone is disabled
bool zone_on(uint8_t zone_id);

// Turn off a specific zone
void zone_off(uint8_t zone_id);

// Turn off all zones
void zones_all_off(void);

// Get currently active zone (0 = none active)
uint8_t zones_get_active(void);

// Check if a specific zone is currently on
bool zone_is_on(uint8_t zone_id);

#endif // ZONES_H
