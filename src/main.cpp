/**
 * main.cpp  -  ESP32 Smart Healthcare IoT Monitor.
 *
 * Implements FreeRTOS Concurrent System Architecture:
 *   1. SupervisorTask     (Priority 5) - Hardware Watchdog (TWDT) feeder & task health monitor
 *   2. AlertTask          (Priority 4) - Non-blocking LED strobes & passive buzzer frequencies
 *   3. MedicationTask     (Priority 4) - 1-second numerical integration of delivered drug dose
 *   4. BedControlTask     (Priority 3) - Smooth PWM servo stepping & preset controller
 *   5. SensorTask         (Priority 3) - Acquires ADC pots, vitals classification, adaptive rate
 *   6. WiFiTask           (Priority 3) - STA connection, exponential backoff (1s->32s), failure detection
 *   7. MQTTTask           (Priority 3) - Dual-role publisher, command receiver, offline ring buffer
 *   8. EnvTask            (Priority 2) - DHT22 room temperature/humidity & O2/AQI bounded drift
 *   9. BpTask             (Priority 2) - Blood pressure simulation derived from heart rate
 *  10. EcgTask            (Priority 2) - ECG rhythm classification
 *  11. DisplayTask        (Priority 2) - SSD1306 OLED rendering, multi-page rotation, alerts overlay
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

#include "config.h"
#include "shared_types.h"
#include "alert_task.h"
#include "bed_task.h"
#include "medication_task.h"
#include "sensor_task.h"
#include "display_task.h"
#include "wifi_manager.h"
#include "mqtt_task.h"
#include "supervisor_task.h"
#include "env_task.h"
#include "bp_task.h"
#include "ecg_task.h"
#include "conn_state.h"

// ─────────────────────────────────────────────────────────────────────────────
//  GLOBAL FREERTOS HANDLES (declared extern in shared_types.h)
// ─────────────────────────────────────────────────────────────────────────────
QueueHandle_t      xSensorDataQueue  = nullptr;
QueueHandle_t      xDisplayMsgQueue  = nullptr;
QueueHandle_t      xAlertQueue       = nullptr;
QueueHandle_t      xBedCmdQueue      = nullptr;
QueueHandle_t      xSamplingCmdQueue = nullptr;

SemaphoreHandle_t  xSyncRequestSem   = nullptr;
SemaphoreHandle_t  xI2CMutex         = nullptr;
SemaphoreHandle_t  xSharedStateMutex = nullptr;
EventGroupHandle_t xTaskHealthBits   = nullptr;

// Shared system state (protected by xSharedStateMutex)
SystemState_t g_systemState = {
    .connState         = CONN_OFFLINE,
    .wifiConnected     = false,
    .mqttConnected     = false,
    .simMode           = SIM_NONE,
    .lastMqttPublishMs = 0,
    .lastReading       = {},
    .medication        = {
        .currentRateMgHr      = 0.0f,
        .pendingRateMgHr      = 0.0f,
        .requiresConfirmation = false,
        .totalDeliveredMg     = 0.0f,
        .lastIntegralMs       = 0,
        .safetyLevel          = STATUS_NORMAL
    },
    .bed               = {
        .currentAngle = 0,
        .targetAngle  = 0,
        .mode         = BED_MODE_MANUAL,  // Boot in MANUAL at 0 degrees
        .isMoving     = false
    },
    .sampling          = {
        .intervalMs = 15000,
        .isAutoMode = true
    },
    .offlineBufferCount= 0,
    .systemHealthy     = true,
    .lastHealthReason  = {'O', 'K', '\0'}
};

// ─────────────────────────────────────────────────────────────────────────────
//  Simulate failure button toggle (3-mode cycle) with 50ms debounce
//  Cycle: SIM_NONE -> SIM_WIFI_DOWN (OFFLINE) -> SIM_MQTT_DOWN (DEGRADED) -> SIM_NONE
// ─────────────────────────────────────────────────────────────────────────────
static bool          s_btnLastState   = HIGH;
static SimFailMode_t s_simMode        = SIM_NONE;
static uint32_t      s_lastDebounceMs = 0;

static const char* simModeName(SimFailMode_t m) {
    switch (m) {
        case SIM_WIFI_DOWN: return "SIM_WIFI_DOWN (-> OFFLINE)";
        case SIM_MQTT_DOWN: return "SIM_MQTT_DOWN (-> DEGRADED)";
        default:            return "SIM_NONE (-> ONLINE)";
    }
}

static void pollSimFailButton(void) {
    bool btnNow = digitalRead(PIN_BTN_SIM_FAIL);

    if (s_btnLastState == HIGH && btnNow == LOW) {
        if (millis() - s_lastDebounceMs >= 50) {
            s_lastDebounceMs = millis();
            switch (s_simMode) {
                case SIM_NONE:      s_simMode = SIM_WIFI_DOWN; break;
                case SIM_WIFI_DOWN: s_simMode = SIM_MQTT_DOWN; break;
                default:            s_simMode = SIM_NONE;      break;
            }

            if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_systemState.simMode = s_simMode;
                xSemaphoreGive(xSharedStateMutex);
            }

            updateConnState();

            Serial.printf("[Main] Sim-fail button -> %s\n", simModeName(s_simMode));
        }
    }
    s_btnLastState = btnNow;
}

// ─────────────────────────────────────────────────────────────────────────────
//  setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup(void) {
    Serial.begin(115200);
    delay(400);

    Serial.println("\n=======================================================");
    Serial.println("  ESP32 Smart Healthcare IoT Monitor - Final Firmware");
    Serial.println("=======================================================");
    Serial.printf("  Broker Prefix : %s\n", MQTT_TOPIC_PREFIX);
    Serial.printf("  Free Heap     : %u bytes\n", ESP.getFreeHeap());
    Serial.println("-------------------------------------------------------");

    // Configure simulate-failure button pin
    pinMode(PIN_BTN_SIM_FAIL, INPUT_PULLUP);

    // Create FreeRTOS Queues
    xSensorDataQueue  = xQueueCreate(SENSOR_QUEUE_DEPTH, sizeof(SensorData_t));
    xDisplayMsgQueue  = xQueueCreate(DISPLAY_QUEUE_DEPTH, sizeof(DisplayMessage_t));
    xAlertQueue       = xQueueCreate(1, sizeof(AlertPattern_t)); // mailbox
    xBedCmdQueue      = xQueueCreate(5, sizeof(BedCmd_t));
    xSamplingCmdQueue = xQueueCreate(SAMPLING_QUEUE_DEPTH, sizeof(SamplingCmd_t));

    // Create Binary Semaphore for offline flush signaling
    xSyncRequestSem   = xSemaphoreCreateBinary();

    // Create Mutexes
    xI2CMutex         = xSemaphoreCreateMutex();
    xSharedStateMutex = xSemaphoreCreateMutex();

    // Create Event Group
    xTaskHealthBits   = xEventGroupCreate();

    if (!xSensorDataQueue || !xDisplayMsgQueue || !xAlertQueue || !xBedCmdQueue ||
        !xSamplingCmdQueue || !xSyncRequestSem ||
        !xI2CMutex || !xSharedStateMutex || !xTaskHealthBits) {
        Serial.println("[FATAL] FreeRTOS resource allocation failed! System halted.");
        while (true) { delay(1000); }
    }

    Serial.println("[Main] FreeRTOS queues, semaphores & mutexes initialized successfully");

    // Initialize all modular tasks in priority order
    alert_task_init();       // Priority 4 - LED & Buzzer alarms
    medication_task_init();  // Priority 4 - Infusion rate & dosage integral
    bed_task_init();         // Priority 3 - Smooth Servo Bed control
    sensor_task_init();      // Priority 3 - Vitals & Adaptive sampling
    wifi_manager_init();     // Priority 3 - Wi-Fi & Exponential backoff
    mqtt_task_init();        // Priority 3 - Dual Dashboards, Commands & Offline Buffer
    env_task_init();         // Priority 2 - DHT22 + O2/AQI drift
    bp_task_init();          // Priority 2 - Blood pressure simulation
    ecg_task_init();         // Priority 2 - ECG rhythm classification
    display_task_init();     // Priority 2 - SSD1306 Multi-page OLED
    supervisor_task_init();  // Priority 5 - Watchdog supervisor (created last)

    Serial.println("[Main] All 11 FreeRTOS tasks initialized and running!");
    Serial.printf("[Main] Free heap after init: %u bytes\n", ESP.getFreeHeap());
}

// ─────────────────────────────────────────────────────────────────────────────
//  loop()
// ─────────────────────────────────────────────────────────────────────────────
void loop(void) {
    pollSimFailButton();
    delay(40);
}
