import subprocess
import json
from pathlib import Path

# Charger ton JSON
path_example = Path("/home/iris/Desktop/CO_BIEN_MQTT_Dictionnary/mqtt_examples/imu_changes.json")
with path_example.open("r", encoding="utf-8") as f:
    data = json.load(f)

# Modifier une valeur
data["time"]["config"]["data"]["IMMOBILE_TIME_MS"] = 5000
data["threshold"]["config"]["data"]["MOTION_THRESHOLD"] = 1

# Lancer mosquitto_pub
subprocess.run([
    "mosquitto_pub",
    "-h", "localhost",
    "-t", "time/config",
    "-m", json.dumps(data["time"]["config"]["data"])
])

# Publier MOTION_THRESHOLD sur threshold/config
subprocess.run([
    "mosquitto_pub",
    "-h", "localhost",
    "-t", "threshold/config",
    "-m", json.dumps(data["threshold"]["config"]["data"])
])

