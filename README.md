# CoCIS Smart Control and Monitoring System

**Coursework Project:** Smart Control and Monitoring System for the College of Computing and Information Sciences (CoCIS), Makerere University  
**Programmes:** Bachelor of Science in Software Engineering (BSE III) & Bachelor of Science in Computer Science (CS II), Academic Year 2026/2027  
**Platform:** ESP32 Microcontrollers / ESP-IDF v6.0.2 / FreeRTOS / Wokwi Simulator  
**Architecture:** Distributed 3-Node Networked System (Main Entrance Hub + Lecture Room 1 + Lecture Room 2)

---

## 1. Project Overview

The CoCIS complex houses valuable computational infrastructure requiring strict access control against unauthorized intrusions. In addition, high energy expenses occur when lighting in lecture rooms remains on while unoccupied.

This automated embedded solution solves both challenges across three physically separate control terminals coordinating over simulated Wi-Fi (`Wokwi-GUEST`) via an MQTT broker:

1. **Main Entrance Coordinator (`apps/entrance_node`)**:
   - Owns the authoritative user database (8 campus personas across students, faculty, security, and visitors).
   - Manages the weekly building access schedule in East Africa Time (`EAT-3`, UTC+3).
   - Dual-portal access control with motorized ENTRY and EXIT servo turnstiles and strict **anti-passback** enforcement.
   - Real-time **"who is inside"** occupancy ledger tracking duration of stay.
   - Append-only audit and access logs on SPI MicroSD card (`/sdcard`) with running **SHA-256 tamper-evident hash chaining** and boot-time power-loss recovery.
   - 8-menu interactive UART0 management console and 16x2 LCD status display.
2. **Lecture Room 1 Terminal (`apps/room1_node`)**:
   - Autonomous PIR motion sensing with 100 ms hardware debouncing.
   - 3-state lighting finite state machine (`OFF_VACANT`, `ON_ACTIVE`, `ON_GRACE_HOLD`) with configurable grace period and 15s vacancy countdown.
   - Continuous energy savings calculation in milliwatt-hours (`mWh`) and Ugandan Shillings (`UGX`).
   - 32-entry circular FIFO store-and-forward ring buffer ensuring **zero event loss** during hub network outages.
   - Diagnostic serial console for viewing telemetry and injecting simulated motion.
3. **Lecture Room 2 Terminal (`apps/room2_node`)**:
   - Structurally identical terminal controlling Lecture Room 2 with its own PIR sensor and lighting relay.
4. **Shared Component (`components/cocis_common`)**:
   - Reusable infrastructure linked by all three nodes: cryptographic HMAC-SHA256 message signing, wire serialization/deserialization, MQTT v3.1.1 client over lwIP TCP sockets, Wi-Fi station manager, and SNTP timekeeping.

---

## 2. Directory Layout

```
COCIS_Smart_Ctrl/
├── components/
│   └── cocis_common/              # Shared library component for all 3 nodes
│       ├── include/ (cocis_types.h, cocis_config.h, cocis_crypto.h, cocis_protocol.h,
│       │             cocis_mqtt.h, cocis_wifi.h, cocis_sync.h, cocis_time.h, cocis_health.h)
│       └── src/     (cocis_crypto.c, cocis_protocol.c, cocis_mqtt.c, cocis_wifi.c,
│                     cocis_sync.c, cocis_time.c, cocis_health.c)
├── apps/
│   ├── entrance_node/              # Main Entrance Coordinator Application
│   │   ├── CMakeLists.txt  wokwi.toml  diagram.json  sdkconfig.defaults
│   │   └── main/ (main.c, include/, src/)
│   ├── room1_node/                 # Lecture Room 1 Terminal Application
│   │   ├── CMakeLists.txt  wokwi.toml  diagram.json  sdkconfig.defaults
│   │   └── main/ (main.c, include/, src/)
│   └── room2_node/                 # Lecture Room 2 Terminal Application
│       ├── CMakeLists.txt  wokwi.toml  diagram.json  sdkconfig.defaults
│       └── main/ (main.c, include/, src/)
├── docs/                           # Complete University Documentation Suite
│   ├── ARCHITECTURE.md             # Distributed topology and layered subsystem diagrams
│   ├── PROTOCOL.md                 # Packet schemas, HMAC-SHA256 signing, and topic map
│   ├── SYNCHRONIZATION.md          # Multi-MCU concurrency, mutex hierarchy, deduplication
│   ├── CLI_REFERENCE.md            # Hub Admin CLI & Room Diagnostic CLI manuals
│   ├── DESIGN_DECISIONS.md         # Wokwi multi-ESP empirical findings & architecture choices
│   ├── TESTING.md                  # 10 core verification scenarios & test scripts
│   └── GLOSSARY.md                 # Embedded systems and distributed computing terminology
└── README.md                       # This file
```

---

## 3. How to Build the System

### Prerequisites:
- **ESP-IDF v6.0.2** installed and activated:
  ```bash
  export IDF_PYTHON_ENV_PATH=/Users/spooky/.espressif/tools/python/v6.0.2/venv
  source /Users/spooky/.espressif/v6.0.2/esp-idf/export.sh
  ```

### Build Each Node Application:
Each application compiles independently into its own ELF binary:

```bash
# 1. Build Main Entrance Hub
cd apps/entrance_node
idf.py build

# 2. Build Lecture Room 1 Terminal
cd ../room1_node
idf.py build

# 3. Build Lecture Room 2 Terminal
cd ../room2_node
idf.py build
```

---

## 4. How to Run the Distributed Simulation in Wokwi

To observe all three microcontrollers communicating concurrently:

1. Open three separate VS Code windows:
   - Window 1: Open folder `apps/entrance_node`
   - Window 2: Open folder `apps/room1_node`
   - Window 3: Open folder `apps/room2_node`
2. In each window, open `diagram.json` and start the simulation (`Cmd+Shift+P` $\rightarrow$ `Wokwi: Start Simulator`).
3. All three boards will:
   - Connect to virtual Wi-Fi `Wokwi-GUEST`.
   - Establish an MQTT session with `broker.hivemq.com:1883`.
   - Exchange HMAC-SHA256 authenticated heartbeats and live occupancy telemetry.

---

## 5. Live Demonstration & Verification

### Interactive Management Console (Entrance Hub UART0)
In Window 1 (Entrance Hub), access the interactive management console:
```
============================================================
 CoCIS Smart Control System - Main Entrance Coordinator
============================================================
 [1] View Access Logs (from SD Card)
 [2] View Current Building Occupancy (Who is inside)
 [3] View Lecture Room Status (Room 1 & Room 2 live state)
 [4] Configure Building Access Schedule (Requires Admin PIN/Tag)
 [5] Override Lecture Room Lighting (ON / OFF / AUTO)
 [6] Emergency Door Release (Manual unlock override)
 [7] System Health & Network Telemetry (All nodes RSSI, IP, Latency)
 [8] Run Automated Multi-Node Verification Suite
 [0] Refresh Menu
============================================================
Enter option [0-8]:
```

### Automated 10-Scenario Self-Test:
Press Option `[8]` (or type `sim selftest`) on the Entrance Hub console to execute the end-to-end automated verification suite covering valid entry, anti-passback, safe exit override, emergency evacuation, room lighting FSM, store-and-forward replay, and HMAC tamper defense.

---

## 6. Coursework Rubric & Documentation Index

| Documentation File | Contents & Purpose |
| :--- | :--- |
| [`docs/ARCHITECTURE.md`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/ARCHITECTURE.md) | Multi-node system architecture, physical node split, hardware data flows |
| [`docs/PROTOCOL.md`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/PROTOCOL.md) | JSON schemas, HMAC-SHA256 signature algorithm, MQTT topic taxonomy |
| [`docs/SYNCHRONIZATION.md`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/SYNCHRONIZATION.md) | Mutex hierarchy, causal event ordering, store-and-forward consistency |
| [`docs/CLI_REFERENCE.md`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/CLI_REFERENCE.md) | Comprehensive command reference for Hub Admin and Room consoles |
| [`docs/DESIGN_DECISIONS.md`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/DESIGN_DECISIONS.md) | Wokwi multi-ESP empirical findings, strapping pins, architectural decisions |
| [`docs/TESTING.md`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/TESTING.md) | Step-by-step verification guide for all 10 coursework scenarios |
| [`docs/GLOSSARY.md`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/GLOSSARY.md) | Comprehensive glossary of embedded systems and networking terms |
