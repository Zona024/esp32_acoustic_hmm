#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

// Define a "TAG" for our logs so we know where they came from
static const char *TAG = "MAIN_APP";

void app_main(void) {
  // Log an informational message (LOGI = Log Info)
  ESP_LOGI(TAG, "The ESP32 has successfully booted.");
  ESP_LOGI(TAG, "Starting the main execution loop...");

  int counter = 0;

  while (1) {
    // Print the current count
    ESP_LOGI(TAG, "System running. Heartbeat count: %d", counter);
    counter++;

    // Give the CPU back to FreeRTOS for 1000 milliseconds
    // This is mandatory to prevent watchdog resets
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
