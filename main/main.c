#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_manager.h"
#include <stdio.h>

// Define a "TAG" for our logs so we know where they came from
static const char *TAG = "MAIN_APP";

void app_main(void) {
  // Log an informational message (LOGI = Log Info)
  ESP_LOGI(TAG, "The ESP32 has successfully booted.");

  bool is_nvs_available = init_nvs_memory();
  if (is_nvs_available) { // Here comes all the code that needs NVS particular
    count_reboots();
  } else {
    ESP_LOGW(TAG, "Non-Volatile Storage not accesible!");
  }

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
