/*
 * COVID-19 access control — Arduino firmware
 *
 * Grants access only when all checks succeed:
 *   1. identity   : registered RFID card (MFRC522) or PIN on a 3x3 keypad
 *   2. proximity  : user within PROXIMITY_CM of the HC-SR04 ultrasonic sensor
 *   3. temperature: reading below FEVER_THRESHOLD_C (DHT11 as a stand-in for
 *                   the recommended MLX90614 contactless sensor)
 * A servo moves 90 degrees to simulate the door lock.
 *
 * After a valid PIN the user may enrol a new RFID card (cards are stored in
 * EEPROM). User-facing messages are in Italian.
 *
 * Libraries: MFRC522, Keypad, LiquidCrystal_SR3W, SR04, dht_nonblocking,
 *            plus the Arduino core ones (EEPROM, Servo, SPI, Wire).
 */

#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_SR3W.h>
#include <Keypad.h>
#include <Servo.h>
#include "SR04.h"
#include <dht_nonblocking.h>

// ---------------------------------------------------------------- settings
const char     ACCESS_PIN[]        = "1234";   // change before deployment
const uint8_t  PIN_LENGTH          = 4;        // must equal strlen(ACCESS_PIN)
const float    FEVER_THRESHOLD_C   = 37.5;
const long     PROXIMITY_CM        = 6;
const uint8_t  SERVO_CLOSED_DEG    = 0;
const uint8_t  SERVO_OPEN_DEG      = 90;
const unsigned long DOOR_OPEN_MS   = 5000;
const unsigned long RFID_WAIT_MS   = 5000;     // time allowed to present a card
const unsigned long DHT_TIMEOUT_MS = 5000;

// -------------------------------------------------------------------- pins
const uint8_t PIN_RFID_SS   = 10;
const uint8_t PIN_RFID_RST  = 9;
const uint8_t PIN_LCD_STROBE = 7;  // IC4094 STCP
const uint8_t PIN_LCD_DATA   = 6;  // IC4094 DS
const uint8_t PIN_LCD_CLOCK  = 5;  // IC4094 SHCP
const uint8_t PIN_SERVO      = 3;
const uint8_t PIN_US_TRIG    = 4;
const uint8_t PIN_US_ECHO    = 2;
const uint8_t PIN_DHT        = 8;

// ------------------------------------------------------------------ EEPROM
// Layout: byte 0 = number of stored cards, then fixed 9-byte records
// (8 hex characters + NUL, i.e. the first 4 bytes of the card UID).
const int EEPROM_MAX_ADDR = EEPROM.length() - 1;
const int UID_CHARS       = 8;
const int UID_RECORD_SIZE = UID_CHARS + 1;
const int MAX_CARDS       = (EEPROM_MAX_ADDR) / UID_RECORD_SIZE;  // 56 on an ATmega328P

// ----------------------------------------------------------------- devices
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
LiquidCrystal_SR3W lcd(PIN_LCD_DATA, PIN_LCD_CLOCK, PIN_LCD_STROBE, 7, POSITIVE);
Servo lockServo;
SR04 sr04(PIN_US_ECHO, PIN_US_TRIG);
DHT_nonblocking dht_sensor(PIN_DHT, DHT_TYPE_11);

const byte ROWS = 3, COLS = 3;  // fourth row/column left unconnected
char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
};
byte rowPins[ROWS] = {19, 18, 17};  // A5, A4, A3
byte colPins[COLS] = {16, 15, 14};  // A2, A1, A0
Keypad keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---------------------------------------------------------------- UI helpers
void show(const __FlashStringHelper* line0, const __FlashStringHelper* line1 = nullptr) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line0);
  if (line1) {
    lcd.setCursor(0, 1);
    lcd.print(line1);
  }
}

// Blocks until the user presses `a` or `b`.
char waitForKey(char a, char b) {
  for (;;) {
    char k = keypad.getKey();
    if (k == a || k == b) return k;
  }
}

// ------------------------------------------------------------ EEPROM storage
uint8_t cardCount() {
  uint8_t n = EEPROM.read(0);
  return n > MAX_CARDS ? 0 : n;  // 0xFF on a blank EEPROM => treat as empty
}

int recordAddr(uint8_t index) { return 1 + index * UID_RECORD_SIZE; }

bool isCardRegistered(const char* uid) {
  for (uint8_t i = 0, n = cardCount(); i < n; i++) {
    int addr = recordAddr(i);
    uint8_t j = 0;
    while (j < UID_CHARS && EEPROM.read(addr + j) == (uint8_t)uid[j]) j++;
    if (j == UID_CHARS) return true;
  }
  return false;
}

bool storeCard(const char* uid) {
  uint8_t n = cardCount();
  if (n >= MAX_CARDS) return false;
  int addr = recordAddr(n);
  for (uint8_t j = 0; j < UID_CHARS; j++) EEPROM.update(addr + j, uid[j]);
  EEPROM.update(addr + UID_CHARS, 0);
  EEPROM.update(0, n + 1);
  return true;
}

// ------------------------------------------------------------------- RFID
// Polls the reader for up to RFID_WAIT_MS. On success writes the UID as
// 8 lowercase hex chars (+NUL) into `out` and halts the card.
bool readCardUid(char* out) {
  unsigned long start = millis();
  while (millis() - start < RFID_WAIT_MS) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      for (uint8_t i = 0; i < UID_CHARS / 2; i++) {
        uint8_t b = i < rfid.uid.size ? rfid.uid.uidByte[i] : 0;
        const char* hex = "0123456789abcdef";
        out[2 * i]     = hex[b >> 4];
        out[2 * i + 1] = hex[b & 0x0F];
      }
      out[UID_CHARS] = 0;
      rfid.PICC_HaltA();
      return true;
    }
  }
  return false;
}

// --------------------------------------------------------------- sensors
// Asks the user to stand within PROXIMITY_CM of the ultrasonic sensor.
void waitForProximity() {
  for (;;) {
    long d = sr04.Distance();
    if (d > 0 && d <= PROXIMITY_CM) break;
    show(F("AVVICINARSI A 6"), F("CM DAL SENSORE"));
    delay(1000);
    lcd.clear();
    lcd.print(F("SEI A "));
    lcd.print(d);
    lcd.setCursor(0, 1);
    lcd.print(F("CM DAL SENSORE"));
    delay(1000);
  }
  show(F("CONTROLLO"), F("IN CORSO"));
  delay(3000);
}

// Returns true only if a valid reading below the fever threshold was obtained.
bool temperatureOk() {
  float t, h;
  unsigned long start = millis();
  while (!dht_sensor.measure(&t, &h)) {
    if (millis() - start > DHT_TIMEOUT_MS) {
      show(F("ERRORE SENSORE"), F("TEMPERATURA"));
      delay(3000);
      return false;  // fail closed
    }
  }
  Serial.println(t);
  show(F("TEMPERATURA"));
  lcd.setCursor(0, 1);
  lcd.print(t);
  delay(1000);
  return t < FEVER_THRESHOLD_C;
}

void openDoor() {
  lockServo.write(SERVO_OPEN_DEG);
  delay(DOOR_OPEN_MS);
  lockServo.write(SERVO_CLOSED_DEG);
}

// Proximity + temperature check, then open the door.
void healthCheckAndOpen() {
  waitForProximity();
  if (temperatureOk()) {
    show(F("PREGO, PUO'"), F("ACCEDERE"));
    delay(3000);
    openDoor();
  } else {
    show(F("RILEVATA FEBBRE"), F("VIETATO ENTRARE"));
    delay(3000);
  }
}

// ------------------------------------------------------------- access flows
void rfidAccess() {
  show(F("Avvicinare RFID"));
  char uid[UID_RECORD_SIZE];
  if (!readCardUid(uid)) {
    show(F("ERRORE DI LETTURA"));
    delay(2000);
    return;
  }
  if (!isCardRegistered(uid)) {
    show(F("RFID"), F("NON ABILITATO"));
    delay(3000);
    return;
  }
  show(F("RFID"), F("ABILITATO"));
  delay(3000);
  healthCheckAndOpen();
}

bool pinAccepted() {
  char entered[PIN_LENGTH];
  show(F("Inserire PIN"));
  for (uint8_t i = 0; i < PIN_LENGTH;) {
    char k = keypad.getKey();
    if (k) {
      entered[i] = k;
      lcd.setCursor(i, 1);
      lcd.print('*');
      i++;
    }
  }
  delay(1000);
  uint8_t diff = 0;  // constant-time comparison
  for (uint8_t i = 0; i < PIN_LENGTH; i++) diff |= entered[i] ^ ACCESS_PIN[i];
  return diff == 0;
}

void enrolCard() {
  show(F("Avvicinare RFID"));
  char uid[UID_RECORD_SIZE];
  if (!readCardUid(uid)) {
    show(F("ERRORE DURANTE"), F("LA LETTURA"));
  } else if (isCardRegistered(uid)) {
    show(F("RFID GIA'"), F("ABILITATO"));
  } else if (storeCard(uid)) {
    show(F("OPERAZIONE"), F("COMPLETATA"));
  } else {
    show(F("MEMORIA PIENA"));
  }
  delay(3000);
}

void pinAccess() {
  if (!pinAccepted()) {
    show(F("PIN ERRATO"));
    delay(3000);
    return;
  }
  show(F("ACCESSO"), F("CONSENTITO"));
  delay(3000);

  show(F("VUOI REGISTRARE"), F("RFID? 1 SI 2 NO"));
  if (waitForKey('1', '2') == '1') enrolCard();
  healthCheckAndOpen();
}

// ------------------------------------------------------------------ Arduino
void setup() {
  Serial.begin(9600);
  lockServo.attach(PIN_SERVO);
  lockServo.write(SERVO_CLOSED_DEG);
  lcd.begin(16, 2);
  SPI.begin();
  rfid.PCD_Init();
}

void loop() {
  show(F("RFID ACCESS: 1"), F("PIN  ACCESS: 2"));
  if (waitForKey('1', '2') == '1') rfidAccess();
  else pinAccess();
}
