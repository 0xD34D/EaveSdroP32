// audio_stream.h
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void i2s_audio_init();
size_t i2s_audio_read(uint8_t *buffer, size_t max_len);

#ifdef __cplusplus
}
#endif
