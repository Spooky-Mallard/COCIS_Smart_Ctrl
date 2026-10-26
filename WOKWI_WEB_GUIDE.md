# Wokwi Web Simulation Guide (`wokwi-web` branch)

**Project:** Smart Control and Monitoring System for the College of Computing and Information Sciences (CoCIS), Makerere University  
**Target Platform:** Single ESP32 DevKit v1 in Wokwi Web Browser Simulator  
**Branch:** `wokwi-web`  
**Firmware File:** [`main.c`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main.c) / [`main/main.c`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/main.c)  
**Wiring Configuration:** [`diagram.json`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/diagram.json)

---

## 1. Why the `wokwi-web` Branch Exists

While physical production deployment and the `feature/three-nodes` branch utilize **three physically separate ESP32 boards** communicating over MQTT, running multi-node simulations on **Wokwi Web (`wokwi.com`)** requires a unified single-MCU simulation because browser-based Wokwi executes one microcontroller diagram at a time.

On this `wokwi-web` branch:
1. **All 21 modules** across the Entrance Coordinator, Lecture Room 1, and Lecture Room 2 are consolidated into a single self-contained [`main.c`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main.c).
2. All three node roles run concurrently as isolated FreeRTOS tasks inside one ESP32.
3. Inter-terminal communication operates via a fast in-memory FreeRTOS queue bus while simultaneously maintaining real MQTT v3.1.1 socket client capabilities over virtual Wi-Fi (`Wokwi-GUEST`).
4. Evaluators and students can test the **entire system** (turnstiles, LCD, SD card, APB, Room 1 & 2 lighting, store-and-forward, tamper-evident hash chaining, and emergency evacuation) in a single browser window with zero local software installation.

---

## 2. How to Run in Wokwi Web

### Method A: Import directly from GitHub (Fastest)
1. Navigate to:
   ```
   https://wokwi.com/projects/import-github/Spooky-Mallard/COCIS_Smart_Ctrl
   ```
2. Select the **`wokwi-web`** branch.
3. Wokwi Web will automatically load `main/main.c` and `diagram.json`.
4. Click the green **"Start the simulation"** button (`Play` icon).

### Method B: Manual Copy-Paste
1. Open [https://wokwi.com/projects/new/esp32](https://wokwi.com/projects/new/esp32).
2. Choose **ESP-IDF** (C).
3. Replace the contents of the code editor with the contents of [`main.c`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main.c).
4. Switch to the `diagram.json` tab and paste the contents of [`diagram.json`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/diagram.json).
5. Click **Play**!

---

## 3. Hardware Pinout on the Unified Canvas

| Subsystem | Component | Pin / Bus | Action in Simulation |
| :--- | :--- | :--- | :--- |
| **Main Entrance** | Entry Turnstile Servo | `GPIO 13` (LEDC PWM) | Rotates to 90° (unlocked) for 3 seconds on valid entry |
| **Main Entrance** | Exit Turnstile Servo | `GPIO 12` (LEDC PWM) | Rotates to 90° (unlocked) for 3 seconds on valid exit |
| **Main Entrance** | Entry Granted LED | `GPIO 2` (Green) | Flashes on valid scan |
| **Main Entrance** | Exit Granted LED | `GPIO 4` (Green) | Flashes on valid scan |
| **Main Entrance** | Access Denied / Alarm LED | `GPIO 15` (Red) | Illuminates on APB violation or intruder tag |
| **Main Entrance** | Multi-Tone Buzzer | `GPIO 14` (LEDC PWM) | Chimes on grant, buzzes on deny, sirens on emergency |
| **Main Entrance** | 16x2 I2C LCD Display | `GPIO 21` (SDA), `GPIO 22` (SCL) | Displays live headcount, schedule state, and alerts |
| **Main Entrance** | MicroSD Card Storage | `GPIO 5, 18, 23, 19` (SDSPI) | Logs `access.csv`, `lighting.csv`, and `audit.csv` with SHA-256 |
| **Lecture Room 1** | PIR Motion Sensor 1 | `GPIO 34` (Input) | Click to trigger human presence in Room 1 |
| **Lecture Room 1** | Lighting Relay 1 (Yellow LED) | `GPIO 25` (Output) | Lights up on motion; turns off 15s after vacancy |
| **Lecture Room 2** | PIR Motion Sensor 2 | `GPIO 35` (Input) | Click to trigger human presence in Room 2 |
| **Lecture Room 2** | Lighting Relay 2 (Yellow LED) | `GPIO 26` (Output) | Lights up on motion; turns off 15s after vacancy |
| **Interactive Buttons**| Entry Card Pushbutton | `GPIO 27` (Active-low GND) | Simulates student tapping RFID tag at Entry turnstile |
| **Interactive Buttons**| Exit Card Pushbutton | `GPIO 32` (Active-low GND) | Simulates student tapping RFID tag at Exit turnstile |
| **Interactive Buttons**| Intruder Tag Pushbutton | `GPIO 33` (Active-low GND) | Simulates unregistered card scan (Access Denied) |
| **Interactive Buttons**| Emergency Evac Pushbutton | `GPIO 16 / RX2` (Active-low) | Triggers building-wide fire evacuation unlock |

---

## 4. Interactive Serial Commands

Open the **Serial Monitor** tab in Wokwi Web to use the interactive console:

### Live Monitoring Commands:
* `status`: Complete system dashboard (Clock, Doors, Rooms, SD, Heap, Uptime).
* `inside` or `occupancy`: View the authoritative "Who is inside" roll-call with entry times and duration.
* `rooms` or `lights`: View Lecture Room 1 & 2 PIR states, relay status, and energy saved in mWh and UGX.
* `log [n]`: View the last $n$ tamper-evident hash-chained entries from `/sdcard/access.csv`.
* `stats`: View access grants, denials, and anti-passback violation counts.
* `nodes`: View distributed terminal link health, sequence counters, and store-and-forward queue depths.

### Administration Commands (Default login: `admin` / `Cocis@2026`):
* `login admin Cocis@2026`: Authenticates administrative session.
* `schedule show`: Displays weekly authorized access hours in East Africa Time (`EAT-3`).
* `schedule set <day> <open_HH:MM> <close_HH:MM>`: Updates building operational windows.
* `override_exit <uid>`: Grants a 5-minute supervisor safe-exit pass.
* `emergency on` / `emergency off`: Manual fire evacuation override (unlocks all turnstiles).
* `user list`: Lists all enrolled students, staff, admins, and visitors.
* `sd verify`: Recomputes the SHA-256 hash chain from genesis to verify log integrity.

### Automated Verification Suite:
* `sim selftest`: Executes the complete **10-scenario automated coursework verification suite** and prints a formatted summary table verifying 100% compliance with coursework requirements!
