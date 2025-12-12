# The MQTT server verifies authenticity and integrity using HMAC and a counter,
# then decrypts the data and stores it in a JSON file associated with the sending ESP32.

import json
import os
import time
import binascii
import paho.mqtt.client as mqtt

from Crypto.Cipher import AES
from Crypto.Util.Padding import unpad
from Crypto.Hash import HMAC, SHA256

# ===== CONFIG =====
DATA_DIR = "/var/www/dht11/dashboard/data"
MQTT_BROKER = "localhost"
KEY_DB_FILE = "keys.json"

# ===== Charger la base de clés =====
with open(KEY_DB_FILE, "r") as f:
    KEY_DB = json.load(f)

# Anti-replay par device
last_seen_counter = {}

# Créer le dossier de données si besoin
os.makedirs(DATA_DIR, exist_ok=True)

# --------------------------------------------------
# Gestion des fichiers par ESP32
# --------------------------------------------------
def get_data_file(device_id):
    return os.path.join(DATA_DIR, f"{device_id}.json")

def load_data(device_id):
    file_path = get_data_file(device_id)

    if os.path.exists(file_path):
        with open(file_path, "r") as f:
            try:
                return json.load(f)
            except json.JSONDecodeError:
                pass

    return {"temperature": [], "humidity": []}

def save_data(device_id, data):
    file_path = get_data_file(device_id)

    with open(file_path, "w") as f:
        json.dump(data, f, indent=4)

# --------------------------------------------------
# MQTT callbacks
# --------------------------------------------------
def on_connect(client, userdata, flags, rc):
    print("✅ Connected to broker (code:", rc, ")")
    client.subscribe("esp32/+/secure")

def on_message(client, userdata, msg):
    try:
        # ---- Identifier l'ESP32 via le topic ----
        device_id = msg.topic.split("/")[1]

        if device_id not in KEY_DB:
            print(f"⛔ Device inconnu : {device_id}")
            return

        SECRET_KEY = bytes.fromhex(KEY_DB[device_id])

        # ---- Décodage hex ----
        encrypted_bytes = binascii.unhexlify(msg.payload.decode())

        if len(encrypted_bytes) < 48:
            print("⚠️ Packet trop court")
            return

        iv = encrypted_bytes[:16]
        received_hmac = encrypted_bytes[-32:]
        ciphertext = encrypted_bytes[16:-32]

        # ---- Vérification HMAC ----
        h = HMAC.new(SECRET_KEY, digestmod=SHA256)
        h.update(iv)
        h.update(ciphertext)
        h.verify(received_hmac)

        # ---- Déchiffrement ----
        cipher = AES.new(SECRET_KEY, AES.MODE_CBC, iv)
        decrypted = unpad(cipher.decrypt(ciphertext), AES.block_size)
        payload = json.loads(decrypted.decode())

        temp_val = payload.get("t")
        hum_val  = payload.get("h")
        cnt      = payload.get("cnt")

        # ---- Anti-replay ----
        last = last_seen_counter.get(device_id, 0)
        if cnt <= last:
            print(f"⛔ REPLAY ATTACK for {device_id}")
            return
        last_seen_counter[device_id] = cnt

        print(f"🔓 {device_id} → T={temp_val}°C H={hum_val}% cnt={cnt}")

        # ---- Sauvegarde par ESP32 ----
        current_time = time.strftime("%Y-%m-%d %H:%M:%S")
        data = load_data(device_id)

        if temp_val is not None:
            data["temperature"].append({
                "value": temp_val,
                "timestamp": current_time
            })

        if hum_val is not None:
            data["humidity"].append({
                "value": hum_val,
                "timestamp": current_time
            })

        save_data(device_id, data)
        print(f"✅ Data saved to {device_id}.json")

    except Exception as e:
        print(f"❌ Critical Error processing message: {e}")

# --------------------------------------------------
# Main
# --------------------------------------------------
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect(MQTT_BROKER, 1883, 60)
client.loop_forever()

