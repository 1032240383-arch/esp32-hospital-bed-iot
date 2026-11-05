# BUGLOG — ESP32 Healthcare IoT Monitor

All bugs found during development are recorded here with their root cause and fix.
This log is kept for internship report evidence and documentation.

---

## BUG-001 | PlatformIO / Encoding | 2026-10-11 | Medium

**Symptom:** PlatformIO CLI failed to parse `platformio.ini` with `MissingSectionHeaderError: File contains no section headers`.
**Root cause:** PowerShell default encoding on Windows inserted a UTF-8 Byte Order Mark (BOM: `EF BB BF`) before `[env:esp32dev]`. Python's `ConfigParser` could not recognize the initial section header.
**Fix:** Rewrote `platformio.ini` using explicit UTF-8 without BOM (`System.Text.UTF8Encoding($false)`).
**Files changed:** `platformio.ini`

---

## BUG-002 | Watchdog / ESP-IDF Compatibility | 2026-10-11 | High

**Symptom:** `error: 'esp_task_wdt_config_t' was not declared in this scope; esp_task_wdt_init(&wdtCfg)` during `pio run`.
**Root cause:** PlatformIO ESP32 core 2.0.x is based on ESP-IDF 4.4, where `esp_task_wdt_init` takes `(uint32_t timeout, bool panic)`. `esp_task_wdt_config_t` was only introduced in ESP-IDF 5.0+.
**Fix:** Added `#include <esp_idf_version.h>` and dual version conditional compilation:
```cpp
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    esp_task_wdt_config_t wdtCfg = {
        .timeout_ms     = WATCHDOG_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic  = false
    };
    esp_task_wdt_init(&wdtCfg);
#else
    esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, false);
#endif
```
**Files changed:** `src/supervisor_task.cpp`

---

## BUG-003 | Aggregation Syntax & Queue Depths | 2026-10-11 | Low

**Symptom:** `error: 'SENSOR_QUEUE_DEPTH' was not declared in this scope` and `unable to find numeric literal operator 'operator""fs_agg.rtempMax'`.
**Root cause:** Missing macro defines in `config.h` after multi-feature expansion, plus a missing semicolon in `mqtt_task.cpp` rolling aggregation reset.
**Fix:** Added `#define SENSOR_QUEUE_DEPTH 5` and `#define DISPLAY_QUEUE_DEPTH 10` in `config.h`, and corrected semicolon in `mqtt_task.cpp`.
**Files changed:** `src/config.h`, `src/mqtt_task.cpp`

---

## BUG-004 | Wokwi Simulator / TOML Encoding | 2026-10-11 | Medium

**Symptom:** Wokwi extension failed to start with error: `Unknown character "65279" at row 1, col 2, pos 1`.
**Root cause:** `wokwi.toml` contained a UTF-8 Byte Order Mark (character code 65279 / `0xFEFF`) created during Windows PowerShell file generation. TOML parsers do not support BOMs.
**Fix:** Rewrote `wokwi.toml` using clean UTF-8 without BOM (confirmed initial byte `0x5B` for `[`).
**Files changed:** `wokwi.toml`

---

## BUG-005 | Continuous Alarm & Potentiometer Uninitialized Boot | 2026-10-11 | Medium

**Symptom:** Simulator boots with buzzer continuously sounding critical emergency alarm and bed auto-elevating.
**Root cause:** Wokwi potentiometers default to 0 at power-on without an initial value attribute. ADC 0 mapped to SpO2 85%, triggering immediate hypoxia alert (< 90%). In `alert_task.cpp`, critical alerts looped without clearing when vitals stabilize.
**Fix:** Added initial values in `diagram.json` (`pot_hr` 512 / ~75 bpm, `pot_spo2` 3600 / ~98%). Added `ALERT_NONE` and `noTone()` in `alert_task.cpp` and `sensor_task.cpp` to silence the alarm when vitals are normal.
**Files changed:** `diagram.json`, `src/alert_task.cpp`, `src/sensor_task.cpp`

---

## BUG-006 | OLED Garbled Display & Invalid GND Pins | 2026-10-11 | High

