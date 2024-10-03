#include <Arduino.h>

#include "AudioStreamer.h"
#include "INMP441AudioSource.h"
#include "RTSPServer.h"

#define NUM_WIFI_CHANNELS 11

#define uS_TO_S_FACTOR \
  1000000ULL                  // Conversion factor for microseconds to seconds
#define MS_TO_S_FACTOR 1000L  // Conversion factor for milliseconds to seconds
#define TIME_TO_SLEEP_S 30    // Time ESP32 will go to sleep (in seconds)
#define WAKE_UP_TIME_S 60     // Time ESP32 will stay awake (in seconds)

const char* ssid = "EaveSdroP32";
const char* password = "3avesdr0p";

const int port = 554;

INMP441AudioSource micSource = INMP441AudioSource();
AudioStreamer micStreamer = AudioStreamer(&micSource);

RTSPServer rtsp(&micStreamer, port);

// WARNING: This function is called from a separate FreeRTOS task (thread)!
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      log_i("Client connected");
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      log_i("Client disconnected");
      break;
    default:
      break;
  }
}

void startAP() {
  // scan surrounding networks and select the channel with
  // the lowest RSSI
  log_i("Scanning surrounding networks...");
  int32_t max_rssis[NUM_WIFI_CHANNELS] = {INT32_MIN};
  int16_t num_networks = WiFi.scanNetworks(false, true);
  for (int16_t i = 0; i < num_networks; i++) {
    log_i("Checking %s on channel %d with RSSI of %d dBm", WiFi.SSID(i),
          WiFi.channel(i), WiFi.RSSI(i));
    int32_t channel = WiFi.channel(i) - 1;
    int32_t rssi = WiFi.RSSI(i);
    if (channel < NUM_WIFI_CHANNELS && rssi > max_rssis[channel]) {
      log_i("Setting channel %d max RSSI to %d dBm", channel + 1, rssi);
      max_rssis[channel] = rssi;
    }
  }

  int32_t best_channel = 1;
  int32_t min_rssi = INT32_MAX;
  for (int16_t i = 0; i < NUM_WIFI_CHANNELS; i++) {
    if (max_rssis[i] < min_rssi) {
      min_rssi = max_rssis[i];
      best_channel = i + 1;
    }
  }
  log_i("Starting AP on channel %d with an RSSI of %d dBm", best_channel,
        min_rssi);
  WiFi.softAP(ssid, password, best_channel, 0, 1);
  esp_wifi_set_ps(WIFI_PS_NONE);
  if (esp_wifi_set_max_tx_power(84) != ESP_OK) {
    log_e("Unable to set wifi max tx power");
  }
  WiFi.onEvent(WiFiEvent, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent(WiFiEvent, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

  log_i("[***] connect to rtsp://%s:%d [***]", WiFi.softAPIP().toString(),
        port);
}

void sleep() {
  log_i("Going to deep sleep for %d seconds...", TIME_TO_SLEEP_S);
  WiFi.softAPdisconnect(true);

  // Sleep configuration
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_S * uS_TO_S_FACTOR);
  Serial.flush();
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  startAP();
  rtsp.runAsync();
}

void loop() {
  static uint32_t next_sleep = millis() + (WAKE_UP_TIME_S * MS_TO_S_FACTOR);

  wifi_sta_list_t wifi_sta_list;
  esp_wifi_ap_get_sta_list(&wifi_sta_list);

  if (wifi_sta_list.num <= 0 && millis() > next_sleep) {
    sleep();
  } else {
    if (wifi_sta_list.num > 0) {
      next_sleep = millis() + (WAKE_UP_TIME_S * MS_TO_S_FACTOR);
    }
    delay(1000);
  }
}