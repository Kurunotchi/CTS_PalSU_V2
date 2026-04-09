#include "time.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP_Google_Sheet_Client.h>
#include <INA226.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

// WiFi credentials
const char *ssid = "Salamat Shopee";
const char *pass = "wednesday";
WebServer server(80);
unsigned long previousStatusTime = 0;
const unsigned long statusInterval = 3000;

void connectToWiFi() {
  WiFi.begin(ssid, pass);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 8000) {
    delay(100);
  }
}

// Keypad setup
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {{'1', '2', '3', 'A'},
                         {'4', '5', '6', 'B'},
                         {'7', '8', '9', 'C'},
                         {'*', '0', '#', 'D'}};
byte rowPins[ROWS] = {15, 4, 25, 26};
byte colPins[COLS] = {27, 14, 12, 13};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// LCD display
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Relay pins (active LOW)
const int DMS = 23;
const int DCD = 19;
const int BMS = 18;
const int BCD = 5;
const int AMS = 17;
const int ACD = 16;
const int CMS = 32;
const int CCD = 33;

// INA226 addresses
#define ADDR_A 0x40
#define ADDR_B 0x41
#define ADDR_C 0x44
#define ADDR_D 0x45
INA226 inaA, inaB, inaC, inaD;

// Calibration values
float currentCalA = 100.3720, voltageCalA = 0.9991;
float voltageCalAdischarge = 1.0248, voltageCalAcharge = 0.9874;
float currentCalB = 97.8846, voltageCalB = 1.0005;
float voltageCalBdischarge = 1.0326, voltageCalBcharge = 0.9898;
float currentCalC = 98.8484, voltageCalC = 1.0005;
float voltageCalCdischarge = 1.0239, voltageCalCcharge = 0.9948;
float currentCalD = 98.6328, voltageCalD = 1.0008;
float voltageCalDdischarge = 1.0305, voltageCalDcharge = 0.9875;

// Sensor read timing
unsigned long prevA = 0, prevB = 0, prevC = 0, prevD = 0;
const unsigned long interval = 3000;

// Screen state
char previousSlot = '\0';
bool inSettingScreen = false;
bool inStopPrompt = false;
bool stopMessageActive = false;
unsigned long stopMessageStart = 0;
const unsigned long stopMessageDuration = 2000;

// Battery number input
bool inBattNumberInput = false;
String battNumberBuffer = "";
char battNumberSlot = '\0';

// Live readings
float voltageA = 0, currentA = 0;
float voltageB = 0, currentB = 0;
float voltageC = 0, currentC = 0;
float voltageD = 0, currentD = 0;
String statusA = "No Battery", statusB = "No Battery";
String statusC = "No Battery", statusD = "No Battery";

// Operation flags
bool Acharge = false, Adischarge = false;
bool Bcharge = false, Bdischarge = false;
bool Ccharge = false, Cdischarge = false;
bool Dcharge = false, Ddischarge = false;

// Cycle flags
bool Acycle = false, Bcycle = false, Ccycle = false, Dcycle = false;
int cycleTargetA = 0, cycleTargetB = 0, cycleTargetC = 0, cycleTargetD = 0;
int cycleCountA = 0, cycleCountB = 0, cycleCountC = 0, cycleCountD = 0;

// Cycle input state
bool inCycleInput = false;
String cycleBuffer = "";
char cycleSlot = '\0';
bool confirmingCycle = false;
int pendingCycleValue = 0;

// Battery numbers
int battNumA = 0, battNumB = 0, battNumC = 0, battNumD = 0;

// -------------------------------------------------------
// DISCHARGE WAIT (standalone discharge delay — 10 seconds)
// -------------------------------------------------------
bool AdischargePending = false, BdischargePending = false;
bool CdischargePending = false, DdischargePending = false;
bool AdischargeWait = false, BdischargeWait = false;
bool CdischargeWait = false, DdischargeWait = false;
unsigned long AdischargeStart = 0, BdischargeStart = 0;
unsigned long CdischargeStart = 0, DdischargeStart = 0;

// -------------------------------------------------------
// CYCLE TRANSITION PAUSE — charge → pause → discharge
// -------------------------------------------------------
bool AcyclePauseWait = false, BcyclePauseWait = false;
bool CcyclePauseWait = false, DcyclePauseWait = false;
unsigned long AcyclePauseStart = 0, BcyclePauseStart = 0;
unsigned long CcyclePauseStart = 0, DcyclePauseStart = 0;

// -------------------------------------------------------
// CYCLE RECHARGE PAUSE — discharge → pause → charge
// (NEW: mirrors the charge-to-discharge pause)
// -------------------------------------------------------
bool AcycleRechargePauseWait = false, BcycleRechargePauseWait = false;
bool CcycleRechargePauseWait = false, DcycleRechargePauseWait = false;
unsigned long AcycleRechargePauseStart = 0, BcycleRechargePauseStart = 0;
unsigned long CcycleRechargePauseStart = 0, DcycleRechargePauseStart = 0;

const unsigned long CYCLE_PAUSE_MS = 2000;

// Elapsed time
unsigned long elapsedA = 0, elapsedB = 0, elapsedC = 0, elapsedD = 0;

// Voltage limits
const float CHARGE_STOP_V = 4.10;
const float DISCHARGE_STOP_V = 2.80;

// ============ SIMPSON'S RULE VARIABLES ============
float capacityA = 0, capacityB = 0, capacityC = 0, capacityD = 0;
float prevCurrentA = 0, prevCurrentB = 0, prevCurrentC = 0, prevCurrentD = 0;
float prevPrevCurrentA = 0, prevPrevCurrentB = 0, prevPrevCurrentC = 0,
      prevPrevCurrentD = 0;
unsigned long prevTimeA = 0, prevTimeB = 0, prevTimeC = 0, prevTimeD = 0;
unsigned long prevPrevTimeA = 0, prevPrevTimeB = 0, prevPrevTimeC = 0,
              prevPrevTimeD = 0;
int sampleCountA = 0, sampleCountB = 0, sampleCountC = 0, sampleCountD = 0;
bool capacityTrackingA = false, capacityTrackingB = false;
bool capacityTrackingC = false, capacityTrackingD = false;
bool cycleCompleteA = false, cycleCompleteB = false;
bool cycleCompleteC = false, cycleCompleteD = false;
// ==================================================

Preferences prefs;

// Google Sheets
#define PROJECT_ID "potent-terminal-478407-m6"
#define CLIENT_EMAIL                                                           \
  "randolereviewcenter@potent-terminal-478407-m6.iam.gserviceaccount.com"
const char PRIVATE_KEY[] PROGMEM =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQChtT4Z809VN3Lo\neWX4X5r1"
    "Sg4O12RNF91b7EPMmNt5HlbkggmDkb/"
    "NTEGV6e+4hZ5Id+IvSAyfM+MA\ndZCkdcZBCcdMaxnhtUS2jXW/"
    "kLnGAgnQiGDhBiIvBUcCLm+yeTlZCrJQ/"
    "J6VgIMQ\nbv4XxIMry4GVbeWfVTWUEdicjkWCIZRfbk1nJsw4YNXAeaWdz5aHTWBRwiZihe67"
    "\ngUGqM/"
    "VY95skn8YLwsHQpXaAm2HDzWW4UVHlDdKqlxlkkwjvjrm6Z7Evzui0CQn2\namXJge5FfyYfbM"
    "8tl2C8F/ISw/"
    "2n4StN4q5pdbugig1RB6KU16FILYAI6QtnxVS+\nDo5TZpJtAgMBAAECggEAMod+UMCRHRk2/"
    "EKW5O4G7zfFPcj7TAW1gzhIFUH8bpPW\n5g9mJqkf7Gg0JEKVyCxkgdOIJ2sVmpetiqKx4Fn26"
    "bLDBnN/AmLQhlScow/"
    "3pNJV\nO8apsxbmDphREHLvLy8nBtZLUvglG6UtDzEHj+"
    "i1bjVomAdflZKcK9kJvR3NxXP2\nThlcPelI4N8OHdz1T+"
    "LRnotyeYsOZzrNRVSqrUHl3CzmCJc1NLNLeTgP5HeHO9C+"
    "\nV6YOnUHtGcN1EEkhqLZMV74171fZNJxxBR4tnehnfe35ArIWJL01OADLhSHpbrto\novsqHc"
    "0ta2COV/"
    "n4LLBC41JpoEvwO2ZKcIKHMoUc9wKBgQDYzgUCa7fbdR5CfsdX\nuJ2+2S3jdGTZMzC+"
    "jQKsbTAJCVHVvkpYVA6At7FyI2hhRikZxUeC+"
    "JJAGGciwvTF\nWgI8q0BBUHZ3qEfvZ9CpMCWFHTZsERXH17KWf3Ki6sqLB5vTBkfal/"
    "Nq7oVuHRbj\nn18bAI82J4ivoo89BsDw+96o3wKBgQC+8UdcdH3s6rBTwMRyeZQ0tRqbHFzIi+"
    "5E\npnQ7a5L1BGBVB3YTx+"
    "lbIv9Knmx6htSKv8LEwBXPhFK31hA5GQ19s9WNxIihyzKf\nd013N8jp6aFD6GLOeJXqfizk0w"
    "NFzV6yxnQ9+5vDqmYoUzUhW5w+tGj3clx4Z93e\nk6tzPPzSMwKBgQCB+"
    "soAEIqS9N1malGi4tkYAWbElhScL1eK9kljDLcew8qfRc2W\ntRZYz0iAMIA0yXZ8r8zW1aYA7"
    "WBv88gBxZvPua/"
    "1OIM969Ls0iXEOUxVSRVGptuT\nC1tTZSdaSz+"
    "RKMegNYTAphbWxheS07fUUckYDDbP9dW5ztDnenQURjzQqwKBgQCx\njg/"
    "7y1+lxX7+As0qXiAQ+y+"
    "oeTFWU7jXIaoH7zqSmOUzbGLCdi1rUBnxO2xIa8SM\n2VC2QKCHfdalmGsxjThcYbP9xnn/"
    "acLDQt9IMxmjWltZmGj48m0FxxrcFdR/PkAH\nIj/Ju4TW6EdizC0lvdiG/qB1KWUPmhZY+Rx/"
    "ZoD6vQKBgBGTzXEWzB+"
    "mT643DkaX\nt83ZRDXsTPWg3Sk21MUkQjKSGQpzAijPc5Sjk9yaQfyjdIsojLDeEGvomTb49uH"
    "3\n8uOhCMv3mbUQ+PY11pgaNMuXVzk2a+6o//"
    "F1CcaAgezn83nA7PTvlff1B2s5YfuR\ngzk4lX5lyewQ+3KvIgK3QUhA\n"
    "-----END PRIVATE KEY-----\n";

#define SHEET_ID_A "1liVQR26X5vO0VPv9nVeQRkEXtCxbicngRhdJrHw05lc"
#define SHEET_ID_B "1aGVDJZaUeVPIGU43q_Mu6sZrNcv4g4YG6oHHVGlfAyE"
#define SHEET_ID_C "1UNclOw0Z7xe073mdUaDNap8xIiTkwQHakoat5yu541k"
#define SHEET_ID_D "1Kpbmw2CI7hMAssOOEdyrMFwYx_6z55q2dYv9i1Dj8w8"

unsigned long lastRead = 0;
const unsigned long readInterval = 3000;
unsigned long lastLogA = 0, lastLogB = 0, lastLogC = 0, lastLogD = 0;
const unsigned long logInterval = 60000;

bool wasConnected = false;
bool gsheetReadyOnce = false;

