#include <Arduino.h>
#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <INA226.h>
#include <WiFi.h>
#include <Preferences.h>
#include "time.h"
#include <ESP_Google_Sheet_Client.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// WiFi credentials - change these to your network
const char* ssid = "Kodicpogi21";
const char* pass = "11123232";
WebServer server(80); // WebServer for Dashboard
unsigned long previousStatusTime = 0;
const unsigned long statusInterval = 3000; // Check WiFi every 3 seconds
void connectToWiFi() {
  WiFi.begin(ssid, pass);
  unsigned long startAttemptTime = millis();
  // Try to connect for up to 8 seconds
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 8000) {
    delay(100);
  }
}
// Keypad setup - 4x4 matrix
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {15, 4, 25, 26};
byte colPins[COLS] = {27, 14, 12, 13};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
// LCD display - 20x4 I2C
LiquidCrystal_I2C lcd(0x27, 20, 4);
// Relay pins - active LOW means LOW turns relay ON
const int DMS = 23;
const int DCD = 19;
const int BMS = 18;
const int BCD = 5;
const int AMS = 17;
const int ACD = 16;
const int CMS = 32;
const int CCD = 33;
// INA226 power sensor addresses
#define ADDR_A 0x40
#define ADDR_B 0x41
#define ADDR_C 0x44
#define ADDR_D 0x45
INA226 inaA;
INA226 inaB;
INA226 inaC;
INA226 inaD;
// Calibration values - each sensor needs slight adjustments
float currentCalA = 100.3720;
float voltageCalA = 0.9991;
float voltageCalAdischarge = 1.0248;
float voltageCalAcharge = 0.9874;
float currentCalB = 97.8846;
float voltageCalB = 1.0005;
float voltageCalBdischarge = 1.0326;
float voltageCalBcharge = 0.9898;
float currentCalC = 98.8484;
float voltageCalC = 1.0005;
float voltageCalCdischarge = 1.0239;
float voltageCalCcharge = 0.9948;
float currentCalD = 98.6328;
float voltageCalD = 1.0008;
float voltageCalDdischarge = 1.0305;
float voltageCalDcharge = 0.9875;
// Track when each sensor was last read
unsigned long prevA = 0, prevB = 0, prevC = 0, prevD = 0;
const unsigned long interval = 3000; // Read sensors every 3 seconds
// Current screen states
char previousSlot = '\0';
bool inSettingScreen = false;
bool inStopPrompt = false;
bool stopMessageActive = false;
unsigned long stopMessageStart = 0;
const unsigned long stopMessageDuration = 2000; // Show stop message for 2 seconds
// Battery number entry mode
bool inBattNumberInput = false;
String battNumberBuffer = "";
char battNumberSlot = '\0';
// Live readings for each slot
float voltageA = 0, currentA = 0;
float voltageB = 0, currentB = 0;
float voltageC = 0, currentC = 0;
float voltageD = 0, currentD = 0;
String statusA = "No Battery";
String statusB = "No Battery";
String statusC = "No Battery";
String statusD = "No Battery";
// What each slot is currently doing
bool Acharge = false, Adischarge = false;
bool Bcharge = false, Bdischarge = false;
bool Ccharge = false, Cdischarge = false;
bool Dcharge = false, Ddischarge = false;
// Cycle test mode
bool Acycle = false, Bcycle = false, Ccycle = false, Dcycle = false;
// How many full charge/discharge cycles to perform and current count
int cycleTargetA = 0, cycleTargetB = 0, cycleTargetC = 0, cycleTargetD = 0;
int cycleCountA = 0, cycleCountB = 0, cycleCountC = 0, cycleCountD = 0;
// Input state for cycle count entry
bool inCycleInput = false;
String cycleBuffer = "";
char cycleSlot = '\0';
bool confirmingCycle = false;
int pendingCycleValue = 0;
// Battery tracking numbers - stored separately from operation
int battNumA = 0, battNumB = 0, battNumC = 0, battNumD = 0;
// Track if discharge is waiting to start or pending log
bool AdischargePending = false, BdischargePending = false;
bool CdischargePending = false, DdischargePending = false;
bool AdischargeWait = false, BdischargeWait = false;
bool CdischargeWait = false, DdischargeWait = false;
unsigned long AdischargeStart = 0, BdischargeStart = 0;
unsigned long CdischargeStart = 0, DdischargeStart = 0;
// How long each battery has been running (seconds)
unsigned long elapsedA = 0, elapsedB = 0, elapsedC = 0, elapsedD = 0;
// Voltage limits
const float CHARGE_STOP_V = 4.10;    // Stop charging at 4.10V
const float DISCHARGE_STOP_V = 2.80; // Stop discharging at 2.80V
// ============ SIMPSON'S RULE VARIABLES ============
// Capacity tracking in Ampere-hours (Ah)
float capacityA = 0, capacityB = 0, capacityC = 0, capacityD = 0;
// Previous current values for Simpson's rule
float prevCurrentA = 0, prevCurrentB = 0, prevCurrentC = 0, prevCurrentD = 0;
float prevPrevCurrentA = 0, prevPrevCurrentB = 0, prevPrevCurrentC = 0, prevPrevCurrentD = 0;
// Previous timestamps for Simpson's rule
unsigned long prevTimeA = 0, prevTimeB = 0, prevTimeC = 0, prevTimeD = 0;
unsigned long prevPrevTimeA = 0, prevPrevTimeB = 0, prevPrevTimeC = 0, prevPrevTimeD = 0;
// Sample counters for Simpson's rule
int sampleCountA = 0, sampleCountB = 0, sampleCountC = 0, sampleCountD = 0;
// Track if we're in a discharge cycle for capacity calculation
bool capacityTrackingA = false, capacityTrackingB = false, capacityTrackingC = false, capacityTrackingD = false;
// ==================================================
// For saving data between reboots
Preferences prefs;
// Google Sheets setup
#define PROJECT_ID       "potent-terminal-478407-m6"
#define CLIENT_EMAIL     "randolereviewcenter@potent-terminal-478407-m6.iam.gserviceaccount.com"
const char PRIVATE_KEY[] PROGMEM =
"-----BEGIN PRIVATE KEY-----\n"
"MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQChtT4Z809VN3Lo\neWX4X5r1Sg4O12RNF91b7EPMmNt5HlbkggmDkb/NTEGV6e+4hZ5Id+IvSAyfM+MA\ndZCkdcZBCcdMaxnhtUS2jXW/kLnGAgnQiGDhBiIvBUcCLm+yeTlZCrJQ/J6VgIMQ\nbv4XxIMry4GVbeWfVTWUEdicjkWCIZRfbk1nJsw4YNXAeaWdz5aHTWBRwiZihe67\ngUGqM/VY95skn8YLwsHQpXaAm2HDzWW4UVHlDdKqlxlkkwjvjrm6Z7Evzui0CQn2\namXJge5FfyYfbM8tl2C8F/ISw/2n4StN4q5pdbugig1RB6KU16FILYAI6QtnxVS+\nDo5TZpJtAgMBAAECggEAMod+UMCRHRk2/EKW5O4G7zfFPcj7TAW1gzhIFUH8bpPW\n5g9mJqkf7Gg0JEKVyCxkgdOIJ2sVmpetiqKx4Fn26bLDBnN/AmLQhlScow/3pNJV\nO8apsxbmDphREHLvLy8nBtZLUvglG6UtDzEHj+i1bjVomAdflZKcK9kJvR3NxXP2\nThlcPelI4N8OHdz1T+LRnotyeYsOZzrNRVSqrUHl3CzmCJc1NLNLeTgP5HeHO9C+\nV6YOnUHtGcN1EEkhqLZMV74171fZNJxxBR4tnehnfe35ArIWJL01OADLhSHpbrto\novsqHc0ta2COV/n4LLBC41JpoEvwO2ZKcIKHMoUc9wKBgQDYzgUCa7fbdR5CfsdX\nuJ2+2S3jdGTZMzC+jQKsbTAJCVHVvkpYVA6At7FyI2hhRikZxUeC+JJAGGciwvTF\nWgI8q0BBUHZ3qEfvZ9CpMCWFHTZsERXH17KWf3Ki6sqLB5vTBkfal/Nq7oVuHRbj\nn18bAI82J4ivoo89BsDw+96o3wKBgQC+8UdcdH3s6rBTwMRyeZQ0tRqbHFzIi+5E\npnQ7a5L1BGBVB3YTx+lbIv9Knmx6htSKv8LEwBXPhFK31hA5GQ19s9WNxIihyzKf\nd013N8jp6aFD6GLOeJXqfizk0wNFzV6yxnQ9+5vDqmYoUzUhW5w+tGj3clx4Z93e\nk6tzPPzSMwKBgQCB+soAEIqS9N1malGi4tkYAWbElhScL1eK9kljDLcew8qfRc2W\ntRZYz0iAMIA0yXZ8r8zW1aYA7WBv88gBxZvPua/1OIM969Ls0iXEOUxVSRVGptuT\nC1tTZSdaSz+RKMegNYTAphbWxheS07fUUckYDDbP9dW5ztDnenQURjzQqwKBgQCx\njg/7y1+lxX7+As0qXiAQ+y+oeTFWU7jXIaoH7zqSmOUzbGLCdi1rUBnxO2xIa8SM\n2VC2QKCHfdalmGsxjThcYbP9xnn/acLDQt9IMxmjWltZmGj48m0FxxrcFdR/PkAH\nIj/Ju4TW6EdizC0lvdiG/qB1KWUPmhZY+Rx/ZoD6vQKBgBGTzXEWzB+mT643DkaX\nt83ZRDXsTPWg3Sk21MUkQjKSGQpzAijPc5Sjk9yaQfyjdIsojLDeEGvomTb49uH3\n8uOhCMv3mbUQ+PY11pgaNMuXVzk2a+6o//F1CcaAgezn83nA7PTvlff1B2s5YfuR\ngzk4lX5lyewQ+3KvIgK3QUhA\n"
"-----END PRIVATE KEY-----\n";
// Different spreadsheet for each slot
#define SHEET_ID_A "1liVQR26X5vO0VPv9nVeQRkEXtCxbicngRhdJrHw05lc"
#define SHEET_ID_B "1aGVDJZaUeVPIGU43q_Mu6sZrNcv4g4YG6oHHVGlfAyE"
#define SHEET_ID_C "1UNclOw0Z7xe073mdUaDNap8xIiTkwQHakoat5yu541k"
#define SHEET_ID_D "1Kpbmw2CI7hMAssOOEdyrMFwYx_6z55q2dYv9i1Dj8w8"
// Logging timing
unsigned long lastRead = 0;
const unsigned long readInterval = 3000;   // Read sensors every 3 seconds
unsigned long lastLogA = 0, lastLogB = 0, lastLogC = 0, lastLogD = 0;
const unsigned long logInterval = 60000;   // Log to sheets every 60 seconds
// Connection tracking
bool wasConnected = false;
bool gsheetReadyOnce = false;
// Function declarations
void resetSlotBooleans(char slot);
void setupSensor(INA226 &sensor, const char* label, uint8_t address);
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
void formatDateTime(const char* fmt, char* result, size_t size);
void logSlotToSheet(const char* sheetId, const String& slotStatus, int battNum, unsigned long elapsedSecs, float voltage, float current, float capacity, bool inCycle = false, const char* operationMode = "None");
void stopOperation(char slot, bool fromForceStop = false);
void calculateSimpsonCapacity(char slot, float current, unsigned long currentTime);
void resetCapacity(char slot);// ================= WEB SERVER ENDPOINTS =================
void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204);
}

void handleStatus() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  StaticJsonDocument<1024> doc;
  
  auto addSlotData = [&](char slot, int bn, unsigned long elapsed, float v, float c, float cap, bool charge, bool discharge, bool cycle, int cycleCnt, int cycleTgt) {
    JsonObject obj = doc.createNestedObject();
    obj["slot"] = String(slot);
    obj["battery_num"] = bn;
    obj["elapsed"] = elapsed;
    obj["voltage"] = v;
    obj["current"] = c;
    obj["capacity"] = cap;
    String mode;
    if (cycle) {
      if (charge) mode = "CYCLE - CHARGING";
      else if (discharge) mode = "CYCLE - DISCHARGING";
      else mode = "CYCLE - IDLE";
    }
    else if (charge) mode = "CHARGING";
    else if (discharge) mode = "DISCHARGING";
    else mode = "IDLE";
    obj["mode"] = mode;
    obj["cycle_current"] = cycleCnt;
    obj["cycle_target"] = cycleTgt;
  };
  
  addSlotData('A', battNumA, elapsedA, voltageA, currentA, capacityA, Acharge, Adischarge, Acycle, cycleCountA, cycleTargetA);
  addSlotData('B', battNumB, elapsedB, voltageB, currentB, capacityB, Bcharge, Bdischarge, Bcycle, cycleCountB, cycleTargetB);
  addSlotData('C', battNumC, elapsedC, voltageC, currentC, capacityC, Ccharge, Cdischarge, Ccycle, cycleCountC, cycleTargetC);
  addSlotData('D', battNumD, elapsedD, voltageD, currentD, capacityD, Dcharge, Ddischarge, Dcycle, cycleCountD, cycleTargetD);
  
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
  
  const char* cmd = doc["command"];
  const char* slot = doc["slot"];
  
  if (cmd && slot) {
    char cmdChar = cmd[0];
    char slotChar = slot[0];
    previousSlot = slotChar;
    
    switch(cmdChar) {
      case '1': // Charge
        updateSlotRelays(slotChar, '1');
        break;
      case '2': // Discharge
        resetCapacity(slotChar);
        startDischargeWithDelay(slotChar);
        break;
      case '3': { // Cycle
        int cycles = doc["cycles"] | 3;
        switch (slotChar) {
          case 'A': cycleTargetA = cycles; cycleCountA = 0; resetCapacity('A'); break;
          case 'B': cycleTargetB = cycles; cycleCountB = 0; resetCapacity('B'); break;
          case 'C': cycleTargetC = cycles; cycleCountC = 0; resetCapacity('C'); break;
          case 'D': cycleTargetD = cycles; cycleCountD = 0; resetCapacity('D'); break;
        }
        startCycle(slotChar);
        break;
      }
      case '4': // Set Battery Number
        if (doc["battery_number"] > 0) {
          saveBattNum(slotChar, doc["battery_number"]);
        }
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
// ========================================================

void setup() {
  Serial.begin(115200);
  Serial.println("System Starting...");
  // Start with fresh battery numbers
  prefs.begin("battNums", false);
  prefs.putInt("A_num", 0);
  prefs.putInt("B_num", 0);
  prefs.putInt("C_num", 0);
  prefs.putInt("D_num", 0);
  battNumA = 0;
  battNumB = 0;
  battNumC = 0;
  battNumD = 0;
  Wire.begin();
  lcd.init();
  lcd.backlight();
  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  connectToWiFi();
  // Load saved battery numbers (if any)
  prefs.begin("cts", false);
  battNumA = prefs.getInt("A_num", 0);
  battNumB = prefs.getInt("B_num", 0);
  battNumC = prefs.getInt("C_num", 0);
  battNumD = prefs.getInt("D_num", 0);
  // Start with cycles turned off
  Acycle = Bcycle = Ccycle = Dcycle = false;
  showHome();
  // Set all relays OFF (HIGH)
  pinMode(AMS, OUTPUT); digitalWrite(AMS, HIGH);
  pinMode(ACD, OUTPUT); digitalWrite(ACD, HIGH);
  pinMode(BMS, OUTPUT); digitalWrite(BMS, HIGH);
  pinMode(BCD, OUTPUT); digitalWrite(BCD, HIGH);
  pinMode(CMS, OUTPUT); digitalWrite(CMS, HIGH);
  pinMode(CCD, OUTPUT); digitalWrite(CCD, HIGH);
  pinMode(DMS, OUTPUT); digitalWrite(DMS, HIGH);
  pinMode(DCD, OUTPUT); digitalWrite(DCD, HIGH);
  // Initialize all power sensors
  setupSensor(inaA, "A", ADDR_A);
  setupSensor(inaB, "B", ADDR_B);
  setupSensor(inaC, "C", ADDR_C);
  setupSensor(inaD, "D", ADDR_D);
  Serial.println("Sensors initialized (A–D active).");
  Serial.print("Loaded batt numbers -- A: "); Serial.print(battNumA);
  Serial.print(" B: "); Serial.print(battNumB);
  Serial.print(" C: "); Serial.print(battNumC);
  Serial.print(" D: "); Serial.println(battNumD);
  // Setup Google Sheets
  configTime(0, 3600, "pool.ntp.org"); // Get current time from internet
  Serial.println("Initializing GSheet client...");
  GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);
  Serial.println("Requesting token...");
  unsigned long start = millis();
  while (!GSheet.ready() && millis() - start < 10000) {
    Serial.print(".");
    delay(500);
  }
  if (GSheet.ready()) {
    Serial.println("\nGSheet Ready!");
    gsheetReadyOnce = true;
  } else {
    Serial.println("\nGSheet not ready after timeout. Will retry on logs.");
    gsheetReadyOnce = false;
  }
  setupWebServer();
  Serial.println("Setup complete.");
}
// ============ SIMPSON'S RULE IMPLEMENTATION ============
void calculateSimpsonCapacity(char slot, float current, unsigned long currentTime) {
  float *capacity;
  float *prevCurrent, *prevPrevCurrent;
  unsigned long *prevTime, *prevPrevTime;
  int *sampleCount;
  bool *tracking;
  
  // Select the appropriate variables based on slot
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
  
  // Track capacity during any active operation (Charge, Discharge, Cycle)
  bool isActive = false;
  switch (slot) {
    case 'A': isActive = (Acharge || Adischarge || Acycle); break;
    case 'B': isActive = (Bcharge || Bdischarge || Bcycle); break;
    case 'C': isActive = (Ccharge || Cdischarge || Ccycle); break;
    case 'D': isActive = (Dcharge || Ddischarge || Dcycle); break;
  }
  
  // Start tracking when operation begins
  if (isActive && !*tracking) {
    *tracking = true;
    *sampleCount = 0;
    *capacity = 0;
    *prevTime = 0;
    *prevPrevTime = 0;
    Serial.print("Slot "); Serial.print(slot); Serial.println(" Capacity tracking started");
  }
  
  // Stop tracking when operation ends
  if (!isActive && *tracking) {
    *tracking = false;
    Serial.print("Slot "); Serial.print(slot); 
    Serial.print(" Capacity tracking ended. Total: "); Serial.print(*capacity, 4); Serial.println(" mAh");
  }
  
  // If not tracking, exit
  if (!*tracking) return;
  
  // First sample
  if (*sampleCount == 0) {
    *prevPrevTime = currentTime;
    *prevPrevCurrent = current;
    *sampleCount = 1;
    return;
  }
  
  // Second sample
  if (*sampleCount == 1) {
    *prevTime = currentTime;
    *prevCurrent = current;
    *sampleCount = 2;
    return;
  }
  
  // Third sample: apply non-overlapping Simpson's 1/3 rule
  float dt_seconds = (currentTime - *prevPrevTime) / 1000.0;
  float segment_mAs = (dt_seconds / 6.0) * (*prevPrevCurrent + 4.0 * (*prevCurrent) + current);
  float segment_mAh = segment_mAs / 3600.0;
  
  *capacity += segment_mAh;
  
  // Strict non-overlapping shift: current endpoint becomes next start point
  *prevPrevTime = currentTime;
  *prevPrevCurrent = current;
  *sampleCount = 1; // Immediately look for the next mid-point
  
  if (millis() % 10000 < 3000) {
    Serial.print("Slot "); Serial.print(slot);
    Serial.print(" Capacity: "); Serial.print(*capacity, 4); Serial.println(" mAh");
  }
}
void resetCapacity(char slot) {
  switch (slot) {
    case 'A': 
      capacityA = 0; 
      sampleCountA = 0;
      capacityTrackingA = false;
      break;
    case 'B': 
      capacityB = 0; 
      sampleCountB = 0;
      capacityTrackingB = false;
      break;
    case 'C': 
      capacityC = 0; 
      sampleCountC = 0;
      capacityTrackingC = false;
      break;
    case 'D': 
      capacityD = 0; 
      sampleCountD = 0;
      capacityTrackingD = false;
      break;
  }
}
// =======================================================
void resetSlotBooleans(char slot) {
  switch (slot) {
    case 'A':
      Acharge = false;
      Adischarge = false;
      Acycle = false;
      resetCapacity('A');
      break;
    case 'B':
      Bcharge = false;
      Bdischarge = false;
      Bcycle = false;
      resetCapacity('B');
      break;
    case 'C':
      Ccharge = false;
      Cdischarge = false;
      Ccycle = false;
      resetCapacity('C');
      break;
    case 'D':
      Dcharge = false;
      Ddischarge = false;
      Dcycle = false;
      resetCapacity('D');
      break;
  }
}
void loop() {
  server.handleClient();
  char key = keypad.getKey();
  unsigned long now = millis();
  // Keep WiFi connected
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }
  // Show WiFi status every 3 seconds
  if (millis() - previousStatusTime >= statusInterval) {
    previousStatusTime = millis();
    Serial.print("Wi-Fi: ");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected");
    } else {
      Serial.println("Not Connected");
    }
    // Update home screen with WiFi status
    if (previousSlot == '\0' && !inSettingScreen && !inStopPrompt && !stopMessageActive && !inBattNumberInput && !inCycleInput && !confirmingCycle) {
      lcd.setCursor(0,3);
      if (WiFi.status() == WL_CONNECTED) lcd.print("  Wi-Fi: Connected  ");
      else lcd.print("Wi-Fi: Not Connected");
    }
  }
  // Read all sensors every 3 seconds
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
  // Handle the "Stopped" message timing
  if (stopMessageActive) {
    if (millis() - stopMessageStart >= stopMessageDuration) {
        stopMessageActive = false;
        // Refresh the display with current readings
        switch (previousSlot) {
          case 'A': readSensor(inaA, 'A'); break;
          case 'B': readSensor(inaB, 'B'); break;
          case 'C': readSensor(inaC, 'C'); break;
          case 'D': readSensor(inaD, 'D'); break;
        }
        showSlot(previousSlot);
    }
  }
  // Check if any waiting discharges should start
  processDeferredDischarge();
  // Track WiFi connection changes
  if (WiFi.status() != WL_CONNECTED) {
    if (wasConnected) Serial.println("WiFi LOST!");
    wasConnected = false;
  } else {
    if (!wasConnected) Serial.println("WiFi Connected!");
    wasConnected = true;
  }
  // Keep trying to get GSheet ready if not already
  if (!gsheetReadyOnce && (now % 30000UL) < 200) {
    if (GSheet.ready()) {
      Serial.println("GSheet Ready!");
      gsheetReadyOnce = true;
    }
  }
  // LOGGING SECTION - Send data to Google Sheets
  // Force one log when discharge starts
  if (AdischargePending && battNumA > 0) {
    logSlotToSheet(SHEET_ID_A, statusA, battNumA, elapsedA, voltageA, currentA, capacityA, Acycle, Adischarge ? "Discharge" : (Acharge ? "Charge" : "None"));
    Serial.println("Force Log: Slot A");
    AdischargePending = false;
  }
  if (BdischargePending && battNumB > 0) {
    logSlotToSheet(SHEET_ID_B, statusB, battNumB, elapsedB, voltageB, currentB, capacityB, Bcycle, Bdischarge ? "Discharge" : (Bcharge ? "Charge" : "None"));
    Serial.println("Force Log: Slot B");
    BdischargePending = false;
  }
  if (CdischargePending && battNumC > 0) {
    logSlotToSheet(SHEET_ID_C, statusC, battNumC, elapsedC, voltageC, currentC, capacityC, Ccycle, Cdischarge ? "Discharge" : (Ccharge ? "Charge" : "None"));
    Serial.println("Force Log: Slot C");
    CdischargePending = false;
  }
  if (DdischargePending && battNumD > 0) {
    logSlotToSheet(SHEET_ID_D, statusD, battNumD, elapsedD, voltageD, currentD, capacityD, Dcycle, Ddischarge ? "Discharge" : (Dcharge ? "Charge" : "None"));
    Serial.println("Force Log: Slot D");
    DdischargePending = false;
  }
  // Regular interval logging every 60 seconds when discharging
  // Slot A
  bool Aactive = (statusA == "Discharging" && battNumA > 0);
  static bool lastAactive = false;
  if (Aactive && !lastAactive) delay(1000);
  lastAactive = Aactive;
  if (Aactive && (now - lastLogA >= logInterval)) {
    lastLogA = now;
    bool present = (voltageA > 2.0);
    if (Aactive && !lastAactive) {
        elapsedA = 0;
        lastLogA = now;
    }
    if (!present) elapsedA = 0;
    if (GSheet.ready()) {
        logSlotToSheet(SHEET_ID_A, statusA, battNumA, elapsedA, voltageA, currentA, capacityA, Acycle, Adischarge ? "Discharge" : (Acharge ? "Charge" : "None"));
    }
    elapsedA += 60;
  }
  // Slot B
  bool Bactive = (statusB == "Discharging" && battNumB > 0);
  static bool lastBactive = false;
  if (Bactive && !lastBactive) delay(1000);
  lastBactive = Bactive;
  if (Bactive && (now - lastLogB >= logInterval)) {
    lastLogB = now;
    bool present = (voltageB > 2.0);
    if (Bactive && !lastBactive) {
        elapsedB = 0;
        lastLogB = now;
    }
    if (!present) elapsedB = 0;
    if (GSheet.ready()) {
        logSlotToSheet(SHEET_ID_B, statusB, battNumB, elapsedB, voltageB, currentB, capacityB, Bcycle, Bdischarge ? "Discharge" : (Bcharge ? "Charge" : "None"));
    }
    elapsedB += 60;
  }
  // Slot C
  bool Cactive = (statusC == "Discharging" && battNumC > 0);
  static bool lastCactive = false;
  if (Cactive && !lastCactive) delay(1000);
  lastCactive = Cactive;
  if (Cactive && (now - lastLogC >= logInterval)) {
    lastLogC = now;
    bool present = (voltageC > 2.0);
    if (Cactive && !lastCactive) {
        elapsedC = 0;
        lastLogC = now;
    }
    if (!present) elapsedC = 0;
    if (GSheet.ready()) {
        logSlotToSheet(SHEET_ID_C, statusC, battNumC, elapsedC, voltageC, currentC, capacityC, Ccycle, Cdischarge ? "Discharge" : (Ccharge ? "Charge" : "None"));
    }
    elapsedC += 60;
  }
  // Slot D
  bool Dactive = (statusD == "Discharging" && battNumD > 0);
  static bool lastDactive = false;
  if (Dactive && !lastDactive) delay(1000);
  lastDactive = Dactive;
  if (Dactive && (now - lastLogD >= logInterval)) {
    lastLogD = now;
    bool present = (voltageD > 2.0);
    if (Dactive && !lastDactive) {
        elapsedD = 0;
        lastLogD = now;
    }
    if (!present) elapsedD = 0;
    if (GSheet.ready()) {
        logSlotToSheet(SHEET_ID_D, statusD, battNumD, elapsedD, voltageD, currentD, capacityD, Dcycle, Ddischarge ? "Discharge" : (Dcharge ? "Charge" : "None"));
    }
    elapsedD += 60;
  }
  // If no key pressed, we're done
  if (!key) return;
  Serial.print("Key pressed: "); Serial.println(key);
  // Handle battery number input mode
  if (inBattNumberInput) {
    handleBattNumberInputKey(key);
    return;
  }
  // Handle cycle input mode
  if (inCycleInput) {
    handleCycleInputKey(key);
    return;
  }
  // Handle cycle confirmation
  if (confirmingCycle) {
    if (key == '#') {
      // User confirmed - now actually start the cycle
      int value = pendingCycleValue;
      
      // Store target and reset count
      switch (cycleSlot) {
        case 'A': 
          cycleTargetA = value; 
          cycleCountA = 0; 
          resetCapacity('A');
          Serial.print("Slot A cycle target set to: "); Serial.println(value);
          break;
        case 'B': 
          cycleTargetB = value; 
          cycleCountB = 0;
          resetCapacity('B');
          Serial.print("Slot B cycle target set to: "); Serial.println(value);
          break;
        case 'C': 
          cycleTargetC = value; 
          cycleCountC = 0;
          resetCapacity('C');
          Serial.print("Slot C cycle target set to: "); Serial.println(value);
          break;
        case 'D': 
          cycleTargetD = value; 
          cycleCountD = 0;
          resetCapacity('D');
          Serial.print("Slot D cycle target set to: "); Serial.println(value);
          break;
      }
      
      // Show starting message
      lcd.clear();
      lcd.setCursor(0,1);
      lcd.print("Starting Cycle Test");
      lcd.setCursor(0,2);
      lcd.print("Slot ");
      lcd.print(cycleSlot);
      lcd.print(" - ");
      lcd.print(value);
      lcd.print(" cycles");
      lcd.setCursor(0,3);
      lcd.print("Ends on CHARGE");
      delay(2000);
      
      // Start cycle now that target is set
      startCycle(cycleSlot);
      
      // Log the start event
      char sheetId[50]; 
      float v=0, c=0, cap=0; 
      int battNum = 0;
      
      if (cycleSlot=='A') { 
        strcpy(sheetId,SHEET_ID_A); 
        v=voltageA; 
        c=currentA; 
        cap=capacityA;
        elapsedA=0;
        battNum = battNumA;
      }
      else if (cycleSlot=='B') { 
        strcpy(sheetId,SHEET_ID_B); 
        v=voltageB; 
        c=currentB; 
        cap=capacityB;
        elapsedB=0;
        battNum = battNumB;
      }
      else if (cycleSlot=='C') { 
        strcpy(sheetId,SHEET_ID_C); 
        v=voltageC; 
        c=currentC; 
        cap=capacityC;
        elapsedC=0;
        battNum = battNumC;
      }
      else if (cycleSlot=='D') { 
        strcpy(sheetId,SHEET_ID_D); 
        v=voltageD; 
        c=currentD; 
        cap=capacityD;
        elapsedD=0;
        battNum = battNumD;
      }
      
      if (battNum > 0) {
        logSlotToSheet(sheetId, "Cycle Start", battNum, 0, v, c, cap, true, "Charge");
      }
      
      // Clear state and proceed
      confirmingCycle = false;
      cycleSlot = '\0';
      cycleBuffer = "";
      pendingCycleValue = 0;
      
      // Go back to slot display
      if (previousSlot != '\0') {
        showSlot(previousSlot);
      }
      return;
    }
    else if (key == '*') {
      // User canceled
      lcd.clear();
      lcd.setCursor(0,1);
      lcd.print("Cycle test CANCELLED");
      delay(1500);
      
      confirmingCycle = false;
      cycleSlot = '\0';
      cycleBuffer = "";
      pendingCycleValue = 0;
      
      // Go back to settings menu
      showSlotSetting(previousSlot);
      inSettingScreen = true;
      return;
    }
    return; // Ignore other keys during confirmation
  }
  // Handle stop prompt (from '0' key)
  if (inStopPrompt) {
    if (key == '0') { // User confirmed stop with 0 key
      stopOperation(previousSlot, true); // true = from force stop
      return;
    } else if (key == '5') { // User canceled stop
      inStopPrompt = false;
      stopMessageActive = false;
      refreshSlotDisplay();
      return;
    }
    return;
  }
  // Handle settings menu
  if (inSettingScreen) {
    if (key == '1') { // Charge only
      if (battNumForSlot(previousSlot) > 0) {
        // Battery number exists, start charge
        updateSlotRelays(previousSlot, '1');
        
        // Log to Google Sheets when charge starts
        char sheetId[50];
        float v = 0, c = 0, cap = 0;
        String status = "Charging";
        if (previousSlot == 'A') { strcpy(sheetId, SHEET_ID_A); v = voltageA; c = currentA; cap = capacityA; elapsedA = 0; }
        else if (previousSlot == 'B') { strcpy(sheetId, SHEET_ID_B); v = voltageB; c = currentB; cap = capacityB; elapsedB = 0; }
        else if (previousSlot == 'C') { strcpy(sheetId, SHEET_ID_C); v = voltageC; c = currentC; cap = capacityC; elapsedC = 0; }
        else if (previousSlot == 'D') { strcpy(sheetId, SHEET_ID_D); v = voltageD; c = currentD; cap = capacityD; elapsedD = 0; }
        logSlotToSheet(sheetId, status, battNumForSlot(previousSlot), 0, v, c, cap, false, "Charge");
        
        inSettingScreen = false;
        refreshSlotDisplay();
      } else {
        // No battery number, show error
        lcd.clear();
        lcd.setCursor(0,1);
        lcd.print(" Set batt# first!");
        delay(1500);
        showSlotSetting(previousSlot);
      }
      return;
    }
    else if (key == '2') { // Discharge only
      if (battNumForSlot(previousSlot) > 0) {
        // Battery number exists, start discharge
        resetCapacity(previousSlot);
        startDischargeWithDelay(previousSlot);
        
        // Log to Google Sheets when discharge starts
        char sheetId[50];
        float v = 0, c = 0, cap = 0;
        String status = "Discharging";
        if (previousSlot == 'A') { strcpy(sheetId, SHEET_ID_A); v = voltageA; c = currentA; cap = capacityA; }
        else if (previousSlot == 'B') { strcpy(sheetId, SHEET_ID_B); v = voltageB; c = currentB; cap = capacityB; }
        else if (previousSlot == 'C') { strcpy(sheetId, SHEET_ID_C); v = voltageC; c = currentC; cap = capacityC; }
        else if (previousSlot == 'D') { strcpy(sheetId, SHEET_ID_D); v = voltageD; c = currentD; cap = capacityD; }
        logSlotToSheet(sheetId, status, battNumForSlot(previousSlot), 0, v, c, cap, false, "Discharge");
        
        inSettingScreen = false;
      } else {
        // No battery number, show error
        lcd.clear();
        lcd.setCursor(0,1);
        lcd.print(" Set batt# first!");
        delay(1500);
        showSlotSetting(previousSlot);
      }
      return;
    }
    else if (key == '3') { // Cycle test — ask for number of cycles first
      if (battNumForSlot(previousSlot) > 0) {
        // Battery number exists, prompt user to enter target cycle count
        Serial.println("Cycle option selected - showing input screen");
        showCycleInput(previousSlot);
        inSettingScreen = false;
      } else {
        // No battery number, show error
        lcd.clear();
        lcd.setCursor(0,1);
        lcd.print(" Set batt# first!");
        delay(1500);
        showSlotSetting(previousSlot);
      }
      return;
    }
    else if (key == '4') { // Set battery number
      showBattNumberInput(previousSlot);
      inBattNumberInput = true;
      inSettingScreen = false;
      return;
    }
    else if (key == '5') return;
  }
  // Main menu navigation
  switch (key) {
    case '0': // Stop current operation (with confirmation)
      if (previousSlot != '\0') {
        // Check if any operation is active
        bool isActive = false;
        switch (previousSlot) {
          case 'A': isActive = (Acharge || Adischarge || Acycle); break;
          case 'B': isActive = (Bcharge || Bdischarge || Bcycle); break;
          case 'C': isActive = (Ccharge || Cdischarge || Ccycle); break;
          case 'D': isActive = (Dcharge || Ddischarge || Dcycle); break;
        }
        
        if (isActive) {
          showForceStopPrompt(previousSlot);
          inStopPrompt = true;
        } else {
          // No active operation, show message
          lcd.clear();
          lcd.setCursor(0,1);
          lcd.print("No active operation");
          lcd.setCursor(0,2);
          lcd.print("in Slot ");
          lcd.print(previousSlot);
          delay(1500);
          showSlot(previousSlot);
        }
      }
      break;
    case '#': // Return to home screen
      showHome();
      previousSlot = '\0';
      inSettingScreen = false;
      inStopPrompt = false;
      inBattNumberInput = false;
      inCycleInput = false;
      confirmingCycle = false;
      break;
    case 'A': showSlot('A'); previousSlot='A'; break;
    case 'B': showSlot('B'); previousSlot='B'; break;
    case 'C': showSlot('C'); previousSlot='C'; break;
    case 'D': showSlot('D'); previousSlot='D'; break;
    case '*': 
      if (previousSlot != '\0') {
        showSlotSetting(previousSlot); 
        inSettingScreen=true;
      }
      break;
  }
}
// Helper to get battery number for a slot
int battNumForSlot(char slot) {
  switch (slot) {
    case 'A': return battNumA;
    case 'B': return battNumB;
    case 'C': return battNumC;
    case 'D': return battNumD;
    default: return 0;
  }
}
// Start discharge with delay
void startDischargeWithDelay(char slot) {
  switch (slot) {
    case 'A':
      Acycle = false; Acharge = false;
      elapsedA = 0;
      AdischargePending = true;
      AdischargeWait = true;
      AdischargeStart = millis();
      break;
    case 'B':
      Bcycle = false; Bcharge = false;
      elapsedB = 0;
      BdischargePending = true;
      BdischargeWait = true;
      BdischargeStart = millis();
      break;
    case 'C':
      Ccycle = false; Ccharge = false;
      elapsedC = 0;
      CdischargePending = true;
      CdischargeWait = true;
      CdischargeStart = millis();
      break;
    case 'D':
      Dcycle = false; Dcharge = false;
      elapsedD = 0;
      DdischargePending = true;
      DdischargeWait = true;
      DdischargeStart = millis();
      break;
  }
}
// Start cycle test
void startCycle(char slot) {
  switch (slot) {
    case 'A':
      Acycle = true;
      cycleCountA = 0;
      resetCapacity('A');
      // Start with charge first
      Acharge = true;
      digitalWrite(AMS, LOW);
      digitalWrite(ACD, HIGH);
      Serial.print("Slot A cycle started - Charging (");
      Serial.print(cycleTargetA);
      Serial.println(" cycles total, will end on charge)");
      break;
    case 'B':
      Bcycle = true;
      cycleCountB = 0;
      resetCapacity('B');
      Bcharge = true;
      digitalWrite(BMS, LOW);
      digitalWrite(BCD, HIGH);
      Serial.print("Slot B cycle started - Charging (");
      Serial.print(cycleTargetB);
      Serial.println(" cycles total, will end on charge)");
      break;
    case 'C':
      Ccycle = true;
      cycleCountC = 0;
      resetCapacity('C');
      Ccharge = true;
      digitalWrite(CMS, LOW);
      digitalWrite(CCD, HIGH);
      Serial.print("Slot C cycle started - Charging (");
      Serial.print(cycleTargetC);
      Serial.println(" cycles total, will end on charge)");
      break;
    case 'D':
      Dcycle = true;
      cycleCountD = 0;
      resetCapacity('D');
      Dcharge = true;
      digitalWrite(DMS, LOW);
      digitalWrite(DCD, HIGH);
      Serial.print("Slot D cycle started - Charging (");
      Serial.print(cycleTargetD);
      Serial.println(" cycles total, will end on charge)");
      break;
  }
}
// Initialize a power sensor
void setupSensor(INA226 &sensor, const char* label, uint8_t address) {
  if (!sensor.begin(address)) {
    Serial.print("Sensor "); Serial.print(label); Serial.println(" not detected! (check wiring)");
    return;
  }
  // Settings: single sample, 1.1ms conversion, continuous mode
  sensor.configure(INA226_AVERAGES_1, INA226_BUS_CONV_TIME_1100US, INA226_SHUNT_CONV_TIME_1100US, INA226_MODE_SHUNT_BUS_CONT);
  // Calibrate for 0.01 ohm shunt and 5A max current
  sensor.calibrate(0.01, 5.0);
  Serial.print("Sensor "); Serial.print(label);
  Serial.println(" initialized.");
}
// Read a sensor and update the corresponding slot's data
void readSensor(INA226 &sensor, char label) {
  float v, c;
  // Apply calibration based on current mode
  switch(label) {
    case 'A':
      if (Acharge) v = sensor.readBusVoltage() * voltageCalAcharge;
      else if (Adischarge) v = sensor.readBusVoltage() * voltageCalAdischarge;
      else v = sensor.readBusVoltage() * voltageCalA;
      c = sensor.readShuntCurrent() * currentCalA;
      break;
    case 'B':
      if (Bcharge) v = sensor.readBusVoltage() * voltageCalBcharge;
      else if (Bdischarge) v = sensor.readBusVoltage() * voltageCalBdischarge;
      else v = sensor.readBusVoltage() * voltageCalB;
      c = sensor.readShuntCurrent() * currentCalB;
      break;
    case 'C':
      if (Ccharge) v = sensor.readBusVoltage() * voltageCalCcharge;
      else if (Cdischarge) v = sensor.readBusVoltage() * voltageCalCdischarge;
      else v = sensor.readBusVoltage() * voltageCalC;
      c = sensor.readShuntCurrent() * currentCalC;
      break;
    case 'D':
      if (Dcharge) v = sensor.readBusVoltage() * voltageCalDcharge;
      else if (Ddischarge) v = sensor.readBusVoltage() * voltageCalDdischarge;
      else v = sensor.readBusVoltage() * voltageCalD;
      c = sensor.readShuntCurrent() * currentCalD;
      break;
    default:
      v = 0; c = 0;
      break;
  }
  // Get battery number for this slot
  int bn = 0;
  if (label == 'A') bn = battNumA;
  else if (label == 'B') bn = battNumB;
  else if (label == 'C') bn = battNumC;
  else if (label == 'D') bn = battNumD;
  // Clean up readings - ignore very low voltages and currents
  if (v < 2.0) v = 0.0;
  if (c > -5.0 && c < 5.0) c = 0.0;
  // Determine status based on voltage and current
  String status;
  if (v == 0.0 && c == 0.0) status = "No Battery";
  else if (v > 0.0 && c == 0.0) status = "Standby";
  else if (v > 0.0 && c > 0.0) status = "Discharging";
  else if (v > 0.0 && c < 0.0) status = "Charging";
  else status = "Unknown";
  // Print to serial monitor for debugging
  Serial.print("Slot "); Serial.print(label);
  Serial.print(" | Status = "); Serial.print(status);
  Serial.print(" | Batt No = "); Serial.print(bn);
  Serial.print(" | Voltage = "); Serial.print(v, 2); Serial.print(" V");
  Serial.print(" | Current = "); Serial.print(c, 0); Serial.println(" mA");
  // Store the readings
  switch (label) {
    case 'A': voltageA=v; currentA=abs(c); statusA=status; checkVoltageLimit('A'); break;
    case 'B': voltageB=v; currentB=abs(c); statusB=status; checkVoltageLimit('B'); break;
    case 'C': voltageC=v; currentC=abs(c); statusC=status; checkVoltageLimit('C'); break;
    case 'D': voltageD=v; currentD=abs(c); statusD=status; checkVoltageLimit('D'); break;
  }
  // Calculate capacity using Simpson's rule
  calculateSimpsonCapacity(label, abs(c), millis());
  // Update display if this slot is currently selected
  if (previousSlot == label && !inSettingScreen && !inStopPrompt && !stopMessageActive && !inBattNumberInput && !inCycleInput && !confirmingCycle) {
    showSlot(label);
  }
}
// Check if voltage limits have been reached (handles cycling)
void checkVoltageLimit(char slot) {
  switch (slot) {
    case 'A': {
      // charging complete
      if (Acharge && (voltageA > CHARGE_STOP_V || voltageA < 1 || currentA < 50)) {
        Acharge = false;
        if (Acycle) {
          // Check if this was the last cycle
          if (cycleCountA >= cycleTargetA) {
            // This was the last charge cycle - stop here
            Acycle = false;
            resetSlotRelays('A');
            Serial.print("Slot A cycle COMPLETE - Stopped after charge cycle ");
            Serial.println(cycleTargetA);
            
            // Log completion
            logSlotToSheet(SHEET_ID_A, "Cycle Complete", battNumA, elapsedA, voltageA, currentA, capacityA, false, "Complete");
            
            // Show completion on LCD if this slot is selected
            if (previousSlot == 'A') {
              lcd.clear();
              lcd.setCursor(0,1);
              lcd.print("Slot A Cycle Done!");
              lcd.setCursor(0,2);
              lcd.print(cycleTargetA);
              lcd.print(" cycles completed");
              delay(2000);
              showSlot('A');
            }
          } else {
            // Start discharge phase of cycle
            resetCapacity('A');
            Adischarge = true;
            digitalWrite(AMS, LOW);
            digitalWrite(ACD, LOW);
            Serial.print("Slot A cycle progress: Charge ");
            Serial.print(cycleCountA + 1);
            Serial.print("/");
            Serial.print(cycleTargetA);
            Serial.println(" complete - Starting Discharge");
          }
        } else {
          resetSlotRelays('A');
        }
        Serial.println("Slot A charge completed.");
      }
      // discharging complete (including 0V/0mA detection)
      if (Adischarge && (voltageA <= DISCHARGE_STOP_V || (voltageA < 1 && currentA == 0.0))) {
        // Log the final capacity before ending discharge
        if (battNumA > 0) {
          logSlotToSheet(SHEET_ID_A, "Discharge Complete", battNumA, elapsedA, voltageA, currentA, capacityA, Acycle, "Complete");
          Serial.print("Slot A final capacity: "); Serial.print(capacityA, 3); Serial.println(" Ah");
        }
        
        Adischarge = false;
        AdischargePending = false;
        if (Acycle) {
          cycleCountA++;
          // Always go back to charging
          resetCapacity('A');
          Acharge = true;
          digitalWrite(AMS, LOW);
          digitalWrite(ACD, HIGH);
          Serial.print("Slot A discharge ");
          Serial.print(cycleCountA);
          Serial.print("/");
          Serial.print(cycleTargetA);
          Serial.println(" complete - Starting Charge");
        } else {
          resetSlotRelays('A');
        }
        Serial.println("Slot A discharge completed.");
      }
    } break;
    case 'B': {
      if (Bcharge && (voltageB > CHARGE_STOP_V || voltageB < 1 || currentB < 50)) {
        Bcharge = false;
        if (Bcycle) {
          // Check if this was the last cycle
          if (cycleCountB >= cycleTargetB) {
            // This was the last charge cycle - stop here
            Bcycle = false;
            resetSlotRelays('B');
            Serial.print("Slot B cycle COMPLETE - Stopped after charge cycle ");
            Serial.println(cycleTargetB);
            
            // Log completion
            logSlotToSheet(SHEET_ID_B, "Cycle Complete", battNumB, elapsedB, voltageB, currentB, capacityB, false, "Complete");
            
            // Show completion on LCD if this slot is selected
            if (previousSlot == 'B') {
              lcd.clear();
              lcd.setCursor(0,1);
              lcd.print("Slot B Cycle Done!");
              lcd.setCursor(0,2);
              lcd.print(cycleTargetB);
              lcd.print(" cycles completed");
              delay(2000);
              showSlot('B');
            }
          } else {
            // Start discharge phase of cycle
            resetCapacity('B');
            Bdischarge = true;
            digitalWrite(BMS, LOW);
            digitalWrite(BCD, LOW);
            Serial.print("Slot B cycle progress: Charge ");
            Serial.print(cycleCountB + 1);
            Serial.print("/");
            Serial.print(cycleTargetB);
            Serial.println(" complete - Starting Discharge");
          }
        } else {
          resetSlotRelays('B');
        }
        Serial.println("Slot B charge completed.");
      }
      if (Bdischarge && (voltageB <= DISCHARGE_STOP_V || (voltageB < 1 && currentB == 0.0))) {
        // Log the final capacity before ending discharge
        if (battNumB > 0) {
          logSlotToSheet(SHEET_ID_B, "Discharge Complete", battNumB, elapsedB, voltageB, currentB, capacityB, Bcycle, "Complete");
          Serial.print("Slot B final capacity: "); Serial.print(capacityB, 3); Serial.println(" Ah");
        }
        
        Bdischarge = false;
        BdischargePending = false;
        if (Bcycle) {
          cycleCountB++;
          // Always go back to charging
          resetCapacity('B');
          Bcharge = true;
          digitalWrite(BMS, LOW);
          digitalWrite(BCD, HIGH);
          Serial.print("Slot B discharge ");
          Serial.print(cycleCountB);
          Serial.print("/");
          Serial.print(cycleTargetB);
          Serial.println(" complete - Starting Charge");
        } else {
          resetSlotRelays('B');
        }
        Serial.println("Slot B discharge completed.");
      }
    } break;
    case 'C': {
      if (Ccharge && (voltageC > CHARGE_STOP_V || voltageC < 1 || currentC < 50)) {
        Ccharge = false;
        if (Ccycle) {
          // Check if this was the last cycle
          if (cycleCountC >= cycleTargetC) {
            // This was the last charge cycle - stop here
            Ccycle = false;
            resetSlotRelays('C');
            Serial.print("Slot C cycle COMPLETE - Stopped after charge cycle ");
            Serial.println(cycleTargetC);
            
            // Log completion
            logSlotToSheet(SHEET_ID_C, "Cycle Complete", battNumC, elapsedC, voltageC, currentC, capacityC, false, "Complete");
            
            // Show completion on LCD if this slot is selected
            if (previousSlot == 'C') {
              lcd.clear();
              lcd.setCursor(0,1);
              lcd.print("Slot C Cycle Done!");
              lcd.setCursor(0,2);
              lcd.print(cycleTargetC);
              lcd.print(" cycles completed");
              delay(2000);
              showSlot('C');
            }
          } else {
            // Start discharge phase of cycle
            resetCapacity('C');
            Cdischarge = true;
            digitalWrite(CMS, LOW);
            digitalWrite(CCD, LOW);
            Serial.print("Slot C cycle progress: Charge ");
            Serial.print(cycleCountC + 1);
            Serial.print("/");
            Serial.print(cycleTargetC);
            Serial.println(" complete - Starting Discharge");
          }
        } else {
          resetSlotRelays('C');
        }
        Serial.println("Slot C charge completed.");
      }
      if (Cdischarge && (voltageC <= DISCHARGE_STOP_V || (voltageC < 1 && currentC == 0.0))) {
        // Log the final capacity before ending discharge
        if (battNumC > 0) {
          logSlotToSheet(SHEET_ID_C, "Discharge Complete", battNumC, elapsedC, voltageC, currentC, capacityC, Ccycle, "Complete");
          Serial.print("Slot C final capacity: "); Serial.print(capacityC, 3); Serial.println(" Ah");
        }
        
        Cdischarge = false;
        CdischargePending = false;
        if (Ccycle) {
          cycleCountC++;
          // Always go back to charging
          resetCapacity('C');
          Ccharge = true;
          digitalWrite(CMS, LOW);
          digitalWrite(CCD, HIGH);
          Serial.print("Slot C discharge ");
          Serial.print(cycleCountC);
          Serial.print("/");
          Serial.print(cycleTargetC);
          Serial.println(" complete - Starting Charge");
        } else {
          resetSlotRelays('C');
        }
        Serial.println("Slot C discharge completed.");
      }
    } break;
    case 'D': {
      if (Dcharge && (voltageD > CHARGE_STOP_V || voltageD < 1 || currentD < 50)) {
        Dcharge = false;
        if (Dcycle) {
          // Check if this was the last cycle
          if (cycleCountD >= cycleTargetD) {
            // This was the last charge cycle - stop here
            Dcycle = false;
            resetSlotRelays('D');
            Serial.print("Slot D cycle COMPLETE - Stopped after charge cycle ");
            Serial.println(cycleTargetD);
            
            // Log completion
            logSlotToSheet(SHEET_ID_D, "Cycle Complete", battNumD, elapsedD, voltageD, currentD, capacityD, false, "Complete");
            
            // Show completion on LCD if this slot is selected
            if (previousSlot == 'D') {
              lcd.clear();
              lcd.setCursor(0,1);
              lcd.print("Slot D Cycle Done!");
              lcd.setCursor(0,2);
              lcd.print(cycleTargetD);
              lcd.print(" cycles completed");
              delay(2000);
              showSlot('D');
            }
          } else {
            // Start discharge phase of cycle
            resetCapacity('D');
            Ddischarge = true;
            digitalWrite(DMS, LOW);
            digitalWrite(DCD, LOW);
            Serial.print("Slot D cycle progress: Charge ");
            Serial.print(cycleCountD + 1);
            Serial.print("/");
            Serial.print(cycleTargetD);
            Serial.println(" complete - Starting Discharge");
          }
        } else {
          resetSlotRelays('D');
        }
        Serial.println("Slot D charge completed.");
      }
      if (Ddischarge && (voltageD <= DISCHARGE_STOP_V || (voltageD < 1 && currentD == 0.0))) {
        // Log the final capacity before ending discharge
        if (battNumD > 0) {
          logSlotToSheet(SHEET_ID_D, "Discharge Complete", battNumD, elapsedD, voltageD, currentD, capacityD, Dcycle, "Complete");
          Serial.print("Slot D final capacity: "); Serial.print(capacityD, 3); Serial.println(" Ah");
        }
        
        Ddischarge = false;
        DdischargePending = false;
        if (Dcycle) {
          cycleCountD++;
          // Always go back to charging
          resetCapacity('D');
          Dcharge = true;
          digitalWrite(DMS, LOW);
          digitalWrite(DCD, HIGH);
          Serial.print("Slot D discharge ");
          Serial.print(cycleCountD);
          Serial.print("/");
          Serial.print(cycleTargetD);
          Serial.println(" complete - Starting Charge");
        } else {
          resetSlotRelays('D');
        }
        Serial.println("Slot D discharge completed.");
      }
    } break;
  }
}
// Turn on relays for the selected mode
void updateSlotRelays(char slot, char action) {
  switch (slot) {
    case 'A':
      Acycle = false;
      Acharge = (action == '1');
      Adischarge = (action == '2');
      if (Acharge){digitalWrite(AMS,LOW);digitalWrite(ACD,HIGH);}
      else if(Adischarge){digitalWrite(AMS,LOW);digitalWrite(ACD,LOW);}
      break;
    case 'B':
      Bcycle = false;
      Bcharge = (action == '1');
      Bdischarge = (action == '2');
      if (Bcharge){digitalWrite(BMS,LOW);digitalWrite(BCD,HIGH);}
      else if(Bdischarge){digitalWrite(BMS,LOW);digitalWrite(BCD,LOW);}
      break;
    case 'C':
      Ccycle = false;
      Ccharge = (action == '1');
      Cdischarge = (action == '2');
      if (Ccharge){digitalWrite(CMS,LOW);digitalWrite(CCD,HIGH);}
      else if(Cdischarge){digitalWrite(CMS,LOW);digitalWrite(CCD,LOW);}
      break;
    case 'D':
      Dcycle = false;
      Dcharge = (action == '1');
      Ddischarge = (action == '2');
      if (Dcharge){digitalWrite(DMS,LOW);digitalWrite(DCD,HIGH);}
      else if(Ddischarge){digitalWrite(DMS,LOW);digitalWrite(DCD,LOW);}
      break;
  }
}
// Turn all relays OFF for a slot
void resetSlotRelays(char slot) {
  switch (slot) {
    case 'A':
      digitalWrite(AMS,HIGH);digitalWrite(ACD,HIGH);
      break;
    case 'B':
      digitalWrite(BMS,HIGH);digitalWrite(BCD,HIGH);
      break;
    case 'C':
      digitalWrite(CMS,HIGH);digitalWrite(CCD,HIGH);
      break;
    case 'D':
      digitalWrite(DMS,HIGH);digitalWrite(DCD,HIGH);
      break;
  }
}
// Helper to center text on LCD
void printCentered(const char* text, int row) {
  int len = strlen(text);
  int col = (20 - len) / 2;
  if (col < 0) col = 0;
  lcd.setCursor(col, row);
  lcd.print(text);
}
// Show the home screen
void showHome() {
  lcd.clear();
  printCentered("Capacity Testing", 0);
  printCentered("Station (CTS) V1.0", 1);
  lcd.setCursor(0,3);
  if (WiFi.status() == WL_CONNECTED) lcd.print("  Wi-Fi: Connected  ");
  else lcd.print("Wi-Fi: Not Connected");
}
// Show detailed info for a specific slot
void showSlot(char slot) {
  // Clear any input modes
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
  String mode = "";
  String cycleInfo = "";
  switch (slot) {
    case 'A': 
      v = voltageA; c = currentA; s = statusA; bn = battNumA; cap = capacityA;
      if (Acharge) mode = "CHARGING";
      else if (Adischarge) mode = "DISCHARGING";
      else if (Acycle) mode = "CYCLE";
      else mode = "IDLE";
      if (Acycle) {
        cycleInfo = "Cyc: " + String(cycleCountA) + "/" + String(cycleTargetA);
      }
      break;
    case 'B': 
      v = voltageB; c = currentB; s = statusB; bn = battNumB; cap = capacityB;
      if (Bcharge) mode = "CHARGING";
      else if (Bdischarge) mode = "DISCHARGING";
      else if (Bcycle) mode = "CYCLE";
      else mode = "IDLE";
      if (Bcycle) {
        cycleInfo = "Cyc: " + String(cycleCountB) + "/" + String(cycleTargetB);
      }
      break;
    case 'C': 
      v = voltageC; c = currentC; s = statusC; bn = battNumC; cap = capacityC;
      if (Ccharge) mode = "CHARGING";
      else if (Cdischarge) mode = "DISCHARGING";
      else if (Ccycle) mode = "CYCLE";
      else mode = "IDLE";
      if (Ccycle) {
        cycleInfo = "Cyc: " + String(cycleCountC) + "/" + String(cycleTargetC);
      }
      break;
    case 'D': 
      v = voltageD; c = currentD; s = statusD; bn = battNumD; cap = capacityD;
      if (Dcharge) mode = "CHARGING";
      else if (Ddischarge) mode = "DISCHARGING";
      else if (Dcycle) mode = "CYCLE";
      else mode = "IDLE";
      if (Dcycle) {
        cycleInfo = "Cyc: " + String(cycleCountD) + "/" + String(cycleTargetD);
      }
      break;
  }
  // Title row
  String title = "Slot ";
  title += slot;
  if (bn > 0) {
    title += " #";
    title += bn;
  }
  printCentered(title.c_str(), 0);
  // Status and mode
  lcd.setCursor(0, 1); lcd.print("Status: "); lcd.print(s);
  lcd.setCursor(0, 2); lcd.print("Mode: "); lcd.print(mode);
  if (cycleInfo.length() > 0) {
    lcd.setCursor(10, 2); lcd.print(cycleInfo);
  }
  
  // Show capacity on third line if discharging
  if (Adischarge || Bdischarge || Cdischarge || Ddischarge) {
    lcd.setCursor(0, 3); lcd.print("Cap:");
    lcd.print(cap, 0);
    lcd.print("mAh V:");
    lcd.print(v, 2);
  } else {
    lcd.setCursor(0, 3); lcd.print("V:"); lcd.print(v, 2); lcd.print(" C:"); lcd.print(c, 0);
  }
}
// Refresh current slot display
void refreshSlotDisplay() {
  if (previousSlot != '\0') showSlot(previousSlot);
}
// Show the settings menu for a slot
void showSlotSetting(char slot) {
  if (slot == '\0') return;
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("SLOT ");
  lcd.print(slot);
  lcd.print(" OPTIONS");
  lcd.setCursor(0,1);
  lcd.print("1.Charge 2.Discharge");
  lcd.setCursor(0,2);
  lcd.print("3.Cycle  4.Batt No.");
  lcd.setCursor(0,3);
  lcd.print("Press * to go back");
}
// Show force stop confirmation prompt
void showForceStopPrompt(char slot) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("FORCE STOP Slot ");
  lcd.print(slot);
  lcd.setCursor(0,1);
  
  // Show current operation
  lcd.print("Current: ");
  switch (slot) {
    case 'A': 
      if (Acharge) lcd.print("CHARGING");
      else if (Adischarge) lcd.print("DISCHARGING");
      else if (Acycle) lcd.print("CYCLE");
      break;
    case 'B': 
      if (Bcharge) lcd.print("CHARGING");
      else if (Bdischarge) lcd.print("DISCHARGING");
      else if (Bcycle) lcd.print("CYCLE");
      break;
    case 'C': 
      if (Ccharge) lcd.print("CHARGING");
      else if (Cdischarge) lcd.print("DISCHARGING");
      else if (Ccycle) lcd.print("CYCLE");
      break;
    case 'D': 
      if (Dcharge) lcd.print("CHARGING");
      else if (Ddischarge) lcd.print("DISCHARGING");
      else if (Dcycle) lcd.print("CYCLE");
      break;
  }
  
  lcd.setCursor(0,2);
  lcd.print("0 = Yes, FORCE STOP");
  lcd.setCursor(0,3);
  lcd.print("5 = No, continue");
}
// Show battery number entry screen
void showBattNumberInput(char slot) {
  if (slot == '\0') return;
  battNumberSlot = slot;
  battNumberBuffer = "";
  inBattNumberInput = true;
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Enter Batt # for ");
  lcd.print(slot);
  renderBattNumberBuffer();
  lcd.setCursor(0,2);
  lcd.print("* Cancel   # OK");
  lcd.setCursor(0,3);
  //lcd.print("Current: ");
  lcd.print(battNumForSlot(slot));
}
// Show cycle count entry screen
void showCycleInput(char slot) {
  if (slot == '\0') return;
  cycleSlot = slot;
  cycleBuffer = "";
  inCycleInput = true;
  confirmingCycle = false;
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Set Cycles for ");
  lcd.print(slot);
  renderCycleBuffer();
  lcd.setCursor(0,2);
  lcd.print("Enter number (1-999)");
  lcd.setCursor(0,3);
  lcd.print("* Cancel   # OK");
  Serial.println("Cycle input screen shown");
}
// Show the current cycle input with brackets
void renderCycleBuffer() {
  String display = "[";
  display += cycleBuffer;
  display += "]";
  int col = (20 - display.length()) / 2;
  if (col < 0) col = 0;
  lcd.setCursor(col,1);
  for (int i=0;i<20;i++) lcd.print(" ");
  lcd.setCursor(col,1);
  lcd.print(display);
}
// Show cycle confirmation screen
void showCycleConfirmation(char slot, int cycles) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Confirm Cycle Test");
  lcd.setCursor(0,1);
  lcd.print("Slot ");
  lcd.print(slot);
  lcd.print(": ");
  lcd.print(cycles);
  lcd.print(" cycles");
  lcd.setCursor(0,2);
  lcd.print("Ends on CHARGE");
  lcd.setCursor(0,3);
  lcd.print("#=START *=CANCEL");
  Serial.println("Cycle confirmation screen shown");
}
// Handle keypad during cycle count entry
void handleCycleInputKey(char key) {
  Serial.print("Cycle input key: "); Serial.println(key);
  
  if (key >= '0' && key <= '9') {
    if (cycleBuffer.length() == 0 && key == '0') return; // no leading zero
    if (cycleBuffer.length() < 3) {
      cycleBuffer += key;
      renderCycleBuffer();
    }
    return;
  }
  if (key == '*') {
    Serial.println("Cycle input cancelled");
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
    int value = 0;
    if (cycleBuffer.length() > 0)
      value = atoi(cycleBuffer.c_str());
    if (value <= 0) value = 1;
    
    Serial.print("Cycle value entered: "); Serial.println(value);
    
    // Store the value and show confirmation screen
    pendingCycleValue = value;
    showCycleConfirmation(cycleSlot, value);
    inCycleInput = false;
    confirmingCycle = true;
    return;
  }
}
// Render battery number buffer
void renderBattNumberBuffer() {
  String display = "[";
  display += battNumberBuffer;
  display += "]";
  int col = (20 - display.length()) / 2;
  if (col < 0) col = 0;
  lcd.setCursor(col,1);
  for (int i=0;i<20;i++) lcd.print(" ");
  lcd.setCursor(col,1);
  lcd.print(display);
}
// Handle keypad during battery number entry
void handleBattNumberInputKey(char key) {
  if (key >= '0' && key <= '9') {
    if (battNumberBuffer.length() == 0 && key == '0') return; // No leading zero
    if (battNumberBuffer.length() < 3) {
      battNumberBuffer += key;
      renderBattNumberBuffer();
    }
    return;
  }
  if (key == '*') { // Cancel
    inBattNumberInput = false;
    battNumberSlot = '\0';
    battNumberBuffer = "";
    showSlotSetting(previousSlot);
    inSettingScreen = true;
    return;
  }
  if (key == '#') { // Confirm
    int value = 0;
    if (battNumberBuffer.length() > 0)
      value = atoi(battNumberBuffer.c_str());
    // Save battery number permanently
    saveBattNum(battNumberSlot, value);
    
    // Show confirmation
    lcd.clear();
    lcd.setCursor(0,1);
    lcd.print(" Batt # saved!");
    lcd.setCursor(0,2);
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
    return;
  }
}
// Start discharge after waiting period
void processDeferredDischarge() {
  unsigned long now = millis();
  if (AdischargeWait && now - AdischargeStart >= 10000) {
    AdischargeWait = false; AdischargePending = false; Adischarge = true;
    digitalWrite(AMS, LOW); digitalWrite(ACD, LOW);
    Serial.println("Slot A DISCHARGE started after delay");
    showSlot('A');
  }
  if (BdischargeWait && now - BdischargeStart >= 10000) {
    BdischargeWait = false; BdischargePending = false; Bdischarge = true;
    digitalWrite(BMS, LOW); digitalWrite(BCD, LOW);
    Serial.println("Slot B DISCHARGE started after delay");
    showSlot('B');
  }
  if (CdischargeWait && now - CdischargeStart >= 10000) {
    CdischargeWait = false; CdischargePending = false; Cdischarge = true;
    digitalWrite(CMS, LOW); digitalWrite(CCD, LOW);
    Serial.println("Slot C DISCHARGE started after delay");
    showSlot('C');
  }
  if (DdischargeWait && now - DdischargeStart >= 10000) {
    DdischargeWait = false; DdischargePending = false; Ddischarge = true;
    digitalWrite(DMS, LOW); digitalWrite(DCD, LOW);
    Serial.println("Slot D DISCHARGE started after delay");
    showSlot('D');
  }
}
// Save battery number to permanent storage
void saveBattNum(char slot, int num) {
  switch (slot) {
    case 'A':
      prefs.putInt("A_num", num);
      battNumA = num;
      Serial.print("Saved A_num = "); Serial.println(num);
      break;
    case 'B':
      prefs.putInt("B_num", num);
      battNumB = num;
      Serial.print("Saved B_num = "); Serial.println(num);
      break;
    case 'C':
      prefs.putInt("C_num", num);
      battNumC = num;
      Serial.print("Saved C_num = "); Serial.println(num);
      break;
    case 'D':
      prefs.putInt("D_num", num);
      battNumD = num;
      Serial.print("Saved D_num = "); Serial.println(num);
      break;
  }
}
// Clear battery number from permanent storage (optional - can be called manually)
void clearBattNum(char slot) {
  switch (slot) {
    case 'A':
      prefs.putInt("A_num", 0);
      battNumA = 0;
      Serial.println("Cleared Battery A number");
      break;
    case 'B':
      prefs.putInt("B_num", 0);
      battNumB = 0;
      Serial.println("Cleared Battery B number");
      break;
    case 'C':
      prefs.putInt("C_num", 0);
      battNumC = 0;
      Serial.println("Cleared Battery C number");
      break;
    case 'D':
      prefs.putInt("D_num", 0);
      battNumD = 0;
      Serial.println("Cleared Battery D number");
      break;
  }
}
// Stop operation function (unified stop handler)
void stopOperation(char slot, bool fromForceStop) {
  // Get the operation that was running before stop
  String operation = "";
  switch (slot) {
    case 'A': 
      if (Acharge) operation = "CHARGING";
      else if (Adischarge) operation = "DISCHARGING";
      else if (Acycle) operation = "CYCLE";
      break;
    case 'B': 
      if (Bcharge) operation = "CHARGING";
      else if (Bdischarge) operation = "DISCHARGING";
      else if (Bcycle) operation = "CYCLE";
      break;
    case 'C': 
      if (Ccharge) operation = "CHARGING";
      else if (Cdischarge) operation = "DISCHARGING";
      else if (Ccycle) operation = "CYCLE";
      break;
    case 'D': 
      if (Dcharge) operation = "CHARGING";
      else if (Ddischarge) operation = "DISCHARGING";
      else if (Dcycle) operation = "CYCLE";
      break;
  }
  // Get current capacity before resetting
  float finalCapacity = 0;
  switch (slot) {
    case 'A': finalCapacity = capacityA; break;
    case 'B': finalCapacity = capacityB; break;
    case 'C': finalCapacity = capacityC; break;
    case 'D': finalCapacity = capacityD; break;
  }
  // Reset all booleans for this slot
  resetSlotBooleans(slot);
  resetSlotRelays(slot);
  
  // Log the force stop event with final capacity
  char sheetId[50];
  float v = 0, c = 0;
  int battNum = battNumForSlot(slot);
  
  switch (slot) {
    case 'A': strcpy(sheetId, SHEET_ID_A); v = voltageA; c = currentA; break;
    case 'B': strcpy(sheetId, SHEET_ID_B); v = voltageB; c = currentB; break;
    case 'C': strcpy(sheetId, SHEET_ID_C); v = voltageC; c = currentC; break;
    case 'D': strcpy(sheetId, SHEET_ID_D); v = voltageD; c = currentD; break;
  }
  
  if (battNum > 0) {
    logSlotToSheet(sheetId, "Force Stopped", battNum, 0, v, c, finalCapacity, false, "Stopped");
  }
  // Show stop message
  lcd.clear();
  lcd.setCursor(0,1);
  if (fromForceStop) {
    lcd.print(" FORCE STOPPED!");
  } else {
    lcd.print("   OPERATION");
    lcd.setCursor(0,2);
    lcd.print("    STOPPED!");
  }
  lcd.setCursor(0,3);
  lcd.print("Slot ");
  lcd.print(slot);
  if (operation.length() > 0) {
    lcd.print(" - ");
    lcd.print(operation);
  }
  
  stopMessageStart = millis();
  stopMessageActive = true;
  inStopPrompt = false;
  
  Serial.print("Slot "); Serial.print(slot); 
  Serial.print(" operation stopped. Was: "); Serial.println(operation);
  if (finalCapacity > 0) {
    Serial.print("Final capacity: "); Serial.print(finalCapacity, 3); Serial.println(" Ah");
  }
}
// Get current time as formatted string
void formatDateTime(const char* fmt, char* result, size_t size) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    strftime(result, size, fmt, &timeinfo);
  } else {
    strncpy(result, "1970-01-01 00:00:00", size);
  }
}
// Send one row of data to Google Sheets - MODIFIED to include capacity
void logSlotToSheet(const char* sheetId,
                    const String& slotStatus,
                    int battNum,
                    unsigned long elapsedSecs,
                    float voltage,
                    float current,
                    float capacity,  // New parameter
                    bool inCycle,
                    const char* operationMode)
{
    if (!GSheet.ready()) {
        Serial.println("GSheet not ready - skipping append");
        return;
    }
    FirebaseJson response;
    FirebaseJson valueRange;
    char timeStamp[25];
    formatDateTime("%Y-%m-%d %H:%M:%S", timeStamp, sizeof(timeStamp));
    char voltageStr[10];
    char currentStr[10];
    char capacityStr[12];  // string for capacity
    dtostrf(voltage, 5, 2, voltageStr);
    dtostrf(current, 5, 2, currentStr);
    dtostrf(capacity, 8, 4, capacityStr);  // Format capacity with 4 decimal places
    // Build consolidated Status field
    String finalStatus;
    if (inCycle) {
        finalStatus = "Cycle";
    } else if (strcmp(operationMode, "Charge") == 0) {
        finalStatus = "Charging";
    } else if (strcmp(operationMode, "Discharge") == 0) {
        finalStatus = "Discharging";
    } else if (strcmp(operationMode, "Complete") == 0) {
        finalStatus = "Complete";
    } else if (strcmp(operationMode, "Stopped") == 0) {
        finalStatus = "Force Stopped";
    } else {
        finalStatus = "Standby";
    }
    valueRange.add("majorDimension", "COLUMNS");
    valueRange.set("values/[0]/[0]", timeStamp);
    valueRange.set("values/[1]/[0]", finalStatus);
    valueRange.set("values/[2]/[0]", battNum);
    valueRange.set("values/[3]/[0]", elapsedSecs);
    valueRange.set("values/[4]/[0]", voltageStr);
    valueRange.set("values/[5]/[0]", currentStr);
    valueRange.set("values/[6]/[0]", capacityStr);  // capacity column
    Serial.print("Sending to Google Sheet ID: ");
    Serial.println(sheetId);
    Serial.print("Capacity: "); Serial.print(capacity, 4); Serial.println(" mAh");
    bool ok = GSheet.values.append(&response, sheetId, "Sheet1!A1:G1", &valueRange);
    if (!ok) {
        Serial.println("Append FAILED!");
        String r; 
        response.toString(r, true);
        Serial.println(r);
    } else {
        Serial.println("Log SUCCESS!");
    }
}