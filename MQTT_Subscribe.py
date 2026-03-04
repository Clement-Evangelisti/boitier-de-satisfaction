import paho.mqtt.client as mqtt
import json
import os
from datetime import datetime

MQTT_SERVER = "192.168.1.57"
MQTT_PORT   = 1883
MQTT_TOPIC  = "CESI/mesure_temp"
JSON_FILE   = "donnees.json"
MQTT_USER = "user1"
MQTT_PWD = "password"

def on_connect(client, userdata, flags, rc):
    print("Connecté au broker MQTT")
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    raw = msg.payload.decode()
    print(f"Message reçu : {raw}")

    # Tentative de parsing JSON
    try:
        payload = json.loads(raw)
    except json.JSONDecodeError:
        # Si ce n'est pas du JSON, on encapsule le message brut
        payload = {"device": "inconnu", "message": raw}

    # Chargement du fichier existant ou création d'une liste vide
    if os.path.exists(JSON_FILE):
        with open(JSON_FILE, "r") as f:
            data = json.load(f)
    else:
        data = []

    # Ajout du nouveau message et sauvegarde
    data.append(payload)
    with open(JSON_FILE, "w") as f:
        json.dump(data, f, indent=2)

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.username_pw_set(MQTT_USER, MQTT_PWD)

client.connect(MQTT_SERVER, MQTT_PORT)
client.loop_forever()