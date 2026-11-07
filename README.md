# Arduino COVID-19 Access Control

An Arduino Uno entry gate that opens a (simulated) door lock only after three independent checks pass: **identity** (RFID card or PIN), **proximity** (ultrasonic distance) and **temperature** (DHT11 reading below a threshold). It was built as an embedded-systems course project (2020) to demonstrate sensor/actuator integration on a resource-constrained 8-bit board.

![Prototype](docs/prototype.jpg)

## Table of contents

1. [What it is and why it exists](#1-what-it-is-and-why-it-exists)
2. [Architecture](#2-architecture)
3. [Requirements](#3-requirements)
4. [Installation (step by step)](#4-installation-step-by-step)
5. [Configuration](#5-configuration)
6. [Usage and expected behaviour](#6-usage-and-expected-behaviour)
7. [Repository layout](#7-repository-layout)
8. [Testing and verification](#8-testing-and-verification)
9. [Troubleshooting](#9-troubleshooting)
10. [Limitations and security notes](#10-limitations-and-security-notes)
11. [Extending and customising](#11-extending-and-customising)
12. [Changes](#12-changes)

## 1. What it is and why it exists

During the COVID-19 pandemic, access to shared spaces was often conditioned on a temperature check. This project is a low-cost proof of concept of such a gate: a user identifies themselves, stands close to a sensor, is measured, and the door opens only if the reading is acceptable. The sketch is a single file (`firmware/AccessControl/AccessControl.ino`) with no host-side software. A project report (PDF) is in `docs/`.

It is a teaching prototype, **not** a medical device and not a secure access-control product (see [section 10](#10-limitations-and-security-notes)).

## 2. Architecture

```
            +------------------+
 keypad --->|                  |---> 16x2 LCD (via IC4094 shift register)
 (3x3)      |                  |
 MFRC522 -->|  Arduino Uno     |---> servo (door lock simulation)
 (SPI)      |  ATmega328P      |
 HC-SR04 -->|                  |---> Serial 9600 baud (temperature log)
 DHT11 ---->|                  |
            |  EEPROM: card    |
            |  whitelist       |
            +------------------+
```

Control flow (one iteration of `loop()`):

```
menu: 1 = RFID, 2 = PIN
  |
  +-- 1: wait up to 5 s for a card
  |       unknown / unreadable -> message, back to menu
  |       registered           -> health check
  |
  +-- 2: enter 4-digit PIN
          wrong -> message, back to menu
          right -> optional "enrol a new RFID card?" (1 yes / 2 no)
                -> health check

health check:
  wait until distance <= PROXIMITY_CM (repeats forever, prompts the user)
  -> read DHT11 (timeout 5 s; failure = access denied, fail-closed)
  -> t < FEVER_THRESHOLD_C ? servo to 90 deg for 5 s, then back to 0
                           : "RILEVATA FEBBRE / VIETATO ENTRARE"
```

Cards are stored in the on-chip EEPROM: byte 0 is the card count, followed by fixed 9-byte records (8 hex characters of the first 4 UID bytes + NUL). All LCD messages are in Italian.

## 3. Requirements

### Hardware

| Part | Notes |
|---|---|
| Arduino Uno (ATmega328P, 1 KB EEPROM) | Other boards are untested; the pin numbers and EEPROM capacity assume an Uno |
| MFRC522 RFID reader + MIFARE cards/tags | 3.3 V logic on the reader; check your module's level shifting |
| 16x2 character LCD driven through an IC4094 shift register | 3-wire interface (see pins below) |
| 3x3 keypad (or a 4x4 with one row and one column unused) | |
| HC-SR04 ultrasonic sensor | |
| DHT11 temperature/humidity sensor | Stand-in only; see limitations |
| Hobby servo (e.g. SG90) | Simulates the lock |
| Jumper wires, breadboard, 5 V supply | A separate supply for the servo is advisable (not verified) |

### Software

- Arduino IDE (any recent 1.8.x or 2.x) or `arduino-cli`; board `Arduino Uno` (core `arduino:avr`).
- Libraries (install via Library Manager; exact versions that the author used are not recorded in the repo):
  `MFRC522`, `Keypad`, `LiquidCrystal_SR3W` (shipped with the NewLiquidCrystal fork), `SR04`, `DHT_nonblocking` (header `dht_nonblocking.h`). `EEPROM`, `Servo`, `SPI` and `Wire` come with the AVR core.

## 4. Installation (step by step)

1. **Wire the hardware** according to the pin table in [section 5](#5-configuration).
2. **Install the libraries.** In the Arduino IDE: *Sketch > Include Library > Manage Libraries*, search for and install `MFRC522`, `Keypad`, `SR04` and `DHT_nonblocking`. `LiquidCrystal_SR3W` is not in the Library Manager under that name: install the NewLiquidCrystal library (https://bitbucket.org/fmalpartida/new-liquidcrystal) by copying it into your `libraries/` folder.
3. **Open the sketch**: `firmware/AccessControl/AccessControl.ino`.
4. **Change the default PIN** (`ACCESS_PIN`) at the top of the sketch.
5. Select *Tools > Board > Arduino Uno* and the right serial port, then click *Upload*.

Equivalent `arduino-cli` flow (commands as documented by the tool; not verified here, see [section 8](#8-testing-and-verification)):

```bash
arduino-cli core install arduino:avr
arduino-cli lib install MFRC522 Keypad SR04 DHT_nonblocking
# install NewLiquidCrystal manually into your sketchbook libraries folder
arduino-cli compile --fqbn arduino:avr:uno firmware/AccessControl
arduino-cli upload  --fqbn arduino:avr:uno -p /dev/ttyACM0 firmware/AccessControl
```

## 5. Configuration

All settings are constants at the top of `AccessControl.ino`.

### Behaviour

| Constant | Default | Meaning |
|---|---|---|
| `ACCESS_PIN` | `"1234"` | PIN accepted on the keypad. **Change before any real use.** |
| `PIN_LENGTH` | `4` | Number of digits read; must equal `strlen(ACCESS_PIN)` |
| `FEVER_THRESHOLD_C` | `37.5` | Readings strictly below this value are accepted (deg C) |
| `PROXIMITY_CM` | `6` | Maximum distance (cm) from the HC-SR04 to proceed. The on-screen text "AVVICINARSI A 6 CM" is hard-coded and does not follow this constant |
| `SERVO_CLOSED_DEG` / `SERVO_OPEN_DEG` | `0` / `90` | Servo angles for locked/unlocked |
| `DOOR_OPEN_MS` | `5000` | How long the door stays open |
| `RFID_WAIT_MS` | `5000` | Time allowed to present a card |
| `DHT_TIMEOUT_MS` | `5000` | Time allowed to obtain a DHT11 reading before failing closed |

### Pins (Arduino Uno)

| Function | Constant | Pin |
|---|---|---|
| MFRC522 SS (SDA) | `PIN_RFID_SS` | D10 |
| MFRC522 RST | `PIN_RFID_RST` | D9 |
| MFRC522 MOSI / MISO / SCK | hardware SPI | D11 / D12 / D13 |
| LCD shift register DS (data) | `PIN_LCD_DATA` | D6 |
| LCD shift register SHCP (clock) | `PIN_LCD_CLOCK` | D5 |
| LCD shift register STCP (strobe) | `PIN_LCD_STROBE` | D7 |
| Servo signal | `PIN_SERVO` | D3 |
| HC-SR04 trigger | `PIN_US_TRIG` | D4 |
| HC-SR04 echo | `PIN_US_ECHO` | D2 |
| DHT11 data | `PIN_DHT` | D8 |
| Keypad rows | `rowPins` | A5, A4, A3 (19, 18, 17) |
| Keypad columns | `colPins` | A2, A1, A0 (16, 15, 14) |

EEPROM constants (`UID_CHARS`, `UID_RECORD_SIZE`, `MAX_CARDS`) derive from `EEPROM.length()`; `MAX_CARDS` is 56 on an ATmega328P. Serial runs at 9600 baud.

## 6. Usage and expected behaviour

Power the board; the LCD shows:

```
RFID ACCESS: 1
PIN  ACCESS: 2
```

**RFID path (key 1).** "Avvicinare RFID" appears; present a card within 5 s. Unknown card: "RFID / NON ABILITATO". Registered card: "RFID / ABILITATO", then the health check.

**PIN path (key 2).** Enter the 4 digits (shown as `*`). Wrong PIN: "PIN ERRATO". Correct PIN: "ACCESSO / CONSENTITO", then "VUOI REGISTRARE RFID? 1 SI 2 NO". Choosing 1 enrols a card (messages: "OPERAZIONE COMPLETATA", "RFID GIA' ABILITATO", "MEMORIA PIENA" or "ERRORE DURANTE LA LETTURA"), then the health check runs.

**Health check.** If the user is farther than `PROXIMITY_CM`, the screen alternates "AVVICINARSI A 6 / CM DAL SENSORE" and the measured distance, indefinitely. Once close, "CONTROLLO IN CORSO" is shown for 3 s, then the temperature is read and printed on the LCD and on the serial port (e.g. `36.00`; value depends on the sensor). Below the threshold: "PREGO, PUO' ACCEDERE" and the servo opens for 5 s. Otherwise: "RILEVATA FEBBRE / VIETATO ENTRARE". Sensor failure: "ERRORE SENSORE TEMPERATURA" and no access.

Expected outputs above are derived from reading the code; they were not observed on hardware.

## 7. Repository layout

```
firmware/AccessControl/AccessControl.ino   the complete firmware (settings, EEPROM store, RFID, sensors, UI, flows)
docs/Arduino-Access-Control-report.pdf     original project report
docs/prototype.jpg                         photo of the prototype
.gitignore                                 build artefacts (hex/elf/bin), editor files
```

Main functions in the sketch: `isCardRegistered` / `storeCard` / `cardCount` (EEPROM whitelist), `readCardUid` (RFID polling), `waitForProximity`, `temperatureOk`, `openDoor`, `healthCheckAndOpen`, `rfidAccess`, `pinAccess`, `enrolCard`, `setup`, `loop`.

## 8. Testing and verification

There is no automated test suite. Suggested manual checks on hardware:

1. Menu appears at power-up; keys other than 1/2 are ignored.
2. PIN flow: wrong PIN is rejected, correct PIN proceeds.
3. Enrol a card via the PIN flow, then reboot and check that the card is accepted (EEPROM persistence).
4. Hold your hand at more than 6 cm: the gate must keep prompting. Disconnect the DHT11: the gate must refuse access.
5. Watch the serial monitor at 9600 baud for the temperature values.

**Verified in this environment:** only a static code read. `arduino-cli` is not installed here, and the sketch was **not compiled** and **not run on hardware** (not verified in this environment). In particular, the `LiquidCrystal_SR3W` constructor call (`lcd(data, clock, strobe, 7, POSITIVE)`) depends on the installed NewLiquidCrystal version and was not checked against it. The author's own note: the refactored sketch was not compiled on hardware after the 2026 clean-up.

## 9. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| Compile error `LiquidCrystal_SR3W.h: No such file` | NewLiquidCrystal not installed; install it manually (see section 4) |
| Compile error `dht_nonblocking.h` or `SR04.h` not found | Install `DHT_nonblocking` and `SR04` libraries |
| LCD blank or garbage | Check the DS/SHCP/STCP wiring to the IC4094, contrast and backlight; constructor arguments depend on library version |
| Card always "ERRORE DI LETTURA" | Wiring on SPI/SS/RST, 3.3 V supply to MFRC522, card not presented within 5 s |
| Card "NON ABILITATO" | Not enrolled; enrol it through the PIN path. Note that a blank EEPROM is treated as empty |
| Always "AVVICINARSI..." | Distance must be 1-6 cm; a reading of 0 is ignored; check trig/echo wiring |
| "ERRORE SENSORE TEMPERATURA" | DHT11 data pin/pull-up; the sensor needs a few seconds after power-up |
| Keys register wrongly | Keypad row/column pins swapped relative to `rowPins`/`colPins` |
| Servo jitters or board resets | Power the servo from a separate 5 V supply with common ground (general advice, not verified here) |

## 10. Limitations and security notes

- RFID UIDs are not secret and can be cloned; only the first 4 UID bytes are stored (collisions are possible with 7- and 10-byte UIDs). RFID here is a convenience, not strong authentication.
- The PIN is compiled into the firmware in plain text and is anyone-with-the-binary readable. Enrolling cards needs only the PIN. There is no lockout after repeated wrong PINs.
- The DHT11 measures ambient temperature and humidity, not body temperature; it only demonstrates the control flow. Use an MLX90614 contactless sensor for any real measurement. Nothing here is a medical device.
- Capacity: 56 cards on an ATmega328P; there is no card deletion function (clear the EEPROM or set byte 0 to 0 to reset).
- LCD text is Italian only, and the "6 CM" text is fixed.
- The code is blocking (`delay`, busy loops): the gate cannot do anything else while waiting.
- Hardware behaviour and the sketch's compilation are not verified in this environment.

## 11. Extending and customising

- Different threshold, distance, PIN, timings: edit the constants in section 5.
- Another language: edit the `F("...")` strings in the UI functions.
- Better temperature sensor: replace the body of `temperatureOk()` (keep it fail-closed: return `false` on read failure).
- Different pins: edit the pin constants; for the keypad, use Arduino pin numbers in `rowPins`/`colPins`.
- Longer UIDs or a larger whitelist: change `UID_CHARS`/`UID_RECORD_SIZE` (the number of bytes taken in `readCardUid` follows `UID_CHARS / 2`) or move storage to an external EEPROM/SD.
- Card removal: add a function that rewrites the records and decrements byte 0.

## 12. Changes

Documentation-only update. No firmware changes were made in this revision: a static review did not reveal a defect that could be confirmed without compiling or running on hardware.

## Author

Luca D'Agati
