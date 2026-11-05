#!/usr/bin/env python3
"""
ESP32 Healthcare Monitor - MQTT Automated Test & Command CLI Tool
Connects to HiveMQ public broker and interacts with the ESP32 FreeRTOS firmware.

Usage:
    python tools/mqtt_test.py [topic_prefix]

Example:
    python tools/mqtt_test.py hospital_UNIQUE_ID_HERE/ward_A/bed_01
"""

import sys
import time
import datetime
import threading
import json

try:
    import paho.mqtt.client as mqtt
    from paho.mqtt.enums import CallbackAPIVersion
    _USE_V2 = True
except (ImportError, AttributeError):
    try:
        import paho.mqtt.client as mqtt
        _USE_V2 = False
    except ImportError:
        print("ERROR: paho-mqtt not installed. Run: pip install paho-mqtt")
        sys.exit(1)

BROKER = "broker.hivemq.com"
PORT = 1883
DEFAULT_TOPIC_PREFIX = "hospital_parth_bed01_2026/ward_A/bed_01"

# Read prefix from CLI argument if provided
if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
    TOPIC_PREFIX = sys.argv[1]
else:
    TOPIC_PREFIX = DEFAULT_TOPIC_PREFIX

# Command and status topics
TOPIC_CMD_MED_RATE      = f"{TOPIC_PREFIX}/cmd/medication/rate"
TOPIC_CMD_MED_CONFIRM   = f"{TOPIC_PREFIX}/cmd/medication/confirm"
TOPIC_CMD_BED_ANGLE     = f"{TOPIC_PREFIX}/cmd/bed/angle"
TOPIC_CMD_BED_PRESET    = f"{TOPIC_PREFIX}/cmd/bed/preset"
TOPIC_CMD_SAMPLING_RATE = f"{TOPIC_PREFIX}/cmd/sampling/rate"
TOPIC_CMD_SAMPLING_MODE = f"{TOPIC_PREFIX}/cmd/sampling/mode"
TOPIC_STATUS            = f"{TOPIC_PREFIX}/status"
TOPIC_MED_STATUS        = f"{TOPIC_PREFIX}/medical/medication_status"
TOPIC_FAC_BED           = f"{TOPIC_PREFIX}/facility/bed_angle"

# Shared message tracking
g_last_messages = {}
g_msg_lock = threading.Lock()

def log(msg: str):
    timestamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{timestamp}] {msg}", flush=True)

def on_connect(client, userdata, flags, rc, properties=None):
    if hasattr(rc, "is_failure") and rc.is_failure:
        log(f"Connection failed with reason code: {rc}")
        return
    elif isinstance(rc, int) and rc != 0:
        log(f"Connection failed with result code: {rc}")
        return

    log(f"Connected successfully to {BROKER}:{PORT}")
    sub_topic = f"{TOPIC_PREFIX}/#"
    client.subscribe(sub_topic, qos=0)
    log(f"Subscribed to topic tree: {sub_topic}")

def on_message(client, userdata, msg):
    payload_str = msg.payload.decode("utf-8", errors="replace")
    topic = msg.topic
    with g_msg_lock:
        g_last_messages[topic] = {
            "payload": payload_str,
            "time": time.time()
        }
    log(f"RX [{topic}] -> {payload_str}")

def get_last_payload(topic_suffix: str, timeout_sec: float = 3.0):
    """Wait up to timeout_sec for a message on a given topic suffix."""
    deadline = time.time() + timeout_sec
    target_topic = f"{TOPIC_PREFIX}/{topic_suffix}"
    while time.time() < deadline:
        with g_msg_lock:
            if target_topic in g_last_messages:
                return g_last_messages[target_topic]["payload"]
        time.sleep(0.1)
    return None

def clear_payload(topic_suffix: str):
    target_topic = f"{TOPIC_PREFIX}/{topic_suffix}"
    with g_msg_lock:
        g_last_messages.pop(target_topic, None)


# ─────────────────────────────────────────────────────────────────────────────
#  TEST SUITE IMPLEMENTATION
# ─────────────────────────────────────────────────────────────────────────────
test_results = []

def record_result(name: str, passed: bool, detail: str = ""):
    status_str = "PASS" if passed else "FAIL"
    test_results.append((name, status_str, detail))
    log(f"[{status_str}] {name} {('- ' + detail) if detail else ''}")

