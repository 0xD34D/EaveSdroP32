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
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(
      WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &wifi_event_handler, NULL));

  char ssid[256];
  // Get base MAC address
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

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
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
  snprintf(buf, sizeof(buf), "{\"sample_rate\":%d}",
           CONFIG_AUDIO_STREAM_SAMPLE_RATE);
  httpd_resp_send(req, buf, strlen(buf));
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

    start_websocket_server(server);
  }
}

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  init_spiffs();
  wifi_init_softap();
  xTaskCreate(captive_dns_task, "captive_dns", 2048, NULL, 5, NULL);
  i2s_audio_init();
  start_http_server();
}
