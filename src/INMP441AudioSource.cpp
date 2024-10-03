#include "INMP441AudioSource.h"

const i2s_config_t i2s_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = 8000,
  .bits_per_sample = i2s_bits_per_sample_t(I2S_BITS_PER_SAMPLE_32BIT),
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
  .intr_alloc_flags = 0,
  .dma_buf_count = 4,
  .dma_buf_len = 128,
  .use_apll = false
};

const i2s_pin_config_t pin_config = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD,
};

void INMP441AudioSource::start() {
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
}

void INMP441AudioSource::stop() {
  i2s_driver_uninstall(I2S_PORT);
}

int INMP441AudioSource::readBytes(void* dest, int maxBytes) {
  int32_t samples32[maxBytes * (sizeof(int32_t) / sizeof(int16_t))];
  size_t bytes_read = 0;

  esp_err_t result = i2s_read(I2S_PORT, &samples32[0], maxBytes * 2, &bytes_read, 1000 / portTICK_PERIOD_MS);
  if (result == ESP_OK) {
    size_t num_samples = bytes_read / sizeof(int32_t);
    int16_t* samples16 = (int16_t*)dest;
    for (size_t i = 0; i < num_samples; i++) {
      int32_t temp = samples32[i] >> 11;
      samples16[i] = std::clamp<int16_t>(temp, INT16_MIN, INT16_MAX);
    }

    return num_samples * sizeof(int16_t);
  }

  return bytes_read;
}