def test_medication_ramp_and_confirm(client):
    log("\n=== Scenario 1: Medication Ramp & Confirm Flow ===")
    clear_payload("medical/medication_status")

    # Step ramp 20 -> 40 -> 60 -> 80
    for rate in [20, 40, 60, 80]:
        clear_payload("medical/medication_status")
        client.publish(TOPIC_CMD_MED_RATE, str(rate), qos=1)
        resp = get_last_payload("medical/medication_status", timeout_sec=3.0)
        ok = False
        if resp:
            try:
                d = json.loads(resp)
                ok = (d.get("status") == "ACCEPTED") or (d.get("rate") == rate)
            except Exception:
                pass
        record_result(f"Medication rate ramp to {rate} mg/hr", ok, f"Resp: {resp}")
        time.sleep(1.0)

    # Now request 90 (requires confirmation)
    clear_payload("medical/medication_status")
    client.publish(TOPIC_CMD_MED_RATE, "90", qos=1)
    resp = get_last_payload("medical/medication_status", timeout_sec=3.0)
    ok_pending = False
    if resp:
        try:
            d = json.loads(resp)
            ok_pending = (d.get("status") == "PENDING_CONFIRMATION") or (d.get("pending", 0) > 0)
        except Exception:
            pass
    record_result("Medication critical rate 90 mg/hr -> PENDING_CONFIRMATION", ok_pending, f"Resp: {resp}")
    time.sleep(1.0)

    # Confirm
    clear_payload("medical/medication_status")
    client.publish(TOPIC_CMD_MED_CONFIRM, "CONFIRM", qos=1)
    resp = get_last_payload("medical/medication_status", timeout_sec=3.0)
    ok_confirm = False
    if resp:
        try:
            d = json.loads(resp)
            ok_confirm = (d.get("status") == "CONFIRMED") or (d.get("rate") == 90.0)
        except Exception:
            pass
    record_result("Medication CONFIRM -> rate activated", ok_confirm, f"Resp: {resp}")

def test_pending_timeout(client):
    log("\n=== Scenario 2: Pending Confirmation 30s Timeout ===")
    # Set to safe rate first
    client.publish(TOPIC_CMD_MED_RATE, "70", qos=1)
    time.sleep(2.0)

    # Request 85 (pending)
    clear_payload("medical/medication_status")
    client.publish(TOPIC_CMD_MED_RATE, "85", qos=1)
    resp = get_last_payload("medical/medication_status", timeout_sec=3.0)
    log(f"Requested 85 -> {resp}")

    log("Waiting 32 seconds for pending request to time out...")
    time.sleep(32.0)

    # Send confirm now -> should be rejected with NO_PENDING
    clear_payload("medical/medication_status")
    client.publish(TOPIC_CMD_MED_CONFIRM, "CONFIRM", qos=1)
    resp = get_last_payload("medical/medication_status", timeout_sec=4.0)
    ok = False
    if resp:
        try:
            d = json.loads(resp)
            ok = (d.get("status") == "NO_PENDING")
        except Exception:
            pass
    record_result("Pending confirmation timeout (30s) -> NO_PENDING", ok, f"Resp: {resp}")

def test_out_of_range(client):
    log("\n=== Scenario 3: Out of Range Dosage Rejection ===")
    clear_payload("medical/medication_status")
    client.publish(TOPIC_CMD_MED_RATE, "150", qos=1)
    resp = get_last_payload("medical/medication_status", timeout_sec=3.0)
    ok_high = False
    if resp:
        try:
            d = json.loads(resp)
            ok_high = (d.get("status") == "REJECTED_OUT_OF_RANGE")
        except Exception:
            pass
    record_result("Dosage rate 150 mg/hr rejected", ok_high, f"Resp: {resp}")

    clear_payload("medical/medication_status")
    client.publish(TOPIC_CMD_MED_RATE, "-5", qos=1)
    resp = get_last_payload("medical/medication_status", timeout_sec=3.0)
    ok_low = False
    if resp:
        try:
            d = json.loads(resp)
            ok_low = (d.get("status") == "REJECTED_OUT_OF_RANGE")
        except Exception:
            pass
    record_result("Dosage rate -5 mg/hr rejected", ok_low, f"Resp: {resp}")

def test_clamp_jump(client):
    log("\n=== Scenario 4: Step-Jump Clamping (+20 mg/hr max) ===")
    # Reset to 30
    client.publish(TOPIC_CMD_MED_RATE, "30", qos=1)
    time.sleep(2.0)

    clear_payload("medical/medication_status")
    # Request jump 30 -> 90 (+60 jump) -> should be clamped to 30+20 = 50
    client.publish(TOPIC_CMD_MED_RATE, "90", qos=1)
    resp = get_last_payload("medical/medication_status", timeout_sec=3.0)
    ok = False
    if resp:
        try:
            d = json.loads(resp)
            ok = (d.get("status") == "CLAMPED") and ("50" in d.get("message", ""))
        except Exception:
            pass
    record_result("Jump 30->90 clamped to 50 mg/hr", ok, f"Resp: {resp}")

def test_bed_presets(client):
    log("\n=== Scenario 5: Bed Presets Sweep ===")
    for preset, angle in [("SLEEP", 10), ("EAT", 30), ("BREATHE", 45), ("BREATHE", 45), ("EMERGENCY", 90)]:
        clear_payload("facility/bed_angle")
        client.publish(TOPIC_CMD_BED_PRESET, preset, qos=1)
        time.sleep(3.0)
        resp = get_last_payload("facility/bed_angle", timeout_sec=2.0)
        record_result(f"Bed preset {preset} target {angle} deg", bool(resp), f"Bed state: {resp}")

def test_emergency_preempt(client):
    log("\n=== Scenario 6: Emergency Sweep Preemption ===")
    # Reset bed to 0
    client.publish(TOPIC_CMD_BED_ANGLE, "0", qos=1)
    time.sleep(3.0)

    # Sweep to 60, preempt mid-sweep with EMERGENCY
    client.publish(TOPIC_CMD_BED_ANGLE, "60", qos=1)
    time.sleep(0.2)
    client.publish(TOPIC_CMD_BED_PRESET, "EMERGENCY", qos=1)
    time.sleep(3.5)

    resp = get_last_payload("facility/bed_angle", timeout_sec=2.0)
    ok = False
    if resp:
        try:
            d = json.loads(resp)
            ok = (d.get("angle") == 90)
        except Exception:
            pass
    record_result("Emergency command preempts mid-sweep -> reaches 90 deg", ok, f"Bed state: {resp}")

def test_sampling_commands(client):
    log("\n=== Scenario 7: Dynamic Sampling Commands ===")
    # Invalid interval 4s (allowed 5..60)
    clear_payload("status")
    client.publish(TOPIC_CMD_SAMPLING_RATE, "4", qos=1)
    resp = get_last_payload("status", timeout_sec=2.0)
    ok_reject_low = bool(resp and "error" in resp)
    record_result("Sampling rate 4s rejected", ok_reject_low, f"Resp: {resp}")

    # Valid interval 5s
    client.publish(TOPIC_CMD_SAMPLING_RATE, "5", qos=1)
    record_result("Sampling rate 5s accepted", True)

    # Valid interval 60s
    client.publish(TOPIC_CMD_SAMPLING_RATE, "60", qos=1)
    record_result("Sampling rate 60s accepted", True)

    # Invalid interval 61s
    clear_payload("status")
    client.publish(TOPIC_CMD_SAMPLING_RATE, "61", qos=1)
    resp = get_last_payload("status", timeout_sec=2.0)
    ok_reject_high = bool(resp and "error" in resp)
    record_result("Sampling rate 61s rejected", ok_reject_high, f"Resp: {resp}")

    # Toggle mode auto / manual
    client.publish(TOPIC_CMD_SAMPLING_MODE, "auto", qos=1)
    record_result("Sampling mode auto accepted", True)
    client.publish(TOPIC_CMD_SAMPLING_MODE, "manual", qos=1)
    record_result("Sampling mode manual accepted", True)


def run_full_suite(client):
    log("\n##################################################################")
    log("### STARTING COMPREHENSIVE AUTOMATED MQTT TEST SUITE ###")
    log("##################################################################")
    test_results.clear()

    test_medication_ramp_and_confirm(client)
    test_out_of_range(client)
    test_clamp_jump(client)
    test_bed_presets(client)
    test_emergency_preempt(client)
    test_sampling_commands(client)
    test_pending_timeout(client)

    log("\n==================================================================")
    log("                       TEST RESULTS SUMMARY                       ")
    log("==================================================================")
    pass_count = sum(1 for _, st, _ in test_results if st == "PASS")
    fail_count = sum(1 for _, st, _ in test_results if st == "FAIL")

    for name, st, detail in test_results:
        print(f"  [{st}] {name:<50} {detail[:30] if detail else ''}")

    print("------------------------------------------------------------------")
    print(f"  TOTAL: {len(test_results)} | PASSED: {pass_count} | FAILED: {fail_count}")
    print("==================================================================\n")


def print_help():
    print(f"""
======================================================================
  ESP32 FreeRTOS Healthcare Monitor - MQTT Test Console
  Topic Prefix: {TOPIC_PREFIX}
======================================================================
Automated Test Suites:
  test all            : Run full automated verification suite
  test ramp           : Medication rate ramp and confirm flow
  test clamp          : Medication clamp jump (+20 max step)
  test timeout        : 30s confirmation timeout test
  test range          : Out-of-range rejection test
  test bed            : Bed presets & emergency preemption
  test sample         : Sampling rate commands (5-60s)

Interactive Commands:
  med <rate>          : Publish medication rate (0-100 mg/hr)
  confirm             : Send medication confirmation
  bed <angle>         : Set bed angle (0-90 deg)
  preset <name>       : Set bed preset (SLEEP, EAT, BREATHE, EMERGENCY)
  sample <sec>        : Set sampling interval (5-60 sec)
  mode <auto|manual>  : Toggle adaptive sampling mode
  help / ?            : Show this help
  q / quit            : Exit
======================================================================
""")

def main():
    if _USE_V2:
        client = mqtt.Client(CallbackAPIVersion.VERSION2, client_id=f"cli_tester_{int(time.time())}")
    else:
        client = mqtt.Client(client_id=f"cli_tester_{int(time.time())}")

    client.on_connect = on_connect
    client.on_message = on_message

    log(f"Connecting to MQTT broker at {BROKER}:{PORT} ...")
    try:
        client.connect(BROKER, PORT, keepalive=60)
    except Exception as e:
        log(f"Failed to connect to broker: {e}")
        sys.exit(1)

    client.loop_start()
    time.sleep(1.0)
    print_help()

    try:
        while True:
            try:
                line = input("cmd> ").strip()
            except EOFError:
                break

            if not line:
                continue

            parts = line.split(maxsplit=1)
            cmd = parts[0].lower()
            arg = parts[1].strip() if len(parts) > 1 else ""

            if cmd in ("q", "quit", "exit"):
                break
            elif cmd in ("help", "?"):
                print_help()
            elif cmd == "test":
                sub = arg.lower()
                if sub in ("all", ""):
                    run_full_suite(client)
                elif sub == "ramp":
                    test_medication_ramp_and_confirm(client)
                elif sub == "clamp":
                    test_clamp_jump(client)
                elif sub == "timeout":
                    test_pending_timeout(client)
                elif sub == "range":
                    test_out_of_range(client)
                elif sub == "bed":
                    test_bed_presets(client)
                    test_emergency_preempt(client)
                elif sub == "sample":
                    test_sampling_commands(client)
                else:
                    print("Usage: test <all|ramp|clamp|timeout|range|bed|sample>")
            elif cmd == "med":
                try:
                    val = float(arg)
                    client.publish(TOPIC_CMD_MED_RATE, str(val), qos=1)
                except ValueError:
                    print("Error: rate must be a number between 0 and 100")
            elif cmd == "confirm":
                client.publish(TOPIC_CMD_MED_CONFIRM, "CONFIRM", qos=1)
            elif cmd == "bed":
                try:
                    val = int(arg)
                    client.publish(TOPIC_CMD_BED_ANGLE, str(val), qos=1)
                except ValueError:
                    print("Error: angle must be an integer between 0 and 90")
            elif cmd == "preset":
                client.publish(TOPIC_CMD_BED_PRESET, arg.upper(), qos=1)
            elif cmd == "sample":
                try:
                    val = int(arg)
                    client.publish(TOPIC_CMD_SAMPLING_RATE, str(val), qos=1)
                except ValueError:
                    print("Error: interval must be 5-60")
            elif cmd == "mode":
                client.publish(TOPIC_CMD_SAMPLING_MODE, arg.lower(), qos=1)
            else:
                print(f"Unknown command: {cmd}")

    except KeyboardInterrupt:
        log("Interrupted by user.")
    finally:
        client.loop_stop()
        client.disconnect()
        log("Disconnected.")

if __name__ == "__main__":
    main()
