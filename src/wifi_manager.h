/**
 * wifi_manager.h  -  Wi-Fi task declaration.
 *
 * WHY a dedicated Wi-Fi task:
 *   Wi-Fi reconnection is a blocking operation (up to several seconds).
 *   Running it in a separate task lets sensor reading and display continue
 *   uninterrupted. The task also feeds the health event group so the
 *   supervisor knows Wi-Fi management is alive.
 */

#pragma once

// Initialise hardware and create the Wi-Fi task.
// Call from setup() AFTER queues and mutexes are created.
void wifi_manager_init(void);

// Returns true if Wi-Fi is currently connected.
bool wifi_is_connected(void);
