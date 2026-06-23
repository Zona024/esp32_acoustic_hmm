#include "sensor_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/ringbuf.h"
#include "nvs_manager.h"
#include "portmacro.h"
#include <math.h>
#include <stdint.h>

// statischer speicher für Audiodaten
static uint8_t audio_buffer_storage[AUDIO_BUFFER_SIZE];
//
static StaticRingbuffer_t audio_buffer_ring;
RingbufHandle_t audio_buffer_handle = NULL;

// initilizing static ring buffer to be used for Sensor byte stream data to be
// handled by the HMM
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

    float current_freq = is_high_freq ? 880.0f : 440.0f;
    is_high_freq = !is_high_freq;
    ESP_LOGI("SENSOR", "Starte Audio-Aufnahme (Simulation: %.0f Hz)",
             current_freq);

    // Ring buffer mit 4 x 1024 Bytes = 4096 Bytes füllen
    for (int chunk_idx = 0; chunk_idx < 4; chunk_idx++) {
      for (int i = 0; i < CHUNK_SAMPLES; i++) {
        // Amplitude von 10000 (passt gut in int16_t)
        audio_chunk[i] = (int16_t)(sinf(phase) * 10000.0f);

        // Phase weiterschieben (2 * PI * f / fs)
        phase += (2.0f * M_PI * current_freq) / SAMPLE_RATE;
        if (phase > 2.0f * M_PI) {
          phase -= 2.0f * M_PI;
        }
      }
      // Chunk in den Puffer schieben (blockiert maximal 100 Ticks, falls voll)
      xRingbufferSend(audio_buffer_handle, audio_chunk, sizeof(audio_chunk),
                      pdMS_TO_TICKS(100));
    }

    ESP_LOGI("SENSOR", "Aufnahme beendet. Sensor geht in Standby.");
  }
}
