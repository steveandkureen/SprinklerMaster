#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "FreeRTOS.h"
#include "task.h"

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
        printf("Hello, world!\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        status = !status;
    }
}

int main()
{
    stdio_init_all();

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // Create the LED task
    xTaskCreate(TurnOnLed, "TurnOnLed", 256, NULL, 1, NULL);

    // Start the FreeRTOS scheduler
    vTaskStartScheduler();

    // Should never reach here
    while (true) {
        tight_loop_contents();
    }
}
