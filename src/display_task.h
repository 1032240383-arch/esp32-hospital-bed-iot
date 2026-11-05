/**
 * display_task.h  -  OLED display task declaration.
 */

#pragma once

#include <Arduino.h>

// Initialise the SSD1306 OLED and create the display FreeRTOS task.
void display_task_init(void);

// Push a one-liner status update to the display queue from any task.
void display_post_status(const char* statusLine);

// Push a multi-line alert overlay to the display queue.
void display_post_alert(const char* l1, const char* l2, const char* l3, uint32_t durationMs);
