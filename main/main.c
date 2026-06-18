#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "nvs_manager.h"
#include "sensor_manager.h"
#include <stdint.h>

// Define a "TAG" for logs to know where they came from
static const char *TAG = "MAIN_APP";
SemaphoreHandle_t terminal_mutex = NULL;

volatile uint32_t idle_counters[2] = {0, 0};
char selection_buffer[10];
char password_buffer[64];

void app_main(void) {
  terminal_mutex = xSemaphoreCreateMutex();
  ESP_LOGI(TAG, "The ESP32 has successfully booted.");

  bool is_nvs_available = init_nvs_memory();
  if (is_nvs_available) { // Here comes all the code that needs NVS particular
    count_reboots();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    setup_wlan_interactive();
  } else {
    ESP_LOGW(TAG, "Non-Volatile Storage not accesible!");
  }

  // Ringbuffer initialisieren
  init_ringbuffer();

  ESP_LOGI(TAG, "Starting the main execution loop...");
  xTaskCreate(idle_monitor_task, "IdleMonitor", 2048, NULL, 1, NULL);
  xTaskCreatePinnedToCore(dummy_load_task, "DummyTask", 2048, NULL, 1, NULL, 1);

  ESP_LOGI(TAG, "All tasks created!");
}
