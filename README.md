# ESP32 Smart Healthcare IoT Monitor (FreeRTOS)

> ⚠️ **BEFORE BUILDING / FLASHING:**  
> Run `python tools/set_prefix.py YOUR_STRING` with your own unique string (4–24 chars: letters, digits, underscore, dash).  
> **Do NOT use a real student ID or personal data** — MQTT topics are published to the public HiveMQ broker.  
> Example: `python tools/set_prefix.py intern_2026_demo`

A production-grade, highly reliable FreeRTOS hospital bed monitoring and control firmware running on the ESP32.  
Built with **PlatformIO** (`espressif32@^6.0.0`, Arduino core 2.x / ESP-IDF 4.4) and fully simulated in the **Wokwi VS Code Extension**.

---

## 📋 Task Compliance Matrix

| # | Requirement | Implementation Summary | Key Source Files | Status |
|:--|:--|:--|:--|:--:|
| **1** | **Multi-Sensor Expansion with FreeRTOS** | 11 concurrent tasks across 4 priority levels. Separate tasks for vitals, environment, derived BP, ECG classification, alerts, medication, bed servo, display, network, and hardware watchdog supervisor. Protected by FreeRTOS queues, mutexes, binary semaphore, and event group. | `src/sensor_task.cpp`, `src/env_task.cpp`, `src/bp_task.cpp`, `src/ecg_task.cpp`, `src/alert_task.cpp`, `src/supervisor_task.cpp`, `src/task_utils.h` | ✅ **PASS** |
| **2** | **Dual-Role Dashboards** | Medical Staff (HR, SpO2, Temp, BP, ECG, alerts) and Facility Management (Room Temp, Humidity, O2, AQI, Bed Angle, alerts) published to dedicated MQTT topics. Retained SVG gauge dashboard with live updates, alert feeds, and periodic 30-second rolling min/max/avg aggregation. | `src/mqtt_task.cpp`, `dashboard/index.html` | ✅ **PASS** |
| **3** | **Smart Medication Dosage** | 0–100 mg/hr closed-loop infusion control. Rate-limiting clamps abrupt jumps to +20 mg/hr max step. Doses >80 mg/hr enter `PENDING_CONFIRMATION` with 30s timeout and activate rapid red LED blinking (4 blinks/s) and buzzer. Discrete numerical dose integral (mg delivered) computed every second. | `src/medication_task.cpp`, `src/alert_task.cpp`, `src/mqtt_task.cpp` | ✅ **PASS** |
| **4** | **Remote Bed Elevation** | 0–90° servo positioning with smooth 1° per 25 ms transition rate limiter. Presets for SLEEP (10°), EAT (30°), BREATHE (45°), and EMERGENCY (90°). EMERGENCY sweeps preempt mid-flight commands; automatic elevation to 45° triggers when patient SpO2 drops <90%. | `src/bed_task.cpp`, `src/sensor_task.cpp` | ✅ **PASS** |
| **5** | **Dynamic Adaptive Sampling** | Telemetry rate dynamically scales between 5s (during abnormal conditions) and 15s (stable conditions). Manual slider command (5–60s) supported through `xSamplingCmdQueue`. Non-blocking 250 ms sliced delays ensure immediate reaction to worsened vitals or incoming commands. | `src/sensor_task.cpp`, `src/mqtt_task.cpp`, `src/config.h` | ✅ **PASS** |
| **6** | **Offline Detection & Ring Buffer** | System connection arbiter reports `ONLINE`, `DEGRADED`, or `OFFLINE` based on Wi-Fi, MQTT, and health status. OLED renders `LOGGING OFFLINE [N]` banner. Distinct transition beeps (3 long beeps for Wi-Fi loss, 2 short beeps for MQTT loss). 30-record SRAM ring buffer with non-lossy 5-record batch flush and exponential backoff retry. | `src/conn_state.cpp`, `src/wifi_manager.cpp`, `src/mqtt_task.cpp`, `src/display_task.cpp` | ✅ **PASS** |

---

## ⚙️ FreeRTOS System Architecture

