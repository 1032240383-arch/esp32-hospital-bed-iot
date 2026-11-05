/**
 * sensor_task.cpp  -  Multi-Sensor Acquisition, Vitals Processing & Adaptive Sampling.
 *
 * Implements:
 *   - Reading ADC potentiometers (HR, SpO2) and force-abnormal button (D13)
 *   - Reading environmental and derived vitals (DHT22, O2, AQI, BP, ECG) from shared state
 *   - Vital classification (Normal, Warning, Critical)
 *   - Intelligent auto-bed elevation trigger to Breathing Support (45 deg) on low SpO2 (<90%)
 *   - Dynamic Adaptive Sampling (5s on abnormal, 15s on stable; slider 5-60s)
 *   - Responsive sliced waiting with sampling command queue drain & early status re-trigger
 */

#include <Arduino.h>
#include "sensor_task.h"
#include "alert_task.h"
#include "bed_task.h"
#include "display_task.h"
#include "config.h"
#include "shared_types.h"
#include "task_utils.h"

TaskHandle_t xHandleSensor = nullptr;

static float s_hrSmooth   = 75.0f;
static float s_spo2Smooth = 98.0f;
static const float ALPHA  = 0.25f;

static VitalStatus_t classifyHR(float hr) {
    if (hr <= HR_CRIT_LOW || hr >= HR_CRIT_HIGH) return STATUS_CRITICAL;
    if (hr <= HR_WARN_LOW || hr >= HR_WARN_HIGH)  return STATUS_WARNING;
    return STATUS_NORMAL;
}

static VitalStatus_t classifySPO2(float spo2) {
    if (spo2 <= SPO2_CRIT_LOW)  return STATUS_CRITICAL;
    if (spo2 <= SPO2_WARN_LOW)  return STATUS_WARNING;
    return STATUS_NORMAL;
}

static VitalStatus_t classifyBTemp(float t) {
    if (t >= BTEMP_CRIT_HIGH) return STATUS_CRITICAL;
    if (t >= BTEMP_WARN_HIGH) return STATUS_WARNING;
    return STATUS_NORMAL;
}

static const char* statusName(VitalStatus_t s) {
    switch (s) {
        case STATUS_CRITICAL: return "CRIT";
        case STATUS_WARNING:  return "WARN";
        default:              return "OK";
    }
}

