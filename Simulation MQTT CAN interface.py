from threading import Thread
import paho.mqtt.client as paho
import paho.mqtt.publish as publish
import json
import can
import can.interface
import time
import numpy as np
import random

class MQTT_to_CAN (Thread):
    def __init__(self, path: str, host: str='localhost'):
        super().__init__()
        #self.CAN = can
        self.path_conv = path
        self.host = host
        self.disconnect = (False, None)
    
    def on_connect (self, client, userdata, flags, reason_code):
        pass
    
    def on_disconnect (self, client, userdata, reason_code):
        self.disconnect = (True, reason_code)
    
    def on_message (self, client, userdata, msg):
        
        # decode the json from the message
        message = str(msg.payload.decode("utf-8", "ignore"))
        message = json.loads(message)
        keys = list(message.keys()) # get all the key from the json
        with open(self.path_conv, 'r') as CONV:
            conv = json.load(CONV)
        
        # translation from MQTT to CAN
        topic = msg.topic.split('/') # extract the key for the translation from the topic
        arbitration_id = conv[topic[0]][topic[1]]['arbitration_id']
        payload = []
        for i in range (len(keys)):
            value = message[keys[i]]
            try :
                if conv[topic[0]][topic[1]]['data'][keys[i]] == 'hex': # hexadecimal translation for color on 3 bytes
                    payload.append(int(value[1:3], 16))
                    payload.append(int(value[3:5], 16))
                    payload.append(int(value[5:7], 16))
                elif conv[topic[0]][topic[1]]['data'][keys[i]] == "int" :
                    payload.append(value)                    
                elif conv[topic[0]][topic[1]]['data'][keys[i]] == "bool": # for boolean variable : 1=True and 0=False
                    if value :
                        payload.append(1)
                    else :
                        payload.append(0)
                else :
                    try :
                        payload.append(conv[topic[0]][topic[1]]['data'][keys[i]][value])
                    except:                        
                        print(f'Unidetified variable type : {keys[i]}')
            except:
                print(f"Unexpected value : {keys[i]}")
        
        # if the payload < 8 bytes add 0             
        if len(payload) < 8:
            for i in range (8-len(payload)):
                payload.append(0)
        print ("Conversion MQTT to CAN successful")
        
        self.can_write (arbitration_id, payload) # send to CAN
        
    def can_write (self, topic: str, payload: list):
        print (f"Id : {topic}/nData : {payload}")
        # Send message on can 
        # msg = can.Message(arbitration_id=topic, data=payload, is_extended_id=False)
        # try:
        #     self.CAN.send(msg)
        #     print (f"CAN message sent : {topic},{payload}")
        # except can.CanError:
        #     print("CAN message failed")    
        
    def run (self):
        client = paho.Client()
        
        # difined function for MQTT        
        client.on_connect = self.on_connect
        client.on_disconnect = self.on_disconnect
        client.on_message = self.on_message
        
        client.connect(self.host)
        
        # subscribe to the differents topics with qos
        client.subscribe("rfid/init", qos=1)
        #client.subscribe("rfid/read", qos=1)
        client.subscribe("sensors/init", qos=1)
        #client.subscribe("sensors/update", qos=1)
        client.subscribe("led/config", qos=1)
        client.subscribe("proximity/config", qos=1)
        #client.subscribe("proximity/update", qos=1)
        client.subscribe("imu/config", qos=1)
        #client.subscribe("imu/update", qos=1)

        while not self.disconnect[1]:
            client.loop_read()
        
        print(f"Disconnected: {self.disconnect[1]}")
        
    
# class CAN_to_MQTT (Thread):
#     def __init__(self, can, path: str, host: str='localhost'):
#         super().__init__()
#         self.can = can
#         self.path_conv = path
#         self.host = host
    
        
#     def run (self):
#         # created listener for CAN bus
#         listener = CAN_Listener(self.path_conv) # use CAN_Listener class as can listener
#         notifier = can.Notifier(self.can, [listener])
#         try:
#             while True:
#                 time.sleep(1)
#         except KeyboardInterrupt:
#             print("Keyboard interrupt")


class CAN_Listener (can.Listener, Thread):
    
    def __init__ (self, path, host: str = 'localhost'):
        super().__init__()
        self.host = host
        self.path_conv = path
        
    def run (self):
        id = 1520
        while (1):
            time.sleep(1)
            payload = []
            for i in range (4):
                #q = bin(random.randint(-31400, 31400))
                q = '{0:16b}'.format(random.randint(0, 62800)) 
                
                payload.append(hex(int(q[0:8],2)))                
                payload.append(hex(int(q[8:16],2)))
            print(f"CAN message received : id={id}, payload={payload}")
            self.on_message_received(id, payload)
        
    
    def on_message_received(self, id, msg): # function executed on can message reception
        # extract id and payload
        arbitration_id = id
        message = msg
        
        # find MQTT coresponding topic
        with open(self.path_conv, 'r') as CONV: # open conversion file
            conv = json.load(CONV)
        path = self.find_path(conv, arbitration_id)
        topic = conv[path[0]][path[1]]['topic']
        
        # payload translation
        payload = {}
        n = 0
        for field, value in conv[path[0]][path[1]]["data"].items():
            if value == 'int':
                payload[field] = message[n]
                n+=1 
            elif value == 'int16': # float conversion from 16bits
                payload[field] = int(f"{message[n]}{message[n+1][2:4]}", 0)
                n+=2
            elif value == 'bool': # for boolean : 1=True and 0=False
                if message[n] == 1:
                    payload[field] = "true"
                else :
                    payload[field] = "false"
                n+=1
            elif value == 'hex': # decimal to hexadeciaml conversion fromat #0F4A6E for RGB
                hexa = "#"
                for i in range (3):
                    num = hex(message[n]).split("x")[-1].upper() # get hexadecimal value
                    if len(num) == 1: # add 0 in front of the value if <16, value must be 6 caractére long
                        num = f"0{num}"
                    hexa = f"{hexa}{num}"
                    n+=1
                print(hexa)
                payload[field] = hexa
            elif isinstance(value, dict):
                data = self.find_path(conv, 1, (path+["data"]+[field]))
                payload[field] = data[-1]
        payload = f'{payload}'
        payload = payload.replace("'",'"')
        
        self.publish(topic, payload) # publish to MQTT
                 
    def find_path(self, data, target, path=None):
        # return path from ditionnaries
        if path is None:
            path = []
            
        for key, value in data.items():
            current_path = path + [key]
            
            if isinstance(value, dict):
                found = self.find_path(value, target, current_path)
                if found:
                    return found
            else:
                if value == target:
                    return current_path
        return None        
    
    def publish (self, topic, payload):
        try :
            publish.single(topic=topic, payload=payload, qos=1, hostname=self.host)
            print (f"MQTT message sent to {topic}: /n{payload}")
        except :
            print ("MQTT publish failed")
    
    def rtr (self):
        pass

if __name__ == '__main__':
    from pathlib import Path
    import os
    
    # can periferal initialisation
    # os.system('sudo ip link set can0 down') # sudo ifconfig can0 down
    # os.system('sudo ip link set can0 type can bitrate 500000') # sudo ip link set can0 type can bitrate 1000000
    # os.system('sudo ip link set can0 up') # sudo ifconfig can0 up
    
    # can_filters = [
    # {"can_id": 0x1310, "can_mask": 0x0, "extended": False},
    # ]
    # bus = can.interface.Bus(bustype='socketcan', channel='can0', bitrate=500000, filter=can_filters)
    path = Path.cwd()/'Test/Interface_MQTT_CAN/conversion.json' # MQTT/CAN transltion json file
    
    r_mqtt = MQTT_to_CAN(path, "localhost")
    r_can = CAN_Listener(path, "localhost")
    
    r_mqtt.start()
    r_can.start()
    
    r_mqtt.join()
    r_can.join()