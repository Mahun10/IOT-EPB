#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"


const char* ssid = "Galaxy A20eDA24";
const char* password = "tzwz1727";
const char* mqtt_server = "10.46.37.145" //"10.112.246.145";//"10.38.211.145"; //"10.46.37.145";
const int mqtt_port = 1883;
const char* topic_temp = "esp32/dht11/temp";
const char* topic_hum = "esp32/dht11/hum";


#define DHTPIN 26
#define DHTTYPE DHT11
#define BUTTON_PIN 27
#define LED1 32
#define LED2 33


DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);


bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;


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
    if (client.connect("ESP32_DHT11")) {
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


void setup() {
  Serial.begin(115200);
  dht.begin();


  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // <-- plus fiable !


  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);


  Serial.println("Système prêt.");
}


void loop() {
  if (!client.connected()) reconnect();
  client.loop();


  int buttonState = digitalRead(BUTTON_PIN);


  if (buttonState != lastButtonState) lastDebounceTime = millis();


  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (buttonState == LOW) { // <-- appui détecté
      digitalWrite(LED2, HIGH);
      digitalWrite(LED1, LOW);


      float h = dht.readHumidity();
      float t = dht.readTemperature();


      if (!isnan(h) && !isnan(t)) {
        char tempStr[8], humStr[8];
        dtostrf(t, 1, 2, tempStr);
        dtostrf(h, 1, 2, humStr);
        client.publish(topic_temp, tempStr);
        client.publish(topic_hum, humStr);
        Serial.printf("Envoyé : %.1f°C  %.1f%%\n", t, h);
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
