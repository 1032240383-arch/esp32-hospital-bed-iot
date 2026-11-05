/**
 * display_task.cpp  -  SSD1306 OLED Rendering (Multi-Page, Overlays & Offline Banner).
 *
 * Uses safe snprintf buffers and oled.print() to guarantee compatibility across
 * all ESP32 toolchain versions without relying on Print::printf.
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "display_task.h"
#include "config.h"
#include "shared_types.h"

TaskHandle_t xHandleDisplay = nullptr;

static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
static uint8_t s_currentPage = 0;
static uint32_t s_lastPageSwitchMs = 0;

static void drawSeparator(uint8_t y) {
    oled.drawFastHLine(0, y, OLED_WIDTH, SSD1306_WHITE);
}

static const char* getPresetName(BedPreset_t p) {
    switch (p) {
        case BED_MODE_SLEEP:     return "SLEEP";
        case BED_MODE_EAT:       return "EAT";
        case BED_MODE_BREATHE:   return "BREATHE";
        case BED_MODE_EMERGENCY: return "EMERGENCY";
        default:                 return "MANUAL";
    }
}

static void renderPageVitals(const SensorData_t& d) {
    char buf[32];

    oled.setCursor(0, 11);
    snprintf(buf, sizeof(buf), "HR  : %3.0f bpm%s", d.heartRate, (d.hrStatus == STATUS_CRITICAL) ? " !" : "");
    oled.print(buf);

    oled.setCursor(0, 21);
    snprintf(buf, sizeof(buf), "SpO2: %3.0f %%%s", d.spo2, (d.spo2Status == STATUS_CRITICAL) ? " !" : "");
    oled.print(buf);

    oled.setCursor(0, 31);
    snprintf(buf, sizeof(buf), "Temp: %4.1fC BP:%d/%d", d.bodyTemp, d.bpSys, d.bpDia);
    oled.print(buf);

    oled.setCursor(0, 41);
    snprintf(buf, sizeof(buf), "ECG : %s", d.ecgStatus[0] ? d.ecgStatus : "Sinus");
    oled.print(buf);

    drawSeparator(51);
    oled.setCursor(0, 54);
    snprintf(buf, sizeof(buf), "Env : %2.0fC  %2.0f%% RH", d.roomTemp, d.humidity);
    oled.print(buf);
}

static void renderPageMedicationAndBed(const SystemState_t& st) {
    char buf[32];

    oled.setCursor(0, 11);
    snprintf(buf, sizeof(buf), "Infuse: %4.1f mg/h%s", st.medication.currentRateMgHr,
             (st.medication.currentRateMgHr > MED_RATE_CRIT) ? " CRIT" :
             (st.medication.currentRateMgHr > MED_RATE_WARN) ? " WARN" : "");
    oled.print(buf);

    oled.setCursor(0, 21);
    snprintf(buf, sizeof(buf), "Dose  : %5.2f mg", st.medication.totalDeliveredMg);
    oled.print(buf);

    oled.setCursor(0, 31);
    if (st.medication.requiresConfirmation) {
        oled.print(F(">> CONFIRM NEEDED <<"));
    } else {
        oled.print(F("RateLim: Max +20mg/h"));
    }

    drawSeparator(41);
    oled.setCursor(0, 44);
    snprintf(buf, sizeof(buf), "Bed : %d deg [%s]", st.bed.currentAngle, getPresetName(st.bed.mode));
    oled.print(buf);

    oled.setCursor(0, 54);
    if (st.simMode == SIM_WIFI_DOWN) {
        snprintf(buf, sizeof(buf), "SIM:WIFI-DOWN %us(%s)",
                 st.sampling.intervalMs / 1000,
                 st.sampling.isAutoMode ? "Au" : "Mn");
    } else if (st.simMode == SIM_MQTT_DOWN) {
        snprintf(buf, sizeof(buf), "SIM:MQTT-DOWN %us(%s)",
                 st.sampling.intervalMs / 1000,
                 st.sampling.isAutoMode ? "Au" : "Mn");
    } else {
        snprintf(buf, sizeof(buf), "Sampling: %us (%s)",
                 st.sampling.intervalMs / 1000,
                 st.sampling.isAutoMode ? "Auto" : "Man");
    }
    oled.print(buf);
}

static void renderScreen(void) {
    static SystemState_t s_cachedState = {};
    if (xSemaphoreTake(xSharedStateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(&s_cachedState, &g_systemState, sizeof(SystemState_t));
        xSemaphoreGive(xSharedStateMutex);
    }

    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);

    // ── Header Bar ──────────────────────────────────────────────────────────
    char hdr[32];
    if (s_cachedState.connState == CONN_OFFLINE) {
        oled.fillRect(0, 0, OLED_WIDTH, 9, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        oled.setCursor(4, 1);
        snprintf(hdr, sizeof(hdr), "LOGGING OFFLINE [%d]", s_cachedState.offlineBufferCount);
        oled.print(hdr);
        oled.setTextColor(SSD1306_WHITE);
    } else if (s_cachedState.connState == CONN_DEGRADED) {
        oled.setCursor(0, 0);
        if (strstr(s_cachedState.lastHealthReason, "TASK") != nullptr) {
            oled.print(F("DEGRADED(TASK FAULT)"));
        } else {
            oled.print(F("DEGRADED (NO MQTT)"));
        }
        drawSeparator(9);
    } else {
        oled.setCursor(4, 0);
        snprintf(hdr, sizeof(hdr), "WARD-A BED-01 [P%d/2]", s_currentPage + 1);
        oled.print(hdr);
        drawSeparator(9);
    }

    // ── Content Pages ───────────────────────────────────────────────────────
    if (s_currentPage == 0) {
        renderPageVitals(s_cachedState.lastReading);
    } else {
        renderPageMedicationAndBed(s_cachedState);
    }

    oled.display();
}

static void vDisplayTask(void* pvParams) {
    (void)pvParams;
    Serial.println("[Display] Task started");

    if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
        Wire.setTimeOut(50);
        bool ok = oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
        // Force safe 100kHz standard speed (Adafruit sets 400kHz which glitches in Wokwi)
        Wire.setClock(100000);
        xSemaphoreGive(xI2CMutex);

        if (!ok) {
            Serial.println("[Display] ERROR: SSD1306 not found on I2C.");
            while (true) {
                xEventGroupSetBits(xTaskHealthBits, BIT_DISPLAY_ALIVE);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
        Serial.println("[Display] SSD1306 init OK (100kHz I2C)");
    }

    // Quick boot splash (500ms)
    if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(15, 15); oled.print(F("SMART HEALTHCARE"));
        oled.setCursor(20, 28); oled.print(F("IoT MONITORING"));
        oled.setCursor(10, 45); oled.print(F("Initializing OS..."));
        oled.display();
        xSemaphoreGive(xI2CMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(600));
    s_lastPageSwitchMs = millis();

    static uint32_t s_overlayEndMs = 0;
    static DisplayMessage_t s_overlayMsg = {};

    while (true) {
        xEventGroupSetBits(xTaskHealthBits, BIT_DISPLAY_ALIVE);

        // Drain display message queue - check for alert / status overlays
        DisplayMessage_t msg;
        while (xQueueReceive(xDisplayMsgQueue, &msg, 0) == pdTRUE) {
            if (msg.type == DISP_ALERT || msg.type == DISP_STATUS) {
                s_overlayMsg = msg;
                s_overlayEndMs = millis() + (msg.durationMs > 0 ? msg.durationMs : 2500);
            }
        }

        bool hasOverlay = (millis() < s_overlayEndMs);

        if (hasOverlay) {
            if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                oled.clearDisplay();
                oled.setTextSize(1);
                oled.setTextColor(SSD1306_WHITE);
                oled.setCursor(0, 0);  oled.print(s_overlayMsg.line1);
                oled.setCursor(0, 13); oled.print(s_overlayMsg.line2);
                oled.setCursor(0, 26); oled.print(s_overlayMsg.line3);
                oled.setCursor(0, 39); oled.print(s_overlayMsg.line4);
                oled.setCursor(0, 52); oled.print(s_overlayMsg.line5);
                oled.display();
                xSemaphoreGive(xI2CMutex);
            }
        } else {
            // Page auto-rotation every 3.5 seconds
            if (millis() - s_lastPageSwitchMs >= 3500) {
                s_currentPage = (s_currentPage + 1) % 2;
                s_lastPageSwitchMs = millis();
            }

            if (xSemaphoreTake(xI2CMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                renderScreen();
                xSemaphoreGive(xI2CMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(600)); // 600ms optimizes Wokwi simulation CPU while keeping UI responsive
    }
}

void display_task_init(void) {
    xTaskCreatePinnedToCore(
        vDisplayTask,
        "DisplayTask",
        STACK_DISPLAY,
        nullptr,
        PRIORITY_DISPLAY,
        &xHandleDisplay,
        1
    );
}

void display_post_status(const char* statusLine) {
    DisplayMessage_t msg = {};
    msg.type = DISP_STATUS;
    msg.durationMs = 2000;
    if (statusLine) strncpy(msg.line1, statusLine, sizeof(msg.line1) - 1);
    xQueueSend(xDisplayMsgQueue, &msg, 0);
}

void display_post_alert(const char* l1, const char* l2, const char* l3, uint32_t durationMs) {
    DisplayMessage_t msg = {};
    msg.type = DISP_ALERT;
    msg.durationMs = durationMs;
    if (l1) strncpy(msg.line1, l1, sizeof(msg.line1) - 1);
    if (l2) strncpy(msg.line2, l2, sizeof(msg.line2) - 1);
    if (l3) strncpy(msg.line3, l3, sizeof(msg.line3) - 1);
    xQueueSend(xDisplayMsgQueue, &msg, 0);
}
