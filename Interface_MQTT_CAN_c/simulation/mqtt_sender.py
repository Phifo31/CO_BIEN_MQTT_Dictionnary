#!/usr/bin/env python3
# mqtt_sender.py — envoie en boucle (1s) des JSON valides sur <topic>/cmd
# Table attendue à:  <racine_projet>/config/conversion.json

import json, time, random, sys, subprocess
from pathlib import Path

# ---- Chemins & paramètres fixes ----
ROOT_DIR   = Path(__file__).resolve().parents[1]          # dossier racine du projet
TABLE_PATH = ROOT_DIR / "config" / "conversion.json"
MQTT_HOST  = "localhost"
MQTT_PORT  = 1883
MQTT_QOS   = 1
PERIOD_S   = 1.0

# ---- Install auto de paho-mqtt si besoin ----
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
from paho.mqtt import client as mqtt

# ---- Génération de valeurs cohérentes par type/champ ----
PALETTE = ["#FF0000", "#00FF00", "#0000FF", "#00FDFF", "#FFFFFF", "#FF00FF", "#00FFFF", "#FFA500"]

def clamp(v, lo, hi): return max(lo, min(hi, v))

def rand_for_field(name: str, typ: str, enum_dict: dict | None):
    t = (typ or "").lower()
    if t in ("int", "uint8", "byte"):
        if name in ("intensity", "brightness"):
            return clamp(random.randint(0, 255), 0, 255)
        if name in ("group_id", "group", "id", "pic_id"):
            return clamp(random.randint(1, 4), 0, 255)
        if name in ("interval", "period", "delay_ms", "touchthreshold", "proximitythreshold",
                    "touchscaling", "proximityscaling"):
            return clamp(random.randint(0, 50), 0, 255)
        return clamp(random.randint(0, 255), 0, 255)
    if t in ("bool", "boolean"):
        return random.choice([False, True])
    if t in ("hex", "rgb"):
        return random.choice(PALETTE)
    if t in ("int16", "i16", "uint16", "u16"):
        # plage sûre int16 (ton bridge accepte 16 bits signés en big-endian)
        return clamp(random.randint(0, 30000), -32768, 32767)
    if t in ("enum", "dict"):
        keys = list((enum_dict or {}).keys())
        return random.choice(keys) if keys else "ON"
    # par défaut: petit entier
    return clamp(random.randint(0, 5), 0, 255)

def load_entries(table_path: Path):
    obj = json.loads(table_path.read_text(encoding="utf-8"))
    entries = []

    def walk(node):
        if isinstance(node, dict):
            # bloc avec topic + data => une "entrée"
            if "topic" in node and "data" in node and isinstance(node["data"], dict):
                topic = str(node["topic"]).rstrip("/")
                data  = node["data"]
                # normaliser la description des champs: {"name":..., "type":..., "dict":...}
                fields = []
                for k, v in data.items():
                    if isinstance(v, str):
                        fields.append({"name": k, "type": v, "dict": None})
                    elif isinstance(v, dict) and v:  # enum/dict (ex: mode)
                        fields.append({"name": k, "type": "enum", "dict": v})
                    else:
                        # champ vide: on l’ignore (payload n’en aura pas)
                        pass
                entries.append({"topic": topic, "fields": fields})
            # continuer à explorer
            for v in node.values():
                if isinstance(v, (dict, list)):
                    walk(v)
        elif isinstance(node, list):
            for it in node:
                walk(it)

    walk(obj)
    return entries

def build_payload(entry):
    payload = {}
    for f in entry["fields"]:
        name, typ, edict = f["name"], f["type"], f.get("dict")
        val = rand_for_field(name, typ, edict)
        payload[name] = val
    # petites règles de cohérence (ex: intensity bornée)
    if "intensity" in payload:
        try: payload["intensity"] = clamp(int(payload["intensity"]), 0, 255)
        except: payload["intensity"] = 128
    return payload

def main():
    # charger la table
    if not TABLE_PATH.exists():
        print(f"[ERR ] Table introuvable: {TABLE_PATH}")
        sys.exit(2)
    entries = load_entries(TABLE_PATH)
    if not entries:
        print("[ERR ] Aucun topic trouvé dans la table (data vides ?).")
        sys.exit(3)

    # init MQTT
    cli = mqtt.Client()
    cli.connect(MQTT_HOST, MQTT_PORT, keepalive=30)
    cli.loop_start()

    print(f"[INFO] {len(entries)} topic(s) détectés. Publication toutes les {PERIOD_S:.1f}s sur '<topic>/cmd'. Ctrl+C pour stopper.")
    try:
        while True:
            for e in entries:
                topic_cmd = f"{e['topic']}/cmd"
                payload   = build_payload(e)
                cli.publish(topic_cmd, json.dumps(payload, separators=(",", ":")), qos=MQTT_QOS)
                print(f"[MQTT] {topic_cmd}  {payload}")
                time.sleep(PERIOD_S)
    except KeyboardInterrupt:
        pass
    finally:
        cli.loop_stop(); cli.disconnect()

if __name__ == "__main__":
    main()
