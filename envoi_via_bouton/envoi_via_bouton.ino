#include <SoftwareSerial.h>
#include <ChainableLED.h>

// =====================
// ---- LoRa ----
// =====================
SoftwareSerial e5(2, 3); // RX=D2, TX=D3  (module LoRa Wio-E5)

#define DEVICE_NAME     "Cafet_Orion"   
#define TX_INTERVAL_MS  5000         // Intervalle entre deux envois automatiques

int counter = 0;
unsigned long lastTxTime = 0;

// =====================
// ---- LED chainable ----
// =====================
#define NUM_LEDS 1
ChainableLED leds(5, 6, NUM_LEDS); // Data=D5, Clock=D6

// =====================
// ---- Boutons ----
// =====================
// /!\ D3 est utilisé par SoftwareSerial (TX du LoRa) => boutons déplacés sur D7 et D8
#define BTN_GREEN  7   // D7 - bouton vert
#define BTN_RED    8   // D8 - bouton rouge

bool dernierEtatVert  = HIGH;
bool dernierEtatRouge = HIGH;

// =====================
// Helpers LED
// =====================
void ledVert() {
  leds.setColorRGB(0, 0, 255, 0);
}

void ledRouge() {
  leds.setColorRGB(0, 255, 0, 0);
}

void ledEteinte() {
  leds.setColorRGB(0, 0, 0, 0);
}

// =====================
// Helpers LoRa
// =====================
void startListening() {
  sendCmd("AT+TEST=RXLRPKT");
  Serial.println("[RX] En écoute...");
}

void sendLoRaMessage(String message) {
  Serial.println("[TX] Envoi : " + message);
  e5.print("AT+TEST=TXLRSTR,\"");
  e5.print(message);
  e5.println("\"");
  delay(500);
  delay(200);
  while (e5.available()) {
    Serial.write(e5.read());
  }
  Serial.println();
  lastTxTime = millis();
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

// =====================
void setup() {
  Serial.begin(9600);
  e5.begin(9600);
  delay(1000);

  // Boutons
  pinMode(BTN_GREEN, INPUT_PULLUP);
  pinMode(BTN_RED,   INPUT_PULLUP);

  // LED
  leds.init();
  ledEteinte();

  Serial.println("=== LORA P2P TX/RX + DUAL BUTTON ===");
  Serial.println("Appareil : " + String(DEVICE_NAME));

  sendCmd("AT+MODE=TEST");
  delay(500);
  sendCmd("AT+TEST=RFCFG,868.35,SF7,125,12,15,14");
  delay(500);

  startListening();
}

// =====================
void loop() {

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

        // Allumer la LED selon le message reçu
        if (message == "vert") {
          ledVert();
        } else if (message == "rouge") {
          ledRouge();
        }
      }
    }
  }

  // --- Bouton VERT : envoie "vert" + allume LED verte ---
  bool etatVert = digitalRead(BTN_GREEN);
  if (etatVert == LOW && dernierEtatVert == HIGH) {
    ledVert();
    sendLoRaMessage("{v," + String(DEVICE_NAME) + "}");
    startListening();
    delay(50); // anti-rebond
  }
  dernierEtatVert = etatVert;

  // --- Bouton ROUGE : envoie "rouge" + allume LED rouge ---
  bool etatRouge = digitalRead(BTN_RED);
  if (etatRouge == LOW && dernierEtatRouge == HIGH) {
    ledRouge();
    sendLoRaMessage("{r," + String(DEVICE_NAME) + "}" );
    startListening();
    delay(50); // anti-rebond
  }
  dernierEtatRouge = etatRouge;

  // --- Envoi manuel depuis le Serial Monitor ---
  if (Serial.available()) {
    String userInput = Serial.readStringUntil('\n');
    userInput.trim();
    if (userInput.length() > 0) {
      sendLoRaMessage(userInput);
      startListening();
    }
  }

}