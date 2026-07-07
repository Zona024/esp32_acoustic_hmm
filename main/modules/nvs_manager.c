#include "nvs_manager.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/idf_additions.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sensor_manager.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WIFI_HISTORY 3

static const char *TAG2 = "IDLE_COUNT";

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
    ESP_LOGE("NVS", "Error opening NVS storage");
    return;
  }

  uint32_t restart_counter = 0;
  err = nvs_get_u32(my_handle, "restart_count", &restart_counter);

  // return instead of break in Fatal cases so the ESP doesnt shut down
  switch (err) {
  case ESP_OK:
    ESP_LOGI("NVS", "Reboot counter successfully loaded!");
    break;

  case ESP_ERR_NVS_NOT_FOUND:
    ESP_LOGW("NVS", "No reboot counter found, setting one at 0.");
    restart_counter = 0; // Zustand sicherstellen
    break;

  case ESP_ERR_NVS_NOT_INITIALIZED:
    ESP_LOGE("NVS", "Fatal error, NVS storage not initialized!");
    nvs_close(my_handle);
    return; // Funktion sofort abbrechen, um Folgeschäden zu verhindern

  default:
    // Übersetzt alle anderen exotischen Fehlercodes (wie PAGE_FULL) in lesbaren
    // Text
    ESP_LOGE("NVS", "Unexpected NVS Error: %s", esp_err_to_name(err));
    nvs_close(my_handle);
    return;
  }
  // Wert erhöhen und in der Konsole ausgeben
  restart_counter++;
  ESP_LOGI("NVS", "the ESP restarted %" PRIu32 "times.", restart_counter);

  // 3. Schreiben: Den neuen Wert in den Speicher legen
  err = nvs_set_u32(my_handle, "restart_count", restart_counter);
  if (err == ESP_OK) {
    // 4. Commit: Physisch in den Flash brennen
    nvs_commit(my_handle);
  }

  // 5. Schließen: Speicher wieder freigeben
  nvs_close(my_handle);
}

// Looks up which Core enters the IdleTask and counts up each time
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

// Function shows RAM usage and CPU usage per Core for monitoring efficiency and
// also memory leaks
void idle_monitor_task(void *pvParameters) {
  // Diese Variablen speichern die Zählerstände des vorherigen Durchlaufs (vor 5
  // Sekunden)
  uint32_t prev_core_0 = 0;
  uint32_t prev_core_1 = 0;

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 1. Live-Snapshot der RAM-Werte und globalen Idle-Zähler abrufen
    uint32_t free_ram = esp_get_free_heap_size();
    uint32_t min_ever_ram = esp_get_minimum_free_heap_size();
    uint32_t current_core_0 = idle_counters[0];
    uint32_t current_core_1 = idle_counters[1];

    // 2. Berechnung der Deltas (Zyklen im aktuellen 5-Sekunden-Intervall)
    uint32_t count_core_0_set = current_core_0 - prev_core_0;
    uint32_t count_core_1_set = current_core_1 - prev_core_1;

    // Historie für den nächsten Zyklus aktualisieren
    prev_core_0 = current_core_0;
    prev_core_1 = current_core_1;

    // 3. Thread-sichere Ausgabe über das tabellarische Layout
    if (xSemaphoreTake(terminal_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      ESP_LOGI(TAG2,
               "\n--------------------------------------------------\n"
               "| System-Statistik   | Core 0     | Core 1       |\n"
               "--------------------------------------------------\n"
               "| Letzte 5s (Loops)  | %-10" PRIu32 " | %-10" PRIu32 "   |\n"
               "| Gesamt    (Loops)  | %-10" PRIu32 " | %-10" PRIu32 "   |\n"
               "--------------------------------------------------\n"
               "| Sensor-Latenz      | %-7" PRIu32 " ms |  -           |\n"
               "| HMM-Rechenzeit     |  -         | %-7" PRIu32 "ms    |\n"
               "--------------------------------------------------\n"
               "| RAM: Free: %-8" PRIu32 "|     Min ever: %-10" PRIu32 "  |\n"
               "--------------------------------------------------",
               count_core_0_set, count_core_1_set, current_core_0,
               current_core_1, last_sensor_duration_ms, last_hmm_duration_ms,
               free_ram, min_ever_ram);

      last_hmm_duration_ms = 0;
      last_sensor_duration_ms = 0;

      xSemaphoreGive(terminal_mutex);
    }
  }
}

