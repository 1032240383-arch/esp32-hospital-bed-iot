# ASSUMPTIONS.md — ESP32 Healthcare IoT Monitor

Design decisions, derivation rationale, and anything that the spec left open.
Date: 2026-11-05.

---

## 1. Simulated Sensor Derivations

### 1.1 Heart Rate & SpO2
Simulated via two ADC potentiometers (GPIO 34, 35). Mapping:
- HR: `60 + (raw / 4095) × 120` → range 60–180 bpm; smoothed with EMA α=0.25.
- SpO2: `85 + (raw / 4095) × 15` → range 85–100%; smoothed with EMA α=0.25.

EMA chosen to prevent abrupt jumps from pot jitter while still tracking intentional changes within 2–3 samples.

### 1.2 Blood Pressure (BpTask)
Derived from HR with ±3 mmHg Gaussian-approximated noise (uniform ±3 as a Wokwi-safe substitute):
```
systolic  = 115 + (HR - 70) × 0.40 + noise
diastolic =  75 + (HR - 70) × 0.22 + noise
```
Clamped to physiological range: systolic 90–180, diastolic 60–120.
Force-abnormal override: 160/102 mmHg (hypertensive crisis).

This derivation is a simplified linear model consistent with published HR–BP correlations for resting adults. It is not diagnostic.

### 1.3 Body Temperature
Derived from HR: `Temp = 36.8 + (HR - 75) × 0.015`. Gives ~36.8°C at rest and ~38.0°C at HR=160.

### 1.4 ECG Rhythm (EcgTask)
Classified from HR (before smoothing in abnormal mode):
- Normal Sinus: 55–110 bpm
- Tachycardia: >110 bpm
- Bradycardia: <55 bpm
- Arrhythmia: when Force Abnormal button (D13) is held

### 1.5 Room O2 & AQI (EnvTask — bounded random walk)
Baseline: O2=20.9%, AQI=42 (indoor "Good").
Each 5 s sample: O2 ±0.10%, AQI ±5; clamped to O2∈[20.0, 21.5], AQI∈[20, 120].

Rationale: static values would produce flat gauge lines on the dashboard, defeating the purpose of live monitoring. A bounded random walk produces realistic sensor drift without exceeding safe bounds during normal operation. Force Abnormal drives O2=19.1% (below `O2_WARN_LOW=19.5`) and AQI=125 (above `AQI_WARN_HIGH=100`).

---

## 2. Safety Thresholds (source: config.h, same as dashboard)

| Parameter | Warn | Critical |
|:--|:--|:--|
| Heart Rate | <50 or >120 bpm | <40 or >150 bpm |
| SpO2 | <94% | <90% |
| Body Temp | >38.0°C | >39.5°C |
| Room Temp | >30°C | — |
| Humidity | >70% | — |
| Room O2 | <19.5% | — |
| AQI | >100 | — |
| Medication rate | >50 mg/hr (WARNING) | >80 mg/hr (CRITICAL, requires confirm) |

Medication normal ≤50, warning 50–80, critical >80. These match the spec verbatim.

---

## 3. RAM Offline Buffer

The offline ring buffer is **30 `SensorData_t` records stored in static SRAM** (declared `static SensorData_t s_ringBuffer[30]` inside `mqtt_task.cpp`). No `malloc`, no SPIFFS.

Capacity: `SensorData_t` ≈ 72 bytes × 30 = **2,160 bytes** resident in RAM.

At 15 s stable sampling, 30 records = 7.5 minutes of offline data.
At 5 s abnormal sampling, 30 records = 2.5 minutes.

When the ring is full, the **oldest record is silently overwritten** (ring head advances). The overwrite count (`s_dropsCount`) is published in the flush-complete summary to `historical/sync`.

SPIFFS was **not used** because:
- Wokwi does not reliably simulate SPIFFS flash.
- 2.1 KB of static RAM is small and always available.
- SPIFFS write-wear and latency are incompatible with 5 s sampling.

---

## 4. Rate-Limit Clamp Design

The spec says "Rate limiting for abrupt increases" and separately says "CLAMPED" as a valid status. We chose to **clamp and apply** rather than reject:

If `requested - current > 20 mg/hr`, we set `applied = current + 20`. This applied value then goes through the normal >80 mg/hr confirmation gate. The response payload contains both `requested_rate` and the actual `applied` rate.

Rationale: a clinical pump that simply says "no" leaves the patient at the old rate. A pump that applies the nearest safe step is safer.

---

## 5. Confirmation Design (>80 mg/hr)

