#include "nvs_manager.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>

void count_reboots(void) {
  nvs_handle_t my_handle;
  esp_err_t err;

  // Namespace Storage im lese-/schreibmodus öffnen
  err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK) {
    ESP_LOGE("NVS", "Fehler beim Öffnen des NVS-Handles!");
    return;
  }
}
