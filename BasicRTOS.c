#include "FreeRTOS.h"
#include "lcd.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "src/config_flash.h"
#include "src/dht22.h"
#include "src/fault_tolerance.h"
#include "src/network.h"
#include "src/scheduler.h"
#include "src/zones.h"
#include "src/lcd_display.h"
#include "task.h"
#include <pico/types.h>
#include <stdio.h>

// FreeRTOS hook functions
void vApplicationMallocFailedHook(void) {
  printf("ERROR: Malloc failed! Out of heap memory.\n");
  zones_all_off();  // Safety: turn off zones before panic
  panic("Heap allocation failed");
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  printf("ERROR: Stack overflow in task: %s\n", pcTaskName);
  zones_all_off();  // Safety: turn off zones before panic
  panic("Stack overflow");
}

// Tick hook - called every tick (1ms)
void vApplicationTickHook(void) {
  fault_tolerance_tick_update();
}

// Idle hook - called when no tasks are running
void vApplicationIdleHook(void) {
  fault_tolerance_idle_check();
}

// FreeRTOS task to blink LED
void LedTask(void *pvParameters) {
  bool status = true;
  while (true) {
    task_heartbeat(TASK_ID_LED);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, status);
    vTaskDelay(pdMS_TO_TICKS(1000));
    status = !status;
  }
}

// Sensor task - reads DHT11 every 3 seconds
void SensorTask(void *pvParameters) {
  dht_init();

  // Wait a bit for sensor to stabilize
  vTaskDelay(pdMS_TO_TICKS(2000));

  while (true) {
    task_heartbeat(TASK_ID_SENSOR);
    if (!dht_read()) {
      printf("Sensor: Read failed\n");
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

// Network task - initializes WiFi, mDNS, NTP, then starts httpd
void NetworkTask(void *pvParameters) {
  task_heartbeat(TASK_ID_NETWORK);
  if (!network_init()) {
    printf("ERROR: Network initialization failed!\n");
  }

  // TODO: Boot stats save disabled - flash_safe_execute deadlocks with FreeRTOS SMP
  // fault_tolerance_save_boot_stats();

  // Wait 60 seconds for system to stabilize before enabling watchdog
  printf("Waiting 60s for system to stabilize before enabling watchdog...\n");
  for (int i = 0; i < 60; i++) {
    task_heartbeat(TASK_ID_NETWORK);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  // Enable watchdog now that startup is complete
  fault_tolerance_enable_watchdog();

  // Start HTTP server (contains its own heartbeat calls)
  httpd_task(pvParameters);
}

int main() {
  stdio_init_all();

  // Wait for USB serial to connect (so we can see boot messages)
  sleep_ms(2000);

  printf("\n=== SprinklerMaster Starting ===\n");

  // Initialize multi-core flash safety (must be before FreeRTOS tasks)
  if (flash_safe_execute_core_init()) {
    printf("Warning: Multi-core flash init returned non-zero\n");
  }

  // Initialize configuration from flash storage
  config_init();

  // Initialize zone GPIO pins
  zones_init();

  // Initialize fault tolerance (watchdog, heartbeat tracking)
  fault_tolerance_init();

  // Initialize LCD
  lcd_init();
  lcd_display_init();

  printf("Creating FreeRTOS tasks...\n");

  // Create tasks
  xTaskCreate(NetworkTask, "Network", 2048, NULL, 1, NULL);
  xTaskCreate(SensorTask, "Sensor", 512, NULL, 0, NULL);
  xTaskCreate(LedTask, "LED", 256, NULL, 0, NULL);

  // Initialize scheduler task (handles automatic zone triggering)
  scheduler_init();

  printf("Starting FreeRTOS scheduler...\n");
  vTaskStartScheduler();

  // Should never reach here
  while (true) {
    tight_loop_contents();
  }
}