// ---- Forward declarations ----
void resetSlotBooleans(char slot);
void setupSensor(INA226 &sensor, const char *label, uint8_t address);
void readSensor(INA226 &sensor, char label);
void checkVoltageLimit(char slot);
void updateSlotRelays(char slot, char action);
void resetSlotRelays(char slot);
void showHome();
void showSlot(char slot);
void refreshSlotDisplay();
void showSlotSetting(char slot);
void showStopPrompt(char slot);
void showForceStopPrompt(char slot);
void showBattNumberInput(char slot);
void renderBattNumberBuffer();
void handleBattNumberInputKey(char key);
void showCycleInput(char slot);
void renderCycleBuffer();
void handleCycleInputKey(char key);
void showCycleConfirmation(char slot, int cycles);
void processDeferredDischarge();
void saveBattNum(char slot, int num);
void clearBattNum(char slot);
int battNumForSlot(char slot);
void startDischargeWithDelay(char slot);
void startCycle(char slot);
void formatDateTime(const char *fmt, char *result, size_t size);
void logSlotToSheet(const char *sheetId, const String &slotStatus, int battNum,
                    unsigned long elapsedSecs, float voltage, float current,
                    float capacity);
void logSeparatorRow(const char *sheetId);
void logSimpsonOutputRow(const char *sheetId, float voltage, float current,
                         float capacity);
void stopOperation(char slot, bool fromForceStop = false);
void calculateSimpsonCapacity(char slot, float current,
                              unsigned long currentTime);
void resetCapacity(char slot);
void resetSimpsonTracking(char slot);
void logPhaseStart(char slot, const String &statusText);
void logModeStartHeader(char slot, const String &headerText);

void logModeStartHeader(char slot, const String &headerText) {
  int bn = battNumForSlot(slot);
  if (bn <= 0 || !GSheet.ready())
    return;
  float v = 0;
  char sheetId[50];
  switch (slot) {
  case 'A':
    strcpy(sheetId, SHEET_ID_A);
    v = voltageA;
    break;
  case 'B':
    strcpy(sheetId, SHEET_ID_B);
    v = voltageB;
    break;
  case 'C':
    strcpy(sheetId, SHEET_ID_C);
    v = voltageC;
    break;
  case 'D':
    strcpy(sheetId, SHEET_ID_D);
    v = voltageD;
    break;
  }
  logSlotToSheet(sheetId, headerText, bn, 0, v, 0, 0);
  delay(100);
  logSeparatorRow(sheetId);
  delay(100);
}

void logPhaseStart(char slot, const String &statusText) {
  int bn = battNumForSlot(slot);
  if (bn <= 0 || !GSheet.ready())
    return;

  char sheetId[50];
  float v = 0, cap = 0;
  INA226 *sensor = nullptr;
  switch (slot) {
  case 'A':
    strcpy(sheetId, SHEET_ID_A);
    v = voltageA;
    cap = capacityA;
    sensor = &inaA;
    break;
  case 'B':
    strcpy(sheetId, SHEET_ID_B);
    v = voltageB;
    cap = capacityB;
    sensor = &inaB;
    break;
  case 'C':
    strcpy(sheetId, SHEET_ID_C);
    v = voltageC;
    cap = capacityC;
    sensor = &inaC;
    break;
  case 'D':
    strcpy(sheetId, SHEET_ID_D);
    v = voltageD;
    cap = capacityD;
    sensor = &inaD;
    break;
  }

  // 1. Log with 0 current to mark the start
  logSlotToSheet(sheetId, statusText, bn, 0, v, 0, cap);

  // 2. Wait for the relays to actually affect the circuit and the INA226 to
  // read it
  delay(200);

  float v_read = 0;
  float c_read = 0;
  if (sensor != nullptr) {
    if (slot == 'A') {
      if (Acharge)
        v_read = sensor->readBusVoltage() * voltageCalAcharge;
      else if (Adischarge)
        v_read = sensor->readBusVoltage() * voltageCalAdischarge;
      else
        v_read = sensor->readBusVoltage() * voltageCalA;
      c_read = sensor->readShuntCurrent() * currentCalA;
    } else if (slot == 'B') {
      if (Bcharge)
        v_read = sensor->readBusVoltage() * voltageCalBcharge;
      else if (Bdischarge)
        v_read = sensor->readBusVoltage() * voltageCalBdischarge;
      else
        v_read = sensor->readBusVoltage() * voltageCalB;
      c_read = sensor->readShuntCurrent() * currentCalB;
    } else if (slot == 'C') {
      if (Ccharge)
        v_read = sensor->readBusVoltage() * voltageCalCcharge;
      else if (Cdischarge)
        v_read = sensor->readBusVoltage() * voltageCalCdischarge;
      else
        v_read = sensor->readBusVoltage() * voltageCalC;
      c_read = sensor->readShuntCurrent() * currentCalC;
    } else if (slot == 'D') {
      if (Dcharge)
        v_read = sensor->readBusVoltage() * voltageCalDcharge;
      else if (Ddischarge)
        v_read = sensor->readBusVoltage() * voltageCalDdischarge;
      else
        v_read = sensor->readBusVoltage() * voltageCalD;
      c_read = sensor->readShuntCurrent() * currentCalD;
    }

    if (v_read < 2.0)
      v_read = 0.0;
    if (c_read > -5.0 && c_read < 5.0)
      c_read = 0.0;
    c_read = abs(c_read);

    // Update the global state so immediate UI shows actual current
    if (slot == 'A') {
      voltageA = v_read;
      currentA = c_read;
    } else if (slot == 'B') {
      voltageB = v_read;
      currentB = c_read;
    } else if (slot == 'C') {
      voltageC = v_read;
      currentC = c_read;
    } else if (slot == 'D') {
      voltageD = v_read;
      currentD = c_read;
    }
  }

  // 3. Update local vars and log actual current
  float c = 0;
  switch (slot) {
  case 'A':
    v = voltageA;
    c = currentA;
    cap = capacityA;
    break;
  case 'B':
    v = voltageB;
    c = currentB;
    cap = capacityB;
    break;
  case 'C':
    v = voltageC;
    c = currentC;
    cap = capacityC;
    break;
  case 'D':
    v = voltageD;
    c = currentD;
    cap = capacityD;
    break;
  }

  logSlotToSheet(sheetId, statusText, bn, 0, v, c, cap);
}

// ================= WEB SERVER =================
void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204);
}

void handleStatus() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  StaticJsonDocument<1024> doc;

  auto addSlotData = [&](char slot, int bn, unsigned long elapsed, float v,
                         float c, float cap, bool charge, bool discharge,
                         bool cycle, int cycleCnt, int cycleTgt) {
    JsonObject obj = doc.createNestedObject();
    obj["slot"] = String(slot);
    obj["battery_num"] = bn;
    obj["elapsed"] = elapsed;
    obj["voltage"] = v;
    obj["current"] = c;
    obj["capacity"] = cap;

    String mode;
    bool isComplete = false;
    float simpsonCap = 0.0f;
    switch (slot) {
    case 'A':
      isComplete = cycleCompleteA;
      simpsonCap = capacityA;
      break;
    case 'B':
      isComplete = cycleCompleteB;
      simpsonCap = capacityB;
      break;
    case 'C':
      isComplete = cycleCompleteC;
      simpsonCap = capacityC;
      break;
    case 'D':
      isComplete = cycleCompleteD;
      simpsonCap = capacityD;
      break;
    }
    if (isComplete && simpsonCap > 0) {
      mode =
          String("Simpson Capacity ") + String(simpsonCap, 2) + String("mAh");
    } else if (cycle) {
      bool pauseActive = false;
      bool rechargePauseActive = false;
      switch (slot) {
      case 'A':
        pauseActive = AcyclePauseWait;
        rechargePauseActive = AcycleRechargePauseWait;
        break;
      case 'B':
        pauseActive = BcyclePauseWait;
        rechargePauseActive = BcycleRechargePauseWait;
        break;
      case 'C':
        pauseActive = CcyclePauseWait;
        rechargePauseActive = CcycleRechargePauseWait;
        break;
      case 'D':
        pauseActive = DcyclePauseWait;
        rechargePauseActive = DcycleRechargePauseWait;
        break;
      }
      if (pauseActive || rechargePauseActive)
        mode = "CYCLE - PAUSING";
      else if (charge)
        mode = "CYCLE - CHARGING";
      else if (discharge)
        mode = "CYCLE - DISCHARGING";
      else
        mode = "CYCLE - IDLE";
    } else if (charge)
      mode = "CHARGING";
    else if (discharge)
      mode = "DISCHARGING";
    else
      mode = "IDLE";

    obj["mode"] = mode;
    obj["cycle_current"] = cycleCnt;
    obj["cycle_target"] = cycleTgt;
  };

  addSlotData('A', battNumA, elapsedA, voltageA, currentA, capacityA, Acharge,
              Adischarge, Acycle, cycleCountA, cycleTargetA);
  addSlotData('B', battNumB, elapsedB, voltageB, currentB, capacityB, Bcharge,
              Bdischarge, Bcycle, cycleCountB, cycleTargetB);
  addSlotData('C', battNumC, elapsedC, voltageC, currentC, capacityC, Ccharge,
              Cdischarge, Ccycle, cycleCountC, cycleTargetC);
  addSlotData('D', battNumD, elapsedD, voltageD, currentD, capacityD, Dcharge,
              Ddischarge, Dcycle, cycleCountD, cycleTargetD);

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleCommand() {
  server.sendHeader("Access-Control-Allow-Origin", "*");

  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Body not received");
    return;
  }

  String body = server.arg("plain");
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    server.send(400, "text/plain", "Failed to parse JSON");
    return;
  }

  const char *cmd = doc["command"];
  const char *slot = doc["slot"];

  if (cmd && slot) {
    char cmdChar = cmd[0];
    char slotChar = slot[0];
    previousSlot = slotChar;

    switch (cmdChar) {
    case '1': // Charge
      if (battNumForSlot(slotChar) > 0) {
        resetCapacity(slotChar);
        resetSimpsonTracking(slotChar);
        updateSlotRelays(slotChar, '1');
        if (slotChar == 'A') {
          elapsedA = 0;
        } else if (slotChar == 'B') {
          elapsedB = 0;
        } else if (slotChar == 'C') {
          elapsedC = 0;
        } else if (slotChar == 'D') {
          elapsedD = 0;
        }
        logModeStartHeader(slotChar, "Charge - Start");
        logPhaseStart(slotChar, "CHARGING");
      }
      break;
    case '2': // Discharge
      if (battNumForSlot(slotChar) > 0) {
        resetCapacity(slotChar);
        startDischargeWithDelay(slotChar);
      }
      break;
    case '3': { // Cycle
      if (battNumForSlot(slotChar) > 0) {
        int cycles = doc["cycles"] | 3;
        switch (slotChar) {
        case 'A':
          cycleTargetA = cycles;
          cycleCountA = 0;
          resetCapacity('A');
          break;
        case 'B':
          cycleTargetB = cycles;
          cycleCountB = 0;
          resetCapacity('B');
          break;
        case 'C':
          cycleTargetC = cycles;
          cycleCountC = 0;
          resetCapacity('C');
          break;
        case 'D':
          cycleTargetD = cycles;
          cycleCountD = 0;
          resetCapacity('D');
          break;
        }
        startCycle(slotChar);
      }
      break;
    }
    case '4': // Set Battery Number
      if (doc["battery_number"] > 0)
        saveBattNum(slotChar, doc["battery_number"]);
      break;
    case '0': // Stop
      stopOperation(slotChar, true);
      break;
    }
  }
  server.send(200, "application/json", "{\"status\":\"success\"}");
}

