#include "nvs_manager.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
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
#include <stdlib.h>
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

void vApplicationIdleHook(void) {
  // which core is currently running this hook?
  int core_id = xPortGetCoreID();

  // Increment counter for this specific core!
  idle_counters[core_id]++;
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

    // 1. Take a snapshot of the CURRENT global counters (initialized in main.c
    // and used in vApplicationIdleHook())
    uint32_t current_core_0 = idle_counters[0];
    uint32_t current_core_1 = idle_counters[1];

    // 2. Calculate the delta (Current - Previous)
    uint32_t count_core_0_set = current_core_0 - prev_core_0;
    uint32_t count_core_1_set = current_core_1 - prev_core_1;

    // 3. Update the history variables for the next 5-second cycle
    prev_core_0 = current_core_0;
    prev_core_1 = current_core_1;

    // 4.Attempt to take the mutex. If the user is currently typing,
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

void terminal_input_task(const char *prompt_text, char *output_buffer,
                         size_t max_len, bool is_password) {
  int rx_count = 0;

  // 1. Block boot_semaphore
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

void setup_wlan_interactive(void) {
  // 1. Netzwerk-Interface anlegen (zwingend erforderlich für LwIP)
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);

  // 2. Wi-Fi Hardware mit Standardkonfiguration booten
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  // 3. Scan konfigurieren (0 = alle Kanäle, aktiver Scan)
  wifi_scan_config_t scan_config = {
      .ssid = 0, .bssid = 0, .channel = 0, .show_hidden = false};

  ESP_LOGI("WLAN", "Starte WLAN-Scan...");

  // Blockierender Aufruf: CPU wartet hier, bis der Hardware-Scan beendet ist
  ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

  // 4. Anzahl gefundener APs abfragen
  uint16_t ap_count = 0;
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

  if (ap_count == 0) {
    ESP_LOGW("WLAN",
             "Keine Access Points gefunden. System-Neustart empfohlen.");
    return;
  }

  // 5. Speicher für Ergebnisse im Heap allozieren
  wifi_ap_record_t *ap_records =
      (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
  if (ap_records == NULL) {
    ESP_LOGE("WLAN", "Nicht genug RAM für die AP-Liste.");
    return;
  }

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

  // 6. Liste formatiert ausgeben (Mutex schützt den UART-Block)
  if (xSemaphoreTake(boot_semaphore, portMAX_DELAY) == pdTRUE) {
    printf("\n========================================\n");
    printf("           GEFUNDENE NETZWERKE          \n");
    printf("========================================\n");
    for (int i = 0; i < ap_count; i++) {
      printf("[%2d] %-20s (%d dBm)\n", i + 1, ap_records[i].ssid,
             ap_records[i].rssi);
    }
    printf("========================================\n");
    xSemaphoreGive(boot_semaphore);
  }

  // 7. Nutzereingabe: AP-Nummer wählen (mit Validierungsschleife)
  int selected_idx = -1;
  char input_buffer[10];

  while (selected_idx < 1 || selected_idx > ap_count) {
    terminal_input_task("---> Wähle die Nummer des Netzwerks: ", input_buffer,
                        sizeof(input_buffer), false);
    selected_idx = atoi(input_buffer);

    if (selected_idx < 1 || selected_idx > ap_count) {
      ESP_LOGW("WLAN",
               "Ungültige Eingabe. Bitte eine Zahl zwischen 1 und %d wählen.",
               ap_count);
    }
  }

  // 8. Gewählte SSID extrahieren und Heap-Speicher sofort freigeben
  char selected_ssid[33] = {0};
  strncpy(selected_ssid, (char *)ap_records[selected_idx - 1].ssid, 32);
  free(ap_records);

  ESP_LOGI("WLAN", "Gewähltes Netzwerk: %s", selected_ssid);

  // 9. Nutzereingabe: Passwort abfragen (mit Sternchen)
  char password_buffer[64] = {0};
  terminal_input_task("---> Passwort eingeben: ", password_buffer,
                      sizeof(password_buffer), true);

  // 10. Konfiguration setzen und Verbindungsaufbau starten
  wifi_config_t wifi_config = {0};
  strncpy((char *)wifi_config.sta.ssid, selected_ssid, 32);
  strncpy((char *)wifi_config.sta.password, password_buffer, 64);
  // WPA2 Minimum Security erzwingen
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_connect());

  ESP_LOGI("WLAN", "Verbindung zu %s wird aufgebaut...", selected_ssid);
}
