#include "audio_stream.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/portmacro.h"
#include "sdkconfig.h"

#define SAMPLE_RATE CONFIG_AUDIO_STREAM_SAMPLE_RATE
#define I2S_BCLK_IO CONFIG_AUDIO_STREAM_I2S_BCLK_IO
#define I2S_WS_IO CONFIG_AUDIO_STREAM_I2S_WS_IO
#define I2S_DATA_IN_IO CONFIG_AUDIO_STREAM_I2S_DATA_IN_IO

static const char *TAG = "audio_stream";
static i2s_chan_handle_t rx_chan;

void i2s_audio_init() {
  ESP_LOGI(TAG, "Initializing I2S using i2s_std driver");

  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

  i2s_std_config_t std_cfg = {
      .clk_cfg =
          {
              .sample_rate_hz = SAMPLE_RATE,
              .clk_src = I2S_CLK_SRC_DEFAULT,
              .ext_clk_freq_hz = 0,
              .mclk_multiple = I2S_MCLK_MULTIPLE_256,
          },
      .slot_cfg =
          {
              .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
              .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
              .slot_mode = I2S_SLOT_MODE_MONO,
              .slot_mask = I2S_STD_SLOT_LEFT,
              .ws_width = I2S_SLOT_BIT_WIDTH_32BIT,
              .ws_pol = false,
              .bit_shift = true,
              .left_align = true,
              .big_endian = false,
              .bit_order_lsb = false,
          },
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = I2S_BCLK_IO,
              .ws = I2S_WS_IO,
              .dout = I2S_GPIO_UNUSED,
              .din = I2S_DATA_IN_IO,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
}

size_t i2s_audio_read(uint8_t *buffer, size_t max_len) {
  size_t bytes_read = 0;
  if (rx_chan) {
    esp_err_t ret =
        i2s_channel_read(rx_chan, buffer, max_len, &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "i2s read failed: %d", ret);
      return 0;
    }
  }
  return bytes_read;
}
