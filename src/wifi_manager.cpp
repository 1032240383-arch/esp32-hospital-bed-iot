/**
 * wifi_manager.cpp  -  Wi-Fi Connection Management with Exponential Backoff.
 *
 * Implements:
 *   - Automatic connection & reconnection with exponential backoff (1s -> 2s -> 4s ... 32s)
 *   - Offline detection: updates ConnState to OFFLINE and triggers 3 long beeps on transition
 *   - Startup grace period (no spurious alarm before first connection attempt)
 *   - Binary semaphore signal (xSyncRequestSem) on reconnect for offline buffer flush
 *   - Simulated failure button support (D12)
 *   - FreeRTOS watchdog heartbeat (BIT_WIFI_ALIVE) via sliced polling
 */

#include <Arduino.h>
#include <WiFi.h>
#include "wifi_manager.h"
#include "alert_task.h"
#include "config.h"
#include "shared_types.h"
#include "task_utils.h"
#include "conn_state.h"

TaskHandle_t xHandleWifi = nullptr;

static void setWifiConnectedState(bool up) {
    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_systemState.wifiConnected = up;
        xSemaphoreGive(xSharedStateMutex);
    }
    updateConnState();
}

static void vWiFiTask(void* pvParams) {
    (void)pvParams;

    Serial.println("[WiFi] Task started");
    WiFi.mode(WIFI_STA);

    uint32_t backoffMs = WIFI_BACKOFF_BASE_MS;
    bool wasOffline = false;
    static bool s_hadFirstAttempt = false;
    static SimFailMode_t s_prevSimMode = SIM_NONE;

    while (true) {
        xEventGroupSetBits(xTaskHealthBits, BIT_WIFI_ALIVE);

        // Check simulated failure mode
        SimFailMode_t simMode = SIM_NONE;
        if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            simMode = g_systemState.simMode;
            xSemaphoreGive(xSharedStateMutex);
        }

        if (simMode == SIM_WIFI_DOWN) {
            if (WiFi.isConnected()) {
                WiFi.disconnect(true);
                Serial.println("[WiFi] SIM_WIFI_DOWN: Disconnecting -> OFFLINE");
            }
            if (s_prevSimMode != SIM_WIFI_DOWN && s_hadFirstAttempt) {
                alert_trigger(ALERT_OFFLINE_WIFI);
            }
            s_prevSimMode = simMode;
            setWifiConnectedState(false);
            wasOffline = true;
            taskSleepWithHeartbeat(1000, BIT_WIFI_ALIVE);
            continue;
        } else if (simMode == SIM_MQTT_DOWN) {
            // Wi-Fi remains connected, MQTTTask will handle disconnect
            s_prevSimMode = simMode;
        } else {
            if (s_prevSimMode != SIM_NONE) {
                Serial.println("[WiFi] Sim failure cleared -> resuming normal Wi-Fi");
                wasOffline = true;
            }
            s_prevSimMode = SIM_NONE;
        }

        // Reconnect if dropped
        if (!WiFi.isConnected()) {
            if (!wasOffline) {
                Serial.println("[WiFi] Connection dropped -> OFFLINE");
                if (s_hadFirstAttempt) {
                    alert_trigger(ALERT_OFFLINE_WIFI);
                }
                wasOffline = true;
            }
            setWifiConnectedState(false);

            Serial.printf("[WiFi] Attempting connect to %s (backoff: %u ms)...\n", WIFI_SSID, backoffMs);
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

            // Poll in heartbeat slices until backoff expires or connected
            uint32_t waited = 0;
            while (waited < backoffMs) {
                uint32_t slice = 500;
                if (slice > backoffMs - waited) slice = backoffMs - waited;
                xEventGroupSetBits(xTaskHealthBits, BIT_WIFI_ALIVE);
                vTaskDelay(pdMS_TO_TICKS(slice));
                waited += slice;
                if (WiFi.isConnected()) break;
            }

            if (WiFi.isConnected()) {
                Serial.printf("[WiFi] Connected! IP=%s\n", WiFi.localIP().toString().c_str());
                s_hadFirstAttempt = true;
                backoffMs = WIFI_BACKOFF_BASE_MS;
                wasOffline = false;

                // Signal MQTT task to flush offline buffer
                if (xSyncRequestSem != nullptr) {
                    xSemaphoreGive(xSyncRequestSem);
                }

                setWifiConnectedState(true);
            } else {
                backoffMs = (backoffMs * 2 > WIFI_BACKOFF_MAX_MS) ? WIFI_BACKOFF_MAX_MS : backoffMs * 2;
            }
        } else {
            wasOffline = false;
            s_hadFirstAttempt = true;
            setWifiConnectedState(true);
            taskSleepWithHeartbeat(3000, BIT_WIFI_ALIVE);
        }
    }
}

void wifi_manager_init(void) {
    xTaskCreatePinnedToCore(
        vWiFiTask,
        "WiFiTask",
        STACK_WIFI,
        nullptr,
        PRIORITY_WIFI,
        &xHandleWifi,
        1
    );
}

bool wifi_is_connected(void) {
    return WiFi.isConnected();
}
