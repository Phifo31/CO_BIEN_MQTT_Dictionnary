import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish
import json
from pathlib import Path

# Path to your JSON file
path_example = Path.cwd() / '/home/iris/Desktop/CoBien/CoBien_MQTT_Dictionnary/CO_BIEN_MQTT_Dictionnary/mqtt_examples/led.json'

# Create client with callback API version 2 (current version)
client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2, protocol=mqtt.MQTTv5)

# Connect to broker
client.connect('localhost')

# Read and publish JSON data
with open(path_example, 'r') as msg:
    data = json.load(msg)

payload = json.dumps(data)

# Publish message
publish.single('led/config', payload, qos=1, hostname='localhost', protocol=mqtt.MQTTv5)
