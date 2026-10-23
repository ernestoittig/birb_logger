#pragma once

#include "esp_err.h"
#include "freertos/semphr.h"

esp_err_t audio_init(void);

int audio_mic_level(void);

#define MIC_CHUNK_FRAMES    256
#define MIC_SAMPLE_RATE_HZ      16000
extern int16_t audio_buf[MIC_CHUNK_FRAMES * 2];

extern SemaphoreHandle_t mic_buf_mutex;
extern SemaphoreHandle_t mic_consume_mutex;