**Symptom:** OLED display showed corrupted, shifted, noisy characters ("SMAFT HEALT-CARE") and froze on the splash screen without progressing.
**Root cause:** In `diagram.json`, non-existent pin names (`esp:GND.4` through `esp:GND.8`) caused Wokwi's connection parser to drop connections. As a result, `oled:VCC` and `oled:GND` were completely disconnected, leaving the SSD1306 unpowered and floating with stray signals on SDA.
**Fix:** Updated all GND lines to valid `esp:GND.1` and `esp:GND.2` pins, re-established all 4 OLED connections (GND, 3V3, SDA=D21, SCL=D22), and recompiled.
**Files changed:** `diagram.json`

---

## BUG-007 | Adafruit SSD1306 Default 400kHz I2C Clock Jitter | 2026-10-11 | High

**Symptom:** Text exhibited bit-flip glitches (`SNART` instead of `SMART`, `1oT` instead of `IoT`) and stalled on the boot splash.
**Root cause:** `Adafruit_SSD1306::begin()` internally overrides the I2C clock to 400 kHz (`Wire->setClock(400000)`). In Wokwi's ESP32 CPU-capped simulation (~8 MHz simulated core), 400 kHz I2C causes timing jitter on simulated clock edges, corrupting transmitted bit sequences and locking up multi-packet transfers.
**Fix:** Explicitly clamped `Wire.setClock(100000)` and set `Wire.setTimeOut(50)` immediately after `oled.begin()`, and added cached state fallback in `renderScreen()`.
**Files changed:** `src/display_task.cpp`

---

## BUG-008 | Sim-Failure Button Contention & State Overwrite | 2026-11-05 | High

**Symptom:** Simulating network failure via push button D12 was erratic; pressing the button would frequently revert immediately or fail to maintain the offline state.
**Root cause:** `sensor_task.cpp` read `PIN_BTN_SIM_FAIL` synchronously every sample iteration and overwrote `g_systemState.simFailureActive` with the raw button pin level. Concurrently, `main.cpp` contained an edge-detecting toggle loop on the same pin. When the button was released (pin returned HIGH), `sensor_task.cpp` cleared `simFailureActive` immediately, defeating the intended toggle. Furthermore, `main.cpp` lacked a debounce timer, causing switch contact chatter.
**Fix:** 
1. Removed all `PIN_BTN_SIM_FAIL` pin configuration and digital reads from `sensor_task.cpp`.
2. Centralized toggle logic in `main.cpp` with a 50 ms non-blocking debounce timer (`millis() - s_lastDebounceMs > 50`).
**Files changed:** `src/sensor_task.cpp`, `src/main.cpp`

---

## BUG-009 | Watchdog Panic Disabled & Sleep Heartbeat Starvation | 2026-11-05 | Critical

**Symptom:** Task lockups or deadlocks failed to reset the ESP32 hardware. Conversely, healthy tasks executing long delays (e.g., WiFi backoff up to 32 seconds, sensor sampling up to 60 seconds) triggered spurious watchdog failure detections because alive bits were not asserted during long `vTaskDelay` intervals.
**Root cause:** 
1. In `supervisor_task.cpp`, `trigger_panic` was set to `false` in both IDF v5 and IDF v4.4 initialization branches (`esp_task_wdt_init(..., false)`), causing watchdog expiration to merely log a warning instead of rebooting the processor.
2. In `sensor_task.cpp`, `wifi_manager.cpp`, and `mqtt_task.cpp`, long delays executed single blocking `vTaskDelay()` calls exceeding the 10-second supervisor window, starving the event group heartbeat.
**Fix:**
1. Set `trigger_panic = true` across both IDF 5 and IDF 4.4 branches in `supervisor_task.cpp`.
2. Created `task_utils.h` providing inline helper `taskSleepWithHeartbeat(totalMs, eventBit)` that slices long delays into <=500 ms increments, setting the task's alive bit in `xTaskHealthBits` at each slice.
3. Updated `sensor_task.cpp`, `wifi_manager.cpp`, and `mqtt_task.cpp` to use heartbeat-safe sliced sleep during reconnects and idle intervals.
**Files changed:** `src/supervisor_task.cpp`, `src/task_utils.h`, `src/sensor_task.cpp`, `src/wifi_manager.cpp`, `src/mqtt_task.cpp`

---

## BUG-010 | Alert Queue Priority Inversion & Mutex Contention | 2026-11-05 | High

