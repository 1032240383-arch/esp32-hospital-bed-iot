/**
 * supervisor_task.cpp  -  Task Health Supervisor & Hardware Watchdog Feeder.
 *
 * Checks all 9 tasks (Sensor, Display, MQTT, WiFi, Alert, Bed, Env, BP, ECG).
 * Clears bits, sleeps, samples bits. If any task hangs or heap drops < 20KB,
 * withholds feed -> HW reset.
 * Also logs uxTaskGetStackHighWaterMark for all tasks once per minute.
 */

#include <Arduino.h>
#include <esp_idf_version.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "shared_types.h"

TaskHandle_t xHandleSupervisor = nullptr;

static void logStackHighWaterMarks(void) {
    Serial.println("-----------------------------------------------------------------");
    Serial.println("[Supervisor] Task Stack High-Water Marks (bytes remaining free):");
    if (xHandleSupervisor) Serial.printf("  SupervisorTask : %u bytes free\n", uxTaskGetStackHighWaterMark(xHandleSupervisor) * sizeof(StackType_t));
    if (xHandleAlert)      Serial.printf("  AlertTask      : %u bytes free\n", uxTaskGetStackHighWaterMark(xHandleAlert) * sizeof(StackType_t));
    if (xHandleMedication) Serial.printf("  MedicationTask : %u bytes free\n", uxTaskGetStackHighWaterMark(xHandleMedication) * sizeof(StackType_t));
    if (xHandleBed)        Serial.printf("  BedControlTask : %u bytes free\n", uxTaskGetStackHighWaterMark(xHandleBed) * sizeof(StackType_t));
    if (xHandleSensor)     Serial.printf("  SensorTask     : %u bytes free\n", uxTaskGetStackHighWaterMark(xHandleSensor) * sizeof(StackType_t));
    if (xHandleWifi)       Serial.printf("  WiFiTask       : %u bytes free\n", uxTaskGetStackHighWaterMark(xHandleWifi) * sizeof(StackType_t));
    if (xHandleMqtt)       Serial.printf("  MQTTTask       : %u bytes free\n", uxTaskGetStackHighWaterMark(xHandleMqtt) * sizeof(StackType_t));
    if (xHandleDisplay)    Serial.printf("  DisplayTask    : %u bytes free\n", uxTaskGetStackHighWaterMark(xHandleDisplay) * sizeof(StackType_t));
    if (xHandleEnv)        Serial.printf("  EnvTask        : %u bytes free\n", uxTaskGetStackHighWaterMark(xHandleEnv) * sizeof(StackType_t));
    if (xHandleBp)         Serial.printf("  BpTask         : %u bytes free\n", uxTaskGetStackHighWaterMark(xHandleBp) * sizeof(StackType_t));
    if (xHandleEcg)        Serial.printf("  EcgTask        : %u bytes free\n", uxTaskGetStackHighWaterMark(xHandleEcg) * sizeof(StackType_t));
    Serial.println("-----------------------------------------------------------------");
}

static void vSupervisorTask(void* pvParams) {
    (void)pvParams;
    Serial.println("[Supervisor] Task started");

    // Initialize HW Task Watchdog Timer with trigger_panic = true for real hardware reboot
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    esp_task_wdt_config_t wdtCfg = {
        .timeout_ms     = WATCHDOG_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic  = true
    };
    esp_task_wdt_init(&wdtCfg);
#else
    esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, true);
#endif

    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();

    // Initial delay for tasks to boot and assert bits
    vTaskDelay(pdMS_TO_TICKS(SUPERVISOR_CHECK_INTERVAL_MS));

    uint8_t loopCount = 0;

    while (true) {
        // Clear all health bits
        xEventGroupClearBits(xTaskHealthBits, ALL_TASK_ALIVE_BITS);

        // Wait check interval for tasks to set their alive bits
        vTaskDelay(pdMS_TO_TICKS(SUPERVISOR_CHECK_INTERVAL_MS));

        EventBits_t bits = xEventGroupGetBits(xTaskHealthBits);

        bool sensorOK  = (bits & BIT_SENSOR_ALIVE)  != 0;
        bool displayOK = (bits & BIT_DISPLAY_ALIVE) != 0;
        bool mqttOK    = (bits & BIT_MQTT_ALIVE)    != 0;
        bool wifiOK    = (bits & BIT_WIFI_ALIVE)    != 0;
        bool alertOK   = (bits & BIT_ALERT_ALIVE)   != 0;
        bool bedOK     = (bits & BIT_BED_ALIVE)     != 0;
        bool envOK     = (bits & BIT_ENV_ALIVE)     != 0;
        bool bpOK      = (bits & BIT_BP_ALIVE)      != 0;
        bool ecgOK     = (bits & BIT_ECG_ALIVE)     != 0;

        Serial.printf("[Supervisor] Health: Sensor=%s Disp=%s MQTT=%s WiFi=%s Alert=%s Bed=%s Env=%s BP=%s ECG=%s\n",
                      sensorOK  ? "OK" : "DEAD",
                      displayOK ? "OK" : "DEAD",
                      mqttOK    ? "OK" : "DEAD",
                      wifiOK    ? "OK" : "DEAD",
                      alertOK   ? "OK" : "DEAD",
                      bedOK     ? "OK" : "DEAD",
                      envOK     ? "OK" : "DEAD",
                      bpOK      ? "OK" : "DEAD",
                      ecgOK     ? "OK" : "DEAD");
        Serial.printf("[Supervisor] Free heap: %u bytes\n", ESP.getFreeHeap());

        bool allAlive = sensorOK && displayOK && mqttOK && wifiOK && alertOK && bedOK && envOK && bpOK && ecgOK;
        bool heapOk   = (ESP.getFreeHeap() >= 20480);

        char healthReason[48] = "OK";
        if (!allAlive) {
            snprintf(healthReason, sizeof(healthReason), "TASK_FAULT");
        } else if (!heapOk) {
            snprintf(healthReason, sizeof(healthReason), "LOW_HEAP");
        }

        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_systemState.systemHealthy = (allAlive && heapOk);
            strncpy(g_systemState.lastHealthReason, healthReason, sizeof(g_systemState.lastHealthReason) - 1);
            xSemaphoreGive(xSharedStateMutex);
        }

        if (allAlive && heapOk) {
            esp_task_wdt_reset(); // Feed watchdog
        } else {
            Serial.printf("[Supervisor] ALERT: %s - starving hardware watchdog!\n", healthReason);
        }

        // Log stack high-water mark every 60s (every 6 loops of 10s)
        loopCount++;
        if (loopCount >= 6) {
            loopCount = 0;
            logStackHighWaterMarks();
        }
    }
}

void supervisor_task_init(void) {
    xTaskCreatePinnedToCore(
        vSupervisorTask,
        "SupervisorTask",
        STACK_SUPERVISOR,
        nullptr,
        PRIORITY_SUPERVISOR,
        &xHandleSupervisor,
        1
    );
}
