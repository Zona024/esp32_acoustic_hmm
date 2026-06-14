#include "nvs_manager.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

bool init_nvs_memory(void) {
  esp_err_t init_err = nvs_flash_init();

  if (init_err != ESP_OK) {
    ESP_LOGE("NVS", "Initializing Non-Volatile Storage failed: %s",
             esp_err_to_name(init_err));
    ESP_LOGW("NVS", "Switch to Offline-Mode without persistent storage");
    return false;
  }
  ESP_LOGI("NVS", "Non-Volatile Storage initialized!");
  return true;
}
void count_reboots(void) {
  nvs_handle_t my_handle;
  esp_err_t err;

  // Namespace Storage im lese-/schreibmodus öffnen
  // "storage" dient hier als namespace also ein "büro im gebäude" ->
  // restart_count hingegen ist der key der als variablen namen in diesem
  // Namespace steht!!
  err = nvs_open("storage", NVS_READWRITE, &my_handle);
  if (err != ESP_OK) {
    ESP_LOGE("NVS", "Fehler beim Öffnen des NVS-Handles!");
    return;
  }

  uint32_t restart_counter = 0;
  err = nvs_get_u32(my_handle, "restart_count", &restart_counter);

  // return instead of break in Fatal cases so the ESP doesnt shut down
  switch (err) {
  case ESP_OK:
    ESP_LOGI("NVS", "Zähler erfolgreich geladen.");
    break;

  case ESP_ERR_NVS_NOT_FOUND:
    ESP_LOGW("NVS", "Key nicht gefunden. Initialisiere Zähler mit 0.");
    restart_counter = 0; // Zustand sicherstellen
    break;

  case ESP_ERR_NVS_NOT_INITIALIZED:
    ESP_LOGE("NVS", "Fataler Fehler: NVS-Treiber ist nicht initialisiert!");
    nvs_close(my_handle);
    return; // Funktion sofort abbrechen, um Folgeschäden zu verhindern

  default:
    // Übersetzt alle anderen exotischen Fehlercodes (wie PAGE_FULL) in lesbaren
    // Text
    ESP_LOGE("NVS", "Unerwarteter NVS-Fehler: %s", esp_err_to_name(err));
    nvs_close(my_handle);
    return;
  }
  // Wert erhöhen und in der Konsole ausgeben
  restart_counter++;
  ESP_LOGI("NVS", "Der ESP32 wurde %" PRIu32 "mal gestartet.", restart_counter);

  // 3. Schreiben: Den neuen Wert in den Speicher legen
  err = nvs_set_u32(my_handle, "restart_count", restart_counter);
  if (err == ESP_OK) {
    // 4. Commit: Physisch in den Flash brennen
    nvs_commit(my_handle);
  }

  // 5. Schließen: Speicher wieder freigeben
  nvs_close(my_handle);
}

void dummy_load_task(void) {
  while (1) {
    // Berechne etwas, das nicht wegoptimiert werden kann
    volatile float x = 0.0f;
    for (int i = 0; i < 100000; i++) {
      x += 0.01f * 0.01f;
    }
    // Gib den CPU-Kern für 10ms komplett frei
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
