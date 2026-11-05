/**
 * conn_state.h  -  Central connection state arbiter.
 */

#pragma once

#include "shared_types.h"

// Computes and updates g_systemState.connState based on WiFi, MQTT,
// simulation mode, system health, and free heap.
void updateConnState(void);

