# System Verification & Testing Guide (`TESTING.md`)

**Project:** CoCIS Smart Control and Monitoring System (Makerere University)  
**Simulator:** Wokwi for VS Code (`v4.1.0`) | **Framework:** ESP-IDF v6.0.2

---

## 1. How to Build and Launch in Wokwi

### Step 1: Compile the Firmware
From the project root (`/Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl`):
```bash
export IDF_PYTHON_ENV_PATH=/Users/spooky/.espressif/tools/python/v6.0.2/venv
source /Users/spooky/.espressif/v6.0.2/esp-idf/export.sh
idf.py build
```

### Step 2: Start the Wokwi Simulation in VS Code
1. Open `/Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl` in VS Code.
2. Open `diagram.json` or press `Cmd+Shift+P` (`F1`) and select **Wokwi: Start Simulator**.
3. The Wokwi canvas will appear with the ESP32, 16x2 I2C LCD, MicroSD card, Entry & Exit Door Servos, 5 Status LEDs, Piezo Buzzer, 2 Lecture Room PIR Motion Sensors, and 4 Interactive Pushbuttons, alongside the integrated Serial Monitor terminal at the bottom.

---

## 2. Automated One-Command Verification (`sim selftest`)

For fast, reproducible grading or demonstration, type the following command in the Wokwi Serial Monitor:
```text
sim selftest
```
The firmware will automatically execute all **10 required coursework test scenarios** in sequence and print a formatted `[PASS] / [FAIL]` verification matrix:
1. Valid RFID Entry & Live Occupancy Increment
2. Anti-Passback Violation on Duplicate Entry
3. Valid RFID Exit & Occupancy Decrement
4. Anti-Passback Violation on Exit When Outside
5. Unknown Tag (`DEADBEEF`) & Inactive User (`0499DEAD`) Denial
6. Schedule Enforcement (Entry Denied Outside Window + Normal Exit Denied After Closing + Supervisor Exit Override + Emergency Evacuation Mode)
7. Lecture Room 1 & 2 PIR Occupancy -> Smart Light ON -> Grace Hold -> Vacancy Timeout OFF & Energy Savings Calculation
8. Concurrent Multi-Source Access & Room Telemetry Synchronization
9. Remote Node Offline Store-and-Forward Buffering & Idempotent Ordered Replay on Reconnect
10. Admin Salted SHA-256 Authentication, Lockout Backoff, Unauthorized Schedule Edit Rejection & SD Hash-Chain Verification

---

## 3. Manual Step-by-Step Test Scenarios

### Scenario 1: Valid Entry & Valid Exit (Physical Buttons or CLI)
- **Action (Entry):** Click the green **`SCAN ENTRY (VALID)`** pushbutton (`GPIO 27`) on the Wokwi diagram (or type `sim scan entry 04A1B2C3`).
- **Expected Result:**
  - Entry Servo (`servo_entry`, `GPIO 13`) rotates to `90°` (Open) for 3 seconds, then returns to `0°` (Closed).
  - Green `ENTRY DOOR` LED (`GPIO 2`) illuminates while open; Buzzer (`GPIO 14`) plays a high-pitched grant chirp.
  - 16x2 LCD displays `"GRANTED: ENTRY  "` / `"Matthew M. [IN:1]"`.
  - Typing `inside` shows `04A1B2C3 (Matthew Mumbere)` inside CoCIS (`count = 1`).
- **Action (Exit):** Click the blue **`SCAN EXIT (VALID)`** pushbutton (`GPIO 32`) (or type `sim scan exit 04A1B2C3`).
- **Expected Result:**
  - Exit Servo (`servo_exit`, `GPIO 12`) opens to `90°` for 3 seconds; Green `EXIT DOOR` LED (`GPIO 4`) turns ON.
  - Live occupancy returns to `0`.

---

### Scenario 2: Anti-Passback Violation (Entry-While-Inside & Exit-While-Outside)
- **Action:**
  1. Scan `04C3D4E5` at Entry: `sim scan entry 04C3D4E5` -> **GRANTED** (`inside = 1`).
  2. Scan `04C3D4E5` at Entry a second time without exiting: `sim scan entry 04C3D4E5`.
- **Expected Result:**
  - Access is **DENIED** with reason `ANTI_PASSBACK_IN`.
  - Red `DENY / ALARM` LED (`GPIO 15`) flashes; Buzzer plays the triple anti-passback warning tone; LCD shows `"DENY: PASSBACK  "`.
  - Occupancy count remains `1` (not incremented to `2`).
- **Action (Exit While Outside):**
  3. Scan `04D4E5F6` (who has not entered) at Exit: `sim scan exit 04D4E5F6`.
- **Expected Result:**
  - Access is **DENIED** with reason `ANTI_PASSBACK_OUT`.

---

### Scenario 3: Unknown Tag & Inactive User Rejection
- **Action:**
  1. Click the red **`SCAN INTRUDER TAG`** button (`GPIO 33`) or run `sim scan entry DEADBEEF`.
  2. Run `sim scan entry 0499DEAD` (enrolled but disabled account).
- **Expected Result:**
  - Both attempts keep the Entry Door locked (`0°`), flash the Red Alarm LED, sound the denial double-beep, and record `DENY_UNKNOWN_TAG` and `DENY_USER_INACTIVE` in `/sdcard/access.csv`.

---

### Scenario 4: Building Access Schedule, After-Hours Exit Restriction & Safety Overrides
- **Action:**
  1. Ensure `04C3D4E5` (Sarah, Student) is inside during normal hours (`sim scan entry 04C3D4E5`).
  2. Log in as admin: `login admin Cocis@2026`.
  3. Advance system clock to `22:30:00` (after closing time `21:00`): `time set 2026-10-24 22:30:00`.
  4. Attempt a new student entry: `sim scan entry 04D4E5F6` -> **DENIED (`OUTSIDE_SCHEDULE_ENTRY`)**.
  5. Attempt normal student exit while still inside after closing: `sim scan exit 04C3D4E5`.
- **Expected Result (Step 5):**
  - **DENIED (`OUTSIDE_SCHEDULE_EXIT`)** per Section 1.6 requirement! Sarah remains recorded as inside (`inside` shows `04C3D4E5`), alerting security.
- **Action (Supervisor Safe-Exit Override):**
  6. Admin grants a one-time supervisor exit pass: `override_exit 04C3D4E5`.
  7. Sarah scans at Exit again: `sim scan exit 04C3D4E5`.
- **Expected Result (Step 7):**
  - **GRANTED (`OVERRIDE_SUPERVISOR_EXIT`)**; Exit Door unlocks, audit trail logs the authorized override, and Sarah is safely checked out (`inside = 0`).
- **Action (Emergency Evacuation Override):**
  8. Press the yellow **`EMERGENCY EVAC`** button (`GPIO 16 / RX2`) or run `emergency on`.
- **Expected Result (Step 8):**
  - Both Entry and Exit servos immediately unlock (`90°`), LCD displays `"!! EMERGENCY !! "` / `"EVACUATE NOW!   "`, CLI prints the roll-call list of everyone currently inside, and any occupant scanning at Exit is immediately allowed out even after hours.

---

### Scenario 5: Lecture Room Automated Lighting, Grace Period & Vacancy Timeout
- **Action:**
  1. Click `pir1` (Room 1 PIR Motion Sensor on the Wokwi canvas) to trigger motion (or run `sim motion 1 on`).
  2. Observe `ROOM 1 LIGHT` Yellow LED (`GPIO 25`) and run `rooms`.
  3. Turn motion off (`sim motion 1 off`) and watch `rooms` as the state transitions from `ON_ACTIVE` -> `GRACE_HOLD` -> `OFF_VACANT` after the vacancy timeout (`15 s`).
- **Expected Result:**
  - Light turns ON within `< 50 ms` of motion.
  - Brief stillness during the grace period does **not** flicker the light off; if motion resumes during grace, the timer resets smoothly without relay chatter.
  - Once vacant for the full timeout, `GPIO 25` turns OFF and `stats` / `rooms` increments the energy-saved counter (`mWh`).

---

### Scenario 6: Distributed Node Offline Store-and-Forward & Idempotent Replay
- **Action:**
  1. Disconnect Lecture Room 1 from the network: `sim node 1 offline`.
  2. Trigger motion On and Off in Room 1 while offline: `sim motion 1 on`, wait 2 seconds, `sim motion 1 off`.
  3. Run `nodes` to inspect Node 1's status.
  4. Reconnect Lecture Room 1: `sim node 1 online`.
  5. Run `nodes` and `log 10`.
- **Expected Result:**
  - While offline, Room 1's local lighting automation continues working autonomously (zero dependency on coordinator for local light control) and buffers the telemetry packets in its store-and-forward ring buffer (`Buffered: 2`).
  - Upon `sim node 1 online`, all buffered packets are replayed in exact chronological sequence (`seq_id` order), acknowledged by Main Entrance, and written once to `/sdcard/lighting.csv` with zero duplicates.

---

### Scenario 7: Admin Authentication Lockout & SD Card Tamper Verification
- **Action:**
  1. Run `logout`.
  2. Attempt to change schedule without login: `schedule set mon 08:00 18:00` -> **Rejected (`ERR: Admin login required`)**.
  3. Enter wrong password 3 times: `login admin wrong1`, `login admin wrong2`, `login admin wrong3`.
  4. Attempt valid login during lockout: `login admin Cocis@2026`.
  5. Verify SD log integrity: `sd verify`.
- **Expected Result:**
  - After 3 failed password attempts, the account locks out for `30 seconds` (`LOCKED OUT`) and logs a security alert in `audit.csv`.
  - `sd verify` recomputes the SHA-256 hash chain across all CSV log entries and confirms `INTEGRITY OK`.
