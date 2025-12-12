//---------------------------------------------------------------------------------------------------
// At the first power-up of the ESP32, it waits for the MQTT server's secret key via its USB port.
// When the key is received, it is stored in NVS and the device reboots.
// After rebooting, symmetric encryption with the server is started.
// Each ESP32 is identified by its device ID, which is a combination of the ESP32 type and the MAC
// address of the chip.
//---------------------------------------------------------------------------------------------------

#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"
#include <Preferences.h>

#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "esp_system.h"   // pour esp_fill_random


// ------------------------------------------------------
// Génère un device_id unique basé sur l'adresse MAC
// ------------------------------------------------------
String getDeviceID() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  snprintf(buf, sizeof(buf), "ESP32_%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

// Stockage global
String device_id;

// ========= CONFIG WIFI / MQTT =========
const char* ssid        = "s690v06k08-2.4";
const char* password    = "xj679wwk";
const char* mqtt_server = "192.168.0.238";
const int   mqtt_port   = 1883;

//const char* topic_secure = "esp32/dht11/secure";

// ========= DHT & GPIO =========
#define DHTPIN   26
#define DHTTYPE  DHT11
#define BUTTON_PIN 27
#define LED1 32
#define LED2 33

DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);
Preferences prefs;

// ========= CLÉ SYMÉTRIQUE (remplie après provisioning) =========
uint8_t aesKey[16];         // clé AES/HMAC
bool    keyLoaded = false;

// ========= ANTI-REBOUND BOUTON / COMPTEUR =========
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

unsigned long messageCounter = 0;

// ------------------------------------------------------
//  UTILITAIRE : lecture de la clé depuis le NVS
// ------------------------------------------------------
bool loadKeyFromNVS() {
  prefs.begin("sec", true);  // lecture seule
  bool ok = false;

  if (prefs.isKey("aes_key")) {
    size_t len = prefs.getBytes("aes_key", aesKey, sizeof(aesKey));
    if (len == 16) {
      ok = true;
    }
  }
  prefs.end();
  return ok;
}

// ------------------------------------------------------
//  MODE PROVISIONING : réception de la clé via USB
// ------------------------------------------------------
void provisioningMode() {
  Serial.println("=== MODE PROVISIONING ===");
  Serial.println("Aucune clé trouvée en NVS.");
  Serial.println("Envoyez une clé hexadécimale de 32 caractères (16 octets) suivie d'un retour chariot.");
  Serial.println("Exemple: 0123456789ABCDEF0123456789ABCDEF");

  while (true) {
    if (Serial.available()) {
      String line = Serial.readStringUntil('\n');
      line.trim();

      if (line.length() < 32) {
        Serial.println("Erreur: clé trop courte (<32 hex). Réessayez.");
        continue;
      }
      if (line.length() % 2 != 0) {
        Serial.println("Erreur: longueur impaire. Réessayez.");
        continue;
      }

      Serial.print("Reçu: ");
      Serial.println(line);

      // On ne prend que les 16 premiers octets (32 caractères hex)
      for (int i = 0; i < 16; i++) {
        String byteStr = line.substring(i * 2, i * 2 + 2);
        aesKey[i] = (uint8_t) strtol(byteStr.c_str(), NULL, 16);
      }

      // Stockage en NVS
      prefs.begin("sec", false);
      prefs.putBytes("aes_key", aesKey, 16);
      prefs.end();

      Serial.println("Clé stockée en NVS. Redémarrage dans 1 seconde...");
      delay(1000);
      ESP.restart();
    }

    delay(100);
  }
}

