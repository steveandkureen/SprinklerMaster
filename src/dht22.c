#include "dht22.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdio.h>

// Global sensor data
sensor_data_t g_sensor_data = {
    .temperature_f = 0,
    .humidity = 0,
    .valid = false,
    .last_read_ms = 0
};

void dht_init(void) {
    gpio_init(DHT_PIN);
}

// Wait for pin to reach expected level, with timeout
// Returns time waited in microseconds, or -1 if timeout
static int wait_for_level(bool level, uint32_t timeout_us) {
    uint32_t start = time_us_32();
    while (gpio_get(DHT_PIN) != level) {
        if ((time_us_32() - start) > timeout_us) {
            return -1;
        }
    }
    return (int)(time_us_32() - start);
}

bool dht_read(void) {
    uint8_t data[5] = {0};
    bool success = true;
    int error_bit = -1;
    const char *error_msg = NULL;

    // Send start signal: pull low for 18ms, then release
    gpio_set_dir(DHT_PIN, GPIO_OUT);
    gpio_put(DHT_PIN, 0);
    sleep_ms(18);
    gpio_put(DHT_PIN, 1);
    sleep_us(30);

    // Switch to input mode to read response
    gpio_set_dir(DHT_PIN, GPIO_IN);

    // Disable interrupts for timing-critical section
    uint32_t irq_status = save_and_disable_interrupts();

    // DHT pulls low for 80us, then high for 80us as response
    if (wait_for_level(false, 100) < 0) {
        error_msg = "No response (low)";
        success = false;
    } else if (wait_for_level(true, 100) < 0) {
        error_msg = "No response (high)";
        success = false;
    } else if (wait_for_level(false, 100) < 0) {
        error_msg = "No data start";
        success = false;
    }

    // Read 40 bits (5 bytes)
    if (success) {
        for (int i = 0; i < 40; i++) {
            // Wait for high level (start of bit)
            if (wait_for_level(true, 100) < 0) {
                error_msg = "Bit start timeout";
                error_bit = i;
                success = false;
                break;
            }

            // Measure how long the pin stays high
            // ~26-28us = 0, ~70us = 1
            int high_time = wait_for_level(false, 100);
            if (high_time < 0) {
                error_msg = "Bit end timeout";
                error_bit = i;
                success = false;
                break;
            }

            // Shift in the bit (high_time > 50us means '1')
            data[i / 8] <<= 1;
            if (high_time > 50) {
                data[i / 8] |= 1;
            }
        }
    }

    // Re-enable interrupts
    restore_interrupts(irq_status);

    if (!success) {
        if (error_bit >= 0) {
            printf("DHT: %s (bit %d)\n", error_msg, error_bit);
        } else {
            printf("DHT: %s\n", error_msg);
        }
        return false;
    }

    // Verify checksum
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        printf("DHT: Checksum error (got %d, expected %d)\n", checksum, data[4]);
        return false;
    }

    // DHT11 format:
    // data[0] = humidity integer
    // data[1] = humidity decimal (always 0 for DHT11)
    // data[2] = temperature integer (Celsius)
    // data[3] = temperature decimal (always 0 for DHT11)
    int temp_c = data[2];
    int humidity = data[0];

    // Convert Celsius to Fahrenheit
    int temp_f = (temp_c * 9 / 5) + 32;

    // Store results
    g_sensor_data.temperature_f = temp_f;
    g_sensor_data.humidity = humidity;
    g_sensor_data.valid = true;
    g_sensor_data.last_read_ms = to_ms_since_boot(get_absolute_time());

    printf("DHT: Temp=%dF Humidity=%d%%\n", temp_f, humidity);

    return true;
}
