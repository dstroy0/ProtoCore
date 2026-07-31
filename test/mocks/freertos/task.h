#pragma once
// Host mock for freertos/task.h, paired with the FreeRTOS.h / queue.h mocks. The transport's
// tcpip-thread self-detection (TaskHandle_t / xTaskGetCurrentTaskHandle, used by pc_tcp_marshal)
// is compiled only inside ARDUINO-guarded code, so on the host this header just has to resolve the
// #include; the type + stub are provided for completeness so any future host reference still links.
#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;

static inline TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return (TaskHandle_t)0;
}
