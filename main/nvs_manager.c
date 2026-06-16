#include "nvs_manager.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "portmacro.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *TAG2 = "IDLE_COUNT";
static const char *TAG_INPUT = "INPUT";

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

void dummy_load_task(void *pvParameters) {
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

void idle_monitor_task(void *pvParameters) {
  // These variables remember the count from 5 seconds ago.
  // They are initialized to 0 only once when the task starts.
  uint32_t prev_core_0 = 0;
  uint32_t prev_core_1 = 0;

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 1. Take a snapshot of the CURRENT global counters
    uint32_t current_core_0 = idle_counters[0];
    uint32_t current_core_1 = idle_counters[1];

    // 2. Calculate the delta (Current - Previous)
    uint32_t count_core_0_set = current_core_0 - prev_core_0;
    uint32_t count_core_1_set = current_core_1 - prev_core_1;

    // 3. Update the history variables for the next 5-second cycle
    prev_core_0 = current_core_0;
    prev_core_1 = current_core_1;

    // 4. Print the results (Replaces the is_typing check)
    // Attempt to take the mutex. If the user is currently typing,
    // this fails after 10ms, skipping the print but maintaining the 5s
    // calculation cycle.
    if (xSemaphoreTake(boot_semaphore, pdMS_TO_TICKS(10)) == pdTRUE) {

      ESP_LOGI(TAG2,
               "\n"
               "--------------------------------------------------\n"
               "| Statistik          | Core 0     | Core 1       |\n"
               "--------------------------------------------------\n"
               "| Letzte 5s (Loops)  | %-10" PRIu32 " | %-10" PRIu32 " |\n"
               "| Gesamt    (Loops)  | %-10" PRIu32 " | %-10" PRIu32 " |\n"
               "--------------------------------------------------",
               count_core_0_set, count_core_1_set, current_core_0,
               current_core_1);

      // Immediately release the mutex after printing
      xSemaphoreGive(boot_semaphore);
    }
  }
}

// Die Funktion blockiert den Aufrufer synchron, nutzt aber deinen gewählten
// Namen
void terminal_input_task(const char *prompt_text, char *output_buffer,
                         size_t max_len, bool is_password) {
  int rx_count = 0;

  // 1. Deinen UART-Mutex (boot_semaphore) sperren
  xSemaphoreTake(boot_semaphore, portMAX_DELAY);

  // Prompt ausgeben
  printf("\n%s", prompt_text);
  fflush(stdout);

  // Endlosschleife blockiert den aktuellen Ablauf, bis Enter gedrückt wird
  while (1) {
    int c = getchar();

    if (c != EOF) {
      if (c == '\n' || c == '\r') {
        if (rx_count > 0) {
          // String terminieren
          output_buffer[rx_count] = '\0';
          printf("\n");

          // 2. UART-Mutex (boot_semaphore) wieder freigeben
          xSemaphoreGive(boot_semaphore);

          // Rückkehr zum Aufrufer
          return;
        }
      } else if (rx_count < max_len - 1) {
        output_buffer[rx_count] = (char)c;
        rx_count++;

        if (is_password) {
          putchar('*');
        } else {
          putchar(c);
        }
        fflush(stdout);
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}