void setupWebServer() {
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/command", HTTP_POST, handleCommand);
  server.on("/command", HTTP_OPTIONS, handleOptions);
  server.onNotFound([]() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(404, "text/plain", "Not Found");
  });
  server.begin();
  Serial.println("Web server started on port 80");
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial.println("System Starting...");

  prefs.begin("cts", false);
  prefs.putInt("A_num", 0);
  prefs.putInt("B_num", 0);
  prefs.putInt("C_num", 0);
  prefs.putInt("D_num", 0);
  battNumA = battNumB = battNumC = battNumD = 0;

  Wire.begin();
  lcd.init();
  lcd.backlight();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  connectToWiFi();

  battNumA = prefs.getInt("A_num", 0);
  battNumB = prefs.getInt("B_num", 0);
  battNumC = prefs.getInt("C_num", 0);
  battNumD = prefs.getInt("D_num", 0);

  Acycle = Bcycle = Ccycle = Dcycle = false;
  showHome();

  // All relays OFF
  pinMode(AMS, OUTPUT);
  digitalWrite(AMS, HIGH);
  pinMode(ACD, OUTPUT);
  digitalWrite(ACD, HIGH);
  pinMode(BMS, OUTPUT);
  digitalWrite(BMS, HIGH);
  pinMode(BCD, OUTPUT);
  digitalWrite(BCD, HIGH);
  pinMode(CMS, OUTPUT);
  digitalWrite(CMS, HIGH);
  pinMode(CCD, OUTPUT);
  digitalWrite(CCD, HIGH);
  pinMode(DMS, OUTPUT);
  digitalWrite(DMS, HIGH);
  pinMode(DCD, OUTPUT);
  digitalWrite(DCD, HIGH);

  setupSensor(inaA, "A", ADDR_A);
  setupSensor(inaB, "B", ADDR_B);
  setupSensor(inaC, "C", ADDR_C);
  setupSensor(inaD, "D", ADDR_D);

  Serial.print("Loaded batt numbers -- A:");
  Serial.print(battNumA);
  Serial.print(" B:");
  Serial.print(battNumB);
  Serial.print(" C:");
  Serial.print(battNumC);
  Serial.print(" D:");
  Serial.println(battNumD);

  configTime(0, 3600, "pool.ntp.org");
  GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);
  unsigned long start = millis();
  while (!GSheet.ready() && millis() - start < 10000) {
    Serial.print(".");
    delay(500);
  }
  if (GSheet.ready()) {
    Serial.println("\nGSheet Ready!");
    gsheetReadyOnce = true;
  } else {
    Serial.println("\nGSheet timeout. Will retry.");
    gsheetReadyOnce = false;
  }

  setupWebServer();
  Serial.println("Setup complete.");
}

// ============ SIMPSON'S RULE ============
void resetSimpsonTracking(char slot) {
  switch (slot) {
  case 'A':
    sampleCountA = 0;
    capacityTrackingA = false;
    prevCurrentA = 0;
    prevPrevCurrentA = 0;
    prevTimeA = 0;
    prevPrevTimeA = 0;
    break;
  case 'B':
    sampleCountB = 0;
    capacityTrackingB = false;
    prevCurrentB = 0;
    prevPrevCurrentB = 0;
    prevTimeB = 0;
    prevPrevTimeB = 0;
    break;
  case 'C':
    sampleCountC = 0;
    capacityTrackingC = false;
    prevCurrentC = 0;
    prevPrevCurrentC = 0;
    prevTimeC = 0;
    prevPrevTimeC = 0;
    break;
  case 'D':
    sampleCountD = 0;
    capacityTrackingD = false;
    prevCurrentD = 0;
    prevPrevCurrentD = 0;
    prevTimeD = 0;
    prevPrevTimeD = 0;
    break;
  }
}

void calculateSimpsonCapacity(char slot, float current,
                              unsigned long currentTime) {
  float *capacity, *prevCurrent, *prevPrevCurrent;
  unsigned long *prevTime, *prevPrevTime;
  int *sampleCount;
  bool *tracking;

  switch (slot) {
  case 'A':
    capacity = &capacityA;
    prevCurrent = &prevCurrentA;
    prevPrevCurrent = &prevPrevCurrentA;
    prevTime = &prevTimeA;
    prevPrevTime = &prevPrevTimeA;
    sampleCount = &sampleCountA;
    tracking = &capacityTrackingA;
    break;
  case 'B':
    capacity = &capacityB;
    prevCurrent = &prevCurrentB;
    prevPrevCurrent = &prevPrevCurrentB;
    prevTime = &prevTimeB;
    prevPrevTime = &prevPrevTimeB;
    sampleCount = &sampleCountB;
    tracking = &capacityTrackingB;
    break;
  case 'C':
    capacity = &capacityC;
    prevCurrent = &prevCurrentC;
    prevPrevCurrent = &prevPrevCurrentC;
    prevTime = &prevTimeC;
    prevPrevTime = &prevPrevTimeC;
    sampleCount = &sampleCountC;
    tracking = &capacityTrackingC;
    break;
  case 'D':
    capacity = &capacityD;
    prevCurrent = &prevCurrentD;
    prevPrevCurrent = &prevPrevCurrentD;
    prevTime = &prevTimeD;
    prevPrevTime = &prevPrevTimeD;
    sampleCount = &sampleCountD;
    tracking = &capacityTrackingD;
    break;
  default:
    return;
  }

  bool shouldTrack = false;
  switch (slot) {
  case 'A':
    shouldTrack = (Acharge || Adischarge);
    break;
  case 'B':
    shouldTrack = (Bcharge || Bdischarge);
    break;
  case 'C':
    shouldTrack = (Ccharge || Cdischarge);
    break;
  case 'D':
    shouldTrack = (Dcharge || Ddischarge);
    break;
  }

  if (shouldTrack && !*tracking) {
    *tracking = true;
    *sampleCount = 0;
    *capacity = 0;
    *prevTime = 0;
    *prevPrevTime = 0;
    Serial.print("Slot ");
    Serial.print(slot);
    Serial.println(" capacity tracking started");
  }
  if (!shouldTrack && *tracking) {
    *tracking = false;
    Serial.print("Slot ");
    Serial.print(slot);
    Serial.print(" capacity ended: ");
    Serial.print(*capacity, 4);
    Serial.println(" Ah");
  }
  if (!shouldTrack)
    return;

  if (*sampleCount == 0) {
    *prevPrevTime = currentTime;
    *prevPrevCurrent = current;
    *sampleCount = 1;
    return;
  }
  if (*sampleCount == 1) {
    *prevTime = currentTime;
    *prevCurrent = current;
    *sampleCount = 2;
    return;
  }

  float dt_seconds = (currentTime - *prevPrevTime) / 1000.0;
  float segment_mAs =
      (dt_seconds / 6.0) * (*prevPrevCurrent + 4.0 * (*prevCurrent) + current);
  *capacity += segment_mAs / 3600.0;

  *prevPrevTime = currentTime;
  *prevPrevCurrent = current;
  *sampleCount = 1;
}

void resetCapacity(char slot) {
  switch (slot) {
  case 'A':
    capacityA = 0;
    sampleCountA = 0;
    capacityTrackingA = false;
    prevCurrentA = 0;
    prevPrevCurrentA = 0;
    prevTimeA = 0;
    prevPrevTimeA = 0;
    break;
  case 'B':
    capacityB = 0;
    sampleCountB = 0;
    capacityTrackingB = false;
    prevCurrentB = 0;
    prevPrevCurrentB = 0;
    prevTimeB = 0;
    prevPrevTimeB = 0;
    break;
  case 'C':
    capacityC = 0;
    sampleCountC = 0;
    capacityTrackingC = false;
    prevCurrentC = 0;
    prevPrevCurrentC = 0;
    prevTimeC = 0;
    prevPrevTimeC = 0;
    break;
  case 'D':
    capacityD = 0;
    sampleCountD = 0;
    capacityTrackingD = false;
    prevCurrentD = 0;
    prevPrevCurrentD = 0;
    prevTimeD = 0;
    prevPrevTimeD = 0;
    break;
  }
}

void resetSlotBooleans(char slot) {
  switch (slot) {
  case 'A':
    Acharge = false;
    Adischarge = false;
    Acycle = false;
    AcyclePauseWait = false;
    AcycleRechargePauseWait = false;
    resetCapacity('A');
    break;
  case 'B':
    Bcharge = false;
    Bdischarge = false;
    Bcycle = false;
    BcyclePauseWait = false;
    BcycleRechargePauseWait = false;
    resetCapacity('B');
    break;
  case 'C':
    Ccharge = false;
    Cdischarge = false;
    Ccycle = false;
    CcyclePauseWait = false;
    CcycleRechargePauseWait = false;
    resetCapacity('C');
    break;
  case 'D':
    Dcharge = false;
    Ddischarge = false;
    Dcycle = false;
    DcyclePauseWait = false;
    DcycleRechargePauseWait = false;
    resetCapacity('D');
    break;
  }
}

// ===================== LOOP =====================
void loop() {
  server.handleClient();
  char key = keypad.getKey();
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED)
    connectToWiFi();

  if (now - previousStatusTime >= statusInterval) {
    previousStatusTime = now;
    Serial.print("Wi-Fi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "Connected"
                                                 : "Not Connected");
    if (previousSlot == '\0' && !inSettingScreen && !inStopPrompt &&
        !stopMessageActive && !inBattNumberInput && !inCycleInput &&
        !confirmingCycle) {
      lcd.setCursor(0, 3);
      if (WiFi.status() == WL_CONNECTED)
        lcd.print("  Wi-Fi: Connected  ");
      else
        lcd.print("Wi-Fi: Not Connected");
    }
  }

  if (now - prevA >= interval) {
    prevA = now;
    readSensor(inaA, 'A');
  }
  if (now - prevB >= interval) {
    prevB = now;
    readSensor(inaB, 'B');
  }
  if (now - prevC >= interval) {
    prevC = now;
    readSensor(inaC, 'C');
  }
  if (now - prevD >= interval) {
    prevD = now;
    readSensor(inaD, 'D');
  }

  if (stopMessageActive && millis() - stopMessageStart >= stopMessageDuration) {
    stopMessageActive = false;
    switch (previousSlot) {
    case 'A':
      readSensor(inaA, 'A');
      break;
    case 'B':
      readSensor(inaB, 'B');
      break;
    case 'C':
      readSensor(inaC, 'C');
      break;
    case 'D':
      readSensor(inaD, 'D');
      break;
    }
    showSlot(previousSlot);
  }

  processDeferredDischarge();

  if (WiFi.status() != WL_CONNECTED) {
    if (wasConnected)
      Serial.println("WiFi LOST!");
    wasConnected = false;
  } else {
    if (!wasConnected)
      Serial.println("WiFi Connected!");
    wasConnected = true;
  }

  if (!gsheetReadyOnce && (now % 30000UL) < 200) {
    if (GSheet.ready()) {
      Serial.println("GSheet Ready!");
      gsheetReadyOnce = true;
    }
  }

  // --- 60-second interval logging ---
  bool Aactive = ((Acharge || Adischarge) && battNumA > 0);
  static bool lastAactive = false;
  if (Aactive && !lastAactive) {
    lastLogA = now;
    elapsedA = 0;
  }
  lastAactive = Aactive;
  if (Aactive && (now - lastLogA >= logInterval)) {
    lastLogA = now;
    elapsedA += 60;
    if (voltageA <= 2.0)
      elapsedA = 0;
    if (GSheet.ready()) {
      String statusText =
          Acycle ? (Acharge ? "CYCLE - CHARGING" : "CYCLE - DISCHARGING")
                 : (Acharge ? "CHARGING" : "DISCHARGING");
      logSlotToSheet(SHEET_ID_A, statusText, battNumA, elapsedA, voltageA,
                     currentA, capacityA);
    }
  }

  bool Bactive = ((Bcharge || Bdischarge) && battNumB > 0);
  static bool lastBactive = false;
  if (Bactive && !lastBactive) {
    lastLogB = now;
    elapsedB = 0;
  }
  lastBactive = Bactive;
  if (Bactive && (now - lastLogB >= logInterval)) {
    lastLogB = now;
    elapsedB += 60;
    if (voltageB <= 2.0)
      elapsedB = 0;
    if (GSheet.ready()) {
      String statusText =
          Bcycle ? (Bcharge ? "CYCLE - CHARGING" : "CYCLE - DISCHARGING")
                 : (Bcharge ? "CHARGING" : "DISCHARGING");
      logSlotToSheet(SHEET_ID_B, statusText, battNumB, elapsedB, voltageB,
                     currentB, capacityB);
    }
  }

  bool Cactive = ((Ccharge || Cdischarge) && battNumC > 0);
  static bool lastCactive = false;
  if (Cactive && !lastCactive) {
    lastLogC = now;
    elapsedC = 0;
  }
  lastCactive = Cactive;
  if (Cactive && (now - lastLogC >= logInterval)) {
    lastLogC = now;
    elapsedC += 60;
    if (voltageC <= 2.0)
      elapsedC = 0;
    if (GSheet.ready()) {
      String statusText =
          Ccycle ? (Ccharge ? "CYCLE - CHARGING" : "CYCLE - DISCHARGING")
                 : (Ccharge ? "CHARGING" : "DISCHARGING");
      logSlotToSheet(SHEET_ID_C, statusText, battNumC, elapsedC, voltageC,
                     currentC, capacityC);
    }
  }

  bool Dactive = ((Dcharge || Ddischarge) && battNumD > 0);
  static bool lastDactive = false;
  if (Dactive && !lastDactive) {
    lastLogD = now;
    elapsedD = 0;
  }
  lastDactive = Dactive;
  if (Dactive && (now - lastLogD >= logInterval)) {
    lastLogD = now;
    elapsedD += 60;
    if (voltageD <= 2.0)
      elapsedD = 0;
    if (GSheet.ready()) {
      String statusText =
          Dcycle ? (Dcharge ? "CYCLE - CHARGING" : "CYCLE - DISCHARGING")
                 : (Dcharge ? "CHARGING" : "DISCHARGING");
      logSlotToSheet(SHEET_ID_D, statusText, battNumD, elapsedD, voltageD,
                     currentD, capacityD);
    }
  }

  if (!key)
    return;
  Serial.print("Key: ");
  Serial.println(key);

  if (inBattNumberInput) {
    handleBattNumberInputKey(key);
    return;
  }
  if (inCycleInput) {
    handleCycleInputKey(key);
    return;
  }

  if (confirmingCycle) {
    if (key == '#') {
      int value = pendingCycleValue;
      switch (cycleSlot) {
      case 'A':
        cycleTargetA = value;
        cycleCountA = 0;
        resetCapacity('A');
        break;
      case 'B':
        cycleTargetB = value;
        cycleCountB = 0;
        resetCapacity('B');
        break;
      case 'C':
        cycleTargetC = value;
        cycleCountC = 0;
        resetCapacity('C');
        break;
      case 'D':
        cycleTargetD = value;
        cycleCountD = 0;
        resetCapacity('D');
        break;
      }
      lcd.clear();
      lcd.setCursor(0, 1);
      lcd.print("Starting Cycle Test");
      lcd.setCursor(0, 2);
      lcd.print("Slot ");
      lcd.print(cycleSlot);
      lcd.print(" - ");
      lcd.print(value);
      lcd.print(" cycles");
      lcd.setCursor(0, 3);
      lcd.print("Ends on CHARGE");
      delay(2000);
      startCycle(cycleSlot);

      confirmingCycle = false;
      cycleSlot = '\0';
      cycleBuffer = "";
      pendingCycleValue = 0;
      if (previousSlot != '\0')
        showSlot(previousSlot);
      return;
    } else if (key == '*') {
      lcd.clear();
      lcd.setCursor(0, 1);
      lcd.print("Cycle test CANCELLED");
      delay(1500);
      confirmingCycle = false;
      cycleSlot = '\0';
      cycleBuffer = "";
      pendingCycleValue = 0;
      showSlotSetting(previousSlot);
      inSettingScreen = true;
      return;
    }
    return;
  }

  if (inStopPrompt) {
    if (key == '0') {
      stopOperation(previousSlot, true);
      return;
    } else if (key == '5') {
      inStopPrompt = false;
      stopMessageActive = false;
      refreshSlotDisplay();
      return;
    }
    return;
  }

  if (inSettingScreen) {
    if (key == '1') {
      if (battNumForSlot(previousSlot) > 0) {
        resetCapacity(previousSlot);
        resetSimpsonTracking(previousSlot);
        updateSlotRelays(previousSlot, '1');
        if (previousSlot == 'A') {
          elapsedA = 0;
        } else if (previousSlot == 'B') {
          elapsedB = 0;
        } else if (previousSlot == 'C') {
          elapsedC = 0;
        } else if (previousSlot == 'D') {
          elapsedD = 0;
        }
        logModeStartHeader(previousSlot, "Charge - Start");
        logPhaseStart(previousSlot, "CHARGING");
        inSettingScreen = false;
        refreshSlotDisplay();
      } else {
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print(" Set batt# first!");
        delay(1500);
        showSlotSetting(previousSlot);
      }
      return;
    } else if (key == '2') {
      if (battNumForSlot(previousSlot) > 0) {
        resetCapacity(previousSlot);
        startDischargeWithDelay(previousSlot);
        inSettingScreen = false;
      } else {
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print(" Set batt# first!");
        delay(1500);
        showSlotSetting(previousSlot);
      }
      return;
    } else if (key == '3') {
      if (battNumForSlot(previousSlot) > 0) {
        showCycleInput(previousSlot);
        inSettingScreen = false;
      } else {
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print(" Set batt# first!");
        delay(1500);
        showSlotSetting(previousSlot);
      }
      return;
    } else if (key == '4') {
      showBattNumberInput(previousSlot);
      inBattNumberInput = true;
      inSettingScreen = false;
      return;
    } else if (key == '5')
      return;
  }

  switch (key) {
  case '0':
    if (previousSlot != '\0') {
      bool isActive = false;
      switch (previousSlot) {
      case 'A':
        isActive = (Acharge || Adischarge || Acycle || AcyclePauseWait ||
                    AcycleRechargePauseWait);
        break;
      case 'B':
        isActive = (Bcharge || Bdischarge || Bcycle || BcyclePauseWait ||
                    BcycleRechargePauseWait);
        break;
      case 'C':
        isActive = (Ccharge || Cdischarge || Ccycle || CcyclePauseWait ||
                    CcycleRechargePauseWait);
        break;
      case 'D':
        isActive = (Dcharge || Ddischarge || Dcycle || DcyclePauseWait ||
                    DcycleRechargePauseWait);
        break;
      }
      if (isActive) {
        showForceStopPrompt(previousSlot);
        inStopPrompt = true;
      } else {
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print("No active operation");
        lcd.setCursor(0, 2);
        lcd.print("in Slot ");
        lcd.print(previousSlot);
        delay(1500);
        showSlot(previousSlot);
      }
    }
    break;
  case '#':
    showHome();
    previousSlot = '\0';
    inSettingScreen = false;
    inStopPrompt = false;
    inBattNumberInput = false;
    inCycleInput = false;
    confirmingCycle = false;
    break;
  case 'A':
    showSlot('A');
    previousSlot = 'A';
    break;
  case 'B':
    showSlot('B');
    previousSlot = 'B';
    break;
  case 'C':
    showSlot('C');
    previousSlot = 'C';
    break;
  case 'D':
    showSlot('D');
    previousSlot = 'D';
    break;
  case '*':
    if (previousSlot != '\0') {
      showSlotSetting(previousSlot);
      inSettingScreen = true;
    }
    break;
  }
}

// ===================== HELPERS =====================
int battNumForSlot(char slot) {
  switch (slot) {
  case 'A':
    return battNumA;
  case 'B':
    return battNumB;
  case 'C':
    return battNumC;
  case 'D':
    return battNumD;
  default:
    return 0;
  }
}

void startDischargeWithDelay(char slot) {
  switch (slot) {
  case 'A':
    Acycle = false;
    Acharge = false;
    elapsedA = 0;
    AdischargePending = true;
    AdischargeWait = true;
    AdischargeStart = millis();
    break;
  case 'B':
    Bcycle = false;
    Bcharge = false;
    elapsedB = 0;
    BdischargePending = true;
    BdischargeWait = true;
    BdischargeStart = millis();
    break;
  case 'C':
    Ccycle = false;
    Ccharge = false;
    elapsedC = 0;
    CdischargePending = true;
    CdischargeWait = true;
    CdischargeStart = millis();
    break;
  case 'D':
    Dcycle = false;
    Dcharge = false;
    elapsedD = 0;
    DdischargePending = true;
    DdischargeWait = true;
    DdischargeStart = millis();
    break;
  }
}

void startCycle(char slot) {
  switch (slot) {
  case 'A':
    Acycle = true;
    cycleCountA = 0;
    resetCapacity('A');
    resetSimpsonTracking('A');
    if (battNumA > 0 && GSheet.ready()) {
      // FIX: Log "Cycle - Start" then separator
      logSlotToSheet(SHEET_ID_A, "Cycle - Start", battNumA, 0, voltageA, 0, 0);
      delay(100);
      logSeparatorRow(SHEET_ID_A);
      delay(100);
    }
    Acharge = true;
    digitalWrite(AMS, LOW);
    digitalWrite(ACD, HIGH);
    elapsedA = 0;
    lastLogA = millis();
    logPhaseStart('A', "CYCLE - CHARGING");
    Serial.print("Slot A cycle started - Charging (");
    Serial.print(cycleTargetA);
    Serial.println(" cycles)");
    break;
  case 'B':
    Bcycle = true;
    cycleCountB = 0;
    resetCapacity('B');
    resetSimpsonTracking('B');
    if (battNumB > 0 && GSheet.ready()) {
      logSlotToSheet(SHEET_ID_B, "Cycle - Start", battNumB, 0, voltageB, 0, 0);
      delay(100);
      logSeparatorRow(SHEET_ID_B);
      delay(100);
    }
    Bcharge = true;
    digitalWrite(BMS, LOW);
    digitalWrite(BCD, HIGH);
    elapsedB = 0;
    lastLogB = millis();
    logPhaseStart('B', "CYCLE - CHARGING");
    Serial.print("Slot B cycle started - Charging (");
    Serial.print(cycleTargetB);
    Serial.println(" cycles)");
    break;
  case 'C':
    Ccycle = true;
    cycleCountC = 0;
    resetCapacity('C');
    resetSimpsonTracking('C');
    if (battNumC > 0 && GSheet.ready()) {
      logSlotToSheet(SHEET_ID_C, "Cycle - Start", battNumC, 0, voltageC, 0, 0);
      delay(100);
      logSeparatorRow(SHEET_ID_C);
      delay(100);
    }
    Ccharge = true;
    digitalWrite(CMS, LOW);
    digitalWrite(CCD, HIGH);
    elapsedC = 0;
    lastLogC = millis();
    logPhaseStart('C', "CYCLE - CHARGING");
    Serial.print("Slot C cycle started - Charging (");
    Serial.print(cycleTargetC);
    Serial.println(" cycles)");
    break;
  case 'D':
    Dcycle = true;
    cycleCountD = 0;
    resetCapacity('D');
    resetSimpsonTracking('D');
    if (battNumD > 0 && GSheet.ready()) {
      logSlotToSheet(SHEET_ID_D, "Cycle - Start", battNumD, 0, voltageD, 0, 0);
      delay(100);
      logSeparatorRow(SHEET_ID_D);
      delay(100);
    }
    Dcharge = true;
    digitalWrite(DMS, LOW);
    digitalWrite(DCD, HIGH);
    elapsedD = 0;
    lastLogD = millis();
    logPhaseStart('D', "CYCLE - CHARGING");
    Serial.print("Slot D cycle started - Charging (");
    Serial.print(cycleTargetD);
    Serial.println(" cycles)");
    break;
  }
}

void setupSensor(INA226 &sensor, const char *label, uint8_t address) {
  if (!sensor.begin(address)) {
    Serial.print("Sensor ");
    Serial.print(label);
    Serial.println(" not detected!");
    return;
  }
  sensor.configure(INA226_AVERAGES_1, INA226_BUS_CONV_TIME_1100US,
                   INA226_SHUNT_CONV_TIME_1100US, INA226_MODE_SHUNT_BUS_CONT);
  sensor.calibrate(0.01, 5.0);
  Serial.print("Sensor ");
  Serial.print(label);
  Serial.println(" initialized.");
}

void readSensor(INA226 &sensor, char label) {
  float v, c;
  switch (label) {
  case 'A':
    if (Acharge)
      v = sensor.readBusVoltage() * voltageCalAcharge;
    else if (Adischarge)
      v = sensor.readBusVoltage() * voltageCalAdischarge;
    else
      v = sensor.readBusVoltage() * voltageCalA;
    c = sensor.readShuntCurrent() * currentCalA;
    break;
  case 'B':
    if (Bcharge)
      v = sensor.readBusVoltage() * voltageCalBcharge;
    else if (Bdischarge)
      v = sensor.readBusVoltage() * voltageCalBdischarge;
    else
      v = sensor.readBusVoltage() * voltageCalB;
    c = sensor.readShuntCurrent() * currentCalB;
    break;
  case 'C':
    if (Ccharge)
      v = sensor.readBusVoltage() * voltageCalCcharge;
    else if (Cdischarge)
      v = sensor.readBusVoltage() * voltageCalCdischarge;
    else
      v = sensor.readBusVoltage() * voltageCalC;
    c = sensor.readShuntCurrent() * currentCalC;
    break;
  case 'D':
    if (Dcharge)
      v = sensor.readBusVoltage() * voltageCalDcharge;
    else if (Ddischarge)
      v = sensor.readBusVoltage() * voltageCalDdischarge;
    else
      v = sensor.readBusVoltage() * voltageCalD;
    c = sensor.readShuntCurrent() * currentCalD;
    break;
  default:
    v = 0;
    c = 0;
    break;
  }

  int bn = 0;
  if (label == 'A')
    bn = battNumA;
  else if (label == 'B')
    bn = battNumB;
  else if (label == 'C')
    bn = battNumC;
  else if (label == 'D')
    bn = battNumD;

  if (v < 2.0)
    v = 0.0;
  if (c > -5.0 && c < 5.0)
    c = 0.0;

  String status;
  if (v == 0.0 && c == 0.0)
    status = "No Battery";
  else if (v > 0.0 && c == 0.0) {
    switch (label) {
    case 'A':
      status = (cycleCompleteA && capacityA > 0)
                   ? String("Simpson Capacity ") + String(capacityA, 2) +
                         String("mAh")
                   : "Standby";
      break;
    case 'B':
      status = (cycleCompleteB && capacityB > 0)
                   ? String("Simpson Capacity ") + String(capacityB, 2) +
                         String("mAh")
                   : "Standby";
      break;
    case 'C':
      status = (cycleCompleteC && capacityC > 0)
                   ? String("Simpson Capacity ") + String(capacityC, 2) +
                         String("mAh")
                   : "Standby";
      break;
    case 'D':
      status = (cycleCompleteD && capacityD > 0)
                   ? String("Simpson Capacity ") + String(capacityD, 2) +
                         String("mAh")
                   : "Standby";
      break;
    default:
      status = "Standby";
      break;
    }
  } else if (v > 0.0 && c > 0.0)
    status = "Discharging";
  else if (v > 0.0 && c < 0.0)
    status = "Charging";
  else
    status = "Unknown";

  Serial.print("Slot ");
  Serial.print(label);
  Serial.print(" | ");
  Serial.print(status);
  Serial.print(" | BattNo=");
  Serial.print(bn);
  Serial.print(" | V=");
  Serial.print(v, 2);
  Serial.print(" | I=");
  Serial.print(c, 0);
  Serial.println("mA");

  switch (label) {
  case 'A':
    voltageA = v;
    currentA = abs(c);
    statusA = status;
    checkVoltageLimit('A');
    break;
  case 'B':
    voltageB = v;
    currentB = abs(c);
    statusB = status;
    checkVoltageLimit('B');
    break;
  case 'C':
    voltageC = v;
    currentC = abs(c);
    statusC = status;
    checkVoltageLimit('C');
    break;
  case 'D':
    voltageD = v;
    currentD = abs(c);
    statusD = status;
    checkVoltageLimit('D');
    break;
  }

  // Only calculate capacity during active charge or discharge (NOT during any
  // pause)
  bool isActivelyCharging = false;
  bool isActivelyDischarging = false;
  bool isInPause = false;

  switch (label) {
  case 'A':
    isActivelyCharging = Acharge;
    isActivelyDischarging = Adischarge;
    isInPause = AcyclePauseWait || AcycleRechargePauseWait;
    break;
  case 'B':
    isActivelyCharging = Bcharge;
    isActivelyDischarging = Bdischarge;
    isInPause = BcyclePauseWait || BcycleRechargePauseWait;
    break;
  case 'C':
    isActivelyCharging = Ccharge;
    isActivelyDischarging = Cdischarge;
    isInPause = CcyclePauseWait || CcycleRechargePauseWait;
    break;
  case 'D':
    isActivelyCharging = Dcharge;
    isActivelyDischarging = Ddischarge;
    isInPause = DcyclePauseWait || DcycleRechargePauseWait;
    break;
  }

  if ((isActivelyCharging || isActivelyDischarging) && !isInPause) {
    calculateSimpsonCapacity(label, abs(c), millis());
  } else if (isInPause) {
    resetSimpsonTracking(label);
  }

  if (previousSlot == label && !inSettingScreen && !inStopPrompt &&
      !stopMessageActive && !inBattNumberInput && !inCycleInput &&
      !confirmingCycle) {
    showSlot(label);
  }
}

// ===================== VOLTAGE LIMIT CHECK =====================
// ---------------------------------------------------------------
// SHEET LOGGING FLOW PER CYCLE PHASE:
//
//  CHARGE COMPLETE  →  Simpson output (charge phase)
//                   →  Separator
//                   →  "Cycle - Pause" (relays OFF)
//                   →  [2 s pause]
//                   →  processDeferredDischarge() logs "CYCLE - DISCHARGING"
//
//  DISCHARGE COMPLETE (mid-cycle)
//                   →  Simpson output (discharge phase)
//                   →  Separator
//                   →  "Cycle - Pause"  (relays OFF)
//                   →  [2 s pause]
//                   →  processDeferredDischarge() logs "CYCLE - CHARGING"
//
//  DISCHARGE COMPLETE (last cycle)
//                   →  Simpson output (final)
//                   →  Separator
//                   →  "Cycle Complete"
// ---------------------------------------------------------------
void checkVoltageLimit(char slot) {
  switch (slot) {

  // ============================================================ SLOT A
  case 'A': {
    // ---------- CHARGE COMPLETE ----------
    if (Acharge &&
        (voltageA > CHARGE_STOP_V || voltageA < 1 || currentA < 50)) {
      Acharge = false;
      Serial.println("Slot A charge complete.");
      if (Acycle) {
        if (cycleCountA >= cycleTargetA) {
          cycleCompleteA = true;
          if (battNumA > 0 && GSheet.ready()) {
            logSimpsonOutputRow(SHEET_ID_A, voltageA, currentA, capacityA);
            delay(100);
            logSeparatorRow(SHEET_ID_A);
            delay(100);
            logSlotToSheet(SHEET_ID_A, "Cycle Complete", battNumA, elapsedA,
                           voltageA, 0, capacityA);
          }
          Serial.print("Slot A CYCLE COMPLETE. Final: ");
          Serial.print(capacityA, 4);
          Serial.println(" Ah");
          Acycle = false;
          resetSlotRelays('A');
        } else {
          if (battNumA > 0 && GSheet.ready()) {
            logSimpsonOutputRow(SHEET_ID_A, voltageA, currentA, capacityA);
            delay(100);
            logSeparatorRow(SHEET_ID_A);
            delay(100);
            logSlotToSheet(SHEET_ID_A, "Cycle - Pause", battNumA, elapsedA,
                           voltageA, 0, capacityA);
            delay(100);
            logSeparatorRow(SHEET_ID_A);
          }
          digitalWrite(AMS, HIGH);
          digitalWrite(ACD, HIGH);
          AcyclePauseWait = true;
          AcyclePauseStart = millis();
          Serial.println("Slot A: charge→pause (2 s)");
        }
      } else {
        if (battNumA > 0 && GSheet.ready()) {
          delay(100);
          logSeparatorRow(SHEET_ID_A);
          delay(100);
          logSlotToSheet(SHEET_ID_A, "Charge Complete", battNumA, elapsedA,
                         voltageA, 0, capacityA);
        }
        resetSlotRelays('A');
      }
    }

    // ---------- DISCHARGE COMPLETE ----------
    if (Adischarge &&
        (voltageA <= DISCHARGE_STOP_V || (voltageA < 1 && currentA == 0.0))) {
      Adischarge = false;
      Serial.print("Slot A final capacity: ");
      Serial.print(capacityA, 3);
      Serial.println(" Ah");

      if (Acycle) {
        cycleCountA++;
        if (battNumA > 0 && GSheet.ready()) {
          logSimpsonOutputRow(SHEET_ID_A, voltageA, currentA, capacityA);
          delay(100);
          logSeparatorRow(SHEET_ID_A);
          delay(100);
          logSlotToSheet(SHEET_ID_A, "Cycle - Pause", battNumA, elapsedA,
                         voltageA, 0, capacityA);
          delay(100);
          logSeparatorRow(SHEET_ID_A);
        }
        digitalWrite(AMS, HIGH);
        digitalWrite(ACD, HIGH);
        resetCapacity('A');
        resetSimpsonTracking('A');
        AcycleRechargePauseWait = true;
        AcycleRechargePauseStart = millis();
        Serial.print("Slot A cycle ");
        Serial.print(cycleCountA);
        Serial.print("/");
        Serial.print(cycleTargetA);
        Serial.println(" - discharge→pause (2 s)");
      } else {
        if (battNumA > 0 && GSheet.ready()) {
          delay(100);
          logSeparatorRow(SHEET_ID_A);
          delay(100);
          logSlotToSheet(SHEET_ID_A, "Discharge Complete", battNumA, elapsedA,
                         voltageA, 0, capacityA);
        }
        resetSlotRelays('A');
      }
      Serial.println("Slot A discharge ended.");
    }
  } break;

  // ============================================================ SLOT B
  case 'B': {
    if (Bcharge &&
        (voltageB > CHARGE_STOP_V || voltageB < 1 || currentB < 50)) {
      Bcharge = false;
      Serial.println("Slot B charge complete.");
      if (Bcycle) {
        if (cycleCountB >= cycleTargetB) {
          cycleCompleteB = true;
          if (battNumB > 0 && GSheet.ready()) {
            logSimpsonOutputRow(SHEET_ID_B, voltageB, currentB, capacityB);
            delay(100);
            logSeparatorRow(SHEET_ID_B);
            delay(100);
            logSlotToSheet(SHEET_ID_B, "Cycle Complete", battNumB, elapsedB,
                           voltageB, 0, capacityB);
          }
          Serial.print("Slot B CYCLE COMPLETE. Final: ");
          Serial.print(capacityB, 4);
          Serial.println(" Ah");
          Bcycle = false;
          resetSlotRelays('B');
        } else {
          if (battNumB > 0 && GSheet.ready()) {
            logSimpsonOutputRow(SHEET_ID_B, voltageB, currentB, capacityB);
            delay(100);
            logSeparatorRow(SHEET_ID_B);
            delay(100);
            logSlotToSheet(SHEET_ID_B, "Cycle - Pause", battNumB, elapsedB,
                           voltageB, 0, capacityB);
            delay(100);
            logSeparatorRow(SHEET_ID_B);
          }
          digitalWrite(BMS, HIGH);
          digitalWrite(BCD, HIGH);
          BcyclePauseWait = true;
          BcyclePauseStart = millis();
          Serial.println("Slot B: charge→pause (2 s)");
        }
      } else {
        if (battNumB > 0 && GSheet.ready()) {
          delay(100);
          logSeparatorRow(SHEET_ID_B);
          delay(100);
          logSlotToSheet(SHEET_ID_B, "Charge Complete", battNumB, elapsedB,
                         voltageB, 0, capacityB);
        }
        resetSlotRelays('B');
      }
    }

    if (Bdischarge &&
        (voltageB <= DISCHARGE_STOP_V || (voltageB < 1 && currentB == 0.0))) {
      Bdischarge = false;
      Serial.print("Slot B final capacity: ");
      Serial.print(capacityB, 3);
      Serial.println(" Ah");
      if (Bcycle) {
        cycleCountB++;
        if (battNumB > 0 && GSheet.ready()) {
          logSimpsonOutputRow(SHEET_ID_B, voltageB, currentB, capacityB);
          delay(100);
          logSeparatorRow(SHEET_ID_B);
          delay(100);
          logSlotToSheet(SHEET_ID_B, "Cycle - Pause", battNumB, elapsedB,
                         voltageB, 0, capacityB);
          delay(100);
          logSeparatorRow(SHEET_ID_B);
        }
        digitalWrite(BMS, HIGH);
        digitalWrite(BCD, HIGH);
        resetCapacity('B');
        resetSimpsonTracking('B');
        BcycleRechargePauseWait = true;
        BcycleRechargePauseStart = millis();
        Serial.print("Slot B cycle ");
        Serial.print(cycleCountB);
        Serial.print("/");
        Serial.print(cycleTargetB);
        Serial.println(" - discharge→pause (2 s)");
      } else {
        if (battNumB > 0 && GSheet.ready()) {
          delay(100);
          logSeparatorRow(SHEET_ID_B);
          delay(100);
          logSlotToSheet(SHEET_ID_B, "Discharge Complete", battNumB, elapsedB,
                         voltageB, 0, capacityB);
        }
        resetSlotRelays('B');
      }
      Serial.println("Slot B discharge ended.");
    }
  } break;

  // ============================================================ SLOT C
  case 'C': {
    if (Ccharge &&
        (voltageC > CHARGE_STOP_V || voltageC < 1 || currentC < 50)) {
      Ccharge = false;
      Serial.println("Slot C charge complete.");
      if (Ccycle) {
        if (cycleCountC >= cycleTargetC) {
          cycleCompleteC = true;
          if (battNumC > 0 && GSheet.ready()) {
            logSimpsonOutputRow(SHEET_ID_C, voltageC, currentC, capacityC);
            delay(100);
            logSeparatorRow(SHEET_ID_C);
            delay(100);
            logSlotToSheet(SHEET_ID_C, "Cycle Complete", battNumC, elapsedC,
                           voltageC, 0, capacityC);
          }
          Serial.print("Slot C CYCLE COMPLETE. Final: ");
          Serial.print(capacityC, 4);
          Serial.println(" Ah");
          Ccycle = false;
          resetSlotRelays('C');
        } else {
          if (battNumC > 0 && GSheet.ready()) {
            logSimpsonOutputRow(SHEET_ID_C, voltageC, currentC, capacityC);
            delay(100);
            logSeparatorRow(SHEET_ID_C);
            delay(100);
            logSlotToSheet(SHEET_ID_C, "Cycle - Pause", battNumC, elapsedC,
                           voltageC, 0, capacityC);
            delay(100);
            logSeparatorRow(SHEET_ID_C);
          }
          digitalWrite(CMS, HIGH);
          digitalWrite(CCD, HIGH);
          CcyclePauseWait = true;
          CcyclePauseStart = millis();
          Serial.println("Slot C: charge→pause (2 s)");
        }
      } else {
        if (battNumC > 0 && GSheet.ready()) {
          delay(100);
          logSeparatorRow(SHEET_ID_C);
          delay(100);
          logSlotToSheet(SHEET_ID_C, "Charge Complete", battNumC, elapsedC,
                         voltageC, 0, capacityC);
        }
        resetSlotRelays('C');
      }
    }

    if (Cdischarge &&
        (voltageC <= DISCHARGE_STOP_V || (voltageC < 1 && currentC == 0.0))) {
      Cdischarge = false;
      Serial.print("Slot C final capacity: ");
      Serial.print(capacityC, 3);
      Serial.println(" Ah");
      if (Ccycle) {
        cycleCountC++;
        if (battNumC > 0 && GSheet.ready()) {
          logSimpsonOutputRow(SHEET_ID_C, voltageC, currentC, capacityC);
          delay(100);
          logSeparatorRow(SHEET_ID_C);
          delay(100);
          logSlotToSheet(SHEET_ID_C, "Cycle - Pause", battNumC, elapsedC,
                         voltageC, 0, capacityC);
          delay(100);
          logSeparatorRow(SHEET_ID_C);
        }
        digitalWrite(CMS, HIGH);
        digitalWrite(CCD, HIGH);
        resetCapacity('C');
        resetSimpsonTracking('C');
        CcycleRechargePauseWait = true;
        CcycleRechargePauseStart = millis();
        Serial.print("Slot C cycle ");
        Serial.print(cycleCountC);
        Serial.print("/");
        Serial.print(cycleTargetC);
        Serial.println(" - discharge→pause (2 s)");
      } else {
        if (battNumC > 0 && GSheet.ready()) {
          delay(100);
          logSeparatorRow(SHEET_ID_C);
          delay(100);
          logSlotToSheet(SHEET_ID_C, "Discharge Complete", battNumC, elapsedC,
                         voltageC, 0, capacityC);
        }
        resetSlotRelays('C');
      }
      Serial.println("Slot C discharge ended.");
    }
  } break;

  // ============================================================ SLOT D
  case 'D': {
    if (Dcharge &&
        (voltageD > CHARGE_STOP_V || voltageD < 1 || currentD < 50)) {
      Dcharge = false;
      Serial.println("Slot D charge complete.");
      if (Dcycle) {
        if (cycleCountD >= cycleTargetD) {
          cycleCompleteD = true;
          if (battNumD > 0 && GSheet.ready()) {
            logSimpsonOutputRow(SHEET_ID_D, voltageD, currentD, capacityD);
            delay(100);
            logSeparatorRow(SHEET_ID_D);
            delay(100);
            logSlotToSheet(SHEET_ID_D, "Cycle Complete", battNumD, elapsedD,
                           voltageD, 0, capacityD);
          }
          Serial.print("Slot D CYCLE COMPLETE. Final: ");
          Serial.print(capacityD, 4);
          Serial.println(" Ah");
          Dcycle = false;
          resetSlotRelays('D');
        } else {
          if (battNumD > 0 && GSheet.ready()) {
            logSimpsonOutputRow(SHEET_ID_D, voltageD, currentD, capacityD);
            delay(100);
            logSeparatorRow(SHEET_ID_D);
            delay(100);
            logSlotToSheet(SHEET_ID_D, "Cycle - Pause", battNumD, elapsedD,
                           voltageD, 0, capacityD);
            delay(100);
            logSeparatorRow(SHEET_ID_D);
          }
          digitalWrite(DMS, HIGH);
          digitalWrite(DCD, HIGH);
          DcyclePauseWait = true;
          DcyclePauseStart = millis();
          Serial.println("Slot D: charge→pause (2 s)");
        }
      } else {
        if (battNumD > 0 && GSheet.ready()) {
          delay(100);
          logSeparatorRow(SHEET_ID_D);
          delay(100);
          logSlotToSheet(SHEET_ID_D, "Charge Complete", battNumD, elapsedD,
                         voltageD, 0, capacityD);
        }
        resetSlotRelays('D');
      }
    }

    if (Ddischarge &&
        (voltageD <= DISCHARGE_STOP_V || (voltageD < 1 && currentD == 0.0))) {
      Ddischarge = false;
      Serial.print("Slot D final capacity: ");
      Serial.print(capacityD, 3);
      Serial.println(" Ah");
      if (Dcycle) {
        cycleCountD++;
        if (battNumD > 0 && GSheet.ready()) {
          logSimpsonOutputRow(SHEET_ID_D, voltageD, currentD, capacityD);
          delay(100);
          logSeparatorRow(SHEET_ID_D);
          delay(100);
          logSlotToSheet(SHEET_ID_D, "Cycle - Pause", battNumD, elapsedD,
                         voltageD, 0, capacityD);
          delay(100);
          logSeparatorRow(SHEET_ID_D);
        }
        digitalWrite(DMS, HIGH);
        digitalWrite(DCD, HIGH);
        resetCapacity('D');
        resetSimpsonTracking('D');
        DcycleRechargePauseWait = true;
        DcycleRechargePauseStart = millis();
        Serial.print("Slot D cycle ");
        Serial.print(cycleCountD);
        Serial.print("/");
        Serial.print(cycleTargetD);
        Serial.println(" - discharge→pause (2 s)");
      } else {
        if (battNumD > 0 && GSheet.ready()) {
          delay(100);
          logSeparatorRow(SHEET_ID_D);
          delay(100);
          logSlotToSheet(SHEET_ID_D, "Discharge Complete", battNumD, elapsedD,
                         voltageD, 0, capacityD);
        }
        resetSlotRelays('D');
      }
      Serial.println("Slot D discharge ended.");
    }
  } break;
  }
}

// ===================== DEFERRED DISCHARGE / RECHARGE =====================
void processDeferredDischarge() {
  unsigned long now = millis();

  // ---- Standalone discharge delays (10 s) ----
  if (AdischargeWait && now - AdischargeStart >= 10000) {
    AdischargeWait = false;
    AdischargePending = false;
    Adischarge = true;
    digitalWrite(AMS, LOW);
    digitalWrite(ACD, LOW);
    logModeStartHeader('A', "Discharge - Start");
    logPhaseStart('A', "DISCHARGING");
    Serial.println("Slot A standalone DISCHARGE started");
    showSlot('A');
  }
  if (BdischargeWait && now - BdischargeStart >= 10000) {
    BdischargeWait = false;
    BdischargePending = false;
    Bdischarge = true;
    digitalWrite(BMS, LOW);
    digitalWrite(BCD, LOW);
    logModeStartHeader('B', "Discharge - Start");
    logPhaseStart('B', "DISCHARGING");
    Serial.println("Slot B standalone DISCHARGE started");
    showSlot('B');
  }
  if (CdischargeWait && now - CdischargeStart >= 10000) {
    CdischargeWait = false;
    CdischargePending = false;
    Cdischarge = true;
    digitalWrite(CMS, LOW);
    digitalWrite(CCD, LOW);
    logModeStartHeader('C', "Discharge - Start");
    logPhaseStart('C', "DISCHARGING");
    Serial.println("Slot C standalone DISCHARGE started");
    showSlot('C');
  }
  if (DdischargeWait && now - DdischargeStart >= 10000) {
    DdischargeWait = false;
    DdischargePending = false;
    Ddischarge = true;
    digitalWrite(DMS, LOW);
    digitalWrite(DCD, LOW);
    logModeStartHeader('D', "Discharge - Start");
    logPhaseStart('D', "DISCHARGING");
    Serial.println("Slot D standalone DISCHARGE started");
    showSlot('D');
  }

  // ---- Cycle: charge → pause → discharge (2 s) ----
  if (AcyclePauseWait && now - AcyclePauseStart >= CYCLE_PAUSE_MS) {
    AcyclePauseWait = false;
    Adischarge = true;
    elapsedA = 0;
    lastLogA = now;
    resetCapacity('A');
    resetSimpsonTracking('A');
    digitalWrite(AMS, LOW);
    digitalWrite(ACD, LOW);
    logPhaseStart('A', "CYCLE - DISCHARGING");
    Serial.println("Slot A: → CYCLE DISCHARGING");
    showSlot('A');
  }
  if (BcyclePauseWait && now - BcyclePauseStart >= CYCLE_PAUSE_MS) {
    BcyclePauseWait = false;
    Bdischarge = true;
    elapsedB = 0;
    lastLogB = now;
    resetCapacity('B');
    resetSimpsonTracking('B');
    digitalWrite(BMS, LOW);
    digitalWrite(BCD, LOW);
    logPhaseStart('B', "CYCLE - DISCHARGING");
    Serial.println("Slot B: → CYCLE DISCHARGING");
    showSlot('B');
  }
  if (CcyclePauseWait && now - CcyclePauseStart >= CYCLE_PAUSE_MS) {
    CcyclePauseWait = false;
    Cdischarge = true;
    elapsedC = 0;
    lastLogC = now;
    resetCapacity('C');
    resetSimpsonTracking('C');
    digitalWrite(CMS, LOW);
    digitalWrite(CCD, LOW);
    logPhaseStart('C', "CYCLE - DISCHARGING");
    Serial.println("Slot C: → CYCLE DISCHARGING");
    showSlot('C');
  }
  if (DcyclePauseWait && now - DcyclePauseStart >= CYCLE_PAUSE_MS) {
    DcyclePauseWait = false;
    Ddischarge = true;
    elapsedD = 0;
    lastLogD = now;
    resetCapacity('D');
    resetSimpsonTracking('D');
    digitalWrite(DMS, LOW);
    digitalWrite(DCD, LOW);
    logPhaseStart('D', "CYCLE - DISCHARGING");
    Serial.println("Slot D: → CYCLE DISCHARGING");
    showSlot('D');
  }

  // ---- NEW: Cycle: discharge → pause → charge (2 s) ----
  if (AcycleRechargePauseWait &&
      now - AcycleRechargePauseStart >= CYCLE_PAUSE_MS) {
    AcycleRechargePauseWait = false;
    Acharge = true;
    elapsedA = 0;
    lastLogA = now;
    digitalWrite(AMS, LOW);
    digitalWrite(ACD, HIGH);
    logPhaseStart('A', "CYCLE - CHARGING");
    Serial.print("Slot A cycle ");
    Serial.print(cycleCountA);
    Serial.print("/");
    Serial.print(cycleTargetA);
    Serial.println(" → CYCLE CHARGING");
    showSlot('A');
  }
  if (BcycleRechargePauseWait &&
      now - BcycleRechargePauseStart >= CYCLE_PAUSE_MS) {
    BcycleRechargePauseWait = false;
    Bcharge = true;
    elapsedB = 0;
    lastLogB = now;
    digitalWrite(BMS, LOW);
    digitalWrite(BCD, HIGH);
    logPhaseStart('B', "CYCLE - CHARGING");
    Serial.print("Slot B cycle ");
    Serial.print(cycleCountB);
    Serial.print("/");
    Serial.print(cycleTargetB);
    Serial.println(" → CYCLE CHARGING");
    showSlot('B');
  }
  if (CcycleRechargePauseWait &&
      now - CcycleRechargePauseStart >= CYCLE_PAUSE_MS) {
    CcycleRechargePauseWait = false;
    Ccharge = true;
    elapsedC = 0;
    lastLogC = now;
    digitalWrite(CMS, LOW);
    digitalWrite(CCD, HIGH);
    logPhaseStart('C', "CYCLE - CHARGING");
    Serial.print("Slot C cycle ");
    Serial.print(cycleCountC);
    Serial.print("/");
    Serial.print(cycleTargetC);
    Serial.println(" → CYCLE CHARGING");
    showSlot('C');
  }
  if (DcycleRechargePauseWait &&
      now - DcycleRechargePauseStart >= CYCLE_PAUSE_MS) {
    DcycleRechargePauseWait = false;
    Dcharge = true;
    elapsedD = 0;
    lastLogD = now;
    digitalWrite(DMS, LOW);
    digitalWrite(DCD, HIGH);
    logPhaseStart('D', "CYCLE - CHARGING");
    Serial.print("Slot D cycle ");
    Serial.print(cycleCountD);
    Serial.print("/");
    Serial.print(cycleTargetD);
    Serial.println(" → CYCLE CHARGING");
    showSlot('D');
  }
}

void updateSlotRelays(char slot, char action) {
  switch (slot) {
  case 'A':
    Acycle = false;
    Acharge = (action == '1');
    Adischarge = (action == '2');
    if (Acharge) {
      digitalWrite(AMS, LOW);
      digitalWrite(ACD, HIGH);
    } else if (Adischarge) {
      digitalWrite(AMS, LOW);
      digitalWrite(ACD, LOW);
    }
    break;
  case 'B':
    Bcycle = false;
    Bcharge = (action == '1');
    Bdischarge = (action == '2');
    if (Bcharge) {
      digitalWrite(BMS, LOW);
      digitalWrite(BCD, HIGH);
    } else if (Bdischarge) {
      digitalWrite(BMS, LOW);
      digitalWrite(BCD, LOW);
    }
    break;
  case 'C':
    Ccycle = false;
    Ccharge = (action == '1');
    Cdischarge = (action == '2');
    if (Ccharge) {
      digitalWrite(CMS, LOW);
      digitalWrite(CCD, HIGH);
    } else if (Cdischarge) {
      digitalWrite(CMS, LOW);
      digitalWrite(CCD, LOW);
    }
    break;
  case 'D':
    Dcycle = false;
    Dcharge = (action == '1');
    Ddischarge = (action == '2');
    if (Dcharge) {
      digitalWrite(DMS, LOW);
      digitalWrite(DCD, HIGH);
    } else if (Ddischarge) {
      digitalWrite(DMS, LOW);
      digitalWrite(DCD, LOW);
    }
    break;
  }
}

void resetSlotRelays(char slot) {
  switch (slot) {
  case 'A':
    digitalWrite(AMS, HIGH);
    digitalWrite(ACD, HIGH);
    break;
  case 'B':
    digitalWrite(BMS, HIGH);
    digitalWrite(BCD, HIGH);
    break;
  case 'C':
    digitalWrite(CMS, HIGH);
    digitalWrite(CCD, HIGH);
    break;
  case 'D':
    digitalWrite(DMS, HIGH);
    digitalWrite(DCD, HIGH);
    break;
  }
}

void printCentered(const char *text, int row) {
  int len = strlen(text);
  int col = (20 - len) / 2;
  if (col < 0)
    col = 0;
  lcd.setCursor(col, row);
  lcd.print(text);
}

void showHome() {
  lcd.clear();
  printCentered("Capacity Testing", 0);
  printCentered("Station (CTS) V1.0", 1);
  lcd.setCursor(0, 3);
  if (WiFi.status() == WL_CONNECTED)
    lcd.print("  Wi-Fi: Connected  ");
  else
    lcd.print("Wi-Fi: Not Connected");
}

void showSlot(char slot) {
  inSettingScreen = false;
  inStopPrompt = false;
  stopMessageActive = false;
  inBattNumberInput = false;
  inCycleInput = false;
  confirmingCycle = false;
  lcd.clear();
  float v, c, cap;
  String s;
  int bn = 0;
  String mode = "", cycleInfo = "";

  switch (slot) {
  case 'A':
    v = voltageA;
    c = currentA;
    s = statusA;
    bn = battNumA;
    cap = capacityA;
    if (AcyclePauseWait || AcycleRechargePauseWait)
      mode = "CYCLE-PAUSE";
    else if (Acharge)
      mode = "CHARGING";
    else if (Adischarge)
      mode = "DISCHARGING";
    else if (Acycle)
      mode = "CYCLE";
    else
      mode = "IDLE";
    if (Acycle)
      cycleInfo = "Cyc:" + String(cycleCountA) + "/" + String(cycleTargetA);
    break;
  case 'B':
    v = voltageB;
    c = currentB;
    s = statusB;
    bn = battNumB;
    cap = capacityB;
    if (BcyclePauseWait || BcycleRechargePauseWait)
      mode = "CYCLE-PAUSE";
    else if (Bcharge)
      mode = "CHARGING";
    else if (Bdischarge)
      mode = "DISCHARGING";
    else if (Bcycle)
      mode = "CYCLE";
    else
      mode = "IDLE";
    if (Bcycle)
      cycleInfo = "Cyc:" + String(cycleCountB) + "/" + String(cycleTargetB);
    break;
  case 'C':
    v = voltageC;
    c = currentC;
    s = statusC;
    bn = battNumC;
    cap = capacityC;
    if (CcyclePauseWait || CcycleRechargePauseWait)
      mode = "CYCLE-PAUSE";
    else if (Ccharge)
      mode = "CHARGING";
    else if (Cdischarge)
      mode = "DISCHARGING";
    else if (Ccycle)
      mode = "CYCLE";
    else
      mode = "IDLE";
    if (Ccycle)
      cycleInfo = "Cyc:" + String(cycleCountC) + "/" + String(cycleTargetC);
    break;
  case 'D':
    v = voltageD;
    c = currentD;
    s = statusD;
    bn = battNumD;
    cap = capacityD;
    if (DcyclePauseWait || DcycleRechargePauseWait)
      mode = "CYCLE-PAUSE";
    else if (Dcharge)
      mode = "CHARGING";
    else if (Ddischarge)
      mode = "DISCHARGING";
    else if (Dcycle)
      mode = "CYCLE";
    else
      mode = "IDLE";
    if (Dcycle)
      cycleInfo = "Cyc:" + String(cycleCountD) + "/" + String(cycleTargetD);
    break;
  }

  String title = "Slot ";
  title += slot;
  if (bn > 0) {
    title += " #";
    title += bn;
  }
  printCentered(title.c_str(), 0);
  lcd.setCursor(0, 1);
  lcd.print("Status: ");
  lcd.print(s);
  lcd.setCursor(0, 2);
  lcd.print("Mode: ");
  lcd.print(mode);
  if (cycleInfo.length() > 0) {
    lcd.setCursor(10, 2);
    lcd.print(cycleInfo);
  }
  if (Adischarge || Bdischarge || Cdischarge || Ddischarge) {
    lcd.setCursor(0, 3);
    lcd.print("Cap:");
    lcd.print(cap, 0);
    lcd.print("mAh V:");
    lcd.print(v, 2);
  } else {
    lcd.setCursor(0, 3);
    lcd.print("V:");
    lcd.print(v, 2);
    lcd.print(" C:");
    lcd.print(c, 0);
  }
}

void refreshSlotDisplay() {
  if (previousSlot != '\0')
    showSlot(previousSlot);
}

void showSlotSetting(char slot) {
  if (slot == '\0')
    return;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SLOT ");
  lcd.print(slot);
  lcd.print(" OPTIONS");
  lcd.setCursor(0, 1);
  lcd.print("1.Charge 2.Discharge");
  lcd.setCursor(0, 2);
  lcd.print("3.Cycle  4.Batt No.");
  lcd.setCursor(0, 3);
  lcd.print("Press * to go back");
}

void showForceStopPrompt(char slot) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("FORCE STOP Slot ");
  lcd.print(slot);
  lcd.setCursor(0, 1);
  lcd.print("Current: ");
  switch (slot) {
  case 'A':
    if (AcyclePauseWait || AcycleRechargePauseWait)
      lcd.print("PAUSING");
    else if (Acharge)
      lcd.print("CHARGING");
    else if (Adischarge)
      lcd.print("DISCHARGING");
    else if (Acycle)
      lcd.print("CYCLE");
    break;
  case 'B':
    if (BcyclePauseWait || BcycleRechargePauseWait)
      lcd.print("PAUSING");
    else if (Bcharge)
      lcd.print("CHARGING");
    else if (Bdischarge)
      lcd.print("DISCHARGING");
    else if (Bcycle)
      lcd.print("CYCLE");
    break;
  case 'C':
    if (CcyclePauseWait || CcycleRechargePauseWait)
      lcd.print("PAUSING");
    else if (Ccharge)
      lcd.print("CHARGING");
    else if (Cdischarge)
      lcd.print("DISCHARGING");
    else if (Ccycle)
      lcd.print("CYCLE");
    break;
  case 'D':
    if (DcyclePauseWait || DcycleRechargePauseWait)
      lcd.print("PAUSING");
    else if (Dcharge)
      lcd.print("CHARGING");
    else if (Ddischarge)
      lcd.print("DISCHARGING");
    else if (Dcycle)
      lcd.print("CYCLE");
    break;
  }
  lcd.setCursor(0, 2);
  lcd.print("0 = Yes, FORCE STOP");
  lcd.setCursor(0, 3);
  lcd.print("5 = No, continue");
}

