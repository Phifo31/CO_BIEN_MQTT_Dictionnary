#!/usr/bin/env python3
# mqtt_sender.py — publie en boucle (toutes les 1s) des JSON valides sur <topic>/cmd
# Paramètres FIXES : table="config/conversion.json", host="localhost", port=1883, qos=1, period=1.0s

import json, time, random, sys, subprocess
from pathlib import Path

# --- paramètres fixes ---
TABLE_PATH = "config/conversion.json"
MQTT_HOST  = "localhost"
MQTT_PORT  = 1883
MQTT_QOS   = 1
PERIOD_S   = 1.0

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
from paho.mqtt import client as mqtt  # import après install

PALETTE = ["#FF0000","#00FF00","#0000FF","#00FDFF","#FFFFFF","#FF00FF","#00FFFF","#FFA500","#00AA88"]

def load_entries(path):
    obj = json.loads(Path(path).read_text(encoding="utf-8"))
    out = []
    def walk(node):
        if isinstance(node, dict):
            if "topic" in node and isinstance(node.get("data"), list):
                out.append({"topic": node["topic"].rstrip("/"), "data": node["data"], "id": node.get("id")})
            for v in node.values():
                if isinstance(v, (dict, list)): walk(v)
        elif isinstance(node, list):
            for it in node: walk(it)
    walk(obj)
    return out

def rand_for_field(fd):
    name = fd.get("name") or fd.get("field") or "field"
    t    = (fd.get("type") or "").lower()
    if t in ("int","uint8","byte"):
        if name in ("intensity","brightness"): v = random.randint(0,255)
        elif name in ("group_id","group","id"): v = random.randint(1,4)
        elif name in ("interval","period","delay_ms"): v = random.randint(0,50)
        else: v = random.randint(0,255)
        return name, v
    if t in ("bool","boolean"): return name, random.choice([False, True])
    if t in ("hex","rgb"):      return name, random.choice(PALETTE)
    if t in ("int16","i16","uint16","u16"): return name, random.randint(0,2000)
    if t in ("enum","dict"):
        d = fd.get("dict") or fd.get("enum") or {}
        keys = list(d.keys())
        return name, (random.choice(keys) if keys else "UNKNOWN")
    return name, random.randint(0,5)

def build_payload(entry):
    payload = {}
    for f in entry["data"]:
        k, v = rand_for_field(f)
        payload[k] = v
    if "intensity" in payload:
        try: payload["intensity"] = max(0, min(255, int(payload["intensity"])))
        except: payload["intensity"] = 128
    return payload

def main():
    entries = load_entries(TABLE_PATH)
    if not entries:
        print("[ERR ] Aucun topic trouvé dans la table.")
        sys.exit(2)

    cli = mqtt.Client()
    cli.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
    cli.loop_start()

    print(f"[INFO] {len(entries)} topic(s) détectés. Envoi toutes les {PERIOD_S:.1f}s sur '<topic>/cmd'. Ctrl+C pour arrêter.")
    try:
        while True:
            for e in entries:
                topic = f"{e['topic']}/cmd"
                payload = build_payload(e)
                cli.publish(topic, json.dumps(payload, separators=(",",":")), qos=MQTT_QOS)
                print(f"[MQTT] {topic}  {payload}")
                time.sleep(PERIOD_S)
    except KeyboardInterrupt:
        pass
    finally:
        cli.loop_stop(); cli.disconnect()

if __name__ == "__main__":
    main()
