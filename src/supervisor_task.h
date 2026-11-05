/**
 * supervisor_task.h  -  Watchdog supervisor task declaration.
 *
 * The supervisor is the "guard dog of the guard dog":
 *   - It is the ONLY task registered with the ESP32 hardware Task Watchdog.
 *   - Every other task must set its health bit in xTaskHealthBits within
 *     SUPERVISOR_CHECK_INTERVAL_MS; if any bit is missing the supervisor
 *     stops feeding the HW watchdog, which then triggers a reboot.
 *   - This is safer than each task feeding the watchdog independently because
 *     a "fast spinning" task could keep feeding the watchdog even while doing
 *     nothing useful.
 */

#pragma once

// Create the supervisor task.  Call last in setup() so it monitors all
// other tasks from the moment they start.
void supervisor_task_init(void);