void showBattNumberInput(char slot) {
  if (slot == '\0')
    return;
  battNumberSlot = slot;
  battNumberBuffer = "";
  inBattNumberInput = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter Batt # for ");
  lcd.print(slot);
  renderBattNumberBuffer();
  lcd.setCursor(0, 2);
  lcd.print("* Cancel   # OK");
  lcd.setCursor(0, 3);
  lcd.print(battNumForSlot(slot));
}

void showCycleInput(char slot) {
  if (slot == '\0')
    return;
  cycleSlot = slot;
  cycleBuffer = "";
  inCycleInput = true;
  confirmingCycle = false;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Set Cycles for ");
  lcd.print(slot);
  renderCycleBuffer();
  lcd.setCursor(0, 2);
  lcd.print("Enter number (1-999)");
  lcd.setCursor(0, 3);
  lcd.print("* Cancel   # OK");
}

void renderCycleBuffer() {
  String d = "[";
  d += cycleBuffer;
  d += "]";
  int col = (20 - d.length()) / 2;
  if (col < 0)
    col = 0;
  lcd.setCursor(0, 1);
  for (int i = 0; i < 20; i++)
    lcd.print(" ");
  lcd.setCursor(col, 1);
  lcd.print(d);
}

void showCycleConfirmation(char slot, int cycles) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Confirm Cycle Test");
  lcd.setCursor(0, 1);
  lcd.print("Slot ");
  lcd.print(slot);
  lcd.print(": ");
  lcd.print(cycles);
  lcd.print(" cycles");
  lcd.setCursor(0, 2);
  lcd.print("Ends on CHARGE");
  lcd.setCursor(0, 3);
  lcd.print("#=START *=CANCEL");
}

void handleCycleInputKey(char key) {
  if (key >= '0' && key <= '9') {
    if (cycleBuffer.length() == 0 && key == '0')
      return;
    if (cycleBuffer.length() < 3) {
      cycleBuffer += key;
      renderCycleBuffer();
    }
    return;
  }
  if (key == '*') {
    inCycleInput = false;
    confirmingCycle = false;
    cycleSlot = '\0';
    cycleBuffer = "";
    pendingCycleValue = 0;
    showSlotSetting(previousSlot);
    inSettingScreen = true;
    return;
  }
  if (key == '#') {
    int value = (cycleBuffer.length() > 0) ? atoi(cycleBuffer.c_str()) : 1;
    if (value <= 0)
      value = 1;
    pendingCycleValue = value;
    showCycleConfirmation(cycleSlot, value);
    inCycleInput = false;
    confirmingCycle = true;
  }
}

void renderBattNumberBuffer() {
  String d = "[";
  d += battNumberBuffer;
  d += "]";
  int col = (20 - d.length()) / 2;
  if (col < 0)
    col = 0;
  lcd.setCursor(0, 1);
  for (int i = 0; i < 20; i++)
    lcd.print(" ");
  lcd.setCursor(col, 1);
  lcd.print(d);
}

void handleBattNumberInputKey(char key) {
  if (key >= '0' && key <= '9') {
    if (battNumberBuffer.length() == 0 && key == '0')
      return;
    if (battNumberBuffer.length() < 3) {
      battNumberBuffer += key;
      renderBattNumberBuffer();
    }
    return;
  }
  if (key == '*') {
    inBattNumberInput = false;
    battNumberSlot = '\0';
    battNumberBuffer = "";
    showSlotSetting(previousSlot);
    inSettingScreen = true;
    return;
  }
  if (key == '#') {
    int value =
        (battNumberBuffer.length() > 0) ? atoi(battNumberBuffer.c_str()) : 0;
    saveBattNum(battNumberSlot, value);
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print(" Batt # saved!");
    lcd.setCursor(0, 2);
    lcd.print(" Slot ");
    lcd.print(battNumberSlot);
    lcd.print(" = ");
    lcd.print(value);
    delay(1500);
    inBattNumberInput = false;
    battNumberSlot = '\0';
    battNumberBuffer = "";
    showSlotSetting(previousSlot);
    inSettingScreen = true;
  }
}

void saveBattNum(char slot, int num) {
  switch (slot) {
  case 'A':
    prefs.putInt("A_num", num);
    battNumA = num;
    break;
  case 'B':
    prefs.putInt("B_num", num);
    battNumB = num;
    break;
  case 'C':
    prefs.putInt("C_num", num);
    battNumC = num;
    break;
  case 'D':
    prefs.putInt("D_num", num);
    battNumD = num;
    break;
  }
  Serial.print("Saved ");
  Serial.print(slot);
  Serial.print("_num=");
  Serial.println(num);
}

void clearBattNum(char slot) {
  switch (slot) {
  case 'A':
    prefs.putInt("A_num", 0);
    battNumA = 0;
    break;
  case 'B':
    prefs.putInt("B_num", 0);
    battNumB = 0;
    break;
  case 'C':
    prefs.putInt("C_num", 0);
    battNumC = 0;
    break;
  case 'D':
    prefs.putInt("D_num", 0);
    battNumD = 0;
    break;
  }
  Serial.print("Cleared ");
  Serial.print(slot);
  Serial.println(" number");
}

void stopOperation(char slot, bool fromForceStop) {
  String operation = "";
  switch (slot) {
  case 'A':
    if (AcyclePauseWait || AcycleRechargePauseWait || Acycle)
      operation = "CYCLE";
    else if (Acharge)
      operation = "CHARGING";
    else if (Adischarge)
      operation = "DISCHARGING";
    break;
  case 'B':
    if (BcyclePauseWait || BcycleRechargePauseWait || Bcycle)
      operation = "CYCLE";
    else if (Bcharge)
      operation = "CHARGING";
    else if (Bdischarge)
      operation = "DISCHARGING";
    break;
  case 'C':
    if (CcyclePauseWait || CcycleRechargePauseWait || Ccycle)
      operation = "CYCLE";
    else if (Ccharge)
      operation = "CHARGING";
    else if (Cdischarge)
      operation = "DISCHARGING";
    break;
  case 'D':
    if (DcyclePauseWait || DcycleRechargePauseWait || Dcycle)
      operation = "CYCLE";
    else if (Dcharge)
      operation = "CHARGING";
    else if (Ddischarge)
      operation = "DISCHARGING";
    break;
  }
  float finalCapacity = 0;
  switch (slot) {
  case 'A':
    finalCapacity = capacityA;
    break;
  case 'B':
    finalCapacity = capacityB;
    break;
  case 'C':
    finalCapacity = capacityC;
    break;
  case 'D':
    finalCapacity = capacityD;
    break;
  }

  resetSlotBooleans(slot);
  resetSlotRelays(slot);

  char sheetId[50];
  float v = 0, c = 0;
  int battNum = battNumForSlot(slot);
  switch (slot) {
  case 'A':
    strcpy(sheetId, SHEET_ID_A);
    v = voltageA;
    c = currentA;
    break;
  case 'B':
    strcpy(sheetId, SHEET_ID_B);
    v = voltageB;
    c = currentB;
    break;
  case 'C':
    strcpy(sheetId, SHEET_ID_C);
    v = voltageC;
    c = currentC;
    break;
  case 'D':
    strcpy(sheetId, SHEET_ID_D);
    v = voltageD;
    c = currentD;
    break;
  }
  if (battNum > 0)
    logSlotToSheet(sheetId, "Force Stopped", battNum, 0, v, c, finalCapacity);

  lcd.clear();
  lcd.setCursor(0, 1);
  if (fromForceStop)
    lcd.print(" FORCE STOPPED!");
  else {
    lcd.print("   OPERATION");
    lcd.setCursor(0, 2);
    lcd.print("    STOPPED!");
  }
  lcd.setCursor(0, 3);
  lcd.print("Slot ");
  lcd.print(slot);
  if (operation.length() > 0) {
    lcd.print(" - ");
    lcd.print(operation);
  }

  stopMessageStart = millis();
  stopMessageActive = true;
  inStopPrompt = false;
  Serial.print("Slot ");
  Serial.print(slot);
  Serial.print(" stopped. Was: ");
  Serial.println(operation);
}

void formatDateTime(const char *fmt, char *result, size_t size) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
    strftime(result, size, fmt, &timeinfo);
  else
    strncpy(result, "1970-01-01 00:00:00", size);
}

void logSeparatorRow(const char *sheetId) {
  if (!GSheet.ready())
    return;
  FirebaseJson response, valueRange;
  valueRange.add("majorDimension", "COLUMNS");
  valueRange.set("values/[0]/[0]", "--------------------------");
  valueRange.set("values/[1]/[0]", "--------------------------");
  valueRange.set("values/[2]/[0]", "---");
  valueRange.set("values/[3]/[0]", "---");
  valueRange.set("values/[4]/[0]", "---");
  valueRange.set("values/[5]/[0]", "---");
  valueRange.set("values/[6]/[0]", "---");
  GSheet.values.append(&response, sheetId, "Sheet1!A1:G1", &valueRange);
  Serial.println("Separator row logged.");
}

void logSimpsonOutputRow(const char *sheetId, float voltage, float current,
                         float capacity) {
  if (!GSheet.ready())
    return;
  FirebaseJson response, valueRange;
  char voltageStr[10], currentStr[10], capacityStr[15];
  dtostrf(voltage, 5, 2, voltageStr);
  dtostrf(current, 5, 2, currentStr);
  dtostrf(capacity, 8, 2, capacityStr);
  valueRange.add("majorDimension", "COLUMNS");
  valueRange.set("values/[0]/[0]", "Simpson Capacity Output:");
  valueRange.set("values/[1]/[0]", "");
  valueRange.set("values/[2]/[0]", "");
  valueRange.set("values/[3]/[0]", "");
  valueRange.set("values/[4]/[0]", voltageStr);
  valueRange.set("values/[5]/[0]", currentStr);
  valueRange.set("values/[6]/[0]", capacityStr);
  GSheet.values.append(&response, sheetId, "Sheet1!A1:G1", &valueRange);
  Serial.print("Simpson Output row logged: ");
  Serial.print(capacityStr);
  Serial.println(" Ah");
}

void logSlotToSheet(const char *sheetId, const String &slotStatus, int battNum,
                    unsigned long elapsedSecs, float voltage, float current,
                    float capacity) {
  if (!GSheet.ready()) {
    Serial.println("GSheet not ready - skip");
    return;
  }
  FirebaseJson response, valueRange;
  char timeStamp[25];
  formatDateTime("%Y-%m-%d %H:%M:%S", timeStamp, sizeof(timeStamp));
  char voltageStr[10], currentStr[10], capacityStr[12];
  dtostrf(voltage, 5, 2, voltageStr);
  dtostrf(current, 5, 2, currentStr);
  dtostrf(capacity, 8, 4, capacityStr);

  valueRange.add("majorDimension", "COLUMNS");
  valueRange.set("values/[0]/[0]", timeStamp);
  valueRange.set("values/[1]/[0]", slotStatus);
  valueRange.set("values/[2]/[0]", battNum);
  valueRange.set("values/[3]/[0]", elapsedSecs);
  valueRange.set("values/[4]/[0]", voltageStr);
  valueRange.set("values/[5]/[0]", currentStr);
  valueRange.set("values/[6]/[0]", capacityStr);

  Serial.print("Sheet log → ");
  Serial.print(sheetId);
  Serial.print(" | ");
  Serial.print(slotStatus);
  Serial.print(" | cap=");
  Serial.print(capacity * 1000.0, 4);
  Serial.println(" mAh");

  bool ok =
      GSheet.values.append(&response, sheetId, "Sheet1!A1:G1", &valueRange);
  if (!ok) {
    Serial.println("Append FAILED!");
    String r;
    response.toString(r, true);
    Serial.println(r);
  } else {
    Serial.println("Log OK.");
  }
}
