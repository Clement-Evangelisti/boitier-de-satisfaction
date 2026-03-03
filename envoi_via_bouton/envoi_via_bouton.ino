#include <LiquidCrystal.h>
#include <SoftwareSerial.h>
#include <ChainableLED.h>

// =====================
// ---- LCD ----
// =====================
LiquidCrystal lcd(4, 9, 10, 11, 12, 13);

// =====================
// ---- LoRa (Wio-E5) ----
// =====================
SoftwareSerial e5(2, 3);  // RX=D2, TX=D3

#define DEVICE_NAME "Cafet_Orion"

// =====================
// ---- LED chainable ----
// =====================
#define NUM_LEDS 1
ChainableLED leds(5, 6, NUM_LEDS);  // Data=D5, Clock=D6

// =====================
// ---- Boutons ----
// =====================
#define BTN_VERT   7
#define BTN_ROUGE  8

// =====================
// ---- Machine a etats ----
// =====================
// trois mode d'utilisateurs disponible
// Admin a accès aux statistiques
// Utilisateur procède aux votes
// Maintenance peut modifier les paramètres du boitier
const char* modes[] = { "Admin", "Utilisateur", "Maintenance" };
const int NB_MODES = 3;

enum Etat { ACCUEIL, SELECTION, VOTE };
Etat etatActuel = ACCUEIL;

int modeIndex = 0;

unsigned long dernierTemps = 0;
const int DEBOUNCE = 200;


void ledVert()    { leds.setColorRGB(0,   0, 255,   0); }
void ledRouge()   { leds.setColorRGB(0, 255,   0,   0); }
void ledEteinte() { leds.setColorRGB(0,   0,   0,   0); }





// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(9600);
  e5.begin(9600);
  delay(1000);

  lcd.begin(16, 2);
  pinMode(BTN_VERT,  INPUT_PULLUP);
  pinMode(BTN_ROUGE, INPUT_PULLUP);

  leds.init();
  ledEteinte();

  Serial.println("=== LCD + LORA P2P ===");
  Serial.println("Appareil : " + String(DEVICE_NAME));

  sendCmd("AT+MODE=TEST");
  delay(500);
  sendCmd("AT+TEST=RFCFG,868.35,SF7,125,12,15,14");
  delay(500);
  startListening();

  afficherAccueil();
}

// ============================================================
// Loop
// ============================================================
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
        if      (message == "vert")  ledVert();
        else if (message == "rouge") ledRouge();
      }
    }
  }

  // --- Envoi manuel depuis le Serial Monitor ---
  if (Serial.available()) {
    String userInput = Serial.readStringUntil('\n');
    userInput.trim();
    if (userInput.length() > 0) {
      sendLoRaMessage(userInput);
      startListening();
    }
  }

  // --- Navigation accueil ---
  unsigned long maintenant = millis();
  if (maintenant - dernierTemps < DEBOUNCE) return;

  if (etatActuel == ACCUEIL) {
    if (digitalRead(BTN_VERT) == LOW || digitalRead(BTN_ROUGE) == LOW) {
      etatActuel = SELECTION;
      modeIndex  = 0;
      afficherSelection();
      dernierTemps = maintenant;
    }
  }
}


void sendCmd(String cmd) {
  e5.println(cmd);
  delay(300);
  while (e5.available()) Serial.write(e5.read());
  Serial.println();
}

void startListening() {
  sendCmd("AT+TEST=RXLRPKT");
  Serial.println("[RX] En ecoute...");
}

// Envoi LoRa -- appele UNIQUEMENT depuis voter() pour l'utilisateur
void sendLoRaMessage(String message) {
  Serial.println("[TX] Envoi : " + message);
  e5.print("AT+TEST=TXLRSTR,\"");
  e5.print(message);
  e5.println("\"");
  delay(700);
  while (e5.available()) Serial.write(e5.read());
  Serial.println();
}

String hexToAscii(String hex) {
  String ascii = "";
  for (int i = 0; i < (int)hex.length(); i += 2) {
    String byteStr = hex.substring(i, i + 2);
    ascii += (char)strtol(byteStr.c_str(), NULL, 16);
  }
  return ascii;
}

// ============================================================
// Affichages de l'ecran LCD
// ============================================================
void afficherAccueil() { // retour à l'acceuil
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("  Bienvenue !   ");
  lcd.setCursor(0, 1); lcd.print("Appuyer bouton..");
}

void afficherConfirmation() { // message de confirmation pour le type d'utilisateur choisi
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Mode choisi :");
  lcd.setCursor(0, 1); lcd.print(modes[modeIndex]);
}


bool demanderMotDePasse() { // mot de passe pour admin et maintenance
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Mot de passe :");
  lcd.setCursor(0, 1); lcd.print("Tenir vert 3sec"); // on affiche le mdp pour le proto

  while (true) {
    if (digitalRead(BTN_ROUGE) == LOW) {
      while (digitalRead(BTN_ROUGE) == LOW);
      delay(DEBOUNCE);
      etatActuel = ACCUEIL;
      return false;
    }

    if (digitalRead(BTN_VERT) == LOW) {
      unsigned long debut = millis();
      lcd.setCursor(0, 1); lcd.print("                ");

      while (digitalRead(BTN_VERT) == LOW) {
        unsigned long tenu = millis() - debut;
        int progression = map(tenu, 0, 3000, 0, 16);
        lcd.setCursor(0, 1);
        for (int k = 0; k < 16; k++) lcd.print(k < progression ? "#" : "-");

        if (tenu >= 3000) {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("Mot de passe");
          lcd.setCursor(0, 1); lcd.print("correct !");
          delay(2000);
          return true;
        }
      }
      // Relache trop tot
      lcd.setCursor(0, 0); lcd.print("Mot de passe :");
      lcd.setCursor(0, 1); lcd.print("Tenir vert 3sec");
    }
  }
}

// ============================================================
// Mode Admin qui a accès aux statistiques
// ============================================================
void modeAdmin() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Statistiques");
  lcd.setCursor(0, 1); lcd.print("                ");

  while (digitalRead(BTN_VERT) == HIGH && digitalRead(BTN_ROUGE) == HIGH);
  while (digitalRead(BTN_VERT) == LOW  || digitalRead(BTN_ROUGE) == LOW);
  delay(DEBOUNCE);

  etatActuel = ACCUEIL;
  afficherAccueil();
}

// ============================================================
// Mode Maintenance peut modifier des paramètres du boitier
// ============================================================
void modeMaintenance() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Acces parametres");
  lcd.setCursor(0, 1); lcd.print("                ");

  while (digitalRead(BTN_VERT) == HIGH && digitalRead(BTN_ROUGE) == HIGH);
  while (digitalRead(BTN_VERT) == LOW  || digitalRead(BTN_ROUGE) == LOW);
  delay(DEBOUNCE);

  etatActuel = ACCUEIL;
  afficherAccueil();
}

// ============================================================
// Mode Utilisateur qui ne peut seulement envoyé un avis
// ============================================================
void message_envoye(bool satisfait) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Message envoye !");
  lcd.setCursor(0, 1);
  if (satisfait) {
    lcd.print("Satisfait :)");
    ledVert();
    sendLoRaMessage("{v," + String(DEVICE_NAME) + "}");
  } else {
    lcd.print("Non satisfait :(");
    ledRouge();
    sendLoRaMessage("{r," + String(DEVICE_NAME) + "}");
  }
  startListening();
  delay(3000);
  ledEteinte();
}

void voter() {
  String message  = "Etes-vous satisfait ?  ";
  String loop1    = message + message;
  int    lcdWidth = 16;
  bool   buttonPressed = false;
  int    i = 0;

  while (!buttonPressed) {
    lcd.setCursor(0, 0); lcd.print("Procedez au vote");
    lcd.setCursor(0, 1);
    lcd.print(loop1.substring(i % message.length(), i % message.length() + lcdWidth));

    delay(480);
    i++;

    if (digitalRead(BTN_VERT) == LOW) {
      while (digitalRead(BTN_VERT) == LOW);
      delay(DEBOUNCE);
      message_envoye(true);
      buttonPressed = true;
    }

    if (digitalRead(BTN_ROUGE) == LOW) {
      while (digitalRead(BTN_ROUGE) == LOW);
      delay(DEBOUNCE);
      message_envoye(false);
      buttonPressed = true;
    }
  }

  etatActuel = ACCUEIL;
  afficherAccueil();
}

// ============================================================
// Selection du mode
// ============================================================
void afficherSelection() {
  lcd.clear();

  String message  = "Selectionnez user  ";
  String loop0    = message + message;
  int    lcdWidth = 16;
  bool   buttonPressed = false;
  int    i = 0;

  while (!buttonPressed) {
    lcd.setCursor(0, 0);
    lcd.print(loop0.substring(i % message.length(), i % message.length() + lcdWidth));

    lcd.setCursor(0, 1); lcd.print("> ");
    lcd.print(modes[modeIndex]);
    for (int j = 2 + strlen(modes[modeIndex]); j < 16; j++) lcd.print(" ");

    delay(480);
    i++;

    if (digitalRead(BTN_VERT) == LOW) {
      while (digitalRead(BTN_VERT) == LOW);
      delay(DEBOUNCE);
      modeIndex = (modeIndex + 1) % NB_MODES;
      lcd.clear();
    }

    if (digitalRead(BTN_ROUGE) == LOW) {
      while (digitalRead(BTN_ROUGE) == LOW);
      delay(DEBOUNCE);
      afficherConfirmation();
      delay(2000);

      if (modeIndex == 0) {          // Admin
        bool ok = demanderMotDePasse();
        if (ok) modeAdmin();
        else { etatActuel = ACCUEIL; afficherAccueil(); }

      } else if (modeIndex == 1) {   // Utilisateur -> envoi radio autorise
        etatActuel = VOTE;
        voter();

      } else if (modeIndex == 2) {   // Maintenance
        bool ok = demanderMotDePasse();
        if (ok) modeMaintenance();
        else { etatActuel = ACCUEIL; afficherAccueil(); }
      }

      buttonPressed = true;
    }
  }
}


