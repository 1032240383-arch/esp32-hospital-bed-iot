/**
 * bp_task.cpp - Blood Pressure simulation task.
 * Derives BP from HR with added noise. Runs at priority 2.
 */

#include <Arduino.h>
#include <math.h>
#include "bp_task.h"
#include "config.h"
#include "shared_types.h"
#include "task_utils.h"

TaskHandle_t xHandleBp = nullptr;

static void vBpTask(void* pvParams) {
    (void)pvParams;
    Serial.println("[BP] Blood Pressure Task started");

    while (true) {
        xEventGroupSetBits(xTaskHealthBits, BIT_BP_ALIVE);

        float hr = 75.0f;
        bool forceAbnormal = false;
        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            hr = g_systemState.lastReading.heartRate;
            forceAbnormal = g_systemState.lastReading.forceAbnormal;
            xSemaphoreGive(xSharedStateMutex);
        }

        int bpSys, bpDia;
        if (forceAbnormal) {
            bpSys = 160 + random(-5, 6);
            bpDia = 102 + random(-3, 4);
        } else {
            // Derive from HR with ±3 mmHg noise
            bpSys = 115 + (int)((hr - 70.0f) * 0.40f) + random(-3, 4);
            bpDia = 75  + (int)((hr - 70.0f) * 0.22f) + random(-3, 4);
            // Clamp to physiological range
            if (bpSys < 90)  bpSys = 90;
            if (bpSys > 180) bpSys = 180;
            if (bpDia < 60)  bpDia = 60;
            if (bpDia > 120) bpDia = 120;
        }

        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_systemState.lastReading.bpSys = bpSys;
            g_systemState.lastReading.bpDia = bpDia;
            xSemaphoreGive(xSharedStateMutex);
        }

        taskSleepWithHeartbeat(4000, BIT_BP_ALIVE); // 4s BP sample
    }
}

void bp_task_init(void) {
    xTaskCreatePinnedToCore(
        vBpTask,
        "BpTask",
        STACK_BP,
        nullptr,
        PRIORITY_BP,
        &xHandleBp,
        1
    );
}
