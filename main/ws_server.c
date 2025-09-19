// ws_server.c
#include "ws_server.h"

#include "audio_stream.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define AUDIO_BUFFER_SIZE 4096

static const char *TAG = "ws_server";
static httpd_handle_t ws_httpd = NULL;

// Use a file descriptor and task handle for safe cross-task communication
static int g_client_fd = -1;
static TaskHandle_t g_audio_task_handle = NULL;

static esp_err_t ws_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    ESP_LOGI(TAG, "Handshake done, client connected");
    g_client_fd = httpd_req_to_sockfd(req);
    if (g_audio_task_handle) {
      xTaskNotifyGive(g_audio_task_handle);
    }
    return ESP_OK;
  }
  // We are not expecting any other frames from the client.
  // If a frame is received, it might indicate a disconnection.
  // The sending task will handle the error when it tries to send.
  return ESP_OK;
}

bool ws_is_streaming(void) {
  return g_client_fd != -1;
}

static void audio_stream_task(void *arg) {
  int32_t *buffer = malloc(AUDIO_BUFFER_SIZE);
  if (!buffer) {
    ESP_LOGE(TAG, "Failed to allocate audio buffer");
    vTaskDelete(NULL);
  }

  ESP_LOGI(TAG, "Audio stream task started, waiting for client");

  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Client connected notification received, starting stream");

    while (g_client_fd != -1) {
      size_t bytes_read = i2s_audio_read((uint8_t *)buffer, AUDIO_BUFFER_SIZE);
      ESP_LOGD(TAG, "Read %zu bytes from audio stream", bytes_read);
      if (bytes_read > 0) {
        httpd_ws_frame_t frame = {.payload = (uint8_t *)buffer,
                                  .len = bytes_read,
                                  .type = HTTPD_WS_TYPE_BINARY,
                                  .final = true};

        esp_err_t ret =
            httpd_ws_send_frame_async(ws_httpd, g_client_fd, &frame);
        if (ret != ESP_OK) {
          ESP_LOGW(TAG, "Send failed, client disconnected? Error: %s",
                   esp_err_to_name(ret));
          g_client_fd = -1;
        }
      }
    }
    ESP_LOGI(TAG, "Stream stopped, waiting for new client");
  }
}

void start_websocket_server(httpd_handle_t server) {
  ws_httpd = server;

  httpd_uri_t ws_uri = {.uri = "/ws",
                        .method = HTTP_GET,
                        .handler = ws_handler,
                        .is_websocket = true};

  httpd_register_uri_handler(server, &ws_uri);
  xTaskCreate(audio_stream_task, "audio_stream_task", 2048, NULL, 5,
              &g_audio_task_handle);
}
