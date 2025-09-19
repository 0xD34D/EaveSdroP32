#include <stdio.h>
#include <string.h>

#include "audio_stream.h"
#include "captive_dns.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "ws_server.h"

#define WIFI_SSID_PREFIX CONFIG_WIFI_AP_SSID
#define WIFI_PASSWORD CONFIG_WIFI_AP_PASSWORD
#define MIN_TX_POWER CONFIG_WIFI_AP_TX_POWER_MIN
#define MAX_TX_POWER CONFIG_WIFI_AP_TX_POWER_MAX

// Channel scan interval in milliseconds (e.g., every 2 minutes)
#define CHANNEL_SCAN_INTERVAL_MS (2 * 60 * 1000)

static const char *TAG = "EaveSdroP32";

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
      wifi_event_ap_staconnected_t *event =
          (wifi_event_ap_staconnected_t *)event_data;
      ESP_LOGI(TAG, "Station connected, MAC:" MACSTR ", AID=%d",
               MAC2STR(event->mac), event->aid);
      ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(80));
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
      wifi_event_ap_stadisconnected_t *event =
          (wifi_event_ap_stadisconnected_t *)event_data;
      ESP_LOGI(TAG, "Station disconnected, MAC:" MACSTR ", AID=%d",
               MAC2STR(event->mac), event->aid);
      ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(MIN_TX_POWER));
    }
  }
}

void wifi_init_softap() {
  ESP_LOGI(TAG, "Initializing WiFi AP mode");

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  // Register event handlers for AP mode
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &wifi_event_handler, NULL));

  char ssid[256];
#ifdef CONFIG_WIFI_AP_APPEND_MAC
  uint8_t mac[6];
  ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
  snprintf(ssid, sizeof(ssid), WIFI_SSID_PREFIX "-%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#else
  snprintf(ssid, sizeof(ssid), WIFI_SSID_PREFIX);
#endif

  wifi_config_t wifi_config = {
      .ap = {.ssid = "",
             .ssid_len = strlen(ssid),
             .channel = 1,
             .password = WIFI_PASSWORD,
             .max_connection = 1,
             .authmode = WIFI_AUTH_WPA2_PSK},
  };
  strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(MIN_TX_POWER));

  ESP_LOGI(TAG, "WiFi AP started. SSID: %s", ssid);
}

void init_spiffs(void) {
  ESP_LOGI(TAG, "Initializing SPIFFS");

  esp_vfs_spiffs_conf_t conf = {.base_path = "/spiffs",
                                .partition_label = "spiffs",
                                .max_files = 5,
                                .format_if_mount_failed = true};

  esp_err_t ret = esp_vfs_spiffs_register(&conf);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Failed to mount or format filesystem");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      ESP_LOGE(TAG, "Failed to find SPIFFS partition");
    } else {
      ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
    }
    return;
  }
}

static esp_err_t root_get_handler(httpd_req_t *req) {
  FILE *f = fopen("/spiffs/index.html", "r");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open index.html for reading");
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "text/html");

  char chunk[512];
  size_t bytes_read;
  while ((bytes_read = fread(chunk, 1, sizeof(chunk), f)) > 0) {
    if (httpd_resp_send_chunk(req, chunk, bytes_read) != ESP_OK) {
      fclose(f);
      ESP_LOGE(TAG, "File sending failed!");
      return ESP_FAIL;
    }
  }

  httpd_resp_send_chunk(req, NULL, 0);
  fclose(f);
  return ESP_OK;
}

static esp_err_t captive_portal_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t config_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");
  char buf[64];
  int sr = i2s_audio_get_sample_rate();
  snprintf(buf, sizeof(buf), "{\"sample_rate\":%d}", sr);
  httpd_resp_send(req, buf, strlen(buf));
  return ESP_OK;
}

static const int allowed_sample_rates[] = {8000,  16000, 22050,
                                           32000, 44100, 48000};

static bool is_allowed_rate(int r) {
  for (size_t i = 0;
       i < sizeof(allowed_sample_rates) / sizeof(allowed_sample_rates[0]);
       ++i) {
    if (allowed_sample_rates[i] == r) return true;
  }
  return false;
}

