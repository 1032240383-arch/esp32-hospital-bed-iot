/**
 * medication_task.cpp  -  Smart Medication Dosage Control System.
 *
 * Implements:
 *   - 0-100 mg/hr dosage adjustment
 *   - Running integral (total delivered dose in mg) computed every 1 second
 *   - Rate limiting: clamps sudden jumps > 20 mg/hr (applies current + 20)
 *   - High-dose safety (>80 mg/hr requires confirmation and activates rapid alarms)
 *   - Auto-cancellation of pending high-dose requests after 30 seconds
 *   - Buzzer chirp on valid rate updates
 */

#include <Arduino.h>
#include <math.h>
#include "medication_task.h"
#include "alert_task.h"
#include "config.h"
#include "shared_types.h"

TaskHandle_t xHandleMedication = nullptr;

#define MED_CONFIRM_TIMEOUT_MS 30000
static uint32_t s_pendingRequestMs = 0;

bool medication_request_rate(float rate, char* outReason, size_t reasonSize) {
    // 1. Range check [0, 100]
    if (rate < MED_RATE_MIN || rate > MED_RATE_MAX) {
        snprintf(outReason, reasonSize, "REJECTED_OUT_OF_RANGE: %.1f not in [%.0f,%.0f]",
                 rate, MED_RATE_MIN, MED_RATE_MAX);
        return false;
    }

    float current = 0.0f;
    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        current = g_systemState.medication.currentRateMgHr;
        xSemaphoreGive(xSharedStateMutex);
    }

    // 2. High dosage confirmation check (>80 mg/hr requires explicit confirmation)
    if (rate > MED_RATE_CRIT) {
        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_systemState.medication.pendingRateMgHr = rate;
            g_systemState.medication.requiresConfirmation = true;
            g_systemState.medication.safetyLevel = STATUS_CRITICAL;
            xSemaphoreGive(xSharedStateMutex);
        }
        s_pendingRequestMs = millis();
        alert_trigger(ALERT_MED_CRIT);

        snprintf(outReason, reasonSize,
                 "PENDING_CONFIRMATION: %.1f mg/hr requires confirmation", rate);
        Serial.printf("[Medication] High rate pending confirmation: %.1f mg/hr\n", rate);
        return false;
    }

    float applied = rate;
    bool clamped = false;

    // 3. Rate-limiting for safe rates: clamp sudden jumps > 20 mg/hr
    if ((rate - current) > MED_MAX_STEP_JUMP) {
        applied = current + MED_MAX_STEP_JUMP;
        clamped = true;
    }

    // 4. Apply safe rate
    VitalStatus_t newLevel = (applied > MED_RATE_WARN) ? STATUS_WARNING : STATUS_NORMAL;
    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_systemState.medication.currentRateMgHr = applied;
        g_systemState.medication.pendingRateMgHr = 0.0f;
        g_systemState.medication.requiresConfirmation = false;
        g_systemState.medication.safetyLevel = newLevel;
        xSemaphoreGive(xSharedStateMutex);
    }

    alert_trigger(ALERT_CHIRP);

    if (clamped) {
        snprintf(outReason, reasonSize,
                 "CLAMPED: requested=%.1f applied=%.1f", rate, applied);
    } else {
        snprintf(outReason, reasonSize,
                 "ACCEPTED: rate=%.1f mg/hr", applied);
    }
    Serial.printf("[Medication] Rate set to %.1f mg/hr%s\n", applied, clamped ? " (CLAMPED)" : "");
    return true;
}

bool medication_confirm(char* outMsg, size_t msgSize) {
    bool confirmed = false;
    float rate = 0.0f;

    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (g_systemState.medication.requiresConfirmation &&
            g_systemState.medication.pendingRateMgHr > 0.0f) {
            rate = g_systemState.medication.pendingRateMgHr;
            g_systemState.medication.currentRateMgHr = rate;
            g_systemState.medication.pendingRateMgHr = 0.0f;
            g_systemState.medication.requiresConfirmation = false;
            g_systemState.medication.safetyLevel = STATUS_CRITICAL;
            confirmed = true;
        }
        xSemaphoreGive(xSharedStateMutex);
    }

    if (confirmed) {
        alert_trigger(ALERT_MED_CRIT);
        snprintf(outMsg, msgSize, "CONFIRMED: %.1f mg/hr activated", rate);
        Serial.printf("[Medication] Confirmed critical rate %.1f mg/hr\n", rate);
        return true;
    } else {
        snprintf(outMsg, msgSize, "NO_PENDING: no high-rate pending confirmation");
        return false;
    }
}

static void vMedicationIntegrationTask(void* pvParams) {
    (void)pvParams;
    Serial.println("[Medication] Task started - Integral computation running");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        float rate = 0.0f;
        bool needAlert = false;
        bool cancelPending = false;

        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            rate = g_systemState.medication.currentRateMgHr;
            bool hasPending = g_systemState.medication.requiresConfirmation;

            // Check 30s timeout on pending confirmation
            if (hasPending) {
                if (millis() - s_pendingRequestMs > MED_CONFIRM_TIMEOUT_MS) {
                    g_systemState.medication.requiresConfirmation = false;
                    g_systemState.medication.pendingRateMgHr = 0.0f;
                    g_systemState.medication.safetyLevel = (rate > MED_RATE_WARN) ? STATUS_WARNING : STATUS_NORMAL;
                    cancelPending = true;
                    hasPending = false;
                }
            }

            if (rate > 0.0f) {
                // Add 1 second portion of hourly rate: rate / 3600 mg
                g_systemState.medication.totalDeliveredMg += (rate / 3600.0f);
            }

            // High rate or pending confirmation requires continuous intermittent alarm
            needAlert = (rate > MED_RATE_CRIT) || hasPending;
            xSemaphoreGive(xSharedStateMutex);
        }

        if (cancelPending) {
            Serial.println("[Medication] Pending confirmation TIMED OUT (30s) -> cancelled");
        }

        // Trigger alert AFTER releasing mutex
        if (needAlert) {
            alert_trigger(ALERT_MED_CRIT);
        }
    }
}

void medication_task_init(void) {
    xTaskCreatePinnedToCore(
        vMedicationIntegrationTask,
        "MedicationTask",
        STACK_MEDICATION,
        nullptr,
        PRIORITY_MEDICATION,
        &xHandleMedication,
        1
    );
}
