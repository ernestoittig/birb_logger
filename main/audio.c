#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "portmacro.h"
#include "sdkconfig.h"

#include "audio.h"
#include "board.h"

/*  
    Todo esto esta robado de la docu, para entenderlo bien van a querer leer
    https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2s.html
    Se que es medio gd decirles RTFM pero hay posta demasiadas cosas pasando 
    y no soy bueno explicando.
    La docu tiene muy buenos ejemplos.
*/

static const char *TAG = "audio";

int16_t audio_buf[MIC_CHUNK_FRAMES * 2];
static int16_t buf_new[MIC_CHUNK_FRAMES * 2];
SemaphoreHandle_t mic_buf_mutex;
SemaphoreHandle_t mic_consume_mutex;

static i2s_chan_handle_t s_tx;
static i2s_chan_handle_t s_rx;

static volatile int s_mic_level;


static esp_err_t mic_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_rx), TAG, "new rx channel failed");
    mic_buf_mutex = xSemaphoreCreateCounting(1, 1);
    ESP_RETURN_ON_FALSE(mic_buf_mutex != NULL, ESP_ERR_NO_MEM, TAG, "Mic semaphore couldn't be created");
    mic_consume_mutex = xSemaphoreCreateCounting(1, 0);
    ESP_RETURN_ON_FALSE(mic_consume_mutex != NULL, ESP_ERR_NO_MEM, TAG, "Mic semaphore couldn't be created");

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .clk = BOARD_MIC_CLK_IO,
            .din = BOARD_MIC_DOUT_IO,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_rx_mode(s_rx, &pdm_cfg), TAG, "rx init failed");
    return i2s_channel_enable(s_rx);
}

static void mic_task(void *arg)
{
    int16_t buf_new[MIC_CHUNK_FRAMES * 2];
    float smoothed = 0;

    while (1) {
        size_t bytes = 0;
        if (i2s_channel_read(s_rx, buf_new, sizeof(buf_new), &bytes, 1000) != ESP_OK) {
            continue;
        }
        int frames = bytes / (2 * sizeof(int16_t));
        if (frames == 0) {
            continue;
        }

        float best_rms = 0;
        for (int ch = 0; ch < 2; ch++) {
            int64_t sum = 0, sum_sq = 0;
            for (int i = 0; i < frames; i++) {
                int v = buf_new[i * 2 + ch];
                sum += v;
                sum_sq += (int64_t)v * v;
            }
            float mean = (float)sum / frames;
            float var = (float)sum_sq / frames - mean * mean;
            float rms = var > 0 ? sqrtf(var) : 0;
            if (rms > best_rms) {
                best_rms = rms;
            }
        }

        float level = (20.0f * log10f(best_rms + 1.0f) - 20.0f) * (100.0f / 60.0f);
        level = level < 0 ? 0 : (level > 100 ? 100 : level);

        smoothed = level > smoothed ? level : smoothed * 0.8f + level * 0.2f;

        s_mic_level = (int)(smoothed + 0.5f);
        if (xSemaphoreTake(mic_buf_mutex, 5) == pdTRUE) {
            memcpy((void*)audio_buf, buf_new, sizeof(audio_buf));
            xSemaphoreGive(mic_consume_mutex);
        } else {
            // ESP_LOGI(TAG, "could not acquire lock");
        }
    }
}

esp_err_t audio_init(void)
{
    ESP_RETURN_ON_ERROR(mic_init(), TAG, "mic init failed");
    ESP_RETURN_ON_FALSE(xTaskCreate(mic_task, "mic", 4096, NULL, 5, NULL) == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "mic task create failed");
    ESP_LOGI(TAG, "speaker (I2S1) and microphone (PDM, I2S0) ready");
    return ESP_OK;
}

int audio_mic_level(void)
{
    return s_mic_level;
}

