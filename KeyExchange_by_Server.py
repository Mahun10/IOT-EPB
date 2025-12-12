# At the first power-up, the ESP32 does not have a secret key to encrypt data,
# so the server provides a unique secret key for each ESP32 based on its ID via the USB port.

import serial
import secrets
import time
import json
import os
import re
import sys

# ================== CONFIG ==================
PORT = "COM3"            
BAUDRATE = 115200
KEY_DB_FILE = "keys.json"

DEVICE_ID_REGEX = re.compile(r"ESP32_[0-9A-Fa-f]{12}")

# ================== UTILS ==================
def load_key_db():
    if os.path.exists(KEY_DB_FILE):
        with open(KEY_DB_FILE, "r") as f:
            return json.load(f)
    return {}

def save_key_db(db):
    with open(KEY_DB_FILE, "w") as f:
        json.dump(db, f, indent=4)

# ================== MAIN ==================
def main():
    print(f"🔌 Connexion au port série {PORT}...")
    try:
        ser = serial.Serial(PORT, BAUDRATE, timeout=2)
    except serial.SerialException as e:
        print(" Impossible d’ouvrir le port série :", e)
        sys.exit(1)

    time.sleep(2)

    # ------------------ Lecture initiale ------------------
    device_id = None
    print(" Lecture des messages de l’ESP32...")

    start_time = time.time()
    while time.time() - start_time < 5:
        if ser.in_waiting:
            line = ser.readline().decode(errors="ignore").strip()
            print("ESP32 >", line)

            match = DEVICE_ID_REGEX.search(line)
            if match:
                device_id = match.group()
                break

    if not device_id:
        print(" Impossible de détecter le device_id.")
        ser.close()
        sys.exit(1)

    print(f" Device ID détecté : {device_id}")

    # ------------------ Chargement de la base ------------------
    key_db = load_key_db()

    if device_id in key_db:
        print(f"⚠️ Une clé existe déjà pour {device_id}")
        confirm = input("Écraser la clé existante ? (o/n) : ").lower()
        if confirm != "o":
            print("Abandon.")
            ser.close()
            sys.exit(0)

    # ------------------ Génération de la clé ------------------
    key = secrets.token_bytes(16)
    hex_key = key.hex().upper()

    print(f" Clé générée : {hex_key}")

    # ------------------ Sauvegarde dans keys.json ------------------
    key_db[device_id] = hex_key
    save_key_db(key_db)
    print(f" Clé enregistrée dans {KEY_DB_FILE}")

    # ------------------ Envoi USB ------------------
    print(" Envoi de la clé à l’ESP32...")
    ser.write((hex_key + "\n").encode("ascii"))

    # ------------------ Attente ACK ------------------
    ack_ok = False
    start_time = time.time()

    while time.time() - start_time < 5:
        if ser.in_waiting:
            line = ser.readline().decode(errors="ignore").strip()
            print("ESP32 >", line)

            if "Clé stockée" in line or "Key stored" in line:
                ack_ok = True
                break

    ser.close()

    if not ack_ok:
        print("❌ Aucun accusé de réception reçu de l’ESP32.")
        sys.exit(1)

    print("✅ Provisioning USB terminé avec succès.")
    print("🔁 L’ESP32 va redémarrer en mode normal.")

# ================== ENTRY ==================
if __name__ == "__main__":
    main()

