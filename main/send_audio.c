#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "portmacro.h"
#include "sdkconfig.h"

#include "send_audio.h"
#include "audio.h"

static const char *TAG = "send_audio";

#define WEB_SERVER "10.142.13.19"
#define WEB_PORT "80"

void send_audio_task(void *pvParameters) {
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s;

    while(1) {
        int current_loudness = audio_mic_level();

        if (current_loudness > THRESHOLD) {
            ESP_LOGI(TAG, "Loudness excedida! Conectando a server...");
            TickType_t comienzo = xTaskGetTickCount();
            TickType_t ultimo_loud = comienzo;

            int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

            if(err != 0 || res == NULL) {
                ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }

            /* Code to print the resolved IP.

               Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
            addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
            ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

            s = socket(res->ai_family, res->ai_socktype, 0);
            if(s < 0) {
                ESP_LOGE(TAG, "... Failed to allocate socket.");
                freeaddrinfo(res);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }
            ESP_LOGI(TAG, "... allocated socket");

            if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
                ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
                close(s);
                freeaddrinfo(res);
                vTaskDelay(4000 / portTICK_PERIOD_MS);
                continue;
            }

            ESP_LOGI(TAG, "... connected");
            freeaddrinfo(res);

            bool keep_recording = true;
            while (keep_recording) {
                // TODO: Continuously read mic data into audio_buffer here
                int err = 0;
                if(xSemaphoreTake(mic_consume_mutex, portMAX_DELAY) == pdTRUE) {
                    err = send(s, audio_buf, sizeof(audio_buf), 0);
                    xSemaphoreGive(mic_buf_mutex);
                } else {
                    continue;
                }
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    break;
                }
                
                TickType_t elapsed = xTaskGetTickCount() - comienzo;
                uint32_t elapsed_ms = elapsed * portTICK_PERIOD_MS;
                current_loudness = audio_mic_level();
                if (current_loudness > THRESHOLD) {
                    ultimo_loud = xTaskGetTickCount();
                }
                keep_recording = elapsed_ms < MIN_RECORDING_LENGTH
                    || (elapsed_ms < MAX_RECORDING_LENGTH && current_loudness > THRESHOLD)
                    || (xTaskGetTickCount() - ultimo_loud) * portTICK_PERIOD_MS < BOUNCE_RECORDING_LENGTH;
            }

            if (s != -1) {
                ESP_LOGI(TAG, "Shutting down socket...");
                shutdown(s, 0);
                close(s);
            }
        }
    }
}