**Symptom:** Critical physiological alarms (e.g., ventricular arrhythmia or hypoxia) were frequently silenced or replaced by lower-severity alerts (such as MQTT reconnect chirps or `ALERT_NONE`). In addition, priority inversion occurred when `vMedicationIntegrationTask` attempted to trigger alerts while holding `xSharedStateMutex`.
**Root cause:** `alert_trigger()` simply performed an unconditional `xQueueOverwrite(xAlertQueue, &pattern)`. Any lower-priority or `ALERT_NONE` trigger immediately wiped out an active emergency alarm. Furthermore, calling `alert_trigger()` while holding `xSharedStateMutex` led to lock contention with other tasks needing shared state.
**Fix:**
1. Re-implemented `alert_trigger()` with strict numeric priority ranking:
   `ALERT_MED_CRIT (5) > ALERT_CRIT_VITALS (4) > ALERT_OFFLINE_WIFI (3) > ALERT_DEGRADED_MQTT (2) > ALERT_WARN_VITALS (1) > ALERT_CHIRP (0) > ALERT_NONE (-1)`.
2. `alert_trigger()` peeks at the pending queue item via `xQueuePeek()`; if the new alert's priority is lower than or equal to the current pending alert, the call is safely ignored. `ALERT_NONE` never overwrites an active alert.
3. Refactored `medication_task.cpp` to copy medication rate and state out first, release `xSharedStateMutex`, and only then call `alert_trigger()`.
**Files changed:** `src/alert_task.cpp`, `src/medication_task.cpp`

---

## BUG-011 | Sensor Queue Data Loss & Silent Overwrite | 2026-11-05 | Medium

**Symptom:** During transient network slowdowns, newly produced sensor readings silently destroyed unconsumed readings in `xSensorDataQueue` without telemetry logging.
**Root cause:** `sensor_task.cpp` used `xQueueOverwrite(xSensorDataQueue, &data)`, which always clobbered whatever was in the 1-element queue, losing peak abnormal readings.
**Fix:** Converted `xSensorDataQueue` to a true FIFO queue. The sensor task attempts `xQueueSend(..., 0)`. If full, it pops and drops the oldest reading using `xQueueReceive()`, logs a warning to Serial, and enqueues the fresh sample.
**Files changed:** `src/sensor_task.cpp`

---

## BUG-012 | Monolithic Sensor Task Architectural Coupling | 2026-11-05 | High

**Symptom:** Potentiometer vital sampling, DHT22 environmental reading, Blood Pressure synthesis, and ECG classification all ran sequentially inside `vSensorTask`. If the DHT22 bus timed out (taking 250+ ms), vital sign sampling jittered, and task failure meant complete failure of all telemetry.
**Root cause:** Violation of FreeRTOS separation of concerns. Single task handled distinct subsystems with different timing characteristics and fault domains.
**Fix:** Decomposed the architecture into 4 separate concurrent FreeRTOS tasks:
- `vSensorTask` (Priority 3, Core 1): ADC potentiometer acquisition (HR, SpO2, derived Temp), auto-elevation trigger, adaptive sampling logic.
- `vEnvTask` (Priority 2, Core 1): DHT22 sampling (room temp, humidity) and bounded random-walk room O2 & AQI simulation.
- `vBpTask` (Priority 2, Core 1): Blood Pressure derived from HR + noise, abnormal mode override (160/102 mmHg).
- `vEcgTask` (Priority 2, Core 1): Heart rate classification (Normal Sinus, Tachycardia, Bradycardia, Arrhythmia).
Each task maintains its own health bit (`BIT_ENV_ALIVE`, `BIT_BP_ALIVE`, `BIT_ECG_ALIVE`) and dedicated stack.
**Files changed:** `src/env_task.h`, `src/env_task.cpp`, `src/bp_task.h`, `src/bp_task.cpp`, `src/ecg_task.h`, `src/ecg_task.cpp`, `src/config.h`, `src/main.cpp`, `src/sensor_task.cpp`

---

## BUG-013 | Medication Rate-Limiting Clamp & Confirmation Timeout | 2026-11-05 | High

