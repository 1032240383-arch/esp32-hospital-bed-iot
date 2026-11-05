/**
 * ecg_task.cpp - ECG rhythm classification task.
 * Derives rhythm from HR. Runs at priority 2.
 */

#include <Arduino.h>
#include "ecg_task.h"
#include "config.h"
#include "shared_types.h"
#include "task_utils.h"

TaskHandle_t xHandleEcg = nullptr;

static void vEcgTask(void* pvParams) {
    (void)pvParams;
    Serial.println("[ECG] ECG Task started");

    while (true) {
        xEventGroupSetBits(xTaskHealthBits, BIT_ECG_ALIVE);

        float hr = 75.0f;
        bool forceAbnormal = false;
        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            hr = g_systemState.lastReading.heartRate;
            forceAbnormal = g_systemState.lastReading.forceAbnormal;
            xSemaphoreGive(xSharedStateMutex);
        }

        char rhythm[16];
        if (forceAbnormal) {
            strncpy(rhythm, "Arrhythmia", sizeof(rhythm));
        } else if (hr > 110.0f) {
            strncpy(rhythm, "Tachycardia", sizeof(rhythm));
        } else if (hr < 55.0f) {
            strncpy(rhythm, "Bradycardia", sizeof(rhythm));
        } else {
            strncpy(rhythm, "Normal Sinus", sizeof(rhythm));
        }

        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            strncpy(g_systemState.lastReading.ecgStatus, rhythm,
                    sizeof(g_systemState.lastReading.ecgStatus));
            xSemaphoreGive(xSharedStateMutex);
        }

        taskSleepWithHeartbeat(3000, BIT_ECG_ALIVE); // 3s ECG update
    }
}

void ecg_task_init(void) {
    xTaskCreatePinnedToCore(
        vEcgTask,
        "EcgTask",
        STACK_ECG,
        nullptr,
        PRIORITY_ECG,
        &xHandleEcg,
        1
    );
}
