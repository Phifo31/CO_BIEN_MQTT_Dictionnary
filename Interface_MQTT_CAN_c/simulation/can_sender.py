#!/usr/bin/env python3
# simulation/can_sender.py — envoie 1 trame/s pour chaque entrée de ../config/conversion.json via SocketCAN

import json, time, random, sys, subprocess
from pathlib import Path

# --- chemins & params fixes ---
TABLE_PATH = Path(__file__).resolve().parent.parent / "config" / "conversion.json"
CAN_CHANNEL = "can0"
PERIOD_S = 1.0

def ensure_python_can():
    try:
        import can  # noqa
        return True
    except Exception:
        print("[INFO] python-can introuvable, installation…")
        try:
            subprocess.check_call([sys.executable, "-m", "pip", "install", "--user", "python-can"])
            return True
        except Exception as e:
            print(f"[ERR ] Installation python-can échouée: {e}")
            return False

if not ensure_python_can():
    sys.exit(1)
import can  # après install

PALETTE = ["#FF0000","#00FF00","#0000FF","#00FDFF","#FFFFFF","#FF00FF","#00FFFF","#FFA500","#00AA88"]

def load_entries(path):
    obj = json.loads(Path(path).read_text(encoding="utf-8"))
    out = []
    def walk(node):
        if isinstance(node, dict):
            if "topic" in node and isinstance(node.get("data"), list) and "id" in node:
                out.append({"topic": node["topic"].rstrip("/"), "data": node["data"], "id": int(node["id"])})
            for v in node.values():
                if isinstance(v, (dict, list)): walk(v)
        elif isinstance(node, list):
            for it in node: walk(it)
    walk(obj)
    return out

def rand_for_field(fd):
    name = (fd.get("name") or fd.get("field") or "field")
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

def enum_str_to_code(fs, s):
    d = fs.get("dict") or fs.get("enum") or {}
    return int(d.get(s)) if s in d else None

def parse_hex_rgb(s):
    if not isinstance(s,str) or len(s)!=7 or s[0]!="#": return None
    try:
        r = int(s[1:3],16); g = int(s[3:5],16); b = int(s[5:7],16)
        return [r,g,b]
    except: return None

def pack8(entry, payload):
    out = [0]*8; idx = 0
    for fs in entry["data"]:
        name = (fs.get("name") or fs.get("field"))
        t    = (fs.get("type") or "").lower()
        v    = payload.get(name, None)
        if t in ("int","uint8","byte"):
            if v is None: return None
            v = int(v); 
            if v<0 or v>255: return None
            if idx+1>8: return None
            out[idx]=v; idx+=1
        elif t in ("bool","boolean"):
            if v is None: return None
            out[idx]= 1 if bool(v) else 0; idx+=1
        elif t in ("hex","rgb"):
            rgb = parse_hex_rgb(v)
            if not rgb or idx+3>8: return None
            out[idx:idx+3] = rgb; idx+=3
        elif t in ("int16","i16","uint16","u16"):
            if v is None: return None
            n = int(v) & 0xFFFF
            if idx+2>8: return None
            out[idx] = (n>>8)&0xFF; out[idx+1] = n&0xFF; idx+=2
        elif t in ("enum","dict"):
            if not isinstance(v,str): return None
            code = enum_str_to_code(fs, v)
            if code is None or idx+1>8: return None
            out[idx]=code & 0xFF; idx+=1
        else:
            return None
    return bytes(out)

def main():
    entries = load_entries(TABLE_PATH)
    if not entries:
        print("[ERR ] Aucun mapping (topic,id,data) trouvé dans la table.")
        sys.exit(2)

    try:
        bus = can.interface.Bus(bustype="socketcan", channel=CAN_CHANNEL)
    except Exception as e:
        print(f"[ERR ] Impossible d’ouvrir {CAN_CHANNEL}: {e}\n"
              f"Astuce: sudo ip link set {CAN_CHANNEL} up (et config bitrate si nécessaire)")
        sys.exit(3)

    print(f"[INFO] {len(entries)} entrée(s). Envoi 1 trame/s/entrée sur {CAN_CHANNEL}. Ctrl+C pour arrêter.")
    try:
        while True:
            for e in entries:
                # payload aléatoire cohérent
                payload = {k:v for k,v in (rand_for_field(fd) for fd in e["data"])}
                b = pack8(e, payload)
                if not b:
                    # si erreur de packing aléatoire, régénère une fois
                    payload = {k:v for k,v in (rand_for_field(fd) for fd in e["data"])}
                    b = pack8(e, payload)
                if not b:
                    print(f"[WARN] payload invalide ignoré pour id=0x{e['id']:X} topic={e['topic']}")
                else:
                    msg = can.Message(arbitration_id=e["id"], data=b, is_extended_id=(e["id"]>0x7FF))
                    try:
                        bus.send(msg)
                        print(f"[CAN ] id=0x{e['id']:X} topic={e['topic']} data={b.hex().upper()}")
                    except Exception as ex:
                        print(f"[ERR ] Envoi CAN échoué id=0x{e['id']:X}: {ex}")
                time.sleep(PERIOD_S)
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()
