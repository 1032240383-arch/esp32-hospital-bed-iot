/**
 * mqtt_task.h  -  MQTT connection and publish/subscribe task declaration.
 *
 * Only the MQTT task ever calls PubSubClient methods.
 * WHY: PubSubClient is NOT thread-safe; all access must be from one task.
 *      Other tasks send data via xSensorDataQueue or xDisplayMsgQueue rather
 *      than calling publish() directly.
 */

#pragma once

// Create the MQTT task.  Call from setup() after queues are ready.
void mqtt_task_init(void);

// Returns true if MQTT client is currently connected to the broker.
bool mqtt_is_connected(void);
