/**
 * bed_task.cpp  -  Intelligent Bed Elevation Control (Servo 0-90 deg).
 *
 * Smoothly transitions servo between angles to ensure patient safety and comfort.
 * Supports manual angle adjustment, presets (Sleep 10, Eat 30, Breathe 45, Emergency 90),
 * and automatic elevation triggered by vital signs distress.
 */

#include <Arduino.h>
#include <ESP32Servo.h>
#include "bed_task.h"
#include "alert_task.h"
#include "config.h"
#include "shared_types.h"

TaskHandle_t xHandleBed = nullptr;
static Servo s_bedServo;

static void applyServoAngle(int angle) {
    if (angle < BED_ANGLE_MIN) angle = BED_ANGLE_MIN;
    if (angle > BED_ANGLE_MAX) angle = BED_ANGLE_MAX;

    s_bedServo.write(angle);
}

bool bed_set_angle(int angle) {
    if (angle < BED_ANGLE_MIN || angle > BED_ANGLE_MAX) return false;
    BedCmd_t cmd;
    cmd.targetAngle = angle;
    cmd.preset = BED_MODE_MANUAL;
    cmd.isPreset = false;
    return (xQueueSend(xBedCmdQueue, &cmd, pdMS_TO_TICKS(100)) == pdPASS);
}

bool bed_set_preset(BedPreset_t preset) {
    int target = 0;
    switch (preset) {
        case BED_MODE_SLEEP:     target = BED_PRESET_SLEEP; break;
        case BED_MODE_EAT:       target = BED_PRESET_EAT; break;
        case BED_MODE_BREATHE:   target = BED_PRESET_BREATHE; break;
        case BED_MODE_EMERGENCY: target = BED_PRESET_EMERGENCY; break;
        default: return false;
    }
    BedCmd_t cmd;
    cmd.targetAngle = target;
    cmd.preset = preset;
    cmd.isPreset = true;
    return (xQueueSend(xBedCmdQueue, &cmd, pdMS_TO_TICKS(100)) == pdPASS);
}

static void vBedControlTask(void* pvParams) {
    (void)pvParams;

    // Allocate hardware PWM timers for ESP32Servo (Timers 1-3, leaving Timer 0 for tone)
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    // Initialize ESP32Servo on GPIO 18 (standard 50Hz, 500-2400us)
    s_bedServo.setPeriodHertz(50);
    int channel = s_bedServo.attach(PIN_SERVO_BED, 500, 2400);
    Serial.printf("[Bed] Servo attached on GPIO %d (Channel %d)\n", PIN_SERVO_BED, channel);

    int currentAngle = 0;
    applyServoAngle(currentAngle);

    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_systemState.bed.currentAngle = currentAngle;
        g_systemState.bed.targetAngle = currentAngle;
        g_systemState.bed.mode = BED_MODE_MANUAL; // Boot in MANUAL at 0 deg
        g_systemState.bed.isMoving = false;
        xSemaphoreGive(xSharedStateMutex);
    }

    Serial.println("[Bed] Task started - Servo initialized at 0 deg (MANUAL)");

    while (true) {
        xEventGroupSetBits(xTaskHealthBits, BIT_BED_ALIVE);

        BedCmd_t cmd;
        if (xQueueReceive(xBedCmdQueue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
            int target = cmd.targetAngle;
            if (target < BED_ANGLE_MIN) target = BED_ANGLE_MIN;
            if (target > BED_ANGLE_MAX) target = BED_ANGLE_MAX;

            if (target != currentAngle) {
                Serial.printf("[Bed] Transitioning angle: %d deg -> %d deg (Preset: %d)\n",
                              currentAngle, target, (int)cmd.preset);
                alert_trigger(ALERT_CHIRP);

                if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    g_systemState.bed.targetAngle = target;
                    g_systemState.bed.mode = cmd.preset;
                    g_systemState.bed.isMoving = true;
                    xSemaphoreGive(xSharedStateMutex);
                }

                // Smooth sweep transition: step 1 deg every 25ms (max speed constraint)
                bool isEmergency = (cmd.preset == BED_MODE_EMERGENCY);
                while (true) {
                    xEventGroupSetBits(xTaskHealthBits, BIT_BED_ALIVE);

                    if (currentAngle == target) {
                        break; // Reached target before stepping
                    }

                    // Direction is recomputed every iteration
                    int step = (target > currentAngle) ? 1 : -1;

                    // Peek for incoming commands mid-sweep
                    BedCmd_t peek;
                    if (xQueuePeek(xBedCmdQueue, &peek, 0) == pdTRUE) {
                        if (peek.isPreset && peek.preset == BED_MODE_EMERGENCY) {
                            // EMERGENCY always preempts any sweep
                            Serial.println("[Bed] EMERGENCY command preempts current sweep!");
                            break;
                        } else if (!isEmergency) {
                            // Non-emergency retarget allowed only if current sweep is not EMERGENCY
                            BedCmd_t newCmd;
                            if (xQueueReceive(xBedCmdQueue, &newCmd, 0) == pdTRUE) {
                                target = newCmd.targetAngle;
                                if (target < BED_ANGLE_MIN) target = BED_ANGLE_MIN;
                                if (target > BED_ANGLE_MAX) target = BED_ANGLE_MAX;
                                cmd = newCmd;
                                isEmergency = (newCmd.preset == BED_MODE_EMERGENCY);
                                if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                                    g_systemState.bed.targetAngle = target;
                                    g_systemState.bed.mode = newCmd.preset;
                                    xSemaphoreGive(xSharedStateMutex);
                                }
                                Serial.printf("[Bed] Retargeting mid-sweep to %d deg\n", target);
                                continue;
                            }
                        }
                        // If current sweep is EMERGENCY and peek is not EMERGENCY: leave it in queue
                    }

                    currentAngle += step;

                    // Last-resort clamp guard
                    if (currentAngle < BED_ANGLE_MIN) {
                        Serial.println("[Bed] CLAMP GUARD: min triggered!");
                        currentAngle = BED_ANGLE_MIN;
                    }
                    if (currentAngle > BED_ANGLE_MAX) {
                        Serial.println("[Bed] CLAMP GUARD: max triggered!");
                        currentAngle = BED_ANGLE_MAX;
                    }

                    applyServoAngle(currentAngle);

                    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                        g_systemState.bed.currentAngle = currentAngle;
                        xSemaphoreGive(xSharedStateMutex);
                    }
                    vTaskDelay(pdMS_TO_TICKS(25));
                }

                if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    g_systemState.bed.currentAngle = currentAngle;
                    g_systemState.bed.isMoving = false;
                    xSemaphoreGive(xSharedStateMutex);
                }

                Serial.printf("[Bed] Position reached: %d deg\n", currentAngle);
            }
        }
    }
}

void bed_task_init(void) {
    xTaskCreatePinnedToCore(
        vBedControlTask,
        "BedControlTask",
        STACK_BED,
        nullptr,
        PRIORITY_BED,
        &xHandleBed,
        1
    );
}
