#include <WiFiS3.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>

// =====================
// Variables WIFI
// =====================
char ssid[] = "CESI_Iot";
char pass[] =  "#RO_i0t.n3t"; 
int status = WL_IDLE_STATUS;

// =====================
// Variables MQTT
// =====================
const char* mqtt_server = "192.168.1.57";
const int mqtt_port = 8883;
const char* nameMQTT = "MQTT_Broker_Groupe_1";
const char* topic_sub = "CESI/ACK";
const char* topic_pub = "CESI/BATIMENT/CAFET";
const char* mqtt_user = "user1";
const char* mqtt_password = "password";

// =====================
// Variables LoRa
// =====================
SoftwareSerial e5(2, 3); // RX, TX
#define DEVICE_NAME "MQTT_Broker_Groupe_1"

// =====================
// Objets communication
// =====================
WiFiClient espClient;
PubSubClient client(espClient);

// =====================
// Prototypes
// =====================
void wifiConnexion();
void connectionMQTT();
void publish(const char* topic, const char* message);
void callback(char* topic, byte* payload, unsigned int length);
void startListening();
void sendCmd(String cmd);
String hexToAscii(String hex);

// =====================
void setup() {
  Serial.begin(9600);
  while (!Serial) { ; }

  e5.begin(9600);
  delay(1000);

  Serial.println("=== LORA + MQTT ===");
  Serial.println("Appareil : " + String(DEVICE_NAME));

  // Initialisation LoRa
  sendCmd("AT+MODE=TEST");
  delay(500);
  sendCmd("AT+TEST=RFCFG,868.35,SF7,125,12,15,14");
  delay(500);

  // Connexion WiFi & MQTT
  wifiConnexion();
  connectionMQTT();

  // Démarrage en écoute LoRa
  startListening();
}

// =====================
void loop() {
  client.loop();

  // --- Lecture des messages LoRa entrants ---
  if (e5.available()) {
    String response = e5.readStringUntil('\n');
    response.trim();

    if (response.length() > 0) {
      Serial.print("[LoRa brut] ");
      Serial.println(response);
    }

    if (response.startsWith("+TEST: RX")) {
      int start = response.indexOf('"');
      int end   = response.lastIndexOf('"');
      if (start != -1 && end != -1 && end > start) {
        String hexPayload = response.substring(start + 1, end);
        String message = hexToAscii(hexPayload);
        Serial.println(">>> MESSAGE RECU : " + message);
        Serial.println("---------------------");

        // Publication du message LoRa sur le broker MQTT
        publish(topic_pub, message.c_str());
      }
    }
  }

  // --- Envoi manuel depuis le Serial Monitor ---
  if (Serial.available()) {
    String userInput = Serial.readStringUntil('\n');
    userInput.trim();
    if (userInput.length() > 0) {
      publish(topic_pub, userInput.c_str());
    }
  }
}

// =====================
// Connexion WiFi
// =====================
void wifiConnexion() {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Echec de la communication avec le module WIFI");
    while (true);
  }

  IPAddress ip(192, 168, 1, 59);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns(8, 8, 8, 8);
  WiFi.config(ip, gateway, subnet, dns);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connexion en cours au SSID: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, pass);
    delay(10000);
  }
  Serial.println("Vous êtes connecté au WIFI");
}

// =====================
// Connexion MQTT
// =====================
void connectionMQTT() {
  client.setServer(mqtt_server, mqtt_port);

  while (!client.connected()) {
    Serial.print("Connexion au broker MQTT...");
    if (client.connect(nameMQTT, mqtt_user, mqtt_password)) {
      Serial.println("OK");
      client.subscribe(topic_sub);
      Serial.println("Abonné à CESI/ACK");
      client.setCallback(callback);
    } else {
      Serial.print("Erreur MQTT, rc=");
      Serial.println(client.state());
      delay(5000);
    }
  }
}

// =====================
// Publication MQTT
// =====================
void publish(const char* topic, const char* message) {
  if (client.publish(topic, message)) {
    Serial.print("[MQTT] Message envoyé : ");
    Serial.println(message);
  } else {
    Serial.println("[MQTT] Erreur lors de l'envoi !");
    connectionMQTT();
  }
}

// =====================
// Callback MQTT (messages reçus)
// =====================
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message reçu sur le topic: ");
  Serial.println(topic);
  Serial.print("Contenu: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

// =====================
// Fonctions LoRa
// =====================
void startListening() {
  sendCmd("AT+TEST=RXLRPKT");
  Serial.println("[RX] En écoute...");
}

void sendCmd(String cmd) {
  e5.println(cmd);
  delay(300);
  while (e5.available()) {
    Serial.write(e5.read());
  }
  Serial.println();
}

String hexToAscii(String hex) {
  String ascii = "";
  for (int i = 0; i < hex.length(); i += 2) {
    String byteStr = hex.substring(i, i + 2);
    char c = (char) strtol(byteStr.c_str(), NULL, 16);
    ascii += c;
  }
  return ascii;
}