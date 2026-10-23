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
#include "sdkconfig.h"

#include "audio.h"

#define THRESHOLD 1

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
        int current_loudness = 2;

        if (current_loudness > THRESHOLD) {
            ESP_LOGI(TAG, "Loudness excedida! Conectando a server...");

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

            int n = 1000;
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
                
                n--;
                keep_recording = n >= 0;
            }

            if (s != -1) {
                ESP_LOGI(TAG, "Shutting down socket...");
                shutdown(s, 0);
                close(s);
            }
        }
        for(int countdown = 10; countdown >= 0; countdown--) {
            ESP_LOGI(TAG, "%d... ", countdown);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        ESP_LOGI(TAG, "Starting again!");
    }
}
