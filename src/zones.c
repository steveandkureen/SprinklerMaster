#include "zones.h"
#include "config_flash.h"
#include "hardware/gpio.h"
#include <stdio.h>

// Currently active zone (0 = none)
static uint8_t active_zone = 0;

// Initialize all zone GPIOs as outputs, initially off
void zones_init(void) {
    printf("Zones: Initializing GPIO pins\n");

    for (uint8_t i = 1; i <= MAX_ZONES; i++) {
        const zone_config_t *zone = config_get_zone(i);
        if (zone) {
            gpio_init(zone->gpio_pin);
            gpio_set_dir(zone->gpio_pin, GPIO_OUT);
            gpio_put(zone->gpio_pin, 0);  // Start with all zones off
            printf("Zones: Zone %d -> GPIO %d initialized\n", i, zone->gpio_pin);
        }
    }

    active_zone = 0;
    printf("Zones: All zones initialized and OFF\n");
}

// Turn on a zone (turns off any other active zone first)
bool zone_on(uint8_t zone_id) {
    if (zone_id < 1 || zone_id > MAX_ZONES) {
        return false;
    }

    const zone_config_t *zone = config_get_zone(zone_id);
    if (!zone || !zone->enabled) {
        return false;
    }

    // Safety: Turn off any currently active zone first
    if (active_zone != 0 && active_zone != zone_id) {
        zone_off(active_zone);
    }

    // Turn on the requested zone
    gpio_put(zone->gpio_pin, 1);
    active_zone = zone_id;

    return true;
}

// Turn off a specific zone
void zone_off(uint8_t zone_id) {
    if (zone_id < 1 || zone_id > MAX_ZONES) {
        return;
    }

    const zone_config_t *zone = config_get_zone(zone_id);
    if (!zone) {
        return;
    }

    gpio_put(zone->gpio_pin, 0);

    if (active_zone == zone_id) {
        active_zone = 0;
    }
}

// Turn off all zones
void zones_all_off(void) {
    printf("Zones: Turning all zones OFF\n");

    for (uint8_t i = 1; i <= MAX_ZONES; i++) {
        const zone_config_t *zone = config_get_zone(i);
        if (zone) {
            gpio_put(zone->gpio_pin, 0);
        }
    }

    active_zone = 0;
}

// Get currently active zone (0 = none active)
uint8_t zones_get_active(void) {
    return active_zone;
}

// Check if a specific zone is currently on
bool zone_is_on(uint8_t zone_id) {
    return active_zone == zone_id && zone_id != 0;
}
