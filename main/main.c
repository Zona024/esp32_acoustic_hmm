#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "nvs_manager.h"
#include "portmacro.h"
#include <stdint.h>
#include <stdio.h>

// Define a "TAG" for our logs so we know where they came from
static const char *TAG = "MAIN_APP";
static const char *TAG2 = "IDLE_COUNT";

volatile uint32_t idle_counters[2] = {0, 0};

void vApplicationIdleHook(void) {
  // which core is currently running this hook?
  int core_id = xPortGetCoreID();

  // Increment counter for this specific core!
  idle_counters[core_id]++;
}

void idle_monitor_task(void *pvParameters) {
  // These variables remember the count from 5 seconds ago.
  // They are initialized to 0 only once when the task starts.
  uint32_t prev_core_0 = 0;
  uint32_t prev_core_1 = 0;

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 1. Take a snapshot of the CURRENT global counters
    uint32_t current_core_0 = idle_counters[0];
    uint32_t current_core_1 = idle_counters[1];

    // 2. Calculate the delta (Current - Previous)
    uint32_t count_core_0_set = current_core_0 - prev_core_0;
    uint32_t count_core_1_set = current_core_1 - prev_core_1;

    // 3. Update the history variables for the next 5-second cycle
    prev_core_0 = current_core_0;
    prev_core_1 = current_core_1;

    // 4. Print the results
    ESP_LOGI(TAG2,
             "\n"
             "--------------------------------------------------\n"
             "| Statistik          | Core 0     | Core 1       |\n"
             "--------------------------------------------------\n"
             "| Letzte 5s (Loops)  | %-10lu | %-10lu |\n"
             "| Gesamt    (Loops)  | %-10lu | %-10lu |\n"
             "--------------------------------------------------",
             count_core_0_set, count_core_1_set, current_core_0,
             current_core_1);
  }
}

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

  xTaskCreate(idle_monitor_task, "IdleMonitor", 2048, NULL, 1, NULL);
  xTaskCreate(dummy_load_task, "DummyTask", 2048, NULL, 1, NULL);

  ESP_LOGI(TAG, "All tasks created!");
}
