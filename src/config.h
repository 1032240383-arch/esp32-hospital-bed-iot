/**
 * config.h  -  Single source of truth for every tuneable parameter.
 *
 * Implements configuration for all 6 features:
 *   1. Multi-Sensor FreeRTOS Architecture
 *   2. Dual-Role MQTT Dashboards (Medical & Facility)
 *   3. Smart Medication Dosage Control
 *   4. Intelligent Remote Bed Elevation Control (Servo)
 *   5. Smart Dynamic Sampling Rate (Adaptive Monitoring)
 *   6. Advanced Offline Detection & Ring-Buffer Resync
 */

#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  WIFI
// ─────────────────────────────────────────────────────────────────────────────
#define WIFI_SSID               "Wokwi-GUEST"
#define WIFI_PASSWORD           ""
#define WIFI_RETRY_DELAY_MS     2000
#define WIFI_MAX_RETRIES        20
#define WIFI_BACKOFF_BASE_MS    1000
#define WIFI_BACKOFF_MAX_MS     32000

// ─────────────────────────────────────────────────────────────────────────────
//  MQTT
// ─────────────────────────────────────────────────────────────────────────────
#define MQTT_BROKER             "broker.hivemq.com"
#define MQTT_PORT               1883
#define MQTT_CLIENT_ID_BASE     "ESP32_HC_"
#define MQTT_TOPIC_PREFIX       "hospital_parth_bed01_2026/ward_A/bed_01"
#define MQTT_KEEPALIVE_SEC      60
#define MQTT_RECONNECT_DELAY_MS 3000

// Medical Staff Dashboard Topics
#define TOPIC_MED_HR            MQTT_TOPIC_PREFIX "/medical/heartrate"
#define TOPIC_MED_SPO2          MQTT_TOPIC_PREFIX "/medical/spo2"
#define TOPIC_MED_BTEMP         MQTT_TOPIC_PREFIX "/medical/bodytemp"
#define TOPIC_MED_BP            MQTT_TOPIC_PREFIX "/medical/bp"
#define TOPIC_MED_ECG           MQTT_TOPIC_PREFIX "/medical/ecg"
#define TOPIC_MED_ALERTS        MQTT_TOPIC_PREFIX "/medical/alerts"
#define TOPIC_MED_AGGREGATE     MQTT_TOPIC_PREFIX "/medical/aggregate"

// Facility Management Dashboard Topics
#define TOPIC_FAC_RTEMP         MQTT_TOPIC_PREFIX "/facility/roomtemp"
#define TOPIC_FAC_HUM           MQTT_TOPIC_PREFIX "/facility/humidity"
#define TOPIC_FAC_O2            MQTT_TOPIC_PREFIX "/facility/oxygen"
#define TOPIC_FAC_AQI           MQTT_TOPIC_PREFIX "/facility/aqi"
#define TOPIC_FAC_BED           MQTT_TOPIC_PREFIX "/facility/bed_angle"
#define TOPIC_FAC_ALERTS        MQTT_TOPIC_PREFIX "/facility/alerts"
#define TOPIC_FAC_AGGREGATE     MQTT_TOPIC_PREFIX "/facility/aggregate"

// Medication Status & System Topics
#define TOPIC_MEDICATION_STATUS MQTT_TOPIC_PREFIX "/medical/medication_status"
#define TOPIC_STATUS            MQTT_TOPIC_PREFIX "/status"
#define TOPIC_HISTORICAL        MQTT_TOPIC_PREFIX "/historical/sync"

// Broker -> ESP32 Command Topics
#define TOPIC_CMD_SUB           MQTT_TOPIC_PREFIX "/cmd/#"
#define TOPIC_CMD_MED_RATE      MQTT_TOPIC_PREFIX "/cmd/medication/rate"
#define TOPIC_CMD_MED_CONFIRM   MQTT_TOPIC_PREFIX "/cmd/medication/confirm"
#define TOPIC_CMD_BED_ANGLE     MQTT_TOPIC_PREFIX "/cmd/bed/angle"
#define TOPIC_CMD_BED_PRESET    MQTT_TOPIC_PREFIX "/cmd/bed/preset"
#define TOPIC_CMD_SAMPLING_RATE MQTT_TOPIC_PREFIX "/cmd/sampling/rate"
#define TOPIC_CMD_SAMPLING_MODE MQTT_TOPIC_PREFIX "/cmd/sampling/mode"

// ─────────────────────────────────────────────────────────────────────────────
//  HARDWARE PINS & PWM
// ─────────────────────────────────────────────────────────────────────────────
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22
#define PIN_DHT             4
#define DHT_TYPE            DHT22
#define PIN_POT_HR          34
#define PIN_POT_SPO2        35
#define PIN_BTN_ABNORMAL    13
#define PIN_BTN_SIM_FAIL    12
#define PIN_LED_ALERT       2
#define PIN_BUZZER          25
#define PIN_SERVO_BED       18
#define SERVO_LEDC_CHANNEL  2   // Channel 2 uses Timer 1 (isolated from tone() on Timer 0)

// ─────────────────────────────────────────────────────────────────────────────
//  OLED (SSD1306 128x64, I2C)
// ─────────────────────────────────────────────────────────────────────────────
#define OLED_WIDTH          128
#define OLED_HEIGHT         64
#define OLED_I2C_ADDR       0x3C
#define OLED_RESET_PIN      -1