// Wiederverwendbare funktion um input übers terminal abzufragen
void terminal_input_task(const char *prompt_text, char *output_buffer,
                         size_t max_len, bool is_password) {
  int rx_count = 0;

  // 1. Block every task that wants continious Terminal-Output (must been set
  // also on the task that want to output into the terminal)
  xSemaphoreTake(terminal_mutex, portMAX_DELAY);

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

          // 2. UART-Mutex  wieder freigeben
          xSemaphoreGive(terminal_mutex);

          // Rückkehr zum Aufrufer
          return;
        }
      }
      // Backspace-Logik: ASCII 0x08 (\b) oder 0x7F (DEL)
      else if (c == '\b' || c == 0x7F) {
        if (rx_count > 0) {
          rx_count--;
          // Terminal-Ausgabe korrigieren: Cursor zurück, löschen, wieder zurück
          printf("\b \b");
          fflush(stdout);
        }
      }
      // Nur druckbare Standard-Zeichen zulassen (verhindert Escape-Sequenzen)
      else if (c >= 32 && c <= 126 && rx_count < max_len - 1) {
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
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

static bool get_saved_wifi_password(nvs_handle_t handle,
                                    const char *target_ssid,
                                    char *out_password) {
  char key_ssid[16];
  char key_pass[16];

  for (int i = 0; i < MAX_WIFI_HISTORY; i++) {
    snprintf(key_ssid, sizeof(key_ssid), "w_ssid_%d", i);
    snprintf(key_pass, sizeof(key_pass), "w_pass_%d", i);

    char saved_ssid[33] = {0};
    size_t len = sizeof(saved_ssid);

    // Prüfen ob der SSID-Schlüssel existiert und auslesen
    if (nvs_get_str(handle, key_ssid, saved_ssid, &len) == ESP_OK) {
      if (strcmp(saved_ssid, target_ssid) == 0) {
        // SSID stimmt überein, zugehöriges Passwort laden
        size_t pass_len = 64;
        if (nvs_get_str(handle, key_pass, out_password, &pass_len) == ESP_OK) {
          return true;
        }
      }
    }
  }
  return false;
}

// Überschreibt den ältesten Eintrag (Head) und rotiert den Zeiger
static void save_wifi_credentials(nvs_handle_t handle, const char *ssid,
                                  const char *password) {
  uint8_t head = 0;
  // Aktuellen Head-Index laden (Standardwert 0, falls der Key noch nicht
  // existiert)
  nvs_get_u8(handle, "w_head", &head);

  char key_ssid[16];
  char key_pass[16];
  snprintf(key_ssid, sizeof(key_ssid), "w_ssid_%d", head);
  snprintf(key_pass, sizeof(key_pass), "w_pass_%d", head);

  // Daten auf den aktuellen Slot schreiben
  nvs_set_str(handle, key_ssid, ssid);
  nvs_set_str(handle, key_pass, password);

  // Head rotieren (0 -> 1 -> 2 -> 0) und im NVS aktualisieren
  head = (head + 1) % MAX_WIFI_HISTORY;
  nvs_set_u8(handle, "w_head", head);

  nvs_commit(handle);
  ESP_LOGI("WLAN", "Neues Netzwerk in NVS-Slot gespeichert.");
}

void setup_wlan_interactive(void) {

  printf("\n>>> Start WLAN-Setup? (y/n): ");

  char response[4];
  terminal_input_task("Input: ", response, sizeof(response), false);

  // 2. If not y than leave function
  if (response[0] != 'y' && response[0] != 'Y') {
    ESP_LOGI("WLAN", "WLAN-Setup skipped.");
    return;
  }
  // Network Interface -> needed for LwIP
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);

  // Open Wifi hardware with standard configs
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  wifi_scan_config_t scan_config = {
      .ssid = 0, .bssid = 0, .channel = 0, .show_hidden = false};

  ESP_LOGI("WLAN", "Starting WLAN-Scan...");

  // Blocking call: CPU waits until Scan has ended
  ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

  // 3. Get amount of found APs
  uint16_t ap_count = 0;
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

  if (ap_count == 0) {
    ESP_LOGW("WLAN",
             "Keine Access Points gefunden. System-Neustart empfohlen.");
    return;
  }

  // 4. Heap storage for AP list results
  wifi_ap_record_t *ap_records =
      (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
  if (ap_records == NULL) {
    ESP_LOGE("WLAN", "Nicht genug RAM für die AP-Liste.");
    return;
  }

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

  // 5. Formating list and output (Mutex for Terminal  print Acess)
  if (xSemaphoreTake(terminal_mutex, portMAX_DELAY) == pdTRUE) {
    printf("\n========================================\n");
    printf("           FOUND NETWORKS          \n");
    printf("========================================\n");
    for (int i = 0; i < ap_count; i++) {
      printf("[%2d] %-20s (%d dBm)\n", i + 1, ap_records[i].ssid,
             ap_records[i].rssi);
    }
    printf("========================================\n");
    xSemaphoreGive(terminal_mutex);
  }

  // 6.User choice of AP
  int selected_idx = -1;
  char input_buffer[10];

  while (selected_idx < 1 || selected_idx > ap_count) {
    terminal_input_task("---> Choose Access Point Number: ", input_buffer,
                        sizeof(input_buffer), false);
    selected_idx = atoi(input_buffer);

    if (selected_idx < 1 || selected_idx > ap_count) {
      ESP_LOGW("WLAN",
               "Ungültige Eingabe. Bitte eine Zahl zwischen 1 und %d wählen.",
               ap_count);
    }
  }

  char selected_ssid[33] = {0};
  strncpy(selected_ssid, (char *)ap_records[selected_idx - 1].ssid, 32);
  free(ap_records);

  ESP_LOGI("WLAN", "Choosen Network: %s", selected_ssid);

  char password_buffer[64] = {0};
  bool pass_found = false;
  nvs_handle_t my_handle;
  bool nvs_opened = (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK);

  // Check in case the password is already in NVS
  if (nvs_opened) {
    pass_found =
        get_saved_wifi_password(my_handle, selected_ssid, password_buffer);
  }

  if (pass_found) {
    ESP_LOGI("WLAN", "Passwort erfolgreich aus der NVS-Historie geladen.");
  } else {
    terminal_input_task("---> Passwort eingeben: ", password_buffer,
                        sizeof(password_buffer), true);

    if (nvs_opened) {
      save_wifi_credentials(my_handle, selected_ssid, password_buffer);
    }
  }

  if (nvs_opened) {
    nvs_close(my_handle);
  }

  // 7. Configuring and Connecting Wifi
  wifi_config_t wifi_config = {0};
  strncpy((char *)wifi_config.sta.ssid, selected_ssid, 32);
  strncpy((char *)wifi_config.sta.password, password_buffer, 64);
  // WPA2 Minimum Security erzwingen
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_connect());

  ESP_LOGI("WLAN", "Connecting to %s...", selected_ssid);
}

esp_mqtt_client_handle_t start_mqtt_client(void) {
  char uri_buffer[64];
  terminal_input_task("Type MQTT broker address uri: ", uri_buffer,
                      sizeof(uri_buffer), false);

  const esp_mqtt_client_config_t mqtt_config = {
      .broker.address.uri = uri_buffer,
      .credentials.client_id = "esp32_acoustic_node",
      .session.disable_clean_session = false};

  // Creates MQTT client handle based on the configuration. Returns:
  // mqtt_client_handle if successfully created, NULL on error
  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_config);
  esp_mqtt_client_start(client);
  return client;
}
