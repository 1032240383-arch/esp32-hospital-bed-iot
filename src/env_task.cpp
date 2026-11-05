/**
 * env_task.cpp - Environmental monitoring: DHT22 room temp/humidity,
 *               simulated room O2 and AQI with bounded random walk.
 * Priority: 2 (lower than sensor, higher priority tasks preempt safely)
 */

#include <Arduino.h>
#include <DHT.h>
#include "env_task.h"
#include "config.h"
#include "shared_types.h"
#include "task_utils.h"

static DHT s_dht(PIN_DHT, DHT_TYPE);
TaskHandle_t xHandleEnv = nullptr;

static void vEnvTask(void* pvParams) {
    (void)pvParams;
    s_dht.begin();
    vTaskDelay(pdMS_TO_TICKS(1500)); // DHT22 needs 1s+ to stabilize
    Serial.println("[Env] Environmental Task started");

    // Bounded random walk state for O2 and AQI
    float o2  = 20.9f;
    float aqi = 42.0f;

    while (true) {
        xEventGroupSetBits(xTaskHealthBits, BIT_ENV_ALIVE);

        // Read DHT22
        float t = s_dht.readTemperature();
        float h = s_dht.readHumidity();
        float roomTemp = isnan(t) ? 23.5f : t;
        float humidity = isnan(h) ? 50.0f : h;

        // Check force-abnormal button
        bool forceAbnormal = false;
        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            forceAbnormal = g_systemState.lastReading.forceAbnormal;
            xSemaphoreGive(xSharedStateMutex);
        }

        if (forceAbnormal) {
            o2  = 19.1f;  // below O2_WARN_LOW = 19.5
            aqi = 125.0f; // above AQI_WARN_HIGH = 100
        } else {
            // Bounded random walk: small drift each sample
            float o2Delta  = ((float)(random(-10, 11))) * 0.01f;  // ±0.10 %
            float aqiDelta = ((float)(random(-5, 6)));             // ±5 AQI
            o2  += o2Delta;
            aqi += aqiDelta;
            // Clamp to realistic bounds
            if (o2  < 20.0f) o2  = 20.0f;
            if (o2  > 21.5f) o2  = 21.5f;
            if (aqi < 20.0f) aqi = 20.0f;
            if (aqi > 120.0f) aqi = 120.0f;
        }

        // Write env data to shared state
        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_systemState.lastReading.roomTemp    = roomTemp;
            g_systemState.lastReading.humidity    = humidity;
            g_systemState.lastReading.oxygenLevel = o2;
            g_systemState.lastReading.aqi         = (int)aqi;
            xSemaphoreGive(xSharedStateMutex);
        }

        Serial.printf("[Env] RoomTemp=%.1fC Hum=%.1f%% O2=%.2f%% AQI=%d\n",
                      roomTemp, humidity, o2, (int)aqi);

        taskSleepWithHeartbeat(5000, BIT_ENV_ALIVE); // 5s env sample interval
    }
}

void env_task_init(void) {
    xTaskCreatePinnedToCore(
        vEnvTask,
        "EnvTask",
        STACK_ENV,
        nullptr,
        PRIORITY_ENV,
        &xHandleEnv,
        1
    );
}