```
Priority  Task Name            Core  Stack   Interval / Trigger   Responsibility
──────────────────────────────────────────────────────────────────────────────────────────────────
   5      SupervisorTask        1    2048    Every 10 s           Feeds TWDT (panic=true), checks 9 task bits & free heap
   4      AlertTask             1    2048    Event-driven         Priority-aware LED strobe and passive buzzer tones
   4      MedicationTask        1    2048    Every 1 s            1-second numerical dosage integral, 30s confirmation timer
   3      BedControlTask        1    2048    Every 25 ms (sweep)  Smooth PWM servo stepping, emergency preemption, clamp guard
   3      SensorTask            1    4096    Adaptive 5–15 s      ADC pot acquisition, vitals classification, early wake-up
   3      WiFiTask              1    4096    1–32 s backoff       STA connection, exponential backoff, sync semaphore trigger
   3      MQTTTask              1    8192    Every 50 ms          Dual telemetry publisher, command dispatcher, ring buffer flush
   2      EnvTask               1    3072    Every 5 s            DHT22 reading, O2 / AQI bounded random walk
   2      BpTask                1    2048    Every 4 s            Systolic/diastolic BP simulation derived from HR
   2      EcgTask               1    2048    Every 3 s            ECG rhythm classification (Sinus / Tachycardia / Arrhythmia)
   2      DisplayTask           1    4096    Every 350 ms         SSD1306 OLED multi-page presentation, overlays, 100 kHz I2C
```

### Inter-Task Communication Primitives

| Primitive | Variable | Capacity / Type | Producers → Consumers | Function |
|:--|:--|:--|:--|:--|
| **Queue** | `xSensorDataQueue` | Depth 5 (`SensorData_t`) | `SensorTask` → `MQTTTask` | Primary telemetry stream; uses drop-oldest if full |
| **Queue** | `xDisplayMsgQueue` | Depth 10 (`DisplayMessage_t`) | Any Task → `DisplayTask` | Full-screen alert/status overlays with auto-expiry |
| **Queue** | `xAlertQueue` | Depth 1 mailbox (`AlertPattern_t`) | Any Task → `AlertTask` | Priority-filtered buzzer/LED patterns |
| **Queue** | `xBedCmdQueue` | Depth 5 (`BedCmd_t`) | `MQTTTask`, `SensorTask` → `BedTask` | Target angle and presets; peeked mid-sweep for EMERGENCY |
| **Queue** | `xSamplingCmdQueue` | Depth 4 (`SamplingCmd_t`) | `MQTTTask` → `SensorTask` | Dynamically updates sampling interval or Auto/Manual mode |
| **Semaphore** | `xSyncRequestSem` | Binary Semaphore | `WiFiTask`, `connectMQTT` → `MQTTTask` | Signals reconnection to trigger non-lossy offline ring-buffer flush |
| **Mutex** | `xSharedStateMutex` | Recursive Mutex | All Tasks | Thread-safe access to `g_systemState` |
| **Mutex** | `xI2CMutex` | Mutex | `DisplayTask` | Serializes I2C bus transactions for OLED display |
| **Event Group**| `xTaskHealthBits` | 9 Event Bits | All 9 Worker Tasks → `SupervisorTask` | Heartbeat assertion for Task Watchdog Timer (TWDT) |

---

## 🔊 Alert Priority Hierarchy

`alert_trigger()` evaluates the priority of the incoming pattern against any currently pending alert. Lower-priority patterns **never** overwrite higher-priority alarms:

```
Priority Level   Pattern Name          Trigger Event / Behavior
─────────────────────────────────────────────────────────────────────────────────────────
  5 (Highest)    ALERT_MED_CRIT        Medication rate >80 mg/hr: rapid LED (4 blinks/s) + 3000 Hz buzzer
  4              ALERT_CRIT_VITALS     HR / SpO2 / Temp critical: dual-tone alternating pulse + LED
  3              ALERT_OFFLINE_WIFI    Wi-Fi connection lost: 3 long beeps (1000 Hz, 300 ms), once on transition
  2              ALERT_DEGRADED_MQTT   MQTT broker disconnected: 2 short beeps (1800 Hz, 120 ms), once on transition
  1              ALERT_WARN_VITALS     Elevated temperature / abnormal vital warning: single 1500 Hz tone + LED
  0              ALERT_CHIRP           Bed preset / rate adjustment feedback: single 60 ms chirp (2400 Hz)
 -1 (Lowest)     ALERT_NONE            Silent idle; never replaces an active alert
```

---

## 📡 MQTT Topic Architecture

Broker: `broker.hivemq.com` | TCP Port: `1883` | WebSocket Port: `8884/mqtt`  
Configured Prefix: `hospital_parth_bed01_2026/ward_A/bed_01` (Replace with your unique ID)

