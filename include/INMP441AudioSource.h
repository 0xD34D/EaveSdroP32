#pragma once

#include <algorithm>
#include <driver/i2s.h>
#include "IAudioSource.h"

// Pins connected to INMP441 I2S microphone
#define I2S_WS 4
#define I2S_SD 2
#define I2S_SCK 3
 
// Use I2S port 0
#define I2S_PORT I2S_NUM_0

class INMP441AudioSource : public IAudioSource {

 public:
  INMP441AudioSource() {};

  void start();
  void stop();

  int readBytes(void* dest, int maxBytes) override;
};