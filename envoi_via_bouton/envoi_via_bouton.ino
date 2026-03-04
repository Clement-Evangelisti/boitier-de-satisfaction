#include <LiquidCrystal.h>
#include <SoftwareSerial.h>
#include <ChainableLED.h>
#include <EEPROM.h>

// =====================
// ---- LCD ----
// =====================
LiquidCrystal lcd(4, 9, 10, 11, 12, 13);

// =====================
// ---- LoRa (Wio-E5) ----
// =====================
SoftwareSerial e5(7, 8);  // RX=D7, TX=D8

// =====================
// ---- EEPROM ----
// =====================
#define EEPROM_ADDR_INDEX  0
#define EEPROM_ADDR_VALID  1
#define EEPROM_MAGIC       0xAB

const char* nomsDisponibles[] = { "Cafet_Orion", "Cafet_Cassio" };
const int   NB_NOMS = 2;
String deviceName = "";

// =====================
// ---- LED chainable ----
// =====================
#define NUM_LEDS 1
ChainableLED leds(5, 6, NUM_LEDS);

// =====================
// ---- Boutons ----
// =====================
#define BTN_VERT   2
#define BTN_ROUGE  3

// =====================
// ---- Machine a etats ----
// =====================
// trois mode d'utilisateurs disponible
// Admin a accès aux statistiques
// Utilisateur procède aux votes
// Maintenance peut modifier les paramètres du boitier
const char* modes[] = { "Utilisateur", "Admin", "Maintenance" };
const int   NB_MODES = 3;

enum Etat { ACCUEIL, SELECTION, VOTE };
Etat etatActuel = ACCUEIL;

int modeIndex = 0;

// =====================
// ---- Gestion du temps et de la veille ----
// =====================
#define DEBOUNCE        200
#define DELAI_VEILLE    15000UL   // 30 secondes d'inactivite

// =====================
// ---- Mode eco : niveaux d'animation LCD ----
// =====================

#define ANIM_INTERVAL   800UL     // Intervalle rafraichissement screensaver (ms)
#define POLL_VEILLE     50UL      // Intervalle polling boutons en veille (ms)

unsigned long dernierTemps    = 0;
unsigned long dernierActivite = 0;
bool          enVeille        = false;


// ============================================================
// Reinitialise le timer d'inactivite
// ============================================================
void resetInactivite() {
  dernierActivite = millis();
}


// ============================================================
// LED helpers
// ============================================================
void ledVert()    { leds.setColorRGB(0,   0, 255,   0); }
void ledRouge()   { leds.setColorRGB(0, 255,   0,   0); }
void ledEteinte() { leds.setColorRGB(0,   0,   0,   0); }


// ============================================================
// Screensaver : animation "Z Z Z" qui defile sur le LCD
// Donne un feedback visuel que l'appareil est en veille
// mais reste receptif aux boutons
// ============================================================
void afficherScreensaver() {
  static int    frame        = 0;
  static int    positionZZZ  = 0;

  // Ligne 0 : message fixe
  lcd.setCursor(0, 0);
  lcd.print("  -- VEILLE --  ");

  // Ligne 1 : "z" qui se deplace de gauche a droite
  lcd.setCursor(0, 1);
  lcd.print("                ");   // effacer la ligne
  lcd.setCursor(positionZZZ, 1);
  switch (frame % 3) {
    case 0: lcd.print("z");   break;
    case 1: lcd.print("zz");  break;
    case 2: lcd.print("zzz"); break;
  }

  positionZZZ = (positionZZZ + 1) % 14;  // defilement sur 14 colonnes
  frame++;
}


// ============================================================
// Mode economie d'energie simule
//

//
// Reveil : appui sur BTN_VERT ou BTN_ROUGE
// ============================================================
void mettreEnVeille() {
  if (enVeille) return;   // Evite une double entree
  enVeille = true;

  Serial.println("[ECO] Inactivite -> mode economie d'energie");
  Serial.flush();

  // Eteindre la LED (economie principale)
  ledEteinte();

  // Optionnel : pour eteindre completement le retro-eclairage LCD
  // decommentez la ligne ci-dessous (aucun feedback visuel)
  // lcd.noDisplay();

  unsigned long dernierAnim = 0;

  // Boucle de veille : polling lent, screensaver anime
  while (true) {
    unsigned long maintenant = millis();

    // Rafraichir le screensaver a intervalle reduit
    if (maintenant - dernierAnim >= ANIM_INTERVAL) {
      afficherScreensaver();
      dernierAnim = maintenant;
    }

    // Verifier les boutons de reveil
    if (digitalRead(BTN_VERT) == LOW || digitalRead(BTN_ROUGE) == LOW) {
      break;    // Sortie de la veille
    }

    // Attente courte : reduit la charge CPU sans bloquer les boutons
    delay(POLL_VEILLE);
  }

  // --- REVEIL ---
  enVeille = false;

  // Optionnel : si lcd.noDisplay() utilise plus haut, rallumer ici
  // lcd.display();

  // Anti-rebond apres reveil
  delay(DEBOUNCE);

  resetInactivite();
  Serial.println("[ECO] Reveille par bouton !");
}


// ============================================================
// EEPROM
// ============================================================
void sauvegarderDeviceName(int index) {
  EEPROM.write(EEPROM_ADDR_INDEX, (byte)index);
  EEPROM.write(EEPROM_ADDR_VALID, EEPROM_MAGIC);
}

int chargerIndexDeviceName() {
  byte magic = EEPROM.read(EEPROM_ADDR_VALID);
  if (magic == EEPROM_MAGIC) {
    byte index = EEPROM.read(EEPROM_ADDR_INDEX);
    if (index < NB_NOMS) return (int)index;
  }
  sauvegarderDeviceName(0);
  return 0;
}


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

  int idx = chargerIndexDeviceName();
  deviceName = nomsDisponibles[idx];

  Serial.println("=== LCD + LORA P2P ===");
  Serial.println("Appareil : " + deviceName);

  sendCmd("AT+MODE=TEST");
  delay(500);
  sendCmd("AT+TEST=RFCFG,868.35,SF7,125,12,15,14");
  delay(500);
  startListening();

  resetInactivite();
  afficherAccueil();
}


// ============================================================
// Loop
// ============================================================
void loop() {

  // --- Timeout 30s depuis l'accueil : passage en eco ---
  if (etatActuel == ACCUEIL) {
    if (millis() - dernierActivite >= DELAI_VEILLE) {
      mettreEnVeille();
      afficherAccueil();
      return;
    }
  }

  // --- Lecture LoRa ---
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

  // --- Envoi manuel Serial Monitor ---
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
      resetInactivite();
      etatActuel = SELECTION;
      modeIndex  = 0;
      afficherSelection();
      dernierTemps = maintenant;
    }
  }
}


// ============================================================
// LoRa helpers
// ============================================================
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
// Affichages LCD
// ============================================================
void afficherAccueil() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("  Bienvenue !   ");
  lcd.setCursor(0, 1); lcd.print("Appuyer bouton..");
}

void afficherConfirmation() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Mode choisi :");
  lcd.setCursor(0, 1); lcd.print(modes[modeIndex]);
}


// ============================================================
// Mot de passe : tenir VERT 3 secondes
// ============================================================
bool demanderMotDePasse() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Mot de passe :");
  lcd.setCursor(0, 1); lcd.print("Tenir vert 3sec");

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
          resetInactivite();
          return true;
        }
      }
      lcd.setCursor(0, 0); lcd.print("Mot de passe :");
      lcd.setCursor(0, 1); lcd.print("Tenir vert 3sec");
    }
  }
}


// ============================================================
// Mode Admin
// ============================================================
void modeAdmin() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Statistiques");
  lcd.setCursor(0, 1); lcd.print("                ");

  while (digitalRead(BTN_VERT) == HIGH && digitalRead(BTN_ROUGE) == HIGH);
  while (digitalRead(BTN_VERT) == LOW  || digitalRead(BTN_ROUGE) == LOW);
  delay(DEBOUNCE);

  etatActuel = ACCUEIL;
  resetInactivite();
  afficherAccueil();
}


// ============================================================
// Mode Maintenance
// ============================================================
void modeMaintenance() {
  int nomIndex = 0;
  for (int i = 0; i < NB_NOMS; i++) {
    if (deviceName == nomsDisponibles[i]) { nomIndex = i; break; }
  }

  while (digitalRead(BTN_VERT) == LOW || digitalRead(BTN_ROUGE) == LOW);
  delay(DEBOUNCE);

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Device name :");
  lcd.setCursor(0, 1); lcd.print("> ");
  lcd.print(nomsDisponibles[nomIndex]);

  while (true) {
    resetInactivite();

    if (digitalRead(BTN_VERT) == LOW) {
      while (digitalRead(BTN_VERT) == LOW);
      delay(DEBOUNCE);
      nomIndex = (nomIndex + 1) % NB_NOMS;
      lcd.setCursor(0, 1); lcd.print("> ");
      lcd.print(nomsDisponibles[nomIndex]);
      for (int j = 2 + strlen(nomsDisponibles[nomIndex]); j < 16; j++) lcd.print(" ");
    }

    if (digitalRead(BTN_ROUGE) == LOW) {
      while (digitalRead(BTN_ROUGE) == LOW);
      delay(DEBOUNCE);
      deviceName = nomsDisponibles[nomIndex];
      sauvegarderDeviceName(nomIndex);
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Sauvegarde !");
      lcd.setCursor(0, 1); lcd.print(deviceName);
      Serial.println("[EEPROM] Device name sauvegarde : " + deviceName);
      delay(2000);
      break;
    }
  }

  etatActuel = ACCUEIL;
  resetInactivite();
  afficherAccueil();
}


// ============================================================
// Mode Utilisateur : vote + envoi LoRa
// ============================================================
void message_envoye(bool satisfait) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Message envoye !");
  lcd.setCursor(0, 1);
  if (satisfait) {
    lcd.print("Satisfait :)");
    ledVert();
    sendLoRaMessage('{"device": "' + String(deviceName) + '", "note": "vert"}');
    
    
  } else {
    lcd.print("Non satisfait :(");
    ledRouge();
    sendLoRaMessage('{"device": "' + String(deviceName) + '", "note": "rouge"}');
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
    resetInactivite();

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
  resetInactivite();
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
    resetInactivite();

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

      if (modeIndex == 0) {
        bool ok = demanderMotDePasse();
        if (ok) modeAdmin();
        else { etatActuel = ACCUEIL; afficherAccueil(); }

      } else if (modeIndex == 1) {
        etatActuel = VOTE;
        voter();

      } else if (modeIndex == 2) {
        bool ok = demanderMotDePasse();
        if (ok) modeMaintenance();
        else { etatActuel = ACCUEIL; afficherAccueil(); }
      }

      buttonPressed = true;
    }
  }
}
