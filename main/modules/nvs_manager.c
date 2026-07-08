#include "nvs_manager.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/idf_additions.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sensor_manager.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WIFI_HISTORY 3
// 0 = Connecting, 1 = Connected Successfully, 2 = Connection Failed
static volatile int wifi_status = 0;

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
  nvs_handle_t my_nvs_handle;
  esp_err_t err;

  // opens the name space "storage" to read and write the current reboot count
  // namespace here is more of an tag to find and group data within the 24KB NVS
  err = nvs_open("storage", NVS_READWRITE, &my_nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE("NVS", "Error opening NVS storage");
    return;
  }

  uint32_t restart_counter = 0;
  err = nvs_get_u32(my_nvs_handle, "restart_count", &restart_counter);

  // return instead of break in Fatal cases so the ESP doesn't shut down
  switch (err) {
  case ESP_OK:
    ESP_LOGI("NVS", "Reboot counter successfully loaded!");
    break;

  case ESP_ERR_NVS_NOT_FOUND:
    ESP_LOGW("NVS", "No reboot counter found, setting one at 0.");
    restart_counter = 0;
    break;

  case ESP_ERR_NVS_NOT_INITIALIZED:
    ESP_LOGE("NVS", "Fatal error, NVS storage not initialized!");
    nvs_close(my_nvs_handle);
    return;

  default:
    // For all other errors esp_err_to_name translates them human readable
    ESP_LOGE("NVS", "Unexpected NVS Error: %s", esp_err_to_name(err));
    nvs_close(my_nvs_handle);
    return;
  }
  restart_counter++;
  ESP_LOGI("NVS", "the ESP restarted %" PRIu32 "times.", restart_counter);

  // Write the new value into NVS
  err = nvs_set_u32(my_nvs_handle, "restart_count", restart_counter);
  if (err == ESP_OK) {
    nvs_commit(my_nvs_handle);
  }

  nvs_close(my_nvs_handle);
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
    volatile float x = 0.0f;
    for (int i = 0; i < 100000; i++) {
      x += 0.01f * 0.01f;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Function shows RAM usage and CPU usage per Core for monitoring efficiency and
// also memory leaks
void idle_monitor_task(void *pvParameters) {
  // Variables to save previous counts if idle ticks
  uint32_t prev_core_0 = 0;
  uint32_t prev_core_1 = 0;

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Get current RAM info and also Idle counters
    uint32_t free_ram = esp_get_free_heap_size();
    uint32_t min_ever_ram = esp_get_minimum_free_heap_size();
    uint32_t current_core_0 = idle_counters[0];
    uint32_t current_core_1 = idle_counters[1];

    // Calculate the delta from the prev counts with the current count to get
    // the actuall counts that have happened within the 5 seconds delay
    uint32_t count_core_0_set = current_core_0 - prev_core_0;
    uint32_t count_core_1_set = current_core_1 - prev_core_1;

    // Update previous counts
    prev_core_0 = current_core_0;
    prev_core_1 = current_core_1;

    // Thread save use of the Terminal with the Mutex
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

// Reusable function to get input through the terminal
void terminal_input_task(const char *prompt_text, char *output_buffer,
                         size_t max_len, bool is_password) {
  // variable...
  int rx_count = 0;
  xSemaphoreTake(
      terminal_mutex,
      portMAX_DELAY); // block terminal for other tasks that would
                      // been using the terminal during typing(must be managed
                      // on the other tasks to with same "terminal_mutex")
  printf("\n%s", prompt_text);
  fflush(stdout);
  // while loop is blocking until enter is pressed (vTaskDelay to give room for
  // watchdog and idleTask)
  while (1) {
    int c = getchar();

    if (c != EOF) { // if c == -1 "End of file" -> key was successfully pressed
      if (c == '\n' || c == '\r') {
        if (rx_count > 0) {
          output_buffer[rx_count] =
              '\0'; // String must be terminated by a null character 0x00 = \0
                    // needed for functions, so they know when the string ends
          printf("\n");
          xSemaphoreGive(terminal_mutex);
          return;
        }
      }
      // Backspace-Logic: ASCII 0x08 (\b) or 0x7F (DEL) cover most common
      // standards for Terminal backspace key
      else if (c == '\b' || c == 0x7F) {
        if (rx_count > 0) {
          rx_count--;
          // remove char and move cursor
          printf("\b \b"); // move cursor to the left and cause a blank space to
                           // show and than move back to the left so the
                           // "deleted" input gets actually overwritten
          fflush(stdout);
        }
      }
      // 32 - 126 restricts input to printable standard ASCII characters
      // rx_count < max_len -> buffer overflow protection (when written to the
      // buffer it should always be checked so now overflow occurs)
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
      vTaskDelay(pdMS_TO_TICKS(
          100)); // delay so this loops doesn't block during typing
    }
  }
}

static bool get_saved_wifi_password(nvs_handle_t handle,
                                    const char *target_ssid,
                                    char *out_password) {
  char key_ssid[16];
  char key_pass[16];

  for (int i = 0; i < MAX_WIFI_HISTORY; i++) {
    // If you try to write a 20-character string into a 16-byte buffer, snprintf
    // stops writing at byte 15, inserts \0 at byte 16, and drops the rest. No
    // crash, no memory corruption.
    snprintf(key_ssid, sizeof(key_ssid), "w_ssid_%d", i);
    snprintf(key_pass, sizeof(key_pass), "w_pass_%d", i);

    // According to the official IEEE 802.11 standard, a Wi-Fi SSID can be up to
    // 32 characters long. To safely store a maximum-length Wi-Fi name in C, 32
    // bytes + 1 byte for the null terminator (\0) is needed, which equals 33.
    // Prepares a clean, empty 33-byte buffer to hold the Wi-Fi name
    char saved_ssid[33] = {0};
    size_t len = sizeof(saved_ssid);

    // Looks up the ssid and saves it into the according buffer, compares it to
    // the intended SSID and returns true if password is saved and saves the
    // password into the given out_password pointer
    if (nvs_get_str(handle, key_ssid, saved_ssid, &len) == ESP_OK) {
      if (strcmp(saved_ssid, target_ssid) == 0) {
        size_t pass_len = 64;
        if (nvs_get_str(handle, key_pass, out_password, &pass_len) == ESP_OK) {
          return true;
        }
      }
    }
  }
  // if the loop exits it means all three saved slots where not aligned with the
  // choosen AP
  return false;
}

// Overwriting the old Entry to save the currently used one afterwards rotates
// the pointer of the head
static void save_wifi_credentials(nvs_handle_t handle, const char *ssid,
                                  const char *password) {
  // Current head pointer (Standard at 0)
  uint8_t head = 0;
  // This function forces it to be inside main after NVS is initialized
  // reads the current head value
  nvs_get_u8(handle, "w_head", &head);

  char key_ssid[16];
  char key_pass[16];
  // basically writes down where the head currently points
  snprintf(key_ssid, sizeof(key_ssid), "w_ssid_%d", head);
  snprintf(key_pass, sizeof(key_pass), "w_pass_%d", head);

  // write down the credentials into the "oldest" spot
  nvs_set_str(handle, key_ssid, ssid);
  nvs_set_str(handle, key_pass, password);

  // rotates the head from 0 -> 1 -> 2 -> 0
  head = (head + 1) % MAX_WIFI_HISTORY;
  nvs_set_u8(handle, "w_head", head);

  nvs_commit(handle);
  ESP_LOGI("WLAN", "New network saved in the NVS!");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    wifi_status = 1; // It worked!
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    // You can add your retry logic here if you want, but if it flat out fails:
    wifi_status = 2; // It failed!
  }
}

void setup_wlan_interactive(void) {

  printf("\n>>> Start WLAN-Setup? (y/n): ");

  char response[4];
  terminal_input_task("Input: ", response, sizeof(response), false);

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

  // Blocking call: CPU waits until Scan has ended -> doesn't block with false
  // but needs event to know when its finished
  ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

  // Gets the number of Access Points
  uint16_t ap_count = 0;
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

  if (ap_count == 0) {
    ESP_LOGW("WLAN", "No access points found! No connections available");
    return;
  }

  // Heap storage for the AP list(ap_count to know how many entries needed)
  wifi_ap_record_t *ap_records =
      (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
  if (ap_records == NULL) {
    ESP_LOGE("WLAN", "Nicht genug RAM für die AP-Liste.");
    return;
  }

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

  // Output of the List with all found APs with  respect to terminal_mutex
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

  int selected_idx = -1;
  char input_buffer[10];

  // Lets the User choose an access point
  while (selected_idx < 1 || selected_idx > ap_count) {
    terminal_input_task("---> Choose Access Point Number: ", input_buffer,
                        sizeof(input_buffer), false);
    selected_idx = atoi(input_buffer); // atoi = ASCII to Integer

    if (selected_idx < 1 || selected_idx > ap_count) {
      ESP_LOGW("WLAN", "Invalid input, please use a number between 1 and %d",
               ap_count);
    }
  }

  // buffer for the selected ssid, and copy the ssid value from the List
  char selected_ssid[33] = {0};
  strncpy(selected_ssid, (char *)ap_records[selected_idx - 1].ssid, 32);
  free(ap_records);

  ESP_LOGI("WLAN", "Choosen Network: %s", selected_ssid);

  // now that the AP is chosen, set up everything for the password
  // create handle and open nvs to read and write into the  NVS
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
    ESP_LOGI("WLAN", "Password successfully loaded from Non-Volatile Storage");
  } else {
    terminal_input_task("---> Enter password: ", password_buffer,
                        sizeof(password_buffer), true);
  }
  // close the NVS access
  if (nvs_opened) {
    nvs_close(my_handle);
  }

  // setting up Wifi configuration
  wifi_config_t wifi_config = {0};
  strncpy((char *)wifi_config.sta.ssid, selected_ssid, 32);
  strncpy((char *)wifi_config.sta.password, password_buffer, 64);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_status = 0; // Reset status to "Connecting"
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_connect());
  ESP_LOGI("WLAN", "Connecting to %s...", selected_ssid);
  while (wifi_status == 0) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  if (wifi_status == 1) {
    ESP_LOGI("WLAN", "Connection successful! Saving credentials...");
    if (nvs_opened && !pass_found) {
      save_wifi_credentials(my_handle, selected_ssid, password_buffer);
    }
  } else {
    ESP_LOGE("WLAN", "Connection failed. Credentials not saved.");
    esp_wifi_disconnect();
  }

  if (nvs_opened) {
    nvs_close(my_handle);
  }
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

  if (client == NULL) {
    ESP_LOGE("MQTT", "Failed to init MQTT!");
    return NULL;
  }
  esp_err_t esp_error_check = esp_mqtt_client_start(client);
  if (esp_error_check != ESP_OK) {
    ESP_LOGE("MQTT", "Failed to start the MQTT client: %s",
             esp_err_to_name(esp_error_check));
  }
  return client;
}
