/**
 * sensor_task.h  -  Sensor reading task declaration.
 *
 * Simulates MAX30102 vitals via ADC potentiometers because the Wokwi
 * parts library does not include a MAX30102 component.  A "force abnormal"
 * button lets the user push HR/SpO2 out of safe range to trigger alerts
 * without re-wiring.
 */

#pragma once

// Create the sensor-reading FreeRTOS task and initialise hardware.
// Call from setup() after the global queues have been created.
void sensor_task_init(void);