- Setting a rate >80 mg/hr sends `PENDING_CONFIRMATION` to the status topic.
- A second MQTT message to `cmd/medication/confirm` (any payload) activates the rate.
- If no CONFIRM arrives within **30 seconds**, the pending order is auto-cancelled and status returns to `NO_PENDING`. The OLED ">> CONFIRM NEEDED <<" clears.
- While pending OR while running >80 mg/hr, `ALERT_MED_CRIT` is re-triggered every 1 s by the MedicationTask, sustaining the LED/buzzer pattern.

---

## 6. Emergency Bed Preset = 90°

The spec says "EMERGENCY 90". The original code had `BED_PRESET_EMERGENCY = 0` (flat CPR). We corrected this to 90° (Fowler/Semi-Fowler upright position), which is the standard for respiratory distress, matching the spec literal.

If a flat CPR position is needed in a future revision, add a separate `BED_PRESET_CPR = 0` without removing EMERGENCY.

---

## 7. Auto Bed Elevation (SpO2 < 90%)

Auto elevation fires once per continuous hypoxic episode (flag `autoBedTriggered` cleared when SpO2 recovers). It posts a DISP_ALERT overlay ("AUTO BED ADJUST / SpO2 < 90%! / BED -> 45 deg") visible for 4 s.

---

## 8. Public Broker & Topic Uniqueness

The project uses `broker.hivemq.com:1883` (TCP) for firmware and `:8884` (WSS) for the dashboard. This broker is fully public — **any MQTT client can read and publish to these topics**. For a real submission or demo, change `MQTT_TOPIC_PREFIX` in `config.h` and the `const TOPIC_PREFIX` at the top of `dashboard/index.html` to include a unique student ID or UUID.

---

## 9. Watchdog Safety Reasoning

WATCHDOG_TIMEOUT_SEC = 30, check interval = 10 s.

- Each task sets its alive bit at least once every 500 ms (via `taskSleepWithHeartbeat`).
- The supervisor clears bits, sleeps 10 s, samples. In one 10 s window every healthy task sets its bit.
- The WDT is fed only when **all 9 bits are set AND free heap ≥ 20 KB**.
- With 30 s timeout and 10 s checks: 3 consecutive missed feeds trigger the panic reboot.
- At 60 s manual sampling: `taskSleepWithHeartbeat(60000, BIT_SENSOR_ALIVE)` calls `xEventGroupSetBits` every 500 ms — the supervisor sees the sensor bit set every window. No false reboot.

---

## 10. Stack Sizes

Sizes were chosen conservatively based on function call depth and local variable analysis:

| Task | Stack | Reasoning |
|:--|:--|:--|
| MQTTTask | 8192 | JSON serialisation, PubSubClient TLS-less TCP, ring buffer flush |
| WiFiTask / SensorTask / DisplayTask / EnvTask | 4096/4096/4096/3072 | I2C buffers, Adafruit GFX, DHT string formatting |
| All others | 2048 | Simple logic, small locals |

The supervisor logs `uxTaskGetStackHighWaterMark` every 60 s (requires task handles stored at creation). If any task shows <256 bytes free in serial output, increase its `STACK_xxx` define in `config.h`.

---

## 11. xSyncRequestSem Semantics

`xSyncRequestSem` is a **FreeRTOS binary semaphore** (not a mutex). WiFiTask calls `xSemaphoreGive` on successful reconnect. MQTTTask calls `xSemaphoreTake(..., 0)` (non-blocking) each loop — when it takes it, the offline flush state machine starts. The binary nature means a double-give (WiFi reconnects twice before MQTT flush completes) is absorbed safely.

---

## 12. Stack HWM Logging

`uxTaskGetStackHighWaterMark` requires a valid `TaskHandle_t`. All 11 tasks now save their handle to a named global (e.g., `xHandleSensor`, declared extern in `shared_types.h`). The supervisor reads all handles every 60 s and logs free stack bytes to serial. If a handle is null (task creation failed), the line is skipped.

---

## 13. Items Not Verifiable Without Running Wokwi

| Item | Reason |
|:--|:--|
| Servo physical sweep timing | Requires Wokwi's simulated servo animation |
| DHT22 temperature/humidity values | Wokwi DHT22 uses `diagram.json` attrs (`temperature`, `humidity`); reading is correct per code, but visually requires simulation |
| O2/AQI gauge movement on dashboard | Requires a live Wokwi + MQTT + browser session |
| 30 s confirmation timeout auto-cancel | Requires waiting 30 s in real or simulated time |
| Offline flush order correctness | Requires simulating Wi-Fi drop + reconnect cycle |
| Stack HWM actual values | Requires running firmware; design ensures ≥512 bytes margin |
| MQTT broker connectivity | Requires network access to broker.hivemq.com:1883 from within Wokwi |

