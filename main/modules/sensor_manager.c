#include "sensor_manager.h"
#include "esp_log.h"
#include "esp_rom_sys.h" // Für aktives CPU-Delay
#include "esp_timer.h"   // Für hochpräzise Mikrosekunden-Messungen
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/ringbuf.h"
#include "nvs_manager.h"
#include "portmacro.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SIMULATED_PROCESSING_TIME_US 5000

static uint8_t audio_buffer_storage[AUDIO_BUFFER_SIZE];
static StaticRingbuffer_t audio_buffer_ring;
RingbufHandle_t audio_buffer_handle = NULL;

// initilizing static ring buffer to be used for Sensor byte stream data to be
// handled by the HMM classifier
void init_ringbuffer(void) {
  audio_buffer_handle =
      xRingbufferCreateStatic(AUDIO_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF,
                              audio_buffer_storage, &audio_buffer_ring);
}

void dummy_buffer_load_task(void *pvParameters) {
  const int SAMPLE_RATE = 16000;
  const int CHUNK_SAMPLES = 512;
  int16_t audio_chunk[CHUNK_SAMPLES];
  float phase = 0.0f;
  bool is_high_freq = false;

  while (1) {
    xSemaphoreTake(ringbuffer_sync_semaphore, portMAX_DELAY);

    int64_t start_time = esp_timer_get_time();

    float current_freq = is_high_freq ? 880.0f : 440.0f;
    is_high_freq = !is_high_freq;

    for (int chunk_idx = 0; chunk_idx < 4; chunk_idx++) {
      for (int i = 0; i < CHUNK_SAMPLES; i++) {
        audio_chunk[i] = (int16_t)(sinf(phase) * 10000.0f);
        phase += (2.0f * M_PI * current_freq) / SAMPLE_RATE;
        if (phase > 2.0f * M_PI)
          phase -= 2.0f * M_PI;
      }
      xRingbufferSend(audio_buffer_handle, audio_chunk, sizeof(audio_chunk),
                      pdMS_TO_TICKS(100));
    }

    last_sensor_duration_ms +=
        (uint32_t)((esp_timer_get_time() - start_time) / 1000);
  }
}

void dummy_hmm_task(void *pvParameters) {
  size_t item_size;
  int total_bytes_processed = 0;
  int64_t sequence_start_time = 0;
  volatile int32_t dummy_feature = 0;

  while (1) {
    int16_t *received_data = (int16_t *)xRingbufferReceive(
        audio_buffer_handle, &item_size, portMAX_DELAY);

    if (received_data != NULL) {
      if (total_bytes_processed == 0) {
        sequence_start_time = esp_timer_get_time();
      }

      int num_samples = item_size / sizeof(int16_t);
      dummy_feature = 0;
      for (int i = 0; i < num_samples; i++) {
        dummy_feature += received_data[i];
      }

      esp_rom_delay_us(SIMULATED_PROCESSING_TIME_US);

      total_bytes_processed += item_size;
      vRingbufferReturnItem(audio_buffer_handle, (void *)received_data);

      if (total_bytes_processed >= 4096) {
        last_hmm_duration_ms +=
            (uint32_t)((esp_timer_get_time() - sequence_start_time) / 1000);

        total_bytes_processed = 0;
        xSemaphoreGive(ringbuffer_sync_semaphore);
      }
    }
  }
}