static void vSensorTask(void* pvParams) {
    (void)pvParams;

    pinMode(PIN_POT_HR,       INPUT);
    pinMode(PIN_POT_SPO2,     INPUT);
    pinMode(PIN_BTN_ABNORMAL, INPUT_PULLUP);

    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.println("[Sensor] Sensor Acquisition & Processing Task started");

    bool autoBedTriggered = false;

    while (true) {
        xEventGroupSetBits(xTaskHealthBits, BIT_SENSOR_ALIVE);

        // 1. Read Pushbuttons & Potentiometers
        bool btnAbnormal = (digitalRead(PIN_BTN_ABNORMAL) == LOW);

        int rawHR   = analogRead(PIN_POT_HR);
        int rawSPO2 = analogRead(PIN_POT_SPO2);

        float mappedHR   = 60.0f + ((float)rawHR   / 4095.0f) * 120.0f; // 60 - 180 bpm
        float mappedSPO2 = 85.0f + ((float)rawSPO2 / 4095.0f) *  15.0f; // 85 - 100 %

        s_hrSmooth   = (ALPHA * mappedHR)   + ((1.0f - ALPHA) * s_hrSmooth);
        s_spo2Smooth = (ALPHA * mappedSPO2) + ((1.0f - ALPHA) * s_spo2Smooth);

        SensorData_t data = {};
        data.timestamp     = millis();
        data.forceAbnormal = btnAbnormal;

        if (btnAbnormal) {
            data.heartRate   = SIM_ABNORMAL_HR;
            data.spo2        = SIM_ABNORMAL_SPO2;
            data.bodyTemp    = SIM_ABNORMAL_BTEMP;
            data.bpSys       = 160;
            data.bpDia       = 102;
            strncpy(data.ecgStatus, "Arrhythmia", sizeof(data.ecgStatus) - 1);
            data.ecgStatus[sizeof(data.ecgStatus) - 1] = '\0';
            data.oxygenLevel = 19.1f;
            data.aqi         = 125;
            if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                data.roomTemp = g_systemState.lastReading.roomTemp;
                data.humidity = g_systemState.lastReading.humidity;
                xSemaphoreGive(xSharedStateMutex);
            }
        } else {
            data.heartRate = s_hrSmooth;
            data.spo2      = s_spo2Smooth;
            data.bodyTemp  = SIM_BTEMP_BASE + (s_hrSmooth - 75.0f) * SIM_BTEMP_HR_SCALE;

            // 2. Read auxiliary readings from EnvTask, BpTask, EcgTask
            if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                data.bpSys       = g_systemState.lastReading.bpSys;
                data.bpDia       = g_systemState.lastReading.bpDia;
                strncpy(data.ecgStatus, g_systemState.lastReading.ecgStatus, sizeof(data.ecgStatus) - 1);
                data.ecgStatus[sizeof(data.ecgStatus) - 1] = '\0';
                data.roomTemp    = g_systemState.lastReading.roomTemp;
                data.humidity    = g_systemState.lastReading.humidity;
                data.oxygenLevel = g_systemState.lastReading.oxygenLevel;
                data.aqi         = g_systemState.lastReading.aqi;
                xSemaphoreGive(xSharedStateMutex);
            }

            // Default fallbacks if auxiliary tasks haven't produced first sample yet
            if (data.bpSys == 0) { data.bpSys = 120; data.bpDia = 80; }
            if (data.ecgStatus[0] == '\0') { strncpy(data.ecgStatus, "Normal Sinus", sizeof(data.ecgStatus)); }
            if (data.oxygenLevel == 0.0f) { data.oxygenLevel = 20.9f; }
            if (data.aqi == 0) { data.aqi = 42; }
        }

        // 3. Classify Vital Status
        data.hrStatus    = classifyHR(data.heartRate);
        data.spo2Status  = classifySPO2(data.spo2);
        data.btempStatus = classifyBTemp(data.bodyTemp);

        if (data.hrStatus == STATUS_CRITICAL || data.spo2Status == STATUS_CRITICAL || data.btempStatus == STATUS_CRITICAL) {
            data.overallStatus = STATUS_CRITICAL;
        } else if (data.hrStatus == STATUS_WARNING || data.spo2Status == STATUS_WARNING || data.btempStatus == STATUS_WARNING) {
            data.overallStatus = STATUS_WARNING;
        } else {
            data.overallStatus = STATUS_NORMAL;
        }

        // 4. Intelligent Automatic Bed Elevation on Low SpO2 (<90%)
        if (data.spo2Status == STATUS_CRITICAL) {
            if (!autoBedTriggered) {
                Serial.println("[Bed Auto] Low SpO2 (<90%) detected! Auto-elevating to Breathing Support (45 deg)");
                bed_set_preset(BED_MODE_BREATHE);
                display_post_alert("AUTO BED ADJUST", "SpO2 < 90%!", "BED -> 45 deg", 4000);
                autoBedTriggered = true;
            }
        } else {
            autoBedTriggered = false;
        }

        // 5. Trigger Sound/LED Alarms
        if (data.overallStatus == STATUS_CRITICAL) {
            alert_trigger(ALERT_CRIT_VITALS);
        } else if (data.overallStatus == STATUS_WARNING) {
            alert_trigger(ALERT_WARN_VITALS);
        } else {
            alert_trigger(ALERT_NONE);
        }

        // 6. Store latest reading in global state
        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            g_systemState.lastReading = data;
            xSemaphoreGive(xSharedStateMutex);
        }

        // 7. Forward to MQTT queue using drop-oldest if full
        if (xQueueSend(xSensorDataQueue, &data, 0) != pdPASS) {
            SensorData_t dropped;
            xQueueReceive(xSensorDataQueue, &dropped, 0);
            xQueueSend(xSensorDataQueue, &data, 0);
            Serial.println("[Sensor] WARNING: Sensor queue full, dropped oldest sample");
        }

        Serial.printf("[Sensor] HR=%.0f(%s) SpO2=%.0f%%(%s) Temp=%.1fC BP=%d/%d ECG=%s Env=%.1fC Hum=%.0f%% O2=%.1f%% AQI=%d\n",
                      data.heartRate, statusName(data.hrStatus),
                      data.spo2, statusName(data.spo2Status),
                      data.bodyTemp, data.bpSys, data.bpDia, data.ecgStatus,
                      data.roomTemp, data.humidity, data.oxygenLevel, data.aqi);

        // 8. Adaptive Dynamic Sampling Rate calculation
        uint32_t delayMs = SENSOR_READ_AUTO_STABLE_MS;
        bool isAuto = true;

        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            isAuto  = g_systemState.sampling.isAutoMode;
            delayMs = g_systemState.sampling.intervalMs;
            xSemaphoreGive(xSharedStateMutex);
        }

        if (isAuto) {
            bool isAbnormal = (data.overallStatus != STATUS_NORMAL);
            delayMs = isAbnormal ? SENSOR_READ_AUTO_ABNORMAL_MS : SENSOR_READ_AUTO_STABLE_MS;

            if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_systemState.sampling.intervalMs = delayMs;
                xSemaphoreGive(xSharedStateMutex);
            }
        }

        // 9. Sliced adaptive wait: drains command queue, pulses heartbeat, and re-triggers on status change
        uint32_t slept = 0;
        VitalStatus_t entryStatus = data.overallStatus;

        while (slept < delayMs) {
            uint32_t slice = 250;
            if (slice > delayMs - slept) slice = delayMs - slept;
            xEventGroupSetBits(xTaskHealthBits, BIT_SENSOR_ALIVE);
            vTaskDelay(pdMS_TO_TICKS(slice));
            slept += slice;

            // Drain sampling command queue
            SamplingCmd_t scmd;
            bool intervalChanged = false;
            while (xSamplingCmdQueue != nullptr && xQueueReceive(xSamplingCmdQueue, &scmd, 0) == pdTRUE) {
                if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    if (scmd.type == SAMPLING_CMD_SET_INTERVAL) {
                        g_systemState.sampling.intervalMs = scmd.value;
                        g_systemState.sampling.isAutoMode = false;
                        if (scmd.value < delayMs - slept) {
                            delayMs = slept + scmd.value;
                            intervalChanged = true;
                        }
                    } else if (scmd.type == SAMPLING_CMD_SET_MODE) {
                        g_systemState.sampling.isAutoMode = (scmd.value == 1);
                        if (scmd.value == 1) {
                            // Switched to auto mode: break to recompute immediately
                            delayMs = slept;
                            intervalChanged = true;
                        }
                    }
                    xSemaphoreGive(xSharedStateMutex);
                }
            }

            // Quick re-check: re-read pots and wake early if status worsened
            {
                int rawHRCheck   = analogRead(PIN_POT_HR);
                int rawSPO2Check = analogRead(PIN_POT_SPO2);
                float hrCheck    = 60.0f + ((float)rawHRCheck   / 4095.0f) * 120.0f;
                float spo2Check  = 85.0f + ((float)rawSPO2Check / 4095.0f) *  15.0f;
                VitalStatus_t hrCheckStatus   = classifyHR(hrCheck);
                VitalStatus_t spo2CheckStatus = classifySPO2(spo2Check);
                VitalStatus_t nowStatus = (hrCheckStatus > spo2CheckStatus) ? hrCheckStatus : spo2CheckStatus;
                if (nowStatus > entryStatus) {
                    Serial.println("[Sensor] Status worsened mid-wait, sampling early");
                    break;
                }
            }

            // Quick re-check: if force-abnormal changed or status worsened, sample immediately
            bool nowAbnormal = (digitalRead(PIN_BTN_ABNORMAL) == LOW);
            if (nowAbnormal && entryStatus == STATUS_NORMAL) {
                break;
            }
            if (intervalChanged) {
                break;
            }
        }
    }
}

void sensor_task_init(void) {
    xTaskCreatePinnedToCore(
        vSensorTask,
        "SensorTask",
        STACK_SENSOR,
        nullptr,
        PRIORITY_SENSOR,
        &xHandleSensor,
        1
    );
}
