#include "lcd.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "src/config_flash.h"
#include "src/dht22.h"
#include "src/fault_tolerance.h"
#include "src/network.h"
#include "src/scheduler.h"
#include "src/zones.h"
#include "src/lcd_display.h"
#include "src/ds3231.h"
#include <pico/types.h>
#include <stdio.h>

// Core 0 polling intervals (in milliseconds)
#define SENSOR_POLL_INTERVAL_MS    3000
#define SCHEDULER_POLL_INTERVAL_MS 5000
#define LCD_POLL_INTERVAL_MS       1000
#define ZONE_CHECK_INTERVAL_MS     1000
#define MEMORY_LOG_INTERVAL_MS     60000

// Core 1 polling intervals (in milliseconds)
#define LED_POLL_INTERVAL_MS       1000
#define WIFI_CHECK_INTERVAL_MS     30000

// Shared command queue instance (Core 1 writes, Core 0 reads)
net_cmd_queue_t g_cmd_queue = {0};

// Polling function for sensor reads
static void sensor_poll(void) {
    if (!dht_read()) {
        printf("Sensor: Read failed\n");
    }
}

// Process commands from Core 1 via shared command queue
static void process_core1_commands(void) {
    uint32_t cmd;
    while (net_cmd_pop(&cmd)) {
        uint32_t cmd_type = cmd & NET_CMD_MASK;

        switch (cmd_type) {
        case NET_CMD_CONFIG_RELOAD:
            printf("CMD: Config reload requested\n");
            scheduler_init();
            break;
        case NET_CMD_ZONE_ON: {
            uint8_t zone_id = NET_CMD_ZONE_ID(cmd);
            uint16_t duration = NET_CMD_DURATION(cmd);
            printf("CMD: Zone %d on for %d mins\n", zone_id, duration);
            scheduler_manual_run(zone_id, duration);
            break;
        }
        case NET_CMD_ZONE_OFF:
            printf("CMD: Zone off requested\n");
            scheduler_stop_current();
            break;
        case NET_CMD_PROGRAM_RUN: {
            uint8_t program_id = NET_CMD_ZONE_ID(cmd);
            printf("CMD: Program %d run requested\n", program_id);
            scheduler_run_program(program_id);
            break;
        }
        default:
            printf("CMD: Unknown command 0x%08lX\n", (unsigned long)cmd);
            break;
        }
    }
}

// Core 1 entry point: network polling loop
static void core1_entry(void) {
    printf("Core 1: Starting network loop\n");

    bool led_state = false;
    uint32_t last_led_ms = 0;
    uint32_t last_wifi_ms = 0;

    while (true) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        // Poll WiFi/lwIP stack frequently (~1ms)
        cyw43_arch_poll();

        // LED blink (1 second)
        if ((now_ms - last_led_ms) >= LED_POLL_INTERVAL_MS) {
            last_led_ms = now_ms;
            led_state = !led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
        }

        // WiFi connection check (30 seconds) — skip in AP mode
        if (!network_is_ap_mode() && (now_ms - last_wifi_ms) >= WIFI_CHECK_INTERVAL_MS) {
            last_wifi_ms = now_ms;
            network_check_wifi();
        }

        // Check for pending reboot request from web UI
        if (network_reboot_pending()) {
            printf("Core 1: Reboot requested, triggering watchdog reset\n");
            watchdog_enable(100, 1);
            while (1) tight_loop_contents();
        }

        // Feed watchdog from Core 1 as well
        watchdog_update();

        // Small delay to prevent tight spinning
        sleep_ms(1);
    }
}

int main() {
    stdio_init_all();

    // Wait for USB serial to connect (so we can see boot messages)
    sleep_ms(2000);

    printf("\n=== SprinklerMaster Starting (Dual Core) ===\n");

    // Initialize multi-core flash safety (needed for dual-core flash writes)
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

    // Initialize DS3231 RTC (sets system clock from battery-backed RTC)
    if (ds3231_init()) {
        printf("RTC: System clock set from DS3231\n");
    } else {
        printf("RTC: DS3231 not available, will wait for NTP\n");
    }

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

    // Launch Core 1 for network polling
    printf("Launching Core 1 for network...\n");
    multicore_launch_core1(core1_entry);

    printf("Core 0: Starting main loop\n");

    // Timing variables for Core 0 polling
    uint32_t last_sensor_ms = 0;
    uint32_t last_scheduler_ms = 0;
    uint32_t last_lcd_ms = 0;
    uint32_t last_zone_ms = 0;
    uint32_t last_memory_ms = 0;

    // Core 0 super loop: scheduler, sensors, LCD, zone safety
    while (true) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        // Process any commands from Core 1
        process_core1_commands();

        // Sensor read (3 seconds)
        if ((now_ms - last_sensor_ms) >= SENSOR_POLL_INTERVAL_MS) {
            last_sensor_ms = now_ms;
            sensor_poll();
        }

        // Scheduler check (5 seconds) — skip in AP mode (no WiFi for NTP)
        if (!network_is_ap_mode() && (now_ms - last_scheduler_ms) >= SCHEDULER_POLL_INTERVAL_MS) {
            last_scheduler_ms = now_ms;
            scheduler_poll();
        }

        // LCD display update (1 second)
        if ((now_ms - last_lcd_ms) >= LCD_POLL_INTERVAL_MS) {
            last_lcd_ms = now_ms;
            lcd_display_poll();
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

        // Feed watchdog from Core 0
        watchdog_update();

        // Small delay to prevent tight spinning
        sleep_ms(1);
    }

    // Should never reach here
    return 0;
}
