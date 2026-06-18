#include "sensor_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
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