// ─────────────────────────────────────────────────────────────────────────────
//  VITAL-SIGN & FACILITY THRESHOLDS
// ─────────────────────────────────────────────────────────────────────────────
#define HR_WARN_LOW         50.0f
#define HR_WARN_HIGH        120.0f
#define HR_CRIT_LOW         40.0f
#define HR_CRIT_HIGH        150.0f

#define SPO2_WARN_LOW       94.0f
#define SPO2_CRIT_LOW       90.0f

#define BTEMP_WARN_HIGH     38.0f
#define BTEMP_CRIT_HIGH     39.5f

#define RTEMP_WARN_HIGH     30.0f
#define HUM_WARN_HIGH       70.0f
#define O2_WARN_LOW         19.5f
#define AQI_WARN_HIGH       100.0f

// ─────────────────────────────────────────────────────────────────────────────
//  MEDICATION DOSAGE SAFETY PARAMETERS
// ─────────────────────────────────────────────────────────────────────────────
#define MED_RATE_MIN        0.0f
#define MED_RATE_MAX        100.0f
#define MED_RATE_WARN       50.0f
#define MED_RATE_CRIT       80.0f
#define MED_MAX_STEP_JUMP   20.0f

// ─────────────────────────────────────────────────────────────────────────────
//  BED ELEVATION PRESETS (degrees 0 - 90)
// ─────────────────────────────────────────────────────────────────────────────
#define BED_ANGLE_MIN       0
#define BED_ANGLE_MAX       90
#define BED_PRESET_SLEEP    10
#define BED_PRESET_EAT      30
#define BED_PRESET_BREATHE  45
#define BED_PRESET_EMERGENCY 90  // Emergency upright breathing support (90 deg)

// ─────────────────────────────────────────────────────────────────────────────
//  FREERTOS TASK CONFIGURATION
//  Priority architecture:
//    5: Supervisor (watchdog feeder & health arbiter)
//    4: Alert & Medication (safety critical alarms & precise drug dosage integral)
//    3: Bed, Sensor, WiFi, MQTT (control and communication core)
//    2: Env, BP, ECG, Display (auxiliary telemetry and UI presentation)
// ─────────────────────────────────────────────────────────────────────────────
#define PRIORITY_SUPERVISOR 5
#define PRIORITY_ALERT      4
#define PRIORITY_MEDICATION 4
#define PRIORITY_BED        3
#define PRIORITY_WIFI       3
#define PRIORITY_SENSOR     3
#define PRIORITY_MQTT       3
#define PRIORITY_ENV        2
#define PRIORITY_BP         2
#define PRIORITY_ECG        2
#define PRIORITY_DISPLAY    2

#define STACK_SUPERVISOR    2048
#define STACK_ALERT         2048
#define STACK_MEDICATION    2048
#define STACK_BED           2048
#define STACK_WIFI          4096
#define STACK_SENSOR        4096
#define STACK_MQTT          8192
#define STACK_DISPLAY       4096
#define STACK_ENV           3072
#define STACK_BP            2048
#define STACK_ECG           2048

// ─────────────────────────────────────────────────────────────────────────────
//  TIMING & CAPACITIES
// ─────────────────────────────────────────────────────────────────────────────
#define SENSOR_READ_AUTO_ABNORMAL_MS 5000
#define SENSOR_READ_AUTO_STABLE_MS  15000
#define SENSOR_READ_MAX_MS          60000

#define MQTT_LOOP_INTERVAL_MS          50
#define DISPLAY_REFRESH_INTERVAL_MS   500
#define SUPERVISOR_CHECK_INTERVAL_MS 10000
#define WATCHDOG_TIMEOUT_SEC           30

#define AGGREGATION_INTERVAL_MS     30000
#define OFFLINE_BUFFER_CAPACITY        30
#define SENSOR_QUEUE_DEPTH              5
#define DISPLAY_QUEUE_DEPTH            10
#define SAMPLING_QUEUE_DEPTH            4

// ─────────────────────────────────────────────────────────────────────────────
//  EVENT-GROUP BITS (task health heartbeat)
// ─────────────────────────────────────────────────────────────────────────────
#define BIT_SENSOR_ALIVE    (1 << 0)
#define BIT_DISPLAY_ALIVE   (1 << 1)
#define BIT_MQTT_ALIVE      (1 << 2)
#define BIT_WIFI_ALIVE      (1 << 3)
#define BIT_ALERT_ALIVE     (1 << 4)
#define BIT_BED_ALIVE       (1 << 5)
#define BIT_ENV_ALIVE       (1 << 6)
#define BIT_BP_ALIVE        (1 << 7)
#define BIT_ECG_ALIVE       (1 << 8)

#define ALL_TASK_ALIVE_BITS (BIT_SENSOR_ALIVE  | BIT_DISPLAY_ALIVE | \
                             BIT_MQTT_ALIVE    | BIT_WIFI_ALIVE    | \
                             BIT_ALERT_ALIVE   | BIT_BED_ALIVE     | \
                             BIT_ENV_ALIVE     | BIT_BP_ALIVE      | \
                             BIT_ECG_ALIVE)

// ─────────────────────────────────────────────────────────────────────────────
//  SIMULATION DEFAULTS (Wokwi)
// ─────────────────────────────────────────────────────────────────────────────
#define SIM_ABNORMAL_HR     155.0f
#define SIM_ABNORMAL_SPO2    88.0f
#define SIM_ABNORMAL_BTEMP   39.8f
#define SIM_BTEMP_BASE       36.8f
#define SIM_BTEMP_HR_SCALE   0.015f
