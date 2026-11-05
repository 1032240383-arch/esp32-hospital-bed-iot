/**
 * alert_task.cpp  -  Dedicated task for non-blocking LED and buzzer alarms.
 */

#include <Arduino.h>
#include "alert_task.h"
#include "config.h"
#include "shared_types.h"

TaskHandle_t xHandleAlert = nullptr;
static AlertPattern_t currentPattern = ALERT_NONE;
static uint32_t s_medCritLastTriggerMs = 0;

static int alertPriority(AlertPattern_t p) {
    switch (p) {
        case ALERT_MED_CRIT:      return 5;
        case ALERT_CRIT_VITALS:   return 4;
        case ALERT_OFFLINE_WIFI:  return 3;
        case ALERT_DEGRADED_MQTT: return 2;
        case ALERT_WARN_VITALS:   return 1;
        case ALERT_CHIRP:         return 0;
        default:                  return -1; // ALERT_NONE
    }
}

void alert_trigger(AlertPattern_t pattern) {
    if (xAlertQueue == nullptr) return;

    if (pattern == ALERT_MED_CRIT) {
        s_medCritLastTriggerMs = millis();
    }

    // ALERT_NONE never overwrites a pending non-NONE alert
    AlertPattern_t current = ALERT_NONE;
    xQueuePeek(xAlertQueue, &current, 0);
    if (alertPriority(pattern) <= alertPriority(current)) return;

    // Higher priority: overwrite
    xQueueOverwrite(xAlertQueue, &pattern);
}

static void playBeep(uint16_t freq, uint16_t durationMs) {
    tone(PIN_BUZZER, freq, durationMs);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    noTone(PIN_BUZZER);
}

static void vAlertTask(void* pvParams) {
    (void)pvParams;

    pinMode(PIN_LED_ALERT, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_LED_ALERT, LOW);
    noTone(PIN_BUZZER);

    Serial.println("[Alert] Task started");

    while (true) {
        xEventGroupSetBits(xTaskHealthBits, BIT_ALERT_ALIVE);

        // Check if a new alert arrived
        AlertPattern_t newPattern;
        if (xQueueReceive(xAlertQueue, &newPattern, 0) == pdTRUE) {
            currentPattern = newPattern;
        }

        switch (currentPattern) {
            case ALERT_CHIRP:
                playBeep(2400, 60);
                currentPattern = ALERT_NONE;
                break;

            case ALERT_WARN_VITALS:
                digitalWrite(PIN_LED_ALERT, HIGH);
                playBeep(1500, 150);
                digitalWrite(PIN_LED_ALERT, LOW);
                vTaskDelay(pdMS_TO_TICKS(150));
                currentPattern = ALERT_NONE;
                break;

            case ALERT_CRIT_VITALS:
                // Alarm pattern: dual tone pulse + LED flash
                digitalWrite(PIN_LED_ALERT, HIGH);
                playBeep(2400, 100);
                digitalWrite(PIN_LED_ALERT, LOW);
                vTaskDelay(pdMS_TO_TICKS(50));
                digitalWrite(PIN_LED_ALERT, HIGH);
                playBeep(1800, 100);
                digitalWrite(PIN_LED_ALERT, LOW);
                noTone(PIN_BUZZER);
                currentPattern = ALERT_NONE;
                break;

            case ALERT_MED_CRIT: {
                // Medication > 80 mg/hr safety alarm: rapid 4-blink/s pattern with latching
                for (int b = 0; b < 2; b++) {
                    xEventGroupSetBits(xTaskHealthBits, BIT_ALERT_ALIVE);
                    digitalWrite(PIN_LED_ALERT, HIGH);
                    tone(PIN_BUZZER, 3000, 80);
                    vTaskDelay(pdMS_TO_TICKS(80));
                    xEventGroupSetBits(xTaskHealthBits, BIT_ALERT_ALIVE);
                    digitalWrite(PIN_LED_ALERT, LOW);
                    noTone(PIN_BUZZER);
                    vTaskDelay(pdMS_TO_TICKS(120));
                }
                noTone(PIN_BUZZER);

                // Latch: if re-triggered or last trigger was within 1500ms, stay in MED_CRIT
                AlertPattern_t peek = ALERT_NONE;
                if (xQueuePeek(xAlertQueue, &peek, 0) == pdTRUE && peek == ALERT_MED_CRIT) {
                    xQueueReceive(xAlertQueue, &peek, 0);
                    currentPattern = ALERT_MED_CRIT;
                } else if (millis() - s_medCritLastTriggerMs < 1500) {
                    currentPattern = ALERT_MED_CRIT;
                } else {
                    currentPattern = ALERT_NONE;
                }
                break;
            }

            case ALERT_OFFLINE_WIFI:
                // 3 long beeps for Wi-Fi disconnection (with heartbeats)
                for (int i = 0; i < 3; i++) {
                    xEventGroupSetBits(xTaskHealthBits, BIT_ALERT_ALIVE);
                    digitalWrite(PIN_LED_ALERT, HIGH);
                    tone(PIN_BUZZER, 1000, 300);
                    vTaskDelay(pdMS_TO_TICKS(300));
                    xEventGroupSetBits(xTaskHealthBits, BIT_ALERT_ALIVE);
                    noTone(PIN_BUZZER);
                    digitalWrite(PIN_LED_ALERT, LOW);
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
                currentPattern = ALERT_NONE;
                break;

            case ALERT_DEGRADED_MQTT:
                // 2 short beeps for MQTT loss (with heartbeats)
                for (int i = 0; i < 2; i++) {
                    xEventGroupSetBits(xTaskHealthBits, BIT_ALERT_ALIVE);
                    digitalWrite(PIN_LED_ALERT, HIGH);
                    tone(PIN_BUZZER, 1800, 120);
                    vTaskDelay(pdMS_TO_TICKS(120));
                    xEventGroupSetBits(xTaskHealthBits, BIT_ALERT_ALIVE);
                    noTone(PIN_BUZZER);
                    digitalWrite(PIN_LED_ALERT, LOW);
                    vTaskDelay(pdMS_TO_TICKS(120));
                }
                currentPattern = ALERT_NONE;
                break;

            case ALERT_NONE:
            default:
                digitalWrite(PIN_LED_ALERT, LOW);
                noTone(PIN_BUZZER);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
    }
}

void alert_task_init(void) {
    xTaskCreatePinnedToCore(
        vAlertTask,
        "AlertTask",
        STACK_ALERT,
        nullptr,
        PRIORITY_ALERT,
        &xHandleAlert,
        1
    );
}