static esp_err_t config_post_handler(httpd_req_t *req) {
  // Read body
  char buf[64];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  // Simple parse for JSON like {"sample_rate":44100}
  int new_rate = 0;
  if (sscanf(buf, "{ \"sample_rate\" : %d }", &new_rate) != 1) {
    // try without spaces
    if (sscanf(buf, "{\"sample_rate\":%d}", &new_rate) != 1) {
      httpd_resp_set_status(req, "400 Bad Request");
      httpd_resp_send(req, "{\"error\":\"invalid_json\"}",
                      HTTPD_RESP_USE_STRLEN);
      return ESP_FAIL;
    }
  }

  if (!is_allowed_rate(new_rate)) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_send(req, "{\"error\":\"unsupported_rate\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
  }

  // If streaming, do not allow change
  if (ws_is_streaming()) {
    httpd_resp_set_status(req, "409 Conflict");
    httpd_resp_send(req, "{\"error\":\"streaming\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
  }

  esp_err_t err = i2s_audio_set_sample_rate(new_rate);
  if (err != ESP_OK) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_send(req, "{\"error\":\"reconfigure_failed\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
  }

  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

void start_http_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  httpd_handle_t server = NULL;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t root_uri = {
        .uri = "/", .method = HTTP_GET, .handler = root_get_handler};
    httpd_register_uri_handler(server, &root_uri);

    // Captive portal detection URIs
    httpd_uri_t gen204_uri = {.uri = "/generate_204",
                              .method = HTTP_GET,
                              .handler = captive_portal_handler};
    httpd_register_uri_handler(server, &gen204_uri);

    httpd_uri_t gen204_short_uri = {.uri = "/gen_204",
                                    .method = HTTP_GET,
                                    .handler = captive_portal_handler};
    httpd_register_uri_handler(server, &gen204_short_uri);

    httpd_uri_t hotspot_uri = {.uri = "/hotspot-detect.html",
                               .method = HTTP_GET,
                               .handler = captive_portal_handler};
    httpd_register_uri_handler(server, &hotspot_uri);

    httpd_uri_t ncsi_uri = {.uri = "/ncsi.txt",
                            .method = HTTP_GET,
                            .handler = captive_portal_handler};
    httpd_register_uri_handler(server, &ncsi_uri);

    httpd_uri_t config_uri = {
        .uri = "/config", .method = HTTP_GET, .handler = config_get_handler};
    httpd_register_uri_handler(server, &config_uri);
    // POST handler for changing sample rate
    httpd_uri_t config_post_uri = {
        .uri = "/config", .method = HTTP_POST, .handler = config_post_handler};
    httpd_register_uri_handler(server, &config_post_uri);

    start_websocket_server(server);
  }
}

uint8_t choose_best_channel() {
  wifi_scan_config_t scan_config = {0};
  uint16_t max_aps = 32;
  wifi_ap_record_t ap_records[max_aps];
  uint8_t channels[3] = {1, 6, 11};  // Non-overlapping channels
  int rssi_sum[3] = {0, 0, 0};

  esp_err_t err;
  err = esp_wifi_scan_start(&scan_config, true);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
    return 1;  // Default to channel 1 on scan failure
  }
  uint16_t ap_count = 0;
  err = esp_wifi_scan_get_ap_num(&ap_count);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(err));
    return 1;  // Default to channel 1 on failure
  }
  if (ap_count > max_aps) ap_count = max_aps;
  err = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(err));
    return 1;  // Default to channel 1 on failure
  }

  for (int i = 0; i < ap_count; ++i) {
    uint8_t ch = ap_records[i].primary;
    int rssi = ap_records[i].rssi;
    for (int j = 0; j < 3; ++j) {
      // Count APs on the channel and adjacent channels (overlap)
      if (ch == channels[j] || ch == channels[j] - 1 || ch == channels[j] + 1) {
        rssi_sum[j] += abs(rssi);  // Stronger signals contribute more
      }
    }
  }

  // Find channel with minimum RSSI sum
  int best_idx = 0;
  for (int j = 1; j < 3; ++j) {
    if (rssi_sum[j] < rssi_sum[best_idx]) {
      best_idx = j;
    }
  }
  return channels[best_idx];
}

void channel_hopper_task(void *pvParameter) {
  while (1) {
    // Check if any stations are connected
    wifi_sta_list_t sta_list;
    esp_err_t err = esp_wifi_ap_get_sta_list(&sta_list);
    if (err == ESP_OK && sta_list.num == 0) {
      // No clients connected, perform scan and switch channel
      ESP_LOGI(TAG, "No clients connected, scanning for best channel...");
      uint8_t curr_channel;
      wifi_second_chan_t second_channel;
      err = esp_wifi_get_channel(&curr_channel, &second_channel);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get current channel: %s",
                 esp_err_to_name(err));
        continue;
      }
      int best_channel = choose_best_channel();
      if (best_channel != curr_channel) {
        // Switch back to AP mode and set new channel
        err = esp_wifi_set_channel(best_channel, WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "Failed to set channel: %s", esp_err_to_name(err));
          continue;
        }
        ESP_LOGI(TAG, "Switched AP to channel %d", best_channel);
      } else {
        ESP_LOGI(TAG, "Current channel %d is still best", best_channel);
      }
    } else {
      ESP_LOGI(TAG, "Clients connected, skipping channel switch");
    }
    // Wait CHANNEL_SCAN_INTERVAL_MS before next check
    vTaskDelay(pdMS_TO_TICKS(CHANNEL_SCAN_INTERVAL_MS));
  }
}

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  init_spiffs();
  wifi_init_softap();
  xTaskCreate(captive_dns_task, "captive_dns", 2048, NULL, 5, NULL);
  i2s_audio_init();
  start_http_server();
  xTaskCreate(channel_hopper_task, "channel_hopper", 4096, NULL, 4, NULL);
}
