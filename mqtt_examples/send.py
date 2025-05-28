import paho.mqtt.client as paho
import paho.mqtt.publish as publish
import json
from pathlib import Path

path_example = Path.cwd()/'led.json'

client = paho.Client()
client.connect('localhost')
with open(path_example, 'r') as msg:
    data = json.load(msg)
    payload = json.dumps(data)
    publish.single('led/config', payload, qos=1, hostname='localhost')