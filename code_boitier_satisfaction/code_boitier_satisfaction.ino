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

#define SECRET_KEY "ORION"

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
// ---- Interruptions ----
// =====================
volatile bool btnVertPressed  = false;
volatile bool btnRougePressed = false;

unsigned long lastIsrVert  = 0;
unsigned long lastIsrRouge = 0;
#define ISR_DEBOUNCE 200

void isrBtnVert() {
  unsigned long now = millis();
  if (now - lastIsrVert >= ISR_DEBOUNCE) {
    btnVertPressed = true;
    lastIsrVert    = now;
  }
}

void isrBtnRouge() {
  unsigned long now = millis();
  if (now - lastIsrRouge >= ISR_DEBOUNCE) {
    btnRougePressed = true;
    lastIsrRouge    = now;
  }
}

// =====================
// ---- Machine a etats ----
// =====================
const char* modes[] = { "Utilisateur", "Admin", "Maintenance" };
const int   NB_MODES = 3;

enum Etat { ACCUEIL, SELECTION, VOTE };
Etat etatActuel = ACCUEIL;

int modeIndex = 0;

// =====================
// ---- Gestion du temps et de la veille ----
// =====================
#define DEBOUNCE        200
#define DELAI_VEILLE    15000UL

#define ANIM_INTERVAL   800UL
#define POLL_VEILLE     50UL

unsigned long dernierTemps    = 0;
unsigned long dernierActivite = 0;
bool          enVeille        = false;

// =====================
// ---- Utilisateurs et questions secrètes ----
// =====================
const char* userNames[]     = { "User_1",            "User_2",         "User_3"                };
const char* userQuestions[] = { "Aimes-tu le cafe?", "Es-tu matinal?", "Manges-tu a la cafet?" };
const bool  userReponses[]  = { true,                true,             true                    };
const int   NB_USERS        = 3;


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
// Screensaver
// ============================================================
void afficherScreensaver() {
  static int frame       = 0;
  static int positionZZZ = 0;

  lcd.setCursor(0, 0);
  lcd.print("  -- VEILLE --  ");

  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(positionZZZ, 1);
  switch (frame % 3) {
    case 0: lcd.print("z");   break;
    case 1: lcd.print("zz");  break;
    case 2: lcd.print("zzz"); break;
  }

  positionZZZ = (positionZZZ + 1) % 14;
  frame++;
}


// ============================================================
// Mode veille
// ============================================================
void mettreEnVeille() {
  if (enVeille) return;
  enVeille = true;

  Serial.println("[ECO] Inactivite -> mode economie d'energie");
  Serial.flush();

  ledEteinte();

  unsigned long dernierAnim = 0;

  // Réinitialiser les flags avant d'entrer en veille
  btnVertPressed  = false;
  btnRougePressed = false;

  while (true) {
    unsigned long maintenant = millis();

    if (maintenant - dernierAnim >= ANIM_INTERVAL) {
      afficherScreensaver();
      dernierAnim = maintenant;
    }

    // Réveil via flags d'interruption (ou digitalRead en fallback)
    if (btnVertPressed || btnRougePressed) {
      btnVertPressed  = false;
      btnRougePressed = false;
      break;
    }

    delay(POLL_VEILLE);
  }

  enVeille = false;
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

  // Attacher les interruptions sur FALLING
  attachInterrupt(digitalPinToInterrupt(BTN_VERT),  isrBtnVert,  FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_ROUGE), isrBtnRouge, FALLING);

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

  resetInactivite();
  afficherAccueil();
}


// ============================================================
// Loop
// ============================================================
void loop() {

  if (etatActuel == ACCUEIL) {
    if (millis() - dernierActivite >= DELAI_VEILLE) {
      mettreEnVeille();
      afficherAccueil();
      return;
    }
  }

  unsigned long maintenant = millis();
  if (maintenant - dernierTemps < DEBOUNCE) return;

  if (etatActuel == ACCUEIL) {
    // Lecture des flags posés par les ISR
    if (btnVertPressed || btnRougePressed) {
      btnVertPressed  = false;
      btnRougePressed = false;

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


// ============================================================
// Chiffrement XOR avec la clé SECRET_KEY
// ============================================================
String chiffrer(String message) {
  String result = message;
  int keyLen = strlen(SECRET_KEY);
  for (int i = 0; i < (int)message.length(); i++) {
    result[i] = message[i] ^ SECRET_KEY[i % keyLen];
  }
  return result;
}


void sendLoRaMessage(String message) {
  Serial.println("[TX] Clair  : " + message);

  String chiffre = chiffrer(message);

  Serial.print("[TX] Chiffre: ");
  for (int i = 0; i < (int)chiffre.length(); i++) {
    Serial.print("\\x");
    if ((unsigned char)chiffre[i] < 0x10) Serial.print("0");
    Serial.print((unsigned char)chiffre[i], HEX);
  }
  Serial.println();

  String hex = "";
  for (int i = 0; i < (int)chiffre.length(); i++) {
    char buf[3];
    sprintf(buf, "%02X", (unsigned char)chiffre[i]);
    hex += buf;
  }
  Serial.println("[TX] Hex    : " + hex);

  e5.print("AT+TEST=TXLRPKT,\"");
  e5.print(hex);
  e5.println("\"");

  delay(700);
  while (e5.available()) Serial.write(e5.read());
  Serial.println();
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
// Authentification utilisateur
// ============================================================
bool authentifierUtilisateur() {
  int userIndex = 0;

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Choisir user :");
  lcd.setCursor(0, 1); lcd.print("> "); lcd.print(userNames[userIndex]);

  while (true) {
    resetInactivite();

    if (digitalRead(BTN_VERT) == LOW) {
      while (digitalRead(BTN_VERT) == LOW);
      delay(DEBOUNCE);
      userIndex = (userIndex + 1) % NB_USERS;
      lcd.setCursor(0, 1); lcd.print("                ");
      lcd.setCursor(0, 1); lcd.print("> "); lcd.print(userNames[userIndex]);
    }

    if (digitalRead(BTN_ROUGE) == LOW) {
      while (digitalRead(BTN_ROUGE) == LOW);
      delay(DEBOUNCE);
      break;
    }
  }

  String question      = String(userQuestions[userIndex]) + "      ";
  String loopQ         = question + question;
  int    lcdWidth      = 16;
  int    i             = 0;
  bool   answered      = false;
  bool   correct       = false;

  lcd.clear();

  while (digitalRead(BTN_VERT) == LOW || digitalRead(BTN_ROUGE) == LOW);
  delay(300);

  lcd.setCursor(0, 0);
  lcd.print(loopQ.substring(0, lcdWidth));
  lcd.setCursor(0, 1); lcd.print("Vrt=Oui Rge=Non ");

  unsigned long dernierScroll = millis();

  while (!answered) {
    resetInactivite();

    if (millis() - dernierScroll >= 480) {
      lcd.setCursor(0, 0);
      lcd.print(loopQ.substring(i % question.length(), i % question.length() + lcdWidth));
      lcd.setCursor(0, 1); lcd.print("Vrt=Oui Rge=Non ");
      i++;
      dernierScroll = millis();
    }

    if (digitalRead(BTN_VERT) == LOW) {
      while (digitalRead(BTN_VERT) == LOW);
      delay(DEBOUNCE);
      correct  = true;
      answered = true;
    }

    if (digitalRead(BTN_ROUGE) == LOW) {
      while (digitalRead(BTN_ROUGE) == LOW);
      delay(DEBOUNCE);
      correct  = false;
      answered = true;
    }
  }

  lcd.clear();
  if (correct) {
    lcd.setCursor(0, 0); lcd.print("Bonne reponse !");
    lcd.setCursor(0, 1); lcd.print("Bienvenu ");
    lcd.print(userNames[userIndex]);
    delay(2000);
    return true;
  } else {
    lcd.setCursor(0, 0); lcd.print("Mauvaise reponse");
    lcd.setCursor(0, 1); lcd.print("Acces refuse !");
    ledRouge();
    delay(2000);
    ledEteinte();
    etatActuel = ACCUEIL;
    afficherAccueil();
    return false;
  }
}


// ============================================================
// Envoi du message après vote
// ============================================================
void message_envoye(bool satisfait) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Message envoye !");
  lcd.setCursor(0, 1);
  if (satisfait) {
    lcd.print("Satisfait :)");
    ledVert();
    sendLoRaMessage("{\"device\": \"" + String(deviceName) + "\", \"note\": \"vert\"}");
  } else {
    lcd.print("Non satisfait :(");
    ledRouge();
    sendLoRaMessage("{\"device\": \"" + String(deviceName) + "\", \"note\": \"rouge\"}");
  }

  delay(3000);
  ledEteinte();
}


// ============================================================
// Mode Utilisateur : authentification puis vote
// ============================================================
void voter() {
  if (!authentifierUtilisateur()) return;

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
        etatActuel = VOTE;
        voter();
      } else if (modeIndex == 1) {
        bool ok = demanderMotDePasse();
        if (ok) modeAdmin();
        else { etatActuel = ACCUEIL; afficherAccueil(); }
      } else if (modeIndex == 2) {
        bool ok = demanderMotDePasse();
        if (ok) modeMaintenance();
        else { etatActuel = ACCUEIL; afficherAccueil(); }
      }

      buttonPressed = true;
    }
  }
}