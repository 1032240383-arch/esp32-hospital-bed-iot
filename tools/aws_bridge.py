#!/usr/bin/env python3
"""
AWS IoT Core <-> HiveMQ Real-Time Cloud Bridge
-----------------------------------------------
Bridges live telemetry from ESP32 Wokwi (HiveMQ) into AWS IoT Core.
"""

import argparse
import os
import sys
import time
import ssl
import paho.mqtt.client as mqtt

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(line_buffering=True)

HIVEMQ_HOST = "broker.hivemq.com"
HIVEMQ_PORT = 1883

DEFAULT_ENDPOINT = "a1tx9s1rt01dez-ats.iot.ap-southeast-2.amazonaws.com"
DEFAULT_CERT     = "certs/ESP32_HospitalBed_01.cert.pem"
DEFAULT_KEY      = "certs/ESP32_HospitalBed_01.private.key"
DEFAULT_CA       = "certs/root-CA.crt"
DEFAULT_PREFIX   = "hospital_parth_bed01_2026/ward_A/bed_01"

aws_client = None
hive_client = None
args = None

def on_hive_connect(client, userdata, flags, rc, properties=None):
    is_ok = (rc == 0) if isinstance(rc, int) else (not rc.is_failure)
    if is_ok:
        print("[HiveMQ] Connected successfully.")
        sub_topic = f"{args.prefix}/#"
        client.subscribe(sub_topic)
        print(f"[HiveMQ] Subscribed to topic: {sub_topic}")
    else:
        print(f"[HiveMQ] Connect failed with code {rc}")

def on_hive_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode('utf-8', errors='ignore')
    print(f"[HiveMQ -> AWS IoT] {topic} : {payload}")
    if aws_client and aws_client.is_connected():
        aws_client.publish(topic, payload, qos=0)

def on_aws_connect(client, userdata, flags, rc, properties=None):
    is_ok = (rc == 0) if isinstance(rc, int) else (not rc.is_failure)
    if is_ok:
        print(">>> [AWS IoT Core] CONNECTED SECURELY (CONNACK 0) <<<")
    else:
        print(f"[AWS IoT Core] Connect failed with code {rc}")

def on_aws_message(client, userdata, msg):
    pass

def main():
    global aws_client, hive_client, args

    parser = argparse.ArgumentParser(description="AWS IoT Core Bridge for ESP32 Hospital Bed")
    parser.add_argument("--endpoint", default=DEFAULT_ENDPOINT, help="AWS IoT Core Data Endpoint")
    parser.add_argument("--cert", default=DEFAULT_CERT, help="Path to device certificate")
    parser.add_argument("--key", default=DEFAULT_KEY, help="Path to private key")
    parser.add_argument("--ca", default=DEFAULT_CA, help="Path to Amazon Root CA1")
    parser.add_argument("--prefix", default=DEFAULT_PREFIX, help="MQTT Topic Prefix")
    args = parser.parse_args()

    for f in [args.cert, args.key, args.ca]:
        if not os.path.exists(f):
            print(f"[ERROR] Certificate file not found: {f}")
            sys.exit(1)

    print("=" * 65)
    print("  ESP32 SMART HOSPITAL BED - AWS IoT Core Cloud Bridge")
    print("=" * 65)
    print(f"HiveMQ Broker : {HIVEMQ_HOST}:{HIVEMQ_PORT}")
    print(f"AWS IoT Host  : {args.endpoint}:8883")
    print(f"Topic Prefix  : {args.prefix}")
    print("=" * 65)

    # 1. HiveMQ Client
    hive_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"Bridge_HiveMQ_{int(time.time())}")
    hive_client.on_connect = on_hive_connect
    hive_client.on_message = on_hive_message

    # 2. AWS IoT Core Client with TLS Context & basicPubSub client_id
    ctx = ssl.create_default_context(ssl.Purpose.SERVER_AUTH, cafile=args.ca)
    ctx.load_cert_chain(certfile=args.cert, keyfile=args.key)

    aws_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="basicPubSub")
    aws_client.on_connect = on_aws_connect
    aws_client.on_message = on_aws_message
    aws_client.tls_set_context(ctx)

    try:
        print("[Connecting] Connecting to AWS IoT Core over TLS (Port 8883)...")
        aws_client.connect(args.endpoint, 8883, keepalive=60)
        aws_client.loop_start()

        print("[Connecting] Connecting to HiveMQ...")
        hive_client.connect(HIVEMQ_HOST, HIVEMQ_PORT, keepalive=60)
        hive_client.loop_start()

        print("\n>>> ACTIVE: Live vitals are now syncing from Wokwi -> AWS IoT Core! <<<")
        print("Press Ctrl+C to stop.\n")

        while True:
            time.sleep(1)

    except KeyboardInterrupt:
        print("\n[Stopping] Stopping bridge...")
        aws_client.loop_stop()
        hive_client.loop_stop()
        print("[Done] Bridge stopped.")

if __name__ == "__main__":
    main()
