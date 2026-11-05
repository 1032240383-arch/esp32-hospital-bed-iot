/**
 * mqtt_task.cpp  -  Dual-Role Dashboards, Commands, Aggregation & Offline Ring-Buffer.
 *
 * Implements:
 *   - Medical Staff Dashboard publishing (HR, SpO2, Temp, BP, ECG, Alerts)
 *   - Facility Management Dashboard publishing (RoomTemp, Hum, O2, AQI, Bed, Alerts)
 *   - Periodic min/max/avg aggregation every 30s to medical/aggregate and facility/aggregate
 *     including BP (sys/dia) in medical, and O2/AQI in facility aggregate
 *   - Command dispatch for medication dosage, bed presets, and adaptive sampling queue
 *   - Circular Ring-Buffer offline storage with non-lossy flush state machine and retry backoff
 *   - Periodic 5-second retained medication status publication
 *   - Heartbeat assertion and socket timeout safety
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mqtt_task.h"
#include "medication_task.h"
#include "bed_task.h"
#include "alert_task.h"
#include "config.h"
#include "shared_types.h"
#include "task_utils.h"
#include "conn_state.h"

TaskHandle_t xHandleMqtt = nullptr;

static WiFiClient   wifiClient;
static PubSubClient mqttClient(wifiClient);

// ─────────────────────────────────────────────────────────────────────────────
//  Offline Ring-Buffer State
// ─────────────────────────────────────────────────────────────────────────────
static SensorData_t s_ringBuffer[OFFLINE_BUFFER_CAPACITY];
static uint8_t      s_ringHead    = 0;
static uint8_t      s_ringCount   = 0;
static uint16_t     s_dropsCount  = 0;
static uint16_t     s_syncedCount = 0;

static void ringBufferPush(const SensorData_t& data) {
    if (s_ringCount >= OFFLINE_BUFFER_CAPACITY) {
        s_dropsCount++; // Overwriting oldest record
    }
    s_ringBuffer[s_ringHead] = data;
    s_ringHead = (s_ringHead + 1) % OFFLINE_BUFFER_CAPACITY;
    if (s_ringCount < OFFLINE_BUFFER_CAPACITY) {
        s_ringCount++;
    }

    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        g_systemState.offlineBufferCount = s_ringCount;
        xSemaphoreGive(xSharedStateMutex);
    }
    Serial.printf("[Offline Buffer] Saved reading (count=%d, drops=%d)\n",
                  s_ringCount, s_dropsCount);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Periodic Aggregation State (every 30 seconds)
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    float hrSum;    float hrMin;    float hrMax;
    float spo2Sum;  float spo2Min;  float spo2Max;
    float tempSum;  float tempMin;  float tempMax;
    int   bpSysSum; int   bpSysMin; int   bpSysMax;
    int   bpDiaSum; int   bpDiaMin; int   bpDiaMax;
    float rtempSum; float rtempMin; float rtempMax;
    float humSum;   float humMin;   float humMax;
    float o2Sum;    float o2Min;    float o2Max;
    int   aqiSum;   int   aqiMin;   int   aqiMax;
    uint16_t sampleCount;
} AggregationData_t;

static AggregationData_t s_agg;
static uint32_t s_lastAggMs = 0;

static void aggReset() {
    s_agg.hrSum = 0;    s_agg.hrMin = 999.0f;  s_agg.hrMax = 0.0f;
    s_agg.spo2Sum = 0;  s_agg.spo2Min = 999.0f;s_agg.spo2Max = 0.0f;
    s_agg.tempSum = 0;  s_agg.tempMin = 999.0f;s_agg.tempMax = 0.0f;
    s_agg.bpSysSum = 0; s_agg.bpSysMin = 999;  s_agg.bpSysMax = 0;
    s_agg.bpDiaSum = 0; s_agg.bpDiaMin = 999;  s_agg.bpDiaMax = 0;
    s_agg.rtempSum = 0; s_agg.rtempMin = 999.0f; s_agg.rtempMax = 0.0f;
    s_agg.humSum = 0;   s_agg.humMin = 999.0f; s_agg.humMax = 0.0f;
    s_agg.o2Sum = 0;    s_agg.o2Min = 999.0f;  s_agg.o2Max = 0.0f;
    s_agg.aqiSum = 0;   s_agg.aqiMin = 999;    s_agg.aqiMax = 0;
    s_agg.sampleCount = 0;
}

static void aggAddSample(const SensorData_t& d) {
    s_agg.hrSum += d.heartRate;
    if (d.heartRate < s_agg.hrMin) s_agg.hrMin = d.heartRate;
    if (d.heartRate > s_agg.hrMax) s_agg.hrMax = d.heartRate;

    s_agg.spo2Sum += d.spo2;
    if (d.spo2 < s_agg.spo2Min) s_agg.spo2Min = d.spo2;
    if (d.spo2 > s_agg.spo2Max) s_agg.spo2Max = d.spo2;

    s_agg.tempSum += d.bodyTemp;
    if (d.bodyTemp < s_agg.tempMin) s_agg.tempMin = d.bodyTemp;
    if (d.bodyTemp > s_agg.tempMax) s_agg.tempMax = d.bodyTemp;

    s_agg.bpSysSum += d.bpSys;
    if (d.bpSys < s_agg.bpSysMin) s_agg.bpSysMin = d.bpSys;
    if (d.bpSys > s_agg.bpSysMax) s_agg.bpSysMax = d.bpSys;

    s_agg.bpDiaSum += d.bpDia;
    if (d.bpDia < s_agg.bpDiaMin) s_agg.bpDiaMin = d.bpDia;
    if (d.bpDia > s_agg.bpDiaMax) s_agg.bpDiaMax = d.bpDia;

    s_agg.rtempSum += d.roomTemp;
    if (d.roomTemp < s_agg.rtempMin) s_agg.rtempMin = d.roomTemp;
    if (d.roomTemp > s_agg.rtempMax) s_agg.rtempMax = d.roomTemp;

    s_agg.humSum += d.humidity;
    if (d.humidity < s_agg.humMin) s_agg.humMin = d.humidity;
    if (d.humidity > s_agg.humMax) s_agg.humMax = d.humidity;

    s_agg.o2Sum += d.oxygenLevel;
    if (d.oxygenLevel < s_agg.o2Min) s_agg.o2Min = d.oxygenLevel;
    if (d.oxygenLevel > s_agg.o2Max) s_agg.o2Max = d.oxygenLevel;

    s_agg.aqiSum += d.aqi;
    if (d.aqi < s_agg.aqiMin) s_agg.aqiMin = d.aqi;
    if (d.aqi > s_agg.aqiMax) s_agg.aqiMax = d.aqi;

    s_agg.sampleCount++;
}

static void publishAggregation() {
    if (s_agg.sampleCount == 0) return;

    // 1. Medical Aggregate (HR, SpO2, BodyTemp, BP)
    StaticJsonDocument<384> medDoc;
    medDoc["window_sec"] = AGGREGATION_INTERVAL_MS / 1000;
    medDoc["samples"]    = s_agg.sampleCount;

    JsonObject hr = medDoc.createNestedObject("heartRate");
    hr["min"] = round(s_agg.hrMin);
    hr["max"] = round(s_agg.hrMax);
    hr["avg"] = round(s_agg.hrSum / s_agg.sampleCount);

    JsonObject spo2 = medDoc.createNestedObject("spo2");
    spo2["min"] = round(s_agg.spo2Min);
    spo2["max"] = round(s_agg.spo2Max);
    spo2["avg"] = round(s_agg.spo2Sum / s_agg.sampleCount);

    JsonObject temp = medDoc.createNestedObject("bodyTemp");
    temp["min"] = serialized(String(s_agg.tempMin, 1));
    temp["max"] = serialized(String(s_agg.tempMax, 1));
    temp["avg"] = serialized(String(s_agg.tempSum / s_agg.sampleCount, 1));

    JsonObject bp = medDoc.createNestedObject("bp");
    bp["sys_min"] = s_agg.bpSysMin;
    bp["sys_max"] = s_agg.bpSysMax;
    bp["sys_avg"] = s_agg.bpSysSum / s_agg.sampleCount;
    bp["dia_min"] = s_agg.bpDiaMin;
    bp["dia_max"] = s_agg.bpDiaMax;
    bp["dia_avg"] = s_agg.bpDiaSum / s_agg.sampleCount;

    char medJson[384];
    serializeJson(medDoc, medJson);
    mqttClient.publish(TOPIC_MED_AGGREGATE, medJson, true);

    // 2. Facility Aggregate (RoomTemp, Humidity, O2, AQI)
    StaticJsonDocument<384> facDoc;
    facDoc["window_sec"] = AGGREGATION_INTERVAL_MS / 1000;
    facDoc["samples"]    = s_agg.sampleCount;

    JsonObject rtemp = facDoc.createNestedObject("roomTemp");
    rtemp["min"] = serialized(String(s_agg.rtempMin, 1));
    rtemp["max"] = serialized(String(s_agg.rtempMax, 1));
    rtemp["avg"] = serialized(String(s_agg.rtempSum / s_agg.sampleCount, 1));

    JsonObject hum = facDoc.createNestedObject("humidity");
    hum["min"] = round(s_agg.humMin);
    hum["max"] = round(s_agg.humMax);
    hum["avg"] = round(s_agg.humSum / s_agg.sampleCount);

    JsonObject o2 = facDoc.createNestedObject("oxygen");
    o2["min"] = serialized(String(s_agg.o2Min, 1));
    o2["max"] = serialized(String(s_agg.o2Max, 1));
    o2["avg"] = serialized(String(s_agg.o2Sum / s_agg.sampleCount, 1));

    JsonObject aqi = facDoc.createNestedObject("aqi");
    aqi["min"] = s_agg.aqiMin;
    aqi["max"] = s_agg.aqiMax;
    aqi["avg"] = s_agg.aqiSum / s_agg.sampleCount;

    char facJson[384];
    serializeJson(facDoc, facJson);
    mqttClient.publish(TOPIC_FAC_AGGREGATE, facJson, true);

    Serial.printf("[MQTT Aggregate] Published %d samples to medical and facility aggregate topics\n",
                  s_agg.sampleCount);
    aggReset();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Publish Retained Medication State
// ─────────────────────────────────────────────────────────────────────────────
static void publishRetainedMedicationState() {
    float rate = 0.0f;
    float pending = 0.0f;
    float delivered = 0.0f;
    VitalStatus_t safety = STATUS_NORMAL;

    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        rate      = g_systemState.medication.currentRateMgHr;
        pending   = g_systemState.medication.pendingRateMgHr;
        delivered = g_systemState.medication.totalDeliveredMg;
        safety    = g_systemState.medication.safetyLevel;
        xSemaphoreGive(xSharedStateMutex);
    }

    StaticJsonDocument<192> doc;
    doc["rate"]         = rate;
    doc["safety"]       = (safety == STATUS_CRITICAL) ? "CRITICAL" :
                          (safety == STATUS_WARNING)  ? "WARNING" : "NORMAL";
    doc["pending"]      = pending;
    doc["delivered_mg"] = serialized(String(delivered, 2));

    char buf[192];
    serializeJson(doc, buf);
    mqttClient.publish(TOPIC_MEDICATION_STATUS, buf, true);
}

// ─────────────────────────────────────────────────────────────────────────────
//  MQTT Command Dispatcher
// ─────────────────────────────────────────────────────────────────────────────
static void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    char buf[128] = {};
    size_t copyLen = min((size_t)length, sizeof(buf) - 1);
    memcpy(buf, payload, copyLen);
    String strPayload = String(buf);
    strPayload.trim();

    Serial.printf("[MQTT RX] topic='%s' payload='%s'\n", topic, strPayload.c_str());

    // 1. Medication rate command (0-100 mg/hr)
    if (strcmp(topic, TOPIC_CMD_MED_RATE) == 0) {
        float rate = strPayload.toFloat();
        char reason[128];
        medication_request_rate(rate, reason, sizeof(reason));

        StaticJsonDocument<256> resp;
        resp["command"]        = "medication_rate";
        resp["requested_rate"] = rate;
        if (strstr(reason, "REJECTED") != nullptr) {
            resp["status"] = "REJECTED_OUT_OF_RANGE";
        } else if (strstr(reason, "PENDING") != nullptr) {
            resp["status"] = "PENDING_CONFIRMATION";
        } else if (strstr(reason, "CLAMPED") != nullptr) {
            resp["status"] = "CLAMPED";
        } else {
            resp["status"] = "ACCEPTED";
        }
        resp["message"] = reason;

        char respBuf[256];
        serializeJson(resp, respBuf);
        mqttClient.publish(TOPIC_MEDICATION_STATUS, respBuf, false);
        publishRetainedMedicationState();
    }
    // 2. Medication high-rate confirm
    else if (strcmp(topic, TOPIC_CMD_MED_CONFIRM) == 0) {
        char msg[128];
        bool ok = medication_confirm(msg, sizeof(msg));

        StaticJsonDocument<192> resp;
        resp["command"] = "medication_confirm";
        resp["status"]  = ok ? "CONFIRMED" : "NO_PENDING";
        resp["message"] = msg;

        char respBuf[192];
        serializeJson(resp, respBuf);
        mqttClient.publish(TOPIC_MEDICATION_STATUS, respBuf, false);
        publishRetainedMedicationState();
    }
    // 3. Bed angle command (0-90 deg)
    else if (strcmp(topic, TOPIC_CMD_BED_ANGLE) == 0) {
        int angle = strPayload.toInt();
        bed_set_angle(angle);
    }
    // 4. Bed preset command (Sleep 10, Eat 30, Breathe 45, Emergency 90)
    else if (strcmp(topic, TOPIC_CMD_BED_PRESET) == 0) {
        strPayload.toUpperCase();
        if (strPayload == "SLEEP" || strPayload == "1") {
            bed_set_preset(BED_MODE_SLEEP);
        } else if (strPayload == "EAT" || strPayload == "2") {
            bed_set_preset(BED_MODE_EAT);
        } else if (strPayload == "BREATHE" || strPayload == "3") {
            bed_set_preset(BED_MODE_BREATHE);
        } else if (strPayload == "EMERGENCY" || strPayload == "CPR" || strPayload == "4") {
            bed_set_preset(BED_MODE_EMERGENCY);
        }
    }
    // 5. Sampling rate command (5-60 s) via queue
    else if (strcmp(topic, TOPIC_CMD_SAMPLING_RATE) == 0) {
        int sec = strPayload.toInt();
        if (sec >= 5 && sec <= 60) {
            SamplingCmd_t scmd;
            scmd.type  = SAMPLING_CMD_SET_INTERVAL;
            scmd.value = (uint32_t)(sec * 1000);
            if (xSamplingCmdQueue != nullptr) {
                xQueueSend(xSamplingCmdQueue, &scmd, 0);
            }
            Serial.printf("[Sampling] Queued manual interval: %d s\n", sec);
        } else {
            char reject[64];
            snprintf(reject, sizeof(reject), "{\"error\":\"interval %d out of range [5-60]\"}", sec);
            mqttClient.publish(TOPIC_STATUS, reject, false);
        }
    }
    // 6. Sampling mode command ("auto" / "manual") via queue
    else if (strcmp(topic, TOPIC_CMD_SAMPLING_MODE) == 0) {
        strPayload.toLowerCase();
        SamplingCmd_t scmd;
        scmd.type  = SAMPLING_CMD_SET_MODE;
        scmd.value = (strPayload == "auto") ? 1 : 0;
        if (xSamplingCmdQueue != nullptr) {
            xQueueSend(xSamplingCmdQueue, &scmd, 0);
        }
        Serial.printf("[Sampling] Queued mode command: %s\n", (scmd.value == 1) ? "AUTO" : "MANUAL");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Flush Offline Ring-Buffer State Machine
// ─────────────────────────────────────────────────────────────────────────────
static bool     s_flushActive      = false;
static uint32_t s_flushBackoffMs   = 1000;
static uint32_t s_flushNextRetryMs = 0;

static bool flushNextBatch() {
    if (s_ringCount == 0) return true;

    int sent = 0;
    while (s_ringCount > 0 && sent < 5) {
        // Tail is oldest item in circular ring buffer
        int tailIdx = ((int)s_ringHead - (int)s_ringCount + OFFLINE_BUFFER_CAPACITY) % OFFLINE_BUFFER_CAPACITY;
        SensorData_t& d = s_ringBuffer[tailIdx];

        StaticJsonDocument<300> doc;
        doc["ts"]         = d.timestamp;
        doc["historical"] = true;
        doc["hr"]         = d.heartRate;
        doc["spo2"]       = d.spo2;
        doc["btemp"]      = serialized(String(d.bodyTemp, 1));
        doc["rtemp"]      = serialized(String(d.roomTemp, 1));
        doc["hum"]        = round(d.humidity);
        doc["bp"]         = String(d.bpSys) + "/" + String(d.bpDia);
        doc["ecg"]        = d.ecgStatus;
        doc["o2"]         = serialized(String(d.oxygenLevel, 1));
        doc["aqi"]        = d.aqi;

        char jsonBuf[300];
        serializeJson(doc, jsonBuf);

        bool ok = mqttClient.publish(TOPIC_HISTORICAL, jsonBuf, false);
        if (!ok) {
            // Keep record on failed publish, retry batch later with backoff
            return false;
        }

        // Successfully published: pop oldest record
        s_ringCount--;
        s_syncedCount++;
        sent++;

        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            g_systemState.offlineBufferCount = s_ringCount;
            xSemaphoreGive(xSharedStateMutex);
        }

        mqttClient.loop();
    }
    return (s_ringCount == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Publish Helpers
// ─────────────────────────────────────────────────────────────────────────────
static void publishFloat(const char* topic, float value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", value);
    mqttClient.publish(topic, buf, true);
}

static void publishMedicalDashboard(const SensorData_t& d) {
    publishFloat(TOPIC_MED_HR,    d.heartRate);
    publishFloat(TOPIC_MED_SPO2,  d.spo2);
    publishFloat(TOPIC_MED_BTEMP, d.bodyTemp);

    // BP
    char bpBuf[64];
    snprintf(bpBuf, sizeof(bpBuf), "{\"systolic\":%d,\"diastolic\":%d}", d.bpSys, d.bpDia);
    mqttClient.publish(TOPIC_MED_BP, bpBuf, true);

    // ECG
    char ecgBuf[64];
    snprintf(ecgBuf, sizeof(ecgBuf), "{\"rhythm\":\"%s\",\"hr\":%.0f}", d.ecgStatus, d.heartRate);
    mqttClient.publish(TOPIC_MED_ECG, ecgBuf, true);

    // Medical Alerts feed if abnormal
    if (d.overallStatus != STATUS_NORMAL) {
        char alertBuf[128];
        snprintf(alertBuf, sizeof(alertBuf),
                 "{\"severity\":\"%s\",\"hr\":%.0f,\"spo2\":%.0f,\"temp\":%.1f}",
                 (d.overallStatus == STATUS_CRITICAL) ? "CRITICAL" : "WARNING",
                 d.heartRate, d.spo2, d.bodyTemp);
        mqttClient.publish(TOPIC_MED_ALERTS, alertBuf, false);
    }
}

static void publishFacilityDashboard(const SensorData_t& d) {
    publishFloat(TOPIC_FAC_RTEMP, d.roomTemp);
    publishFloat(TOPIC_FAC_HUM,   d.humidity);

    // Oxygen
    char o2Buf[48];
    snprintf(o2Buf, sizeof(o2Buf), "{\"o2_pct\":%.1f}", d.oxygenLevel);
    mqttClient.publish(TOPIC_FAC_O2, o2Buf, true);

    // AQI
    char aqiBuf[64];
    snprintf(aqiBuf, sizeof(aqiBuf), "{\"aqi\":%d,\"status\":\"%s\"}",
             d.aqi, (d.aqi <= 50) ? "Good" : (d.aqi <= 100) ? "Moderate" : "Unhealthy");
    mqttClient.publish(TOPIC_FAC_AQI, aqiBuf, true);

    // Bed Angle
    int bedAngle = 0;
    BedPreset_t preset = BED_MODE_MANUAL;
    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        bedAngle = g_systemState.bed.currentAngle;
        preset   = g_systemState.bed.mode;
        xSemaphoreGive(xSharedStateMutex);
    }
    char bedBuf[64];
    snprintf(bedBuf, sizeof(bedBuf), "{\"angle\":%d,\"preset\":%d}", bedAngle, (int)preset);
    mqttClient.publish(TOPIC_FAC_BED, bedBuf, true);

    // Facility Alerts feed (including AQI)
    if (d.roomTemp > RTEMP_WARN_HIGH || d.humidity > HUM_WARN_HIGH ||
        d.oxygenLevel < O2_WARN_LOW  || d.aqi > AQI_WARN_HIGH) {
        char facAlert[192];
        snprintf(facAlert, sizeof(facAlert),
                 "{\"severity\":\"WARNING\",\"roomTemp\":%.1f,\"humidity\":%.1f,\"o2\":%.1f,\"aqi\":%d}",
                 d.roomTemp, d.humidity, d.oxygenLevel, d.aqi);
        mqttClient.publish(TOPIC_FAC_ALERTS, facAlert, false);
    }
}

static void publishStatusHeartbeat(const SensorData_t& d) {
    StaticJsonDocument<256> doc;
    doc["ts"]       = d.timestamp;

    ConnState_t cs = CONN_ONLINE;
    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        cs = g_systemState.connState;
        xSemaphoreGive(xSharedStateMutex);
    }
    doc["conn"]     = (cs == CONN_OFFLINE) ? "OFFLINE" :
                      (cs == CONN_DEGRADED) ? "DEGRADED" : "ONLINE";

    doc["hr"]       = d.heartRate;
    doc["spo2"]     = d.spo2;
    doc["status"]   = (d.overallStatus == STATUS_CRITICAL) ? "CRITICAL" :
                      (d.overallStatus == STATUS_WARNING)  ? "WARNING" : "NORMAL";

    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        doc["med_rate"]    = g_systemState.medication.currentRateMgHr;
        doc["med_dose"]    = serialized(String(g_systemState.medication.totalDeliveredMg, 2));
        doc["bed_angle"]   = g_systemState.bed.currentAngle;
        doc["sampling_ms"] = g_systemState.sampling.intervalMs;
        xSemaphoreGive(xSharedStateMutex);
    }

    char jsonBuf[256];
    serializeJson(doc, jsonBuf);
    mqttClient.publish(TOPIC_STATUS, jsonBuf, false);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Connection Helper
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t s_mqttBackoffMs       = 1000;
static uint32_t s_mqttNextRetryMs     = 0;
static bool     s_hadFirstMqttConnect = false;

static bool connectMQTT(void) {
    char clientId[32];
    uint64_t mac = ESP.getEfuseMac();
    snprintf(clientId, sizeof(clientId), "%s%08X",
             MQTT_CLIENT_ID_BASE, (uint32_t)(mac & 0xFFFFFFFF));

    Serial.printf("[MQTT] Connecting to %s:%d as '%s' ...\n",
                  MQTT_BROKER, MQTT_PORT, clientId);

    char lwtTopic[128];
    snprintf(lwtTopic, sizeof(lwtTopic), "%s/status", MQTT_TOPIC_PREFIX);

    bool ok = mqttClient.connect(clientId, nullptr, nullptr,
                                 lwtTopic, 0, true, "{\"state\":\"OFFLINE\"}");

    if (ok) {
        Serial.println("[MQTT] STATE CHANGE: Connected -> ONLINE");

        mqttClient.subscribe(TOPIC_CMD_SUB);
        Serial.printf("[MQTT] Subscribed to %s\n", TOPIC_CMD_SUB);

        mqttClient.publish(lwtTopic, "{\"state\":\"ONLINE\"}", true);

        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_systemState.mqttConnected = true;
            xSemaphoreGive(xSharedStateMutex);
        }
        updateConnState();

        // Trigger sync flush if buffer has records
        if (s_ringCount > 0 && xSyncRequestSem != nullptr) {
            xSemaphoreGive(xSyncRequestSem);
        }
    } else {
        Serial.printf("[MQTT] Connect failed, rc=%d -> DEGRADED\n", mqttClient.state());
        if (s_hadFirstMqttConnect) {
            alert_trigger(ALERT_DEGRADED_MQTT);
        }

        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_systemState.mqttConnected = false;
            xSemaphoreGive(xSharedStateMutex);
        }
        updateConnState();
    }
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
//  MQTT Task Body
// ─────────────────────────────────────────────────────────────────────────────
static void vMQTTTask(void* pvParams) {
    (void)pvParams;
    Serial.println("[MQTT] Task started");

    aggReset();
    s_lastAggMs = millis();

    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(onMqttMessage);
    mqttClient.setKeepAlive(MQTT_KEEPALIVE_SEC);
    mqttClient.setBufferSize(512);
    mqttClient.setSocketTimeout(5);

    uint32_t lastMedStatusPubMs = 0;
    static SimFailMode_t s_prevMqttSimMode = SIM_NONE;

    while (true) {
        xEventGroupSetBits(xTaskHealthBits, BIT_MQTT_ALIVE);

        // Update mqttConnected status and connection state every loop
        bool isConn = mqttClient.connected();
        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            g_systemState.mqttConnected = isConn;
            xSemaphoreGive(xSharedStateMutex);
        }
        updateConnState();

        // Check simulated failure
        SimFailMode_t simMode = SIM_NONE;
        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            simMode = g_systemState.simMode;
            xSemaphoreGive(xSharedStateMutex);
        }

        if (simMode == SIM_MQTT_DOWN) {
            if (mqttClient.connected()) {
                char lwtTopic[128];
                snprintf(lwtTopic, sizeof(lwtTopic), "%s/status", MQTT_TOPIC_PREFIX);
                mqttClient.publish(lwtTopic, "{\"state\":\"DEGRADED\"}", true);
                mqttClient.disconnect();
                Serial.println("[MQTT] SIM_MQTT_DOWN: Disconnecting -> DEGRADED");
            }
            if (s_prevMqttSimMode != SIM_MQTT_DOWN && s_hadFirstMqttConnect) {
                alert_trigger(ALERT_DEGRADED_MQTT);
            }
        } else if (simMode == SIM_WIFI_DOWN) {
            if (mqttClient.connected()) {
                char lwtTopic[128];
                snprintf(lwtTopic, sizeof(lwtTopic), "%s/status", MQTT_TOPIC_PREFIX);
                mqttClient.publish(lwtTopic, "{\"state\":\"OFFLINE\"}", true);
                mqttClient.disconnect();
                Serial.println("[MQTT] SIM_WIFI_DOWN: Disconnecting -> OFFLINE");
            }
        }
        s_prevMqttSimMode = simMode;

        // Non-blocking reconnect if Wi-Fi is ready and no simulated failure is active
        if (simMode == SIM_NONE && !mqttClient.connected() && WiFi.isConnected()) {
            if (millis() >= s_mqttNextRetryMs) {
                xEventGroupSetBits(xTaskHealthBits, BIT_MQTT_ALIVE);
                bool ok = connectMQTT();
                xEventGroupSetBits(xTaskHealthBits, BIT_MQTT_ALIVE);
                if (ok) {
                    s_mqttBackoffMs = 1000;
                    s_hadFirstMqttConnect = true;
                    s_mqttNextRetryMs = 0;
                } else {
                    s_mqttNextRetryMs = millis() + s_mqttBackoffMs;
                    s_mqttBackoffMs = (s_mqttBackoffMs * 2 > 32000) ? 32000 : s_mqttBackoffMs * 2;
                }
            }
        }

        if (mqttClient.connected()) {
            mqttClient.loop();
        }

        // Check for offline resync signal from WiFi reconnect
        if (!s_flushActive && xSyncRequestSem != nullptr) {
            if (xSemaphoreTake(xSyncRequestSem, 0) == pdTRUE) {
                if (s_ringCount > 0) {
                    s_flushActive      = true;
                    s_flushBackoffMs   = 1000;
                    s_flushNextRetryMs = 0;
                    s_syncedCount      = 0;
                    Serial.printf("[MQTT] Starting offline flush: %d records\n", s_ringCount);
                }
            }
        }

        // Run non-blocking offline flush batch
        if (s_flushActive && mqttClient.connected()) {
            if (millis() >= s_flushNextRetryMs) {
                bool done = flushNextBatch();
                if (done) {
                    StaticJsonDocument<128> summary;
                    summary["synced"]    = s_syncedCount;
                    summary["dropped"]   = s_dropsCount;
                    summary["remaining"] = s_ringCount;
                    char sumBuf[128];
                    serializeJson(summary, sumBuf);
                    mqttClient.publish(TOPIC_HISTORICAL, sumBuf, false);
                    s_flushActive = false;
                    s_dropsCount  = 0;
                    s_syncedCount = 0;
                    Serial.println("[MQTT] Offline flush complete");
                } else {
                    s_flushNextRetryMs = millis() + s_flushBackoffMs;
                    s_flushBackoffMs = (s_flushBackoffMs * 2 > 32000) ? 32000 : s_flushBackoffMs * 2;
                }
            }
        }

        // Process incoming sensor readings
        SensorData_t data;
        while (xQueueReceive(xSensorDataQueue, &data, 0) == pdTRUE) {
            aggAddSample(data);

            if (mqttClient.connected()) {
                publishMedicalDashboard(data);
                publishFacilityDashboard(data);
                publishStatusHeartbeat(data);

                if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    g_systemState.lastMqttPublishMs = millis();
                    xSemaphoreGive(xSharedStateMutex);
                }
            } else {
                ringBufferPush(data);
            }
        }

        // Periodic 5-second retained medication state publication
        if (mqttClient.connected() && (millis() - lastMedStatusPubMs >= 5000)) {
            lastMedStatusPubMs = millis();
            publishRetainedMedicationState();
        }

        // Check periodic aggregation timer (every 30 seconds)
        if (mqttClient.connected() && (millis() - s_lastAggMs >= AGGREGATION_INTERVAL_MS)) {
            s_lastAggMs = millis();
            publishAggregation();
        }

        vTaskDelay(pdMS_TO_TICKS(MQTT_LOOP_INTERVAL_MS));
    }
}

void mqtt_task_init(void) {
    xTaskCreatePinnedToCore(
        vMQTTTask,
        "MQTTTask",
        STACK_MQTT,
        nullptr,
        PRIORITY_MQTT,
        &xHandleMqtt,
        1
    );
}

bool mqtt_is_connected(void) {
    return mqttClient.connected();
}
