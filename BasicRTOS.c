#include "lcd.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "src/config_flash.h"
#include "src/dht22.h"
#include "src/fault_tolerance.h"
#include "src/network.h"
#include "src/scheduler.h"
#include "src/zones.h"
#include "src/lcd_display.h"
#include <pico/types.h>
#include <stdio.h>

// Polling intervals (in milliseconds)
#define LED_POLL_INTERVAL_MS       1000
#define SENSOR_POLL_INTERVAL_MS    3000
#define SCHEDULER_POLL_INTERVAL_MS 5000
#define LCD_POLL_INTERVAL_MS       1000
#define WIFI_CHECK_INTERVAL_MS     30000
#define ZONE_CHECK_INTERVAL_MS     1000
#define MEMORY_LOG_INTERVAL_MS     60000

// LED state
static bool led_state = false;

// Polling function for LED blink
static void led_poll(void) {
    led_state = !led_state;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
}

// Polling function for sensor reads
static void sensor_poll(void) {
    if (!dht_read()) {
        printf("Sensor: Read failed\n");
    }
}

int main() {
    stdio_init_all();

    // Wait for USB serial to connect (so we can see boot messages)
    sleep_ms(2000);

    printf("\n=== SprinklerMaster Starting (Super Loop) ===\n");

    // Initialize multi-core flash safety (still needed for dual-core)
    if (flash_safe_execute_core_init()) {
        printf("Warning: Multi-core flash init returned non-zero\n");
    }

    // Initialize configuration from flash storage
    config_init();

    // Initialize zone GPIO pins
    zones_init();

    // Initialize fault tolerance (watchdog, reset detection)
    fault_tolerance_init();

    // Initialize LCD
    lcd_init();
    lcd_display_init();

    // Initialize DHT22 sensor
    dht_init();

    // Initialize network (WiFi, NTP, HTTP server)
    printf("Initializing network...\n");
    if (!network_init()) {
        printf("ERROR: Network initialization failed!\n");
        lcd_display_set_status("Network Failed");
    }

    // Save boot stats now that flash operations are safe
    fault_tolerance_save_boot_stats();

    // Initialize scheduler
    scheduler_init();

    // Wait for system to stabilize before enabling watchdog
    printf("Waiting 60s for system to stabilize before enabling watchdog...\n");
    uint32_t stabilize_start = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - stabilize_start) < 60000) {
        cyw43_arch_poll();
        watchdog_update();  // Keep feeding during stabilization
        sleep_ms(100);
    }

    // Enable watchdog now that startup is complete
    fault_tolerance_enable_watchdog();

    printf("Starting super loop...\n");

    // Timing variables for polling
    uint32_t last_led_ms = 0;
    uint32_t last_sensor_ms = 0;
    uint32_t last_scheduler_ms = 0;
    uint32_t last_lcd_ms = 0;
    uint32_t last_wifi_ms = 0;
    uint32_t last_zone_ms = 0;
    uint32_t last_memory_ms = 0;

    // Super loop
    while (true) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        // CRITICAL: Poll WiFi/lwIP stack frequently (~1ms)
        cyw43_arch_poll();

        // LED blink (1 second)
        if ((now_ms - last_led_ms) >= LED_POLL_INTERVAL_MS) {
            last_led_ms = now_ms;
            led_poll();
        }

        // Sensor read (3 seconds)
        if ((now_ms - last_sensor_ms) >= SENSOR_POLL_INTERVAL_MS) {
            last_sensor_ms = now_ms;
            sensor_poll();
        }

        // Scheduler check (5 seconds)
        if ((now_ms - last_scheduler_ms) >= SCHEDULER_POLL_INTERVAL_MS) {
            last_scheduler_ms = now_ms;
            scheduler_poll();
        }

        // LCD display update (1 second)
        if ((now_ms - last_lcd_ms) >= LCD_POLL_INTERVAL_MS) {
            last_lcd_ms = now_ms;
            lcd_display_poll();
        }

        // WiFi connection check (30 seconds)
        if ((now_ms - last_wifi_ms) >= WIFI_CHECK_INTERVAL_MS) {
            last_wifi_ms = now_ms;
            network_check_wifi();
        }

        // Zone safety timeout check (1 second)
        if ((now_ms - last_zone_ms) >= ZONE_CHECK_INTERVAL_MS) {
            last_zone_ms = now_ms;
            fault_tolerance_check_zone_timeout();
        }

        // Memory stats logging (60 seconds)
        if ((now_ms - last_memory_ms) >= MEMORY_LOG_INTERVAL_MS) {
            last_memory_ms = now_ms;
            fault_tolerance_log_memory_stats();
        }

        // Feed watchdog unconditionally (single-threaded = always healthy)
        watchdog_update();

        // Small delay to prevent tight spinning
        sleep_ms(1);
    }

    // Should never reach here
    return 0;
}
