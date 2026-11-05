/**
 * shared_types.h  -  Data structures and extern handles shared across FreeRTOS tasks.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

// ─────────────────────────────────────────────────────────────────────────────
//  ENUMS & STATUS CODES
// ─────────────────────────────────────────────────────────────────────────────
typedef enum {
    STATUS_NORMAL   = 0,
    STATUS_WARNING  = 1,
    STATUS_CRITICAL = 2
} VitalStatus_t;

typedef enum {
    CONN_ONLINE   = 0,   // Wi-Fi + MQTT both healthy
    CONN_DEGRADED = 1,   // Wi-Fi connected, MQTT lost or degraded health
    CONN_OFFLINE  = 2    // Wi-Fi disconnected (or simulated failure)
} ConnState_t;

typedef enum {
    ALERT_NONE = 0,
    ALERT_CHIRP,            // Single short beep (rate/angle change)
    ALERT_WARN_VITALS,      // Warning pattern
    ALERT_CRIT_VITALS,      // Critical vitals alarm (pulsed buzzer + LED)
    ALERT_MED_CRIT,         // Rapid LED blink + critical dosage alarm
    ALERT_OFFLINE_WIFI,     // 3 long beeps for Wi-Fi loss
    ALERT_DEGRADED_MQTT     // 2 short beeps for MQTT broker loss
} AlertPattern_t;

typedef enum {
    BED_MODE_MANUAL    = 0,
    BED_MODE_SLEEP     = 1,  // 10 deg
    BED_MODE_EAT       = 2,  // 30 deg
    BED_MODE_BREATHE   = 3,  // 45 deg
    BED_MODE_EMERGENCY = 4   // 90 deg EMERGENCY upright
} BedPreset_t;

typedef enum {
    SIM_NONE      = 0,  // Normal operation
    SIM_WIFI_DOWN = 1,  // Wi-Fi disconnected -> OFFLINE
    SIM_MQTT_DOWN = 2   // Wi-Fi up, MQTT held down -> DEGRADED
} SimFailMode_t;

// ─────────────────────────────────────────────────────────────────────────────
//  TASK COMM STRUCTS
// ─────────────────────────────────────────────────────────────────────────────

// Sensor data produced by SensorTask -> MQTTTask & DisplayTask
typedef struct {
    float heartRate;           // bpm
    float spo2;                // %
    float bodyTemp;            // deg C
    int   bpSys;               // mmHg (systolic)
    int   bpDia;               // mmHg (diastolic)
    char  ecgStatus[16];       // "Sinus", "Tachycardia", "Arrhythmia"
    float roomTemp;            // deg C (DHT22)
    float humidity;            // % (DHT22)
    float oxygenLevel;         // % (Room O2, baseline 20.9%)
    int   aqi;                 // Air Quality Index (baseline 42)
    bool  forceAbnormal;       // Sim button held
    uint32_t timestamp;        // millis()
    VitalStatus_t hrStatus;
    VitalStatus_t spo2Status;
    VitalStatus_t btempStatus;
    VitalStatus_t overallStatus;
} SensorData_t;

// Medication State (0-100 mg/hr with safety threshold >80 mg/hr & rate limiting)
typedef struct {
    float currentRateMgHr;
    float pendingRateMgHr;
    bool  requiresConfirmation;
    float totalDeliveredMg;    // running integral
    uint32_t lastIntegralMs;
    VitalStatus_t safetyLevel;
} MedicationState_t;

// Bed Elevation State (0-90 deg servo)
typedef struct {
    int currentAngle;
    int targetAngle;
    BedPreset_t mode;
    bool isMoving;
} BedState_t;

// Bed command message
typedef struct {
    int targetAngle;
    BedPreset_t preset;
    bool isPreset;
} BedCmd_t;

// Dynamic Sampling Rate State
typedef struct {
    uint32_t intervalMs;
    bool isAutoMode;
} SamplingState_t;

// Sampling command (sent via xSamplingCmdQueue)
typedef enum {
    SAMPLING_CMD_SET_INTERVAL = 0,
    SAMPLING_CMD_SET_MODE     = 1
} SamplingCmdType_t;

typedef struct {
    SamplingCmdType_t type;
    uint32_t value; // intervalMs for SET_INTERVAL, 1=auto 0=manual for SET_MODE
} SamplingCmd_t;

// Display Message
typedef enum {
    DISP_SENSOR_UPDATE = 0,
    DISP_ALERT         = 1,
    DISP_STATUS        = 2
} DisplayMsgType_t;

typedef struct {
    DisplayMsgType_t type;
    char line1[22];
    char line2[22];
    char line3[22];
    char line4[22];
    char line5[22];
    uint32_t durationMs;
} DisplayMessage_t;

// Offline buffered record
typedef struct {
    uint32_t timestamp;
    SensorData_t data;
} OfflineRecord_t;

// ─────────────────────────────────────────────────────────────────────────────
//  GLOBAL SYSTEM STATE (protected by xSharedStateMutex)
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    ConnState_t       connState;
    bool              wifiConnected;
    bool              mqttConnected;
    SimFailMode_t     simMode;
    uint32_t          lastMqttPublishMs;
    SensorData_t      lastReading;
    MedicationState_t medication;
    BedState_t        bed;
    SamplingState_t   sampling;
    uint8_t           offlineBufferCount;
    bool              systemHealthy;
    char              lastHealthReason[48];
} SystemState_t;

// ─────────────────────────────────────────────────────────────────────────────
//  EXTERN HANDLES
// ─────────────────────────────────────────────────────────────────────────────
extern QueueHandle_t      xSensorDataQueue;
extern QueueHandle_t      xDisplayMsgQueue;
extern QueueHandle_t      xAlertQueue;
extern QueueHandle_t      xBedCmdQueue;
extern QueueHandle_t      xSamplingCmdQueue;
extern SemaphoreHandle_t  xSyncRequestSem;
extern SemaphoreHandle_t  xI2CMutex;
extern SemaphoreHandle_t  xSharedStateMutex;
extern EventGroupHandle_t xTaskHealthBits;
extern SystemState_t      g_systemState;

// Optional task handles for supervisor stack high-water mark monitoring
extern TaskHandle_t xHandleSupervisor;
extern TaskHandle_t xHandleAlert;
extern TaskHandle_t xHandleBed;
extern TaskHandle_t xHandleMedication;
extern TaskHandle_t xHandleDisplay;
extern TaskHandle_t xHandleSensor;
extern TaskHandle_t xHandleWifi;
extern TaskHandle_t xHandleMqtt;
extern TaskHandle_t xHandleEnv;
extern TaskHandle_t xHandleBp;
extern TaskHandle_t xHandleEcg;
