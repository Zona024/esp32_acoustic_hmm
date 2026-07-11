#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"
#include <stdbool.h>
#include <stdint.h>

#define CONFIDENCE_THRESHOLD 5
#define SYS_STATUS_WIFI_CONNECTED_BIT (1 << 0)
#define SYS_STATUS_MQTT_CONNECTED_BIT (1 << 1)

extern EventGroupHandle_t global_system_event_group;
extern volatile uint32_t idle_counters[2];
extern volatile bool is_typing;
extern SemaphoreHandle_t terminal_mutex;
extern volatile bool mqtt_network_ready;
extern esp_mqtt_client_handle_t app_mqtt_client;

void system_ip_handler(void *arg, esp_event_base_t base, int32_t id,
                       void *data);
bool init_nvs_memory(void);
void count_reboots(void);
void dummy_load_task(void *pvParameters);
void idle_monitor_task(void *pvParameters);
void terminal_input_task(const char *prompt_text, char *output_buffer,
                         size_t max_len, bool is_password);
void setup_wlan_interactive(void);
esp_mqtt_client_handle_t start_mqtt_client(void);

#endif // NVS_MANAGER_H
