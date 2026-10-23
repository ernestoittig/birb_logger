/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_check.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "sdkconfig.h"

#define WEB_SERVER "10.142.13.19"
#define WEB_PORT "80"
#define WEB_PATH "/"

#include "display.h"
#include "wifi.h"
#include "send_audio.h"
#include "audio.h"

bool wifi_ok = false;
bool conn_ok = true;

static const char *TAG = "birdy";

// static void sound_push_task(void *pvParameters)
// {
//     const struct addrinfo hints = {
//         .ai_family = AF_INET,
//         .ai_socktype = SOCK_STREAM,
//     };
//     struct addrinfo *res;
//     struct in_addr *addr;
//     int s, r;
//     char recv_buf[64];
//
//     while(1) {
//         int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);
//
//         if(err != 0 || res == NULL) {
//             ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
//             vTaskDelay(1000 / portTICK_PERIOD_MS);
//             continue;
//         }
//
//         /* Code to print the resolved IP.
//
//            Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
//         addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
//         ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));
//
//         s = socket(res->ai_family, res->ai_socktype, 0);
//         if(s < 0) {
//             ESP_LOGE(TAG, "... Failed to allocate socket.");
//             freeaddrinfo(res);
//             vTaskDelay(1000 / portTICK_PERIOD_MS);
//             continue;
//         }
//         ESP_LOGI(TAG, "... allocated socket");
//
//         if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
//             ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
//             close(s);
//             freeaddrinfo(res);
//             vTaskDelay(4000 / portTICK_PERIOD_MS);
//             continue;
//         }
//
//         ESP_LOGI(TAG, "... connected");
//         freeaddrinfo(res);
//
//         if (write(s, REQUEST, strlen(REQUEST)) < 0) {
//             ESP_LOGE(TAG, "... socket send failed");
//             close(s);
//             vTaskDelay(4000 / portTICK_PERIOD_MS);
//             continue;
//         }
//         ESP_LOGI(TAG, "... socket send success");
//
//         struct timeval receiving_timeout;
//         receiving_timeout.tv_sec = 5;
//         receiving_timeout.tv_usec = 0;
//         if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
//                 sizeof(receiving_timeout)) < 0) {
//             ESP_LOGE(TAG, "... failed to set socket receiving timeout");
//             close(s);
//             vTaskDelay(4000 / portTICK_PERIOD_MS);
//             continue;
//         }
//         ESP_LOGI(TAG, "... set socket receiving timeout success");
//
//         /* Read HTTP response */
//         do {
//             bzero(recv_buf, sizeof(recv_buf));
//             r = read(s, recv_buf, sizeof(recv_buf)-1);
//             for(int i = 0; i < r; i++) {
//                 putchar(recv_buf[i]);
//             }
//         } while(r > 0);
//
//         ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.", r, errno);
//         close(s);
//         for(int countdown = 10; countdown >= 0; countdown--) {
//             ESP_LOGI(TAG, "%d... ", countdown);
//             vTaskDelay(1000 / portTICK_PERIOD_MS);
//         }
//         ESP_LOGI(TAG, "Starting again!");
//     }
// }

void ui(void) {
    display_text(0, 0, wifi_ok ? "O" : "X", true);
    display_flush();
}

void app_main(void)
{
    printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    fflush(stdout);

    bool display_ok = display_init() == ESP_OK;

    if (display_ok) {
        ui();
    }

    wifi_ok = wifi_init();

    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    audio_init();
    xTaskCreate(&send_audio_task, "send_audio_task", 4096, NULL, 5, NULL);

    for (;;) {
        if (display_ok) {
            ui();
        }
    }
}
