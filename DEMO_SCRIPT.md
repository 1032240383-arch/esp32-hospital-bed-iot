# DEMO_SCRIPT.md — Screen Recording Demonstration Guide

A timed, step-by-step demonstration walkthrough for the **ESP32 FreeRTOS Smart Hospital Bed Monitor** internship submission video (Target Duration: 6 to 8 minutes).

---

## 🎬 Pre-Recording Setup Checklist
- [ ] Open VS Code with `c:\RTOS` workspace.
- [ ] Ensure PlatformIO build is clean: `pio run`.
- [ ] Launch `dashboard/index.html?prefix=hospital_UNIQUE_ID_HERE/ward_A/bed_01` in your browser.
- [ ] Open the Serial Monitor at 115200 baud.
- [ ] Position VS Code (Wokwi simulation + Serial) on the left half of the screen, and the Web Dashboard on the right half.

---

## ⏱️ Video Script Timeline (Total: ~7 Minutes)

### 0:00 – 1:00 | Introduction & Requirement 1: Multi-Sensor FreeRTOS Architecture
* **Narration:** "Welcome to the demonstration of the ESP32 FreeRTOS Smart Hospital Bed Monitor. This firmware coordinates 11 concurrent tasks across 4 priority levels to deliver real-time telemetry, medical dosage safety, remote bed articulation, adaptive sampling, and offline resilience."
* **Actions on Screen:**
  1. Highlight `main.cpp` and `config.h`, showing the 11 tasks and priority hierarchy.
  2. Press **Play** in Wokwi Simulator.
  3. Point to the Serial Monitor output showing all 11 tasks starting up:
     ```
     [Main] All 11 FreeRTOS tasks initialized and running!
     [Supervisor] Health: Sensor=OK Disp=OK MQTT=OK WiFi=OK Alert=OK Bed=OK Env=OK BP=OK ECG=OK
     ```
  4. Point to the supervisor task feeding the Task Watchdog Timer (TWDT) and logging stack high-water marks.
  5. Show the SSD1306 OLED boot splash and transition to Page 1 (Vitals: HR, SpO2, Temp, BP, ECG).

---

### 1:00 – 2:15 | Requirement 2: Dual-Role MQTT Dashboards & Aggregation
* **Narration:** "Requirement 2 specifies dual-role dashboards for clinical staff and facility engineers over separate MQTT topics."
* **Actions on Screen:**
  1. On the web dashboard, show the **Medical Staff** tab:
     - Point to the SVG gauges for Heart Rate, SpO2, and Body Temperature.
     - Point to the live derived Blood Pressure (`120/80 - Normotensive`) and ECG rhythm card (`Normal Sinus`).
  2. Switch to the **Facility Mgmt** tab:
     - Point to Room Temperature, Ambient Humidity, Room O2 (`20.9%`), and AQI (`42 - Good`).
  3. Turn the Heart Rate potentiometer up to 135 bpm:
     - Show the Medical Staff gauge sweep into the Amber Warning zone.
     - Show the derived BP automatically calculate higher systolic/diastolic pressure.
     - Show an alert entry instantly pop into the Medical Alerts feed.
  4. Wait for the 30-second mark:
     - Point to the Rolling Aggregation card updating with min, max, and avg values computed over the 30-second window.

---

### 2:15 – 3:30 | Requirement 3: Smart Medication Dosage Control
* **Narration:** "Requirement 3 controls medication dosage with rate-limiting step clamping, running dose integration, and mandatory confirmation for critical rates."
* **Actions on Screen:**
  1. Show the Medication section on the dashboard and Page 2 of the OLED display (`Infuse: 0.0 mg/h`).
  2. **Demonstrate Safety Clamping:** Drag the dosage slider from 0 to 50 mg/hr:
     - The firmware clamps the abrupt jump to +20 mg/hr maximum step.
     - A dashboard toast notification appears: `CLAMPED: requested=50.0 applied=20.0`.
     - The OLED updates to `Infuse: 20.0 mg/h`, and an audible chirp sounds.
  3. Step the rate up safely: 20 -> 40 -> 60 -> 80 mg/hr:
     - Point out the badge transitioning from `NORMAL` to `WARNING` at >50 mg/hr.
     - Show the running dosage integral (`Dose: X.XX mg`) steadily accumulating every second on the OLED and dashboard.
  4. **Demonstrate High-Dose Confirmation (>80 mg/hr):** Request 90 mg/hr:
     - The firmware rejects immediate activation and flags `PENDING_CONFIRMATION`.
     - The red LED starts blinking rapidly (4 blinks/second) and the buzzer sounds.
     - The OLED displays `>> CONFIRM NEEDED <<`.
     - A prominent **"CONFIRM HIGH DOSAGE"** button appears on the dashboard.
  5. Click the Confirm button:
     - The alarm latches into active critical infusion, the rate sets to 90.0 mg/hr, and the badge turns red `CRITICAL`.
  6. Reset dosage back to a safe 30 mg/hr.

---

### 3:30 – 4:45 | Requirement 4: Remote Bed Elevation & Safety Constraints
* **Narration:** "Requirement 4 provides remote bed elevation from 0 to 90 degrees using smooth servo transitions, presets, mid-sweep preemption, and automatic elevation on hypoxic distress."
* **Actions on Screen:**
  1. Show the servo at 0° (flat).
  2. Click the **SLEEP (10°)** preset:
     - The servo steps smoothly at 1° per 25 ms until reaching 10°.
  3. Click the **BREATHE (45°)** preset:
     - The servo sweeps smoothly from 10° to 45°. OLED shows `Bed: 45 deg [BREATHE]`.
  4. **Demonstrate Emergency Preemption:**
     - Command the bed to 0°, then immediately send an angle command to 60°.
     - While the servo is mid-sweep (around 20°), click the **EMERGENCY (90°)** button.
     - Show in the Serial Monitor and servo animation that the 60° sweep is immediately preempted, and the servo drives directly to 90° (upright Fowler position).
  5. **Demonstrate Automatic Hypoxic Elevation:**
     - Return the bed to 0° (flat).
     - Rotate the SpO2 potentiometer on GPIO 35 down below 90% (e.g. 88%).
     - Observe: `SensorTask` detects low SpO2, logs `[Bed Auto] Low SpO2 (<90%) detected! Auto-elevating to Breathing Support (45 deg)`.
     - The OLED flashes a full-screen alert: `AUTO BED ADJUST / SpO2 < 90%! / BED -> 45 deg`.
     - The servo automatically elevates the bed to 45°.

---

### 4:45 – 5:45 | Requirement 5: Dynamic Adaptive Sampling
* **Narration:** "Requirement 5 implements adaptive telemetry sampling, non-blocking sliced waiting, and dynamic queue reconfiguration."
* **Actions on Screen:**
  1. Show the OLED Page 2 displaying `Sampling: 15s (Auto)`.
  2. Press and hold the **Force Abnormal** pushbutton (GPIO 13):
     - Within 250 ms, the sliced wait detects abnormal vitals.
     - Sampling rate drops from 15 seconds to 5 seconds (`Sampling: 5s (Auto)`).
     - Serial shows accelerated telemetry publishing every 5 seconds.
  3. Release the button:
     - Vitals normalize, and the sampling rate smoothly returns to 15 seconds.
  4. Move the **Sampling Rate Slider** to 30 seconds:
     - Show mode transition to `Sampling: 30s (Man)`.
     - Show that the supervisor's 10-second watchdog window continues receiving 500 ms sliced heartbeats, preventing false watchdog reboots.
  5. Toggle back to **Auto** mode.

---

### 5:45 – 7:00 | Requirement 6: Offline Detection, Ring Buffer & Resync
* **Narration:** "Requirement 6 demonstrates three-state network failure detection, local SRAM circular buffering, failure-specific audio alerts, and non-lossy resynchronization."
* **Actions on Screen:**
  1. **Simulate OFFLINE (Wi-Fi Down):** Press the simulate-failure button on GPIO 12 once:
     - Wi-Fi disconnects.
     - The buzzer plays 3 distinct long beeps (1000 Hz).
     - The OLED header renders an inverted white bar reading `LOGGING OFFLINE [N]`.
     - Web dashboard pill turns red: `DEVICE: OFFLINE`.
  2. Watch the buffer count increment on the OLED header (`[1]`, `[2]`, `[3]`, etc.) as readings store into the 30-record SRAM ring buffer.
  3. **Simulate DEGRADED (MQTT Down):** Press GPIO 12 a second time:
     - Wi-Fi reconnects, but MQTT remains disconnected.
     - The buzzer plays 2 distinct short beeps (1800 Hz).
     - The OLED header displays `DEGRADED (NO MQTT)`.
     - Dashboard pill turns amber: `DEVICE: DEGRADED`.
  4. **Return to ONLINE & Flush:** Press GPIO 12 a third time:
     - The system returns to normal.
     - Wi-Fi and MQTT connect (`ONLINE`).
     - `xSyncRequestSem` immediately triggers the non-lossy batch flush state machine.
     - Show the Serial Monitor streaming batches of 5 historical records to `.../historical/sync` until the buffer reaches 0:
       ```
       [MQTT] Starting offline flush: 6 records
       [MQTT] Offline flush complete: synced=6, dropped=0
       ```
     - Show the OLED header restore to normal `WARD-A BED-01 [P1/2]`.
  5. Conclude the video by showing clean build metrics and host test verification.

