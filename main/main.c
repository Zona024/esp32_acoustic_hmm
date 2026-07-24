#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "nvs_manager.h"
#include "sensor_manager.h"
#include <math.h>
#include <stdint.h>

// Define a "TAG" for logs to know where they came from
static const char *TAG = "MAIN_APP";

SemaphoreHandle_t terminal_mutex = NULL;
StaticSemaphore_t semaphore_buffer;
EventGroupHandle_t global_system_event_group = NULL;
SemaphoreHandle_t ringbuffer_sync_semaphore;

// volatile here so the compiler doesnt notice dummy data is useless
volatile uint32_t last_sensor_duration_ms = 0;
volatile uint32_t last_hmm_duration_ms = 0;
volatile uint32_t idle_counters[2] = {0, 0};

void app_main(void) {
  terminal_mutex = xSemaphoreCreateMutex();
  ringbuffer_sync_semaphore = xSemaphoreCreateBinaryStatic(&semaphore_buffer);
  if (ringbuffer_sync_semaphore != NULL) {
    xSemaphoreGive(ringbuffer_sync_semaphore);
  }
  global_system_event_group = xEventGroupCreate();
  assert(global_system_event_group);

  bool is_nvs_available = init_nvs_memory();
  if (is_nvs_available) { // Here comes all the code that needs NVS particular
    count_reboots();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &system_ip_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &system_ip_handler, NULL));
    setup_wlan_interactive();
  } else {
    ESP_LOGW(TAG, "Non-Volatile Storage not accesible!");
  }
  init_ringbuffer();

  idle_counters[0] = 0; 
  idle_counters[1] = 0;

  ESP_LOGI(TAG, "Starting the main execution loop...");
  xTaskCreate(idle_monitor_task, "IdleMonitor", 2048, NULL, 1, NULL);
  // xTaskCreatePinnedToCore(dummy_load_task, "DummyTask", 2048, NULL, 1, NULL,
  // 1);
  xTaskCreatePinnedToCore(dummy_buffer_load_task, "SensorTask", 4096, NULL, 5,
                          NULL, 0);
  xTaskCreatePinnedToCore(dummy_hmm_task, "HMMTask", 4096, NULL, 5, NULL, 1);

  ESP_LOGI(TAG, "All tasks created!");
}
