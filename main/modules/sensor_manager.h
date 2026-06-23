#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "freertos/idf_additions.h"
#include "nvs_manager.h"
#define AUDIO_BUFFER_SIZE 4096

extern SemaphoreHandle_t ringbuffer_sync_semaphore;
extern StaticSemaphore_t semaphore_buffer;

void init_ringbuffer(void);
void dummy_buffer_load_task(void *pvParameters);

#endif // SENSOR_MANAGER_H
