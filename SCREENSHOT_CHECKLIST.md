# SCREENSHOT_CHECKLIST.md — Internship Evidence Checklist

To support the internship final submission report (`REPORT.md`), capture and save each screenshot listed below. Each item details the required filename, capture context, and exact UI or serial elements that must be clearly visible.

---

## 📸 Required Screenshot Manifest

| # | Image Filename | Phase / Requirement | Context to Capture | Visible Elements Required |
|:--|:--|:--|:--|:--|
| **1** | `01_build_success.png` | Build & Verification | VS Code Terminal | Output of `pio run` showing `RAM: 14.9%`, `Flash: 63.8%`, `[SUCCESS]`, and zero compile warnings/errors. |
| **2** | `02_wokwi_boot_serial.png` | Requirement 1 | VS Code + Serial Monitor | Serial output showing 11 FreeRTOS tasks created, priority listing, initial Free Heap (>100KB), and supervisor initial health log. |
| **3** | `03_oled_page1_vitals.png` | Requirement 1 | Wokwi SSD1306 Display | OLED Page 1 rendering: Header `WARD-A BED-01 [P1/2]`, HR (bpm), SpO2 (%), Temp (°C), BP (mmHg), ECG rhythm, and Env telemetry. |
| **4** | `04_oled_page2_controls.png` | Requirement 1 & 3 & 4 | Wokwi SSD1306 Display | OLED Page 2 rendering: Infusion rate (`mg/h`), Dose delivered (`mg`), Rate limit label, Bed angle with preset name, and Sampling rate. |
| **5** | `05_medical_dashboard.png` | Requirement 2 | Browser (Web Dashboard) | Medical Staff Tab: SVG gauges for Heart Rate, SpO2, Body Temp in safe green zones; BP card; ECG card; and 30s aggregate card. |
| **6** | `06_facility_dashboard.png` | Requirement 2 | Browser (Web Dashboard) | Facility Management Tab: Room Temp, Humidity, Room O2 (`20.9%`), AQI gauge (`42 - Good`), and Bed Angle elevation card. |
| **7** | `07_30s_aggregation.png` | Requirement 2 | Browser (Web Dashboard) | Detailed view of the 30-Second Rolling Aggregation tables on both Medical and Facility tabs showing min, max, and avg values. |
| **8** | `08_med_clamped_toast.png` | Requirement 3 | Browser (Web Dashboard) | Dosage slider dragged from 20 to 70 mg/hr; Toast notification showing `CLAMPED: requested=70.0 applied=40.0`; slider moves to 40. |
| **9** | `09_med_critical_pending.png` | Requirement 3 | Wokwi OLED + Browser | Dosage set to 90 mg/hr (>80 threshold); OLED shows `>> CONFIRM NEEDED <<`; Red LED blinking in Wokwi; Browser shows "CONFIRM HIGH DOSAGE" button. |
| **10**| `10_med_dose_integral.png` | Requirement 3 | Browser (Web Dashboard) | Infusion running at active dosage; "Total Delivered Dose" card showing accumulated dose in milligrams (e.g. `Dose: 3.45 mg`). |
| **11**| `11_bed_presets_sweep.png` | Requirement 4 | Wokwi Simulator | SG90 Servo horn articulating between presets; OLED displaying `Bed: 45 deg [BREATHE]`; Serial showing smooth 1°/25ms transition steps. |
| **12**| `12_bed_emergency_preempt.png`| Requirement 4 | Serial Monitor | Serial log proving emergency preemption: `[Bed] EMERGENCY command preempts current sweep! -> 90 deg`. |
| **13**| `13_auto_bed_hypoxia.png` | Requirement 4 | Wokwi OLED + Serial | SpO2 pot dialed <90%; Serial log: `[Bed Auto] Low SpO2 (<90%) detected! Auto-elevating to Breathing Support (45 deg)`; OLED alert overlay. |
| **14**| `14_adaptive_sampling_abnormal.png`| Requirement 5 | OLED Page 2 + Serial | D13 Force Abnormal button held; OLED footer shows `Sampling: 5s (Auto)`; Serial logs rapid 5-second sampling cycles. |
| **15**| `15_manual_sampling_slider.png` | Requirement 5 | Browser + OLED Page 2 | Dashboard slider set to 30s; OLED footer shows `Sampling: 30s (Man)`; Supervisor serial log showing 500ms sliced heartbeats alive. |
| **16**| `16_offline_logging_oled.png` | Requirement 6 | Wokwi SSD1306 Display | D12 button pressed once (SIM_WIFI_DOWN); Inverted white header bar reading `LOGGING OFFLINE [5]`; Web dashboard pill turns red `DEVICE: OFFLINE`. |
| **17**| `17_degraded_mqtt_oled.png` | Requirement 6 | Wokwi SSD1306 Display | D12 button pressed second time (SIM_MQTT_DOWN); OLED header reading `DEGRADED (NO MQTT)`; Web dashboard pill turns amber `DEVICE: DEGRADED`. |
| **18**| `18_offline_batch_resync.png` | Requirement 6 | Serial Monitor | D12 pressed third time (SIM_NONE); Wi-Fi & MQTT reconnect; Serial showing 5-record batches streaming to `.../historical/sync` and final summary. |
| **19**| `19_host_tests_passed.png` | Host Verification | Terminal / Command Line | Execution of `python tools/host_tests.py` showing `=== Summary: 26 PASSED, 0 FAILED ===`. |
| **20**| `20_mqtt_automated_test_suite.png`| Automated Verification| Terminal / Command Line | Execution of `tools/mqtt_test.py` showing full automated test matrix passing with zero failures. |

---

## 📁 Storage Recommendation
Store all captured image files in a dedicated `docs/screenshots/` directory within the project or alongside the final report PDF.

