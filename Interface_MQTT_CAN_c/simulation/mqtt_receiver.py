#!/usr/bin/env python3
# simulation/mqtt_receiver.py — affiche les messages MQTT (cmd/state) lisibles

import sys, subprocess
def ensure_paho():
    try:
        from paho.mqtt import client as mqtt  # noqa
        return True
    except Exception:
        print("[INFO] paho-mqtt introuvable, installation…")
        try:
            subprocess.check_call([sys.executable, "-m", "pip", "install", "--user", "paho-mqtt"])
            return True
        except Exception as e:
            print(f"[ERR ] Installation paho-mqtt échouée: {e}")
            return False
if not ensure_paho():
    sys.exit(1)

from paho.mqtt import client as mqtt  # après install
import time

MQTT_HOST = "localhost"
MQTT_PORT = 1883

def classify(topic: str) -> str:
    if topic.endswith("/state"): return "STATE"
    if topic.endswith("/cmd"):   return "CMD"
    return "BASE"

def on_connect(cli, ud, flags, rc):
    print(f"[INFO] Connecté MQTT rc={rc}, abonnement '#'")
    cli.subscribe("#", qos=1)

def on_message(cli, ud, msg):
    kind = classify(msg.topic)
    try:
        payload = msg.payload.decode("utf-8", errors="replace")
    except Exception:
        payload = str(msg.payload)
    ts = time.strftime("%H:%M:%S")
    print(f"[{ts}] [{kind}] {msg.topic}  {payload}")

def main():
    cli = mqtt.Client()
    cli.on_connect = on_connect
    cli.on_message = on_message
    cli.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
    print("[INFO] En écoute… (Ctrl+C pour arrêter)")
    try:
        cli.loop_forever()
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()