### Medical Staff Dashboard
| Topic Suffix | Direction | QoS / Retain | Description / Sample Payload |
|:--|:--:|:--:|:--|
| `/medical/heartrate` | ESP → Cloud | QoS 0 / Retain | Patient pulse: `75.50` (bpm) |
| `/medical/spo2` | ESP → Cloud | QoS 0 / Retain | Blood oxygenation: `98.00` (%) |
| `/medical/bodytemp` | ESP → Cloud | QoS 0 / Retain | Body temperature: `36.85` (°C) |
| `/medical/bp` | ESP → Cloud | QoS 0 / Retain | Blood pressure: `{"systolic":120,"diastolic":80}` |
| `/medical/ecg` | ESP → Cloud | QoS 0 / Retain | Rhythm classification: `{"rhythm":"Normal Sinus","hr":75}` |
| `/medical/alerts` | ESP → Cloud | QoS 0 / Transient | Vitals emergency: `{"severity":"CRITICAL","hr":155,"spo2":88,"temp":39.8}` |
| `/medical/aggregate` | ESP → Cloud | QoS 0 / Retain | 30s roll-up: `{"window_sec":30,"samples":6,"heartRate":{"min":72,"max":80,"avg":75},...}` |
| `/medical/medication_status` | ESP → Cloud | QoS 0 / Retain | Infusion telemetry: `{"rate":40.0,"safety":"NORMAL","pending":0.0,"delivered_mg":"1.25"}` |

### Facility Management Dashboard
| Topic Suffix | Direction | QoS / Retain | Description / Sample Payload |
|:--|:--:|:--:|:--|
| `/facility/roomtemp` | ESP → Cloud | QoS 0 / Retain | Ambient temperature: `24.50` (°C) |
| `/facility/humidity` | ESP → Cloud | QoS 0 / Retain | Ambient humidity: `48.00` (%) |
| `/facility/oxygen` | ESP → Cloud | QoS 0 / Retain | Room O2 concentration: `{"o2_pct":20.9}` |
| `/facility/aqi` | ESP → Cloud | QoS 0 / Retain | Air Quality Index: `{"aqi":42,"status":"Good"}` |
| `/facility/bed_angle` | ESP → Cloud | QoS 0 / Retain | Bed elevation: `{"angle":10,"preset":1}` |
| `/facility/alerts` | ESP → Cloud | QoS 0 / Transient | Facility warning: `{"severity":"WARNING","roomTemp":31.2,"aqi":110}` |
| `/facility/aggregate` | ESP → Cloud | QoS 0 / Retain | 30s roll-up: `{"window_sec":30,"samples":6,"roomTemp":{"min":24.0,...},"aqi":{...}}` |

### System & Offline Synchronization
| Topic Suffix | Direction | QoS / Retain | Description / Sample Payload |
|:--|:--:|:--:|:--|
| `/status` | ESP ↔ Cloud | QoS 1 / Retain (LWT) | System heartbeat / LWT: `{"state":"ONLINE"}` or `{"state":"OFFLINE"}` |
| `/historical/sync` | ESP → Cloud | QoS 1 / Transient | Buffered offline record: `{"ts":12400,"historical":true,"hr":76,"spo2":98,...}` |

### Command Topics (Dashboard / CLI → ESP32)
| Topic Suffix | Payload Options | Action |
|:--|:--|:--|
| `/cmd/medication/rate` | `0` to `100` (float) | Request new infusion rate (clamped if jump > 20 mg/hr) |
| `/cmd/medication/confirm` | `CONFIRM` | Authorize pending critical infusion rate (>80 mg/hr) |
| `/cmd/bed/angle` | `0` to `90` (integer) | Set absolute bed elevation angle |
| `/cmd/bed/preset` | `SLEEP` \| `EAT` \| `BREATHE` \| `EMERGENCY` | Move bed to preset angle (10°, 30°, 45°, 90°) |
| `/cmd/sampling/rate` | `5` to `60` (integer) | Set manual telemetry interval in seconds |
| `/cmd/sampling/mode` | `auto` \| `manual` | Toggle adaptive vitals sampling mode |

---

## 🔌 Hardware Pin Map

| ESP32 Pin | Peripheral / Component | Signal Description |
|:--|:--|:--|
| **GPIO 21** | SSD1306 0.96" OLED Display | I2C SDA (Standard 100 kHz mode) |
| **GPIO 22** | SSD1306 0.96" OLED Display | I2C SCL (Standard 100 kHz mode) |
| **GPIO 4** | DHT22 Temperature & Humidity Sensor | Single-bus data line |
| **GPIO 34** | 10kΩ Potentiometer (ADC1_CH6) | Heart Rate adjustment input (60–180 bpm) |
| **GPIO 35** | 10kΩ Potentiometer (ADC1_CH7) | SpO2 adjustment input (85–100%) |
| **GPIO 13** | Pushbutton (Active LOW) | Force Abnormal Vitals simulation |
| **GPIO 12** | Pushbutton (Active LOW) | 3-Mode Network Failure toggle (Online → Offline → Degraded) |
| **GPIO 2** | Red LED Indicator | Visual Alarm output (Active HIGH) |
| **GPIO 25** | Passive Piezo Buzzer | Frequency-modulated audible tone output |
| **GPIO 18** | SG90 Micro Servo Motor | Bed elevation actuator (50 Hz PWM, 0–90°) |

---

## 💻 Simulation in Wokwi (VS Code Extension)

1. Open the project folder `c:\RTOS` in VS Code.
2. Install the **Wokwi Simulator** extension if not already installed.
3. Build the project using PlatformIO:
   ```powershell
   $env:PATH += ";C:\Users\Parth\.platformio\penv\Scripts"
   pio run
   ```
4. Press `F1` or `Ctrl+Shift+P`, type `Wokwi: Start Simulator`, and select it.
5. The ESP32 will boot, initialize the I2C OLED at 100 kHz, start all 11 FreeRTOS tasks, connect to `Wokwi-GUEST` Wi-Fi, and establish an MQTT session with HiveMQ.

---

## 📊 Dual-Role Web Dashboard

The web dashboard is a single-file application (`dashboard/index.html`) using **MQTT.js 4.3.7** over Secure WebSockets (`wss://broker.hivemq.com:8884/mqtt`).

### Opening the Dashboard
Open `dashboard/index.html` in any web browser.  
To point to your custom prefix, append `?prefix=` in the URL:
```
file:///c:/RTOS/dashboard/index.html?prefix=hospital_parth_bed01_2026/ward_A/bed_01
```

### Dashboard Features
- **Medical Staff Tab:** High-visibility SVG gauges for Heart Rate, SpO2, and Body Temp. Live Blood Pressure card (derived from HR) and ECG rhythm card. 30-second rolling aggregate summary table and emergency alert feed.
- **Facility Management Tab:** Ambient Room Temperature, Humidity, Room O2 concentration, and AQI gauges. Bed elevation status and interactive control.
- **Medication Control:** Live infusion rate slider (0–100 mg/hr), running total delivered dose (mg), safety status badge, and dynamic **Confirm** button for critical rates (>80 mg/hr).
- **Bed Controls:** Presets (Sleep 10°, Eat 30°, Breathe 45°, Emergency 90°) and manual angle slider.
- **Adaptive Sampling:** Auto/Manual mode toggle with 5–60s slider.
- **Connection Diagnostics:** Dynamic pills showing device connection status (`ONLINE`, `DEGRADED`, `OFFLINE`, `STALE`), broker connectivity, and a live "Last update: Ns ago" watchdog timer.

---

## 🧪 Testing & Verification

### 1. Host Logic Tests (No Hardware Required)
Run the pure-logic test suite exercising the rate clamp, ring buffer overflow, drops counter overflow prevention, sampling interval selection, and alert priority ordering:
```powershell
& "C:\Users\Parth\.platformio\penv\Scripts\python.exe" tools/host_tests.py
```
*Expected Result:* All 26 assertions output `PASS` with 0 failures.

### 2. Automated MQTT Test Tool
Verify firmware behavior over MQTT:
```powershell
& "C:\Users\Parth\.platformio\penv\Scripts\python.exe" tools/mqtt_test.py hospital_parth_bed01_2026/ward_A/bed_01
```

### 3. Step-by-Step Medication Safety Ramp
Because of the safety rate limiter (+20 mg/hr max jump):
1. **Initial State:** Rate is 0 mg/hr (`NORMAL`).
2. **Step 1:** Request `20` mg/hr → accepted (`NORMAL`).
3. **Step 2:** Request `40` mg/hr → accepted (`NORMAL`).
4. **Step 3:** Request `60` mg/hr → accepted (`WARNING`).
5. **Step 4:** Request `80` mg/hr → accepted (`WARNING`).
6. **Step 5 (Critical):** Request `90` mg/hr → `PENDING_CONFIRMATION`. Buzzer sounds, red LED blinks rapidly (4 blinks/s). Dashboard shows Confirm button.
7. **Step 6:** Click Confirm within 30s → Rate activated at 90 mg/hr (`CRITICAL`).
8. **Timeout Test:** If 90 mg/hr is requested but not confirmed within 30s, the request times out and cancels automatically.
