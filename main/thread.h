#pragma once
#include "freertos/FreeRTOS.h"
#include "portmacro.h"

int current_time() {
    return xTaskGetTickCount();
}

int time_elapsed_between_ms(int a, int b) {
    return (a - b) * portTICK_PERIOD_MS;
}
