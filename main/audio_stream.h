// audio_stream.h
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void i2s_audio_init();
size_t i2s_audio_read(uint8_t *buffer, size_t max_len);
int i2s_audio_get_sample_rate(void);
esp_err_t i2s_audio_set_sample_rate(int sample_rate);

#ifdef __cplusplus
}
#endif