**Symptom:** Remote commands requesting sudden medication jumps (e.g., +40 mg/hr) were either abruptly rejected without safety adjustment or jumped dangerously. Requests >80 mg/hr lacked confirmation or remained pending indefinitely without timing out.
**Root cause:** Lack of a clinical safety clamp algorithm and missing confirmation expiration timer in `medication_task.cpp`.
**Fix:**
1. Implemented rate clamp: any positive rate jump exceeding `+MED_RATE_LIMIT_STEP` (+20 mg/hr) is automatically clamped: `target = current + 20.0f`.
2. Critical rates (>80 mg/hr) set `requiresConfirmation = true` and `pendingRateMgHr`.
3. Added a 30-second confirmation timeout: if `CONFIRM` is not received within 30 seconds, `pendingRateMgHr` is cancelled and `requiresConfirmation` is cleared.
4. Returns distinct status responses ("ACCEPTED", "CLAMPED", "PENDING_CONFIRMATION", "REJECTED").
5. Published retained medication state every 5 seconds to `.../medical/medication_status`.
**Files changed:** `src/medication_task.cpp`, `src/mqtt_task.cpp`

---

## BUG-014 | Bed Emergency Preset Angle & Sweep Preemption | 2026-11-05 | High

**Symptom:** Emergency bed preset moved bed to 0° (flat) instead of clinical emergency elevation (90° upright Fowler position). During long sweeps (up to 2.25s for 90°), the task blocked entirely and could not respond to an immediate EMERGENCY command until the previous sweep finished.
**Root cause:**
1. `BED_PRESET_EMERGENCY` was incorrectly defined as 0° (which is CPR flat rather than upright Fowler).
2. The servo sweep loop (`vTaskDelay(25ms)`) did not inspect `xBedCmdQueue` during motion.
**Fix:**
1. Set `BED_PRESET_EMERGENCY = 90` in `config.h`.
2. Updated `vBedControlTask` sweep loop to peek `xBedCmdQueue` using `xQueuePeek()` at every 25 ms step. If an EMERGENCY command arrives, it consumes the command immediately, overrides the target angle to 90°, and preempts the active movement without delay. Non-emergency commands retarget smoothly without overshoot.
**Files changed:** `src/config.h`, `src/bed_task.cpp`

---

## BUG-015 | Blocking Delay in Adaptive Sampling Reconfiguration | 2026-11-05 | Medium

**Symptom:** When switching sampling interval from 60 seconds to 5 seconds via MQTT, the sensor task continued sleeping for the full remaining duration of the 60-second delay before adopting the new rate.
**Root cause:** `vSensorTask` used a single monolithic `vTaskDelay(pdMS_TO_TICKS(currentIntervalMs))`.
**Fix:**
1. Introduced `xSamplingCmdQueue` to decouple command reception in MQTT task from execution in sensor task.
2. Refactored sensor delay into a 250 ms sliced polling loop that continuously drains `xSamplingCmdQueue`, recomputes intervals immediately, and triggers an instant early sample if vitals transition to abnormal.
**Files changed:** `src/shared_types.h`, `src/sensor_task.cpp`, `src/mqtt_task.cpp`, `src/main.cpp`

---

## BUG-016 | MQTT Reconnection Storm & Historical Flush Data Loss | 2026-11-05 | High

**Symptom:** When Wi-Fi reconnected, MQTT attempted rapid reconnect loops without exponential backoff. Upon reconnection, flushing the 30-record offline buffer in a single tight loop caused socket buffer overflow and packet drops at the public HiveMQ broker. If flush failed midway, un-acked records were lost.
**Root cause:** Lack of exponential backoff in MQTT reconnection state machine and lack of non-lossy batching in historical buffer flush.
**Fix:**
1. Implemented socket timeout (`mqttClient.setSocketTimeout(5)`).
2. Added exponential backoff reconnect logic (1s, 2s, 4s, 8s, 16s, max 32s).
3. Built non-lossy batch-flush state machine: transmits 5 records per batch to `.../historical/sync`, verifies each publish return code, only advances the circular buffer read pointer upon confirmed successful transmission, and backs off on failure.
4. Publishes a summary record to `.../historical/summary` upon full buffer drainage.
**Files changed:** `src/mqtt_task.cpp`, `src/config.h`

---

## BUG-017 | Bed Servo Sweep Direction Recomputation & Emergency Sweep Protection | 2026-11-05 | High

**Symptom:** During bed elevation sweeps, if a mid-sweep retarget was received such that `target == currentAngle`, the step direction was left as `-1` from the prior direction and `currentAngle += step` was unconditionally executed without checking for arrival. The loop would count down below 0°, hanging the servo sweep while its heartbeat remained active. Furthermore, non-emergency commands received during an active EMERGENCY sweep would override the emergency Fowler position.
**Root cause:**
1. `step` was calculated only once at loop entry rather than recomputed every iteration.
2. The arrival check `currentAngle == target` occurred after incrementing rather than before.
3. Mid-sweep command peeking did not differentiate between emergency and non-emergency sweeps.
**Fix:**
1. Recomputed `int step = (target > currentAngle) ? 1 : -1;` every iteration of the sweep.
2. Added `if (currentAngle == target) break;` immediately before stepping.
3. Added hard clamp guards: `if (currentAngle < BED_ANGLE_MIN) currentAngle = BED_ANGLE_MIN;` and max guard with Serial logging.
4. Preserved active EMERGENCY sweeps: non-emergency commands arriving during an EMERGENCY sweep are left untouched in `xBedCmdQueue` until the emergency sweep finishes, while an EMERGENCY command immediately preempts any non-emergency sweep.
**Files changed:** `src/bed_task.cpp`

---

## BUG-018 | 3-Mode Simulated Failure Cycle on Pin D12 | 2026-11-05 | Medium

**Symptom:** Pressing the D12 simulated failure button toggled a binary boolean `simFailureActive`, which only tested complete Wi-Fi disconnection (OFFLINE). There was no mechanism to test broker failure with healthy Wi-Fi (DEGRADED).
**Root cause:** Binary flag representation instead of multi-state failure mode enumeration.
**Fix:**
1. Replaced `bool simFailureActive` with `SimFailMode_t simMode` in `shared_types.h` with values `SIM_NONE`, `SIM_WIFI_DOWN`, and `SIM_MQTT_DOWN`.
2. Updated `pollSimFailButton()` in `main.cpp` with 50 ms debounce to cycle through `SIM_NONE -> SIM_WIFI_DOWN -> SIM_MQTT_DOWN -> SIM_NONE`.
3. In `SIM_WIFI_DOWN`, Wi-Fi disconnects, triggering `ALERT_OFFLINE_WIFI` (3 long beeps, once on transition).
4. In `SIM_MQTT_DOWN`, Wi-Fi remains connected while MQTT disconnects and suspends reconnect attempts, triggering `ALERT_DEGRADED_MQTT` (2 short beeps, once on transition).
5. OLED page-1 footer displays active simulation mode (`SIM:WIFI-DOWN` or `SIM:MQTT-DOWN`).
**Files changed:** `src/shared_types.h`, `src/main.cpp`, `src/wifi_manager.cpp`, `src/mqtt_task.cpp`, `src/display_task.cpp`

---

## BUG-019 | Centralized Connection State Arbiter | 2026-11-05 | Medium

**Symptom:** `connState` was independently and redundantly computed across `wifi_manager.cpp` and `mqtt_task.cpp`. `connectMQTT()` directly assigned `g_systemState.connState = CONN_ONLINE` without verifying system health or free heap margin, causing state synchronization glitches.
**Root cause:** Distributed state evaluation logic without a single authoritative arbiter.
**Fix:**
1. Created `src/conn_state.h` and `src/conn_state.cpp` with a unified `updateConnState()` function.
2. Reads `wifiConnected`, `mqttConnected`, `simMode`, `systemHealthy`, and heap status under `xSharedStateMutex`.
3. Evaluates hierarchical priority: `SIM_WIFI_DOWN` or `!wifiConnected` -> `CONN_OFFLINE`; `SIM_MQTT_DOWN`, `!mqttConnected`, `!systemHealthy`, or heap < 20KB -> `CONN_DEGRADED`; otherwise -> `CONN_ONLINE`.
4. Called by `WiFiTask`, `MQTTTask`, `main.cpp`, and connection event handlers.
**Files changed:** `src/conn_state.h`, `src/conn_state.cpp`, `src/wifi_manager.cpp`, `src/mqtt_task.cpp`, `src/main.cpp`

---

## BUG-020 | Hardcoded "ONLINE" in Status Telemetry Heartbeat | 2026-11-05 | Low

**Symptom:** `publishStatusHeartbeat()` published `doc["conn"] = "ONLINE"` unconditionally, even when the system was running in DEGRADED mode.
**Root cause:** Hardcoded string literal in JSON serializer.
**Fix:** Replaced with thread-safe read of `g_systemState.connState`, serializing `"OFFLINE"`, `"DEGRADED"`, or `"ONLINE"`. In addition, published retained LWT status payloads (`{"state":"OFFLINE"}` and `{"state":"DEGRADED"}`) before simulated disconnects.
**Files changed:** `src/mqtt_task.cpp`

---

## BUG-021 | Sliced Wait Did Not Re-Read Potentiometers Mid-Wait | 2026-11-05 | Medium

**Symptom:** When monitoring at 15-second or 60-second intervals, adjusting the potentiometers to simulate a patient emergency did not trigger an immediate telemetry report until the full sleep interval expired. Only the digital button had early-wake logic.
**Root cause:** Sliced delay loop only checked `PIN_BTN_ABNORMAL` and did not sample ADC pots.
**Fix:** Added non-blocking ADC reads (`analogRead(PIN_POT_HR)` and `analogRead(PIN_POT_SPO2)`) inside each 250 ms slice of `vSensorTask`. If re-classified vitals are more severe than at loop entry, the sleep breaks immediately to publish new telemetry and trigger alarms without latency.
**Files changed:** `src/sensor_task.cpp`

---

## BUG-022 | Force-Abnormal Button Overrode Only Primary Vitals | 2026-11-05 | Medium

**Symptom:** Holding the Force Abnormal button (D13) forced HR=155, SpO2=88%, and Temp=39.8°C, but BP, ECG, O2, and AQI were read from shared state and remained normal (120/80, Sinus, 20.9%, 42 AQI) on the first sample, causing conflicting telemetry.
**Root cause:** Auxiliary vitals were only copied from shared state after the abnormal override block.
**Fix:** In `sensor_task.cpp`, when `btnAbnormal` is asserted, explicitly set `bpSys = 160`, `bpDia = 102`, `ecgStatus = "Arrhythmia"`, `oxygenLevel = 19.1f`, and `aqi = 125`, ensuring immediate clinical consistency across all telemetry topics.
**Files changed:** `src/sensor_task.cpp`

---

## BUG-023 | Critical Medication Alarm Non-Latching Behavior | 2026-11-05 | Medium

**Symptom:** When medication rate exceeded 80 mg/hr, `ALERT_MED_CRIT` sounded 2 rapid beeps and immediately reset `currentPattern = ALERT_NONE`, resulting in silence between 1-second task ticks instead of a continuous rapid warning.
**Root cause:** Pattern execution unconditionally cleared `currentPattern` at the end of each pattern cycle.
**Fix:** Added latching logic in `alert_task.cpp`. `alert_trigger(ALERT_MED_CRIT)` timestamps the trigger in `s_medCritLastTriggerMs`. In `case ALERT_MED_CRIT`, after pulsing 2 rapid strobe/beeps (~400 ms), the task checks if another `ALERT_MED_CRIT` was queued or if less than 1500 ms elapsed since the last trigger. If so, it stays in `ALERT_MED_CRIT`, producing seamless continuous rapid blinking until the rate drops <=80 mg/hr.
**Files changed:** `src/alert_task.cpp`

---

## BUG-024 | Starvation of Heartbeat in Disconnection Audio Patterns | 2026-11-05 | Low

**Symptom:** `ALERT_OFFLINE_WIFI` played 3 long beeps (300 ms sound + 200 ms silence = 1500 ms) without setting `BIT_ALERT_ALIVE` during the loop. If the supervisor checked right during this window, it saw a potential transient heartbeat delay.
**Root cause:** Loop used `playBeep()` which blocked FreeRTOS delay without asserting health bits.
**Fix:** Refactored `ALERT_OFFLINE_WIFI` and `ALERT_DEGRADED_MQTT` loops to assert `xEventGroupSetBits(xTaskHealthBits, BIT_ALERT_ALIVE)` between each beep and tone interval.
**Files changed:** `src/alert_task.cpp`

---

## BUG-025 | Offline Ring Buffer Drops Counter 8-Bit Overflow | 2026-11-05 | Low

**Symptom:** During prolonged offline periods (>60 minutes at 5s sampling), the dropped sample counter `s_dropsCount` wrapped around from 255 to 0, misreporting dropped telemetry counts during reconnection synchronization.
**Root cause:** `s_dropsCount` was declared as `uint8_t`.
**Fix:** Upgraded `s_dropsCount` to `uint16_t` in `src/mqtt_task.cpp`, supporting up to 65,535 dropped records without overflow.
**Files changed:** `src/mqtt_task.cpp`
