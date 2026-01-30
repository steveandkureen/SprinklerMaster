#include "FreeRTOS.h"
#include "lcd.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "src/dht22.h"
#include "src/network.h"
#include "task.h"
#include <pico/types.h>
#include <stdio.h>

// FreeRTOS hook functions
void vApplicationMallocFailedHook(void) {
  printf("ERROR: Malloc failed! Out of heap memory.\n");
  panic("Heap allocation failed");
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  printf("ERROR: Stack overflow in task: %s\n", pcTaskName);
  panic("Stack overflow");
}

// FreeRTOS task to turn on LED and print messages
void TurnOnLed(void *pvParameters) {
  // Turn on the Pico W LED
  bool status = true;

  while (true) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, status);
    // lcd_set_text(0, 0, "Running....");
    vTaskDelay(pdMS_TO_TICKS(500));
    status = !status;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, status);
    vTaskDelay(pdMS_TO_TICKS(200));
    status = !status;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, status);
    vTaskDelay(pdMS_TO_TICKS(1000));
    status = !status;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, status);
    vTaskDelay(pdMS_TO_TICKS(3000));
    status = !status;
  }
}

void WebServer(void *pvParameters) {
  if (!init_wifi(pvParameters)) {
    printf("Wifi init failed.");
  }

  run_server(pvParameters);
}

// Sensor task - reads DHT11 every 3 seconds
void SensorTask(void *pvParameters) {
  dht_init();

  // Wait a bit for sensor to stabilize
  vTaskDelay(pdMS_TO_TICKS(2000));

  while (true) {
    if (!dht_read()) {
      printf("Sensor: Read failed\n");
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

int main() {
  stdio_init_all();

  lcd_init(); // init the lcd
  lcd_set_text(0, 0, "Starting up....");
  //  Create the LED task
  //  xTaskCreate(TurnOnLed, "TurnOnLed", 256, NULL, 4, NULL);
  xTaskCreate(WebServer, "WebServer", 1024, NULL, 1, NULL);
  xTaskCreate(SensorTask, "SensorTask", 512, NULL, 0, NULL);

  // lcd_set_text(0, 0, "Tasks Created....");
  //  Start the FreeRTOS scheduler
  vTaskStartScheduler();

  // Should never reach here
  while (true) {
    tight_loop_contents();
  }
}
