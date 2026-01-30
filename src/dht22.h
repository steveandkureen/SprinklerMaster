#ifndef DHT22_H
#define DHT22_H

#include <stdbool.h>
#include <stdint.h>

// DHT11 sensor pin
#define DHT_PIN 15

// Sensor data structure
typedef struct {
    int temperature_f;    // Fahrenheit (integer for DHT11)
    int humidity;         // Percentage
    bool valid;           // Last read successful
    uint32_t last_read_ms; // Timestamp of last read
} sensor_data_t;

// Global sensor data accessible from other modules
extern sensor_data_t g_sensor_data;

// Initialize DHT11 sensor GPIO
void dht_init(void);

// Read temperature and humidity from DHT11
// Returns true on successful read, false on error
bool dht_read(void);

#endif // DHT22_H
