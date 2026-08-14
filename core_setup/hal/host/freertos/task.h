#pragma once
// Host mock for freertos/task.h, paired with the FreeRTOS.h / queue.h mocks. The transport's
// tcpip-thread self-detection (TaskHandle_t / xTaskGetCurrentTaskHandle, used by protocore_tcp_marshal)
// is compiled only inside ARDUINO-guarded code, so on the host this header just has to resolve the
// #include; the type + stub are provided for completeness so any future host reference still links.
#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;

static inline TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return (TaskHandle_t)0;
}

// The task and tick surface the performance_benching device sketches name, so their sources compile
// on the host. A tick is a millisecond, matching CONFIG_FREERTOS_HZ=1000 on the rig.
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef void (*TaskFunction_t)(void *);

#define portTICK_PERIOD_MS 1u

/// Return without sleeping: the host has no scheduler to yield to.
static inline void vTaskDelay(TickType_t ticks)
{
    (void)ticks;
}

/// Record nothing and run nothing; the task body is entered only on silicon.
static inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack_depth, void *arg,
                                                 unsigned priority, TaskHandle_t *created, BaseType_t core_id)
{
    (void)fn;
    (void)name;
    (void)stack_depth;
    (void)arg;
    (void)priority;
    (void)core_id;
    if (created)
    {
        *created = (TaskHandle_t)0;
    }
    return pdTRUE;
}
