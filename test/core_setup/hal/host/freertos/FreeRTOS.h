#pragma once
#include <stdint.h>

typedef void *QueueHandle_t;
typedef struct
{
    uint8_t _opaque[8];
} StaticQueue_t;

#define pdTRUE 1
#define pdFALSE 0
