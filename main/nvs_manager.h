#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stdint.h>

extern volatile uint32_t idle_counters[2];
extern volatile bool is_typing;
extern SemaphoreHandle_t boot_semaphore;

bool init_nvs_memory(void);
void count_reboots(void);
void dummy_load_task(void *pvParameters);
void idle_monitor_task(void *pvParameters);
void terminal_input_task(void *pvParameters);

#endif // NVS_MANAGER_H
