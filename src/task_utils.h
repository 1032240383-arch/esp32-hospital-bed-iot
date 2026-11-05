/**
 * task_utils.h - Shared FreeRTOS helper functions for heartbeat-safe operations.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "shared_types.h"

/**
 * Sleep for totalMs in slices of at most 500ms, setting eventBit on every slice.
 * Guarantees that the supervisor task's 10-second watchdog window sees active heartbeats
 * even during long tasks or network backoffs.
 */
inline void taskSleepWithHeartbeat(uint32_t totalMs, EventBits_t eventBit) {
    uint32_t remaining = totalMs;
    while (remaining > 0) {
        uint32_t slice = (remaining > 500) ? 500 : remaining;
        xEventGroupSetBits(xTaskHealthBits, eventBit);
        vTaskDelay(pdMS_TO_TICKS(slice));
        remaining -= slice;
    }
    xEventGroupSetBits(xTaskHealthBits, eventBit);
}
