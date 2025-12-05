# This script stores DHT data received by the MQTT server in a JSON file
import json
import os
import time
import paho.mqtt.client as mqtt

DATA_FILE = "/var/www/dht11/data.json"
os.makedirs(os.path.dirname(DATA_FILE), exist_ok=True)

# Charger l'ancien fichier s'il existe
def load_data():
    if os.path.exists(DATA_FILE):
        with open(DATA_FILE, "r") as f:
            try:
                return json.load(f)
            except json.JSONDecodeError:
                return {}
    return {"temperature": [], "humidity": []}

# Sauvegarder les nouvelles données
def save_data(data):
    with open(DATA_FILE, "w") as f:
        json.dump(data, f, indent=4)

# Quand connecté au broker
def on_connect(client, userdata, flags, rc):
    print("Connecté au broker (code:", rc, ")")
    client.subscribe("esp32/dht11/#")  # <-- tous les sous-topics de esp32/dht11/

# Quand un message arrive
def on_message(client, userdata, msg):
    topic = msg.topic
    value = msg.payload.decode()
    print(f"[{topic}] {value}")

    try:
        data = load_data()
        entry = {
            "value": float(value),
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S")
        }

        if "temp" in topic:
            data["temperature"].append(entry)
        elif "hum" in topic:
            data["humidity"].append(entry)

        save_data(data)
        print("✅ Donnée ajoutée à data.json")

    except ValueError:
        print("⚠️ Valeur reçue non numérique.")

# Config MQTT
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

# Connexion au broker local (à adapter)
client.connect("localhost", 1883, 60)
client.loop_forever()

