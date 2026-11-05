# TEST_PLAN.md — ESP32 Smart Healthcare IoT Monitor

Comprehensive test plan for evaluating all six requirements, edge cases, safety constraints, and FreeRTOS concurrency.

---

## ⏱️ Pre-Test Timing Notes & Guidelines
1. **Set 5s Sampling before Offline Tests:** In manual sampling tests or offline buffer checks, dial sampling to 5s so records accumulate rapidly.
2. **Medication Ramp Order:** The medication pump rate limiter restricts jumps to $\le +20$ mg/hr. Always ramp: `20 -> 40 -> 60 -> 80 -> 90` mg/hr.
3. **Confirmation Window:** Critical rates ($>80$ mg/hr) wait exactly 30 seconds for confirmation. If testing timeout cancellation, do not send confirmation for 32 seconds.
4. **Servo Sweep Velocity:** The bed servo steps at 1° per 25 ms (~2.25s for 0° to 90°). Peeking commands mid-sweep must be tested during this travel window.

---

## 📋 Comprehensive Verification Matrix (~40 Test Cases)

| # | Requirement Sub-Item | Component / Function | Action (Wokwi / Dashboard / CLI) | Expected System Response (Serial / OLED / LED / Buzzer / Servo / MQTT) | Result |
|:--|:--|:--|:--|:--|:--:|
| **1.1** | FreeRTOS 11 Concurrent Tasks | `main.cpp` / Task inits | Start Wokwi simulation | Serial logs all 11 tasks initialized with correct core and priorities; free heap > 100 KB | [ ] |
| **1.2** | Hardware Watchdog Feed | `supervisor_task.cpp` | Normal running operation | Watchdog fed every 10s; serial: `[Supervisor] Health: Sensor=OK Disp=OK...`; board never resets | [ ] |
| **1.3** | Watchdog Starvation Reset | `supervisor_task.cpp` | Freeze or starve any task alive bit | Supervisor detects missing bit; withholds WDT reset; ESP32 resets after 30s panic timeout | [ ] |
| **1.4** | Sliced Heartbeats | `task_utils.h` | Set sampling to 60s | SensorTask sleeps in 500ms slices, asserting heartbeat bit; supervisor sees alive; no reboot | [ ] |
| **1.5** | Stack Watermark Logging | `supervisor_task.cpp` | Run for 60 seconds | Serial prints high-water mark table for all 11 tasks showing positive bytes remaining free | [ ] |
| **1.6** | Mutex Resource Protection | `xSharedStateMutex` | Concurrent read/writes to state | No heap corruption, no memory leaks, mutex cleanly released before task sleep/delay | [ ] |
| **1.7** | I2C Display Bus Isolation | `xI2CMutex` | Rapid OLED page flip and alerts | Clean screen refreshes at 100 kHz without I2C bus collision or graphic tearing | [ ] |
| **1.8** | Dedicated BP Task | `bp_task.cpp` | Vary HR potentiometer | BP dynamically recalculates systolic/diastolic every 4s based on HR; values published to MQTT | [ ] |
| **1.9** | Dedicated ECG Task | `ecg_task.cpp` | Vary HR potentiometer | ECG rhythm classified: Normal Sinus (55-110), Tachycardia (>110), Bradycardia (<55) | [ ] |
| **1.10**| Dedicated Env Task | `env_task.cpp` | Normal simulation run | DHT22 read every 5s; O2 (20.0-21.5%) and AQI (20-120) undergo realistic bounded random walk | [ ] |
| **2.1** | Medical Telemetry Stream | `mqtt_task.cpp` | Observe MQTT broker | Topics `.../medical/heartrate`, `spo2`, `bodytemp`, `bp`, `ecg` published periodically | [ ] |
| **2.2** | Facility Telemetry Stream| `mqtt_task.cpp` | Observe MQTT broker | Topics `.../facility/roomtemp`, `humidity`, `oxygen`, `aqi`, `bed_angle` published periodically | [ ] |
| **2.3** | Web Dashboard Dual Tabs | `dashboard/index.html` | Click "Medical Staff" vs "Facility Mgmt" | UI tab transitions smoothly; displays relevant gauges and cards without reload | [ ] |
| **2.4** | Gauge Safe Zones | `dashboard/index.html` | Adjust pot HR/SpO2 | Gauge needle sweeps arc; color changes green (normal) -> amber (warn) -> red (crit) | [ ] |
| **2.5** | Medical Alerts Feed | `mqtt_task.cpp` | Set pot HR > 150 bpm | Alert published to `.../medical/alerts`; shows in dashboard alert list with timestamp | [ ] |
| **2.6** | Facility Alerts Feed | `mqtt_task.cpp` | Ambient temp > 30°C or AQI > 100 | Alert published to `.../facility/alerts`; shows in facility alert list | [ ] |
| **2.7** | 30s Rolling Aggregation | `mqtt_task.cpp` | Run for 30 seconds | Min/max/avg published for HR, SpO2, Temp, BP, Room Temp, Hum, O2, AQI; cards update | [ ] |
| **3.1** | Medication Slider Display | `dashboard/index.html` | Move slider 0 -> 40 mg/hr | OLED page 2 updates: `Infuse: 40.0 mg/h`; safety badge shows `NORMAL`; audible chirp | [ ] |
| **3.2** | Medication Step Clamping | `medication_task.cpp` | Jump from 20 -> 70 mg/hr | Request clamped to 40 mg/hr (+20 max step); dashboard toast: `CLAMPED: ... applied=40.0` | [ ] |
| **3.3** | Out of Range Rejection | `medication_task.cpp` | Send rate = 150 or -5 | Status `REJECTED_OUT_OF_RANGE`; error toast; rate remains unchanged | [ ] |
| **3.4** | High Dose Confirmation | `medication_task.cpp` | Request 90 mg/hr (from 80) | Status `PENDING_CONFIRMATION`; OLED shows `>> CONFIRM NEEDED <<`; Confirm button appears | [ ] |
| **3.5** | High Dose Alarm Actuation| `alert_task.cpp` | During pending or >80 mg/hr | Red LED blinks rapidly (4 blinks/s) and buzzer sounds continuously until confirmed or timeout | [ ] |
| **3.6** | Confirm Button Execution | `dashboard/index.html` | Click Confirm button | Status `CONFIRMED`; rate becomes 90 mg/hr; safety badge updates to `CRITICAL` | [ ] |
| **3.7** | Confirmation 30s Timeout | `medication_task.cpp` | Request 90 mg/hr, wait 32s | Pending rate cancelled; OLED banner clears; buzzer/LED stop; status returns to safe rate | [ ] |
| **3.8** | Running Dose Integral | `medication_task.cpp` | Run infusion for 60 seconds | Total delivered dose (mg) increments every second by `rate / 3600`; displayed on OLED & web | [ ] |
| **4.1** | Remote Bed Slider | `bed_task.cpp` | Move bed slider to 45° | Servo sweeps smoothly at 1° per 25 ms; OLED updates bed angle; MQTT publishes angle | [ ] |
| **4.2** | Bed Preset: SLEEP | `bed_task.cpp` | Click "SLEEP" preset | Bed transitions smoothly to 10°; OLED shows `Bed: 10 deg [SLEEP]` | [ ] |
| **4.3** | Bed Preset: EAT | `bed_task.cpp` | Click "EAT" preset | Bed transitions smoothly to 30°; OLED shows `Bed: 30 deg [EAT]` | [ ] |
| **4.4** | Bed Preset: BREATHE | `bed_task.cpp` | Click "BREATHE" preset | Bed transitions smoothly to 45°; OLED shows `Bed: 45 deg [BREATHE]` | [ ] |
| **4.5** | Bed Preset: EMERGENCY | `bed_task.cpp` | Click "EMERGENCY" preset | Bed transitions smoothly to 90° (upright Fowler); OLED shows `Bed: 90 deg [EMERGENCY]` | [ ] |
| **4.6** | Emergency Preemption | `bed_task.cpp` | Sweep to 60°, send EMERGENCY | Current sweep preempted mid-flight; servo steps immediately to 90° without overshoot | [ ] |
| **4.7** | Non-Emergency Preservation| `bed_task.cpp`| During EMERGENCY sweep, send 30°| Non-emergency cmd remains queued; emergency finishes at 90° before executing 30° | [ ] |
| **4.8** | Low SpO2 Auto Elevation | `sensor_task.cpp` | Turn SpO2 pot < 90% | Low SpO2 detected; OLED posts alert overlay; bed auto-elevates to 45° (BREATHE preset) | [ ] |
| **5.1** | Adaptive Auto Stable Rate| `sensor_task.cpp` | Normal vitals (HR=75, SpO2=98) | Sampling occurs every 15 seconds; OLED page 2 shows `Sampling: 15s (Auto)` | [ ] |
| **5.2** | Adaptive Auto Abnormal | `sensor_task.cpp` | Hold D13 button (abnormal) | Sampling switches to 5 seconds within 250 ms; OLED page 2 shows `Sampling: 5s (Auto)` | [ ] |
| **5.3** | Mid-Wait Early Sampling | `sensor_task.cpp` | During 15s wait, twist HR pot | ADC pot re-read detects worse vitals; wait breaks early; immediate telemetry publication | [ ] |
| **5.4** | Manual Sampling Slider | `sensor_task.cpp` | Set sampling slider to 25s | Queue updates interval; mode becomes Manual; OLED shows `Sampling: 25s (Man)` | [ ] |
| **5.5** | Manual Rate Range Clamp | `mqtt_task.cpp` | Send rate 2s or 75s | Rejected by command parser; error message returned; interval clamped to [5, 60]s | [ ] |
| **6.1** | Sim Failure: Wi-Fi Down | `main.cpp` / D12 Button | Press D12 once | Wi-Fi disconnects; state -> `OFFLINE`; OLED header: `LOGGING OFFLINE [N]`; 3 long beeps play | [ ] |
| **6.2** | Offline SRAM Buffering | `mqtt_task.cpp` | While in OFFLINE mode | Samples buffer in SRAM ring (count increments up to 30); `s_dropsCount` tracks overflows | [ ] |
| **6.3** | Sim Failure: MQTT Down | `main.cpp` / D12 Button | Press D12 second time | Wi-Fi reconnects; MQTT held down; state -> `DEGRADED`; OLED: `DEGRADED (NO MQTT)`; 2 short beeps | [ ] |
| **6.4** | Return to Online | `main.cpp` / D12 Button | Press D12 third time | State -> `ONLINE`; Wi-Fi & MQTT connected; OLED restores normal header | [ ] |
| **6.5** | Offline Batch Resync | `mqtt_task.cpp` | Upon return to `ONLINE` | `xSyncRequestSem` triggers flush; sends 5 records/batch to `historical/sync` until count=0 | [ ] |
| **6.6** | Non-Lossy Flush Retry | `mqtt_task.cpp` | Disconnect during batch sync | Records remain in buffer; retry exponential backoff kicks in; resumes on reconnect | [ ] |
| **6.7** | Dashboard Stale Watchdog | `dashboard/index.html` | Halt MQTT publisher | Watchdog expires after max(2*interval, 40s); banner shows `DEVICE: STALE`; clears on resume | [ ] |

