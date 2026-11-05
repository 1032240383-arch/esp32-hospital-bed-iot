/**
 * conn_state.cpp  -  Central connection state arbiter implementation.
 */

#include <Arduino.h>
#include "conn_state.h"
#include "shared_types.h"

void updateConnState(void) {
    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        SimFailMode_t mode   = g_systemState.simMode;
        bool          wifiUp = g_systemState.wifiConnected;
        bool          mqttUp = g_systemState.mqttConnected;
        bool          healthy = g_systemState.systemHealthy;
        uint32_t      heap   = ESP.getFreeHeap();

        if (mode == SIM_WIFI_DOWN || !wifiUp) {
            g_systemState.connState = CONN_OFFLINE;
        } else if (mode == SIM_MQTT_DOWN || !mqttUp || !healthy || heap < 20480) {
            g_systemState.connState = CONN_DEGRADED;
        } else {
            g_systemState.connState = CONN_ONLINE;
        }
        xSemaphoreGive(xSharedStateMutex);
    }
}