// ------------------------------------------------------
//  CHIFFREMENT + HMAC + ENVOI MQTT
// ------------------------------------------------------
void sendSecureMessage(float t, float h) {
  if (!keyLoaded) {
    Serial.println("ERREUR: clé non chargée, message non envoyé.");
    return;
  }

  messageCounter++;

// ---- JSON ----
  char jsonBuffer[64];
  snprintf(jsonBuffer, sizeof(jsonBuffer),
           "{\"t\":%.2f,\"h\":%.2f,\"cnt\":%lu}", t, h, messageCounter);

  String payload = String(jsonBuffer);

  // ---- PADDING PKCS7 ----
  int inputLen = payload.length();
  int padLen   = 16 - (inputLen % 16);
  int totalLen = inputLen + padLen;

  uint8_t inputBuffer[totalLen];
  payload.getBytes(inputBuffer, inputLen + 1);

  for(int i = inputLen; i < totalLen; i++)
    inputBuffer[i] = (uint8_t)padLen;

  // ---- IV ----
  uint8_t iv[16];
  esp_fill_random(iv, 16);


 

  // AES-CBC
  uint8_t outputBuffer[totalLen];
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, aesKey, 128);   // 128 bits

  uint8_t iv_copy[16];
  memcpy(iv_copy, iv, 16);

  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, totalLen, iv_copy, inputBuffer, outputBuffer);
  mbedtls_aes_free(&aes);

  // HMAC-SHA256( key, iv || ciphertext )
  uint8_t hmacResult[32];
  mbedtls_md_context_t ctx;

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);

  mbedtls_md_hmac_starts(&ctx, aesKey, 16);
  mbedtls_md_hmac_update(&ctx, iv, 16);
  mbedtls_md_hmac_update(&ctx, outputBuffer, totalLen);
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);

  // Construire la string hex : IV + CIPHERTEXT + HMAC
  String finalPacket = "";
 // IV
  for(int i = 0; i < 16; i++) {
    if (iv[i] < 16) finalPacket += "0";
    finalPacket += String(iv[i], HEX);
  }

  // Ciphertext
  for(int i = 0; i < totalLen; i++) {
    if (outputBuffer[i] < 16) finalPacket += "0";
    finalPacket += String(outputBuffer[i], HEX);
  }

  // HMAC
  for(int i = 0; i < 32; i++) {
    if (hmacResult[i] < 16) finalPacket += "0";
    finalPacket += String(hmacResult[i], HEX);
  }

  Serial.println("Encrypted Packet: " + finalPacket);

  String topic = "esp32/" + device_id + "/secure";
  client.publish(topic.c_str(), finalPacket.c_str());
}

// ------------------------------------------------------
//  WIFI + MQTT
// ------------------------------------------------------
void setup_wifi() {
  WiFi.begin(ssid, password);
  Serial.print("Connexion WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connecté !");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connexion MQTT...");
    if (client.connect(device_id.c_str())) {
      Serial.println("OK !");
      client.publish("esp32/status", "ESP32 connecté");
    } else {
      Serial.print("Échec (code ");
      Serial.print(client.state());
      Serial.println(")");
      delay(2000);
    }
  }
}

// ------------------------------------------------------
//  SETUP
// ------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("Boot ESP32...");

  device_id = getDeviceID();
  Serial.println("Device ID = " + device_id);

  // 1) Essayer de charger la clé
  keyLoaded = loadKeyFromNVS();

  if (!keyLoaded) {
    // Aucune clé → on passe en mode provisioning USB
    provisioningMode();
    // ne revient jamais ici
  }

  Serial.println("Clé AES trouvée en NVS, passage en mode normal.");

  dht.begin();
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);

  Serial.println("Système prêt (mode normal).");
}

// ------------------------------------------------------
//  LOOP
// ------------------------------------------------------
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  int buttonState = digitalRead(BUTTON_PIN);

  if (buttonState != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (buttonState == HIGH) {
      // Bouton appuyé
      digitalWrite(LED2, HIGH);
      digitalWrite(LED1, LOW);

      float h = dht.readHumidity();
      float t = dht.readTemperature();

      if (!isnan(h) && !isnan(t)) {
        sendSecureMessage(t, h);
        Serial.printf("Données chiffrées envoyées: %.1f°C  %.1f%%\n", t, h);
      } else {
        Serial.println("Erreur DHT !");
      }

      delay(1000);
    } else {
      digitalWrite(LED2, LOW);
      digitalWrite(LED1, HIGH);
    }
  }

  lastButtonState = buttonState;
}