---

## 14. 3-Mode Simulated Failure Cycle Rationale

The specification requires demonstrating three distinct system states: `ONLINE`, `DEGRADED`, and `OFFLINE`.  
A single push button on pin D12 toggles sequentially between three modes:
1. `SIM_NONE` (Normal Operation): Wi-Fi and MQTT both operate normally. The connection state is `ONLINE`.
2. `SIM_WIFI_DOWN`: Wi-Fi disconnects unconditionally. The connection arbiter reports `OFFLINE`. The OLED displays `LOGGING OFFLINE [N]` with an inverted top bar, and 3 long beeps play once on transition. Sensor readings are buffered locally into the SRAM ring buffer.
3. `SIM_MQTT_DOWN`: Wi-Fi stays connected, but the MQTT client disconnects and suspends reconnection attempts. The connection arbiter reports `DEGRADED`. The OLED displays `DEGRADED (NO MQTT)`, and 2 short beeps play once on transition. Sensor readings are also buffered locally in this state.
4. Returning to `SIM_NONE` triggers an immediate Wi-Fi / MQTT reconnect cycle, which asserts `xSyncRequestSem` to flush all buffered records non-lossily.

---

## 15. Stale-Data Watchdog Formula

In `dashboard/index.html`, telemetry data arrives at variable frequencies depending on adaptive sampling (5s abnormal vs 15s stable, or manual 5–60s).  
To prevent spurious "Device Offline" warnings during slow manual sampling (e.g. 60 seconds), the stale watchdog timeout threshold is defined dynamically as:
$$\text{Threshold} = \max(2 \times \text{currentSamplingIntervalMs}, 40000\text{ ms})$$
If no MQTT packet is received within this window, the dashboard marks the device as `DEVICE: STALE` and presents the red alert banner until the next packet arrives.

---

## 16. Latched ALERT_MED_CRIT Alarm

The medication safety requirement specifies that infusion rates >80 mg/hr must sound the buzzer and blink the red LED rapidly.  
Because `MedicationTask` operates on a 1-second interval, clearing the alert immediately after playing a pattern caused silence between seconds.  
To ensure continuous strobe and alarm while the critical rate is maintained:
- `alert_trigger(ALERT_MED_CRIT)` timestamps the trigger in `s_medCritLastTriggerMs`.
- The pattern stays latched for 1500 ms following the last trigger, giving the 1-second medication task a 500 ms margin to sustain seamless continuous blinking.
- As soon as the rate is dialed below 80 mg/hr, the trigger stops and the latch expires cleanly.

---

## 17. Emergency Bed Sweep Preemption & Queue Preservation

When the bed elevation task executes an EMERGENCY sweep (90° Fowler position):
- It rejects mid-sweep override by non-emergency commands (such as manual angle adjustments or sleep presets).
- Non-emergency commands are **not** discarded; they remain safely queued in `xBedCmdQueue` and will only be executed after the emergency sweep completes.
- Conversely, an incoming EMERGENCY command **immediately preempts** any active non-emergency sweep, stepping toward 90° without delay.

---

## 18. Ring Buffer Drops Counter Type (uint16_t)

In `mqtt_task.cpp`, `s_dropsCount` tracks the number of overwritten samples when the 30-record ring buffer is full during extended offline periods.  
At 5-second sampling, an 8-bit counter (`uint8_t`) overflows after only 21 minutes (256 drops).  
`s_dropsCount` is declared as `uint16_t`, supporting up to 65,535 dropped records (>90 hours offline) without arithmetic overflow.

---

## 19. Unique Topic Prefix CLI Synchronizer & Sanitization

To ensure multi-tenant isolation on the shared public HiveMQ broker, all project topics share a common base prefix: `hospital_<ID>/ward_A/bed_01`.  
The helper tool `tools/set_prefix.py` validates the unique ID against the regular expression `^[A-Za-z0-9_-]{4,24}$` and atomically updates:
1. `src/config.h` (`MQTT_TOPIC_PREFIX`)
2. `dashboard/index.html` (`TOPIC_PREFIX`)
3. `tools/mqtt_test.py` (`TOPIC_PREFIX`)
4. `README.md` (all documentation examples)

The web dashboard also supports dynamic override via the `?prefix=` URL parameter.
