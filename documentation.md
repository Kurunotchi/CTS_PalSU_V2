# PSU_CTS - Battery Capacity Testing Station v2.0

## Overview
**PSU_CTS** is an ESP32-based 4-slot battery capacity testing station designed for precise Li-ion battery testing. Each slot independently supports charging, discharging, and cycle testing with automatic voltage cutoffs, real-time monitoring, and data logging.

**Key Features:**
- ✅ 4 independent testing slots (A, B, C, D)
- ✅ INA226 precision current/voltage sensors (calibrated per slot/mode)
- ✅ LCD + 4x4 keypad for manual control
- ✅ WiFi TCP server (port 8888) for Python dashboard
- ✅ Google Sheets logging (1-minute intervals per slot)
- ✅ Automatic capacity calculation (trapezoidal integration)
- ✅ Cycle testing (user-defined cycles, auto charge-discharge)
- ✅ Voltage limits: Charge stop 4.10V, Discharge stop 2.80V
- ✅ Real-time Python GUI with Simpson's 1/3 rule capacity

## Hardware Requirements
```
ESP32 DevKit v1
x4 INA226 modules (I2C addresses: 0x40, 0x41, 0x44, 0x45)
20x4 I2C LCD (address 0x27)
4x4 Matrix Keypad
x8 Logic level shifters (ESP32 3.3V → sensors/relays)
Power supplies:
  - Charging: 5V/2A per slot
  - Discharging: Load resistors or electronic loads
```

### Pinout
| Pin | Function | Slot |
|-----|----------|------|
| 15,4,25,26 | Keypad Rows | - |
| 27,14,12,13 | Keypad Columns | - |
| 23/19 | DMS/DCD | D |
| 18/5  | BMS/BCD | B |
| 17/16 | AMS/ACD | A |
| 32/33 | CMS/CCD | C |
| SDA/SCL | I2C Bus (all INA226 + LCD) | - |

**Wiring Diagram:** [Include Fritzing/image here]

## Software Architecture
```
Core: ESP32 Arduino Framework
├── Sensors: INA226 library (calibrated voltage/current)
├── UI: Keypad + LiquidCrystal_I2C
├── Network: WiFi TCP Server + Google Sheets API
├── Control: State machine per slot (IDLE/CHARGING/DISCHARGING/CYCLE)
├── Data: Preferences (battery numbers), JSON over TCP
└── Dashboard: Python/Tkinter + Matplotlib (Simpson capacity)
```

## Quick Start
### 1. Flash ESP32
```bash
pio run -t upload -e esp32dev
pio device monitor -e esp32dev
```

### 2. Update WiFi (hardcoded in main.cpp)
```
ssid = "Salamat Shopee"
pass  = "wednesday"
```

### 3. Google Sheets Setup
- Create 4 sheets (one per slot)
- Service Account credentials in main.cpp
- Sheet IDs: SHEET_ID_A/B/C/D

### 4. Operation
**LCD Controls:**
```
Home: # 
Slot A/B/C/D
* → Settings: 1=Charge, 2=Discharge, 3=Cycle, 4=Batt#, 0=STOP
Batt# input: 0-9, #=save, *=cancel
0 → Force Stop active operation
```

**Modes:**
- **Charge**: AMS LOW, ACD HIGH → Stop @ 4.10V
- **Discharge**: AMS LOW, ACD LOW → Stop @ 2.80V (10s delay)
- **Cycle**: Charge→Discharge→... (ends on final charge)

### 5. Python Dashboard
```bash
cd src
python CTS.py
# Enter ESP32 IP, Connect
```

## TCP Protocol (Port 8888)
**ESP32 → Python (JSON):**
```json
{
  "type": "sensor_data",
  "slot": "A",
  "timestamp": 12345,
  "battery_num": 42,
  "voltage": 3.85,
  "current": 1250,
  "capacity": 0.347,
  "elapsed_minutes": 120,
  "mode": "DISCHARGING",
  "cycle_current": 2,
  "cycle_target": 5
}
```

**Python → ESP32 (JSON):**
```json
{"command": "1", "slot": "A"}  // Charge Slot A
{"command": "2", "slot": "B"}  // Discharge Slot B  
{"command": "3", "slot": "C", "cycles": 5}  // 5-cycle test
{"command": "4", "slot": "D", "battery_number": 123}  // Set batt#
{"command": "0", "slot": "A"}  // Stop
```

## Calibration Values (main.cpp)
```
Slot A: currentCalA=100.3720, voltageCalA=0.9991 (charge=0.9874/disch=1.0248)
Slot B: currentCalB=97.8846, voltageCalB=1.0005 (charge=0.9898/disch=1.0326)
Slot C: currentCalC=98.8484, voltageCalC=1.0005 (charge=0.9948/disch=1.0239)
Slot D: currentCalD=98.6328, voltageCalD=1.0008 (charge=0.9875/disch=1.0305)
```

## Google Sheets Logging
**Every 60s per active slot:**
```
Timestamp | Status | Batt# | Elapsed(min) | Voltage | Current | Capacity(mAh)
```

**Simpson logs** on charge/discharge complete.

## Troubleshooting
| Issue | Solution |
|-------|----------|
| `Sensor X not detected` | Check I2C addresses/wiring |
| **WiFi "doesnt connect"** | 1. Flash + `pio device monitor` - watch detailed debug (SSID, status, IP, RSSI)<br>2. Verify SSID/pass in main.cpp matches router (2.4GHz network)<br>3. RSSI <-70dBm? Move closer<br>4. Status: 3=wrong pass, 6=no AP<br>5. Power cycle ESP32/router |
| Capacity inaccurate | Recalibrate INA226 shunt/resistor |
| Sheets fail | Verify service account keys |
| TCP disconnect | Check ESP32 IP (`pio device monitor`) |

## Libraries (platformio.ini)
```
Keypad, LiquidCrystal_I2C, INA226, ESP-Google-Sheet-Client, ArduinoJson
```

## Future Improvements
- [ ] Bluetooth control
- [ ] Web dashboard (ESPAsyncWebServer)
- [ ] BLE advertising for mobile app
- [ ] OTA updates
- [ ] Battery matching algorithm

---
*Built with PlatformIO | ESP32 Arduino | Python 3*

