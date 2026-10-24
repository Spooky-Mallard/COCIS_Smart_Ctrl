# CoCIS Smart Control and Monitoring System (ESP32 / ESP-IDF v6.0.2 / Wokwi)

**College of Computing and Information Sciences (CoCIS), Makerere University**  
**Programmes:** Bachelor of Software Engineering (BSE III) & Bachelor of Computer Science (CS II) — 2026/2027 Pilot

---

## 1. System Overview

This project implements a complete, distributed **Smart Access Control, Building Occupancy Monitoring, and Automated Lecture Room Lighting System** for CoCIS, Makerere University, written in modular **ESP-IDF v6.0.2 C** with **FreeRTOS** and simulated in **Wokwi**.

### Core Capabilities
- **Dual-Channel Main Entrance Access Control**: Separate **ENTRY** and **EXIT** RFID scan points, motorized servo doors (`50 Hz` LEDC PWM), visual status LEDs, and multi-tone piezo buzzer feedback.
- **Authoritative Live Occupancy Ledger & Strict Anti-Passback**: Real-time tracking of every individual inside CoCIS (`inside`), entry timestamps, peak headcount, and anti-passback enforcement (blocking duplicate entries or unrecorded exits).
- **Configurable Day-of-Week Building Access Schedule with Safety-Compliant Egress**: Per-day open/close windows in East Africa Time (`EAT`, UTC+3) persisted in NVS. Blocks unauthorized entry and normal after-hours exit while providing a **Supervisor Safe-Exit Override (`override_exit`)** and a hardware/CLI **Emergency Evacuation Mode (`emergency on`)** with live roll-call output.
- **Automated Lecture Room Lighting (Room 1 & Room 2)**: PIR motion sensors with interrupt-driven debounce, a flicker-free `VACANT -> OCCUPIED -> GRACE_HOLD -> VACANT` state machine, configurable vacancy timeouts, and real-time electricity savings calculation (`mWh` and scaled UGX).
- **Distributed Multi-Terminal Coordination (`net_protocol`)**: Three terminal nodes (`NODE_ENTRANCE`, `NODE_ROOM_1`, `NODE_ROOM_2`) communicating via sequenced packets (`seq_id`), acknowledgements with retry, sliding-window duplicate suppression, offline **Store-and-Forward** buffering, and dual transport (FreeRTOS Virtual Bus + **MQTT v3.1.1 over lwIP TCP sockets** on `Wokwi-GUEST`).
- **Tamper-Evident SD Card Storage (`SDSPI` + `FATFS`)**: Asynchronous queue-driven CSV logger writing `/sdcard/access.csv`, `/sdcard/lighting.csv`, and `/sdcard/audit.csv` with a **SHA-256 cryptographic hash chain** (`sd verify`) and automatic boot-time occupancy recovery after power loss.
- **Salted SHA-256 Admin Authentication & Interactive Serial CLI**: Constant-time password hash verification (`psa/crypto.h`), exponential failed-login lockout, inactivity auto-logout, optional 2FA Admin RFID badge check, and 25+ monitoring/admin/simulation CLI commands.

---

## 2. Quick Start: Build & Run in Wokwi

### 2.1 Build with ESP-IDF v6.0.2
```bash
export IDF_PYTHON_ENV_PATH=/Users/spooky/.espressif/tools/python/v6.0.2/venv
source /Users/spooky/.espressif/v6.0.2/esp-idf/export.sh
idf.py build
```

### 2.2 Run in Wokwi for VS Code
1. Open this folder (`COCIS_Smart_Ctrl`) in VS Code.
2. Press `F1` (`Cmd+Shift+P` on macOS) and run **Wokwi: Start Simulator**.
3. Interact using either:
   - **Physical Controls on the Wokwi Canvas**:
     - **Green Button (`GPIO 27`)**: Scan valid student tag (`04A1B2C3`) at **ENTRY**.
     - **Blue Button (`GPIO 32`)**: Scan valid student tag (`04A1B2C3`) at **EXIT**.
     - **Red Button (`GPIO 33`)**: Scan unknown intruder tag (`DEADBEEF`) at **ENTRY**.
     - **Yellow Button (`GPIO 16 / RX2`)**: Toggle **Emergency Evacuation Mode**.
     - **PIR Sensors (`pir1` on `GPIO 34`, `pir2` on `GPIO 35`)**: Click in Wokwi to trigger motion in Lecture Room 1 or Lecture Room 2.
   - **Serial CLI Terminal (`115200 baud`)**:
     - Type `help`, `status`, `inside`, `rooms`, `nodes`, `schedule show`, `sim selftest`, or `login admin Cocis@2026`.

---

## 3. System Documentation Index (`docs/`)

| Document | Purpose |
| :--- | :--- |
| [docs/ARCHITECTURE.md](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/ARCHITECTURE.md) | Layered block diagrams, FreeRTOS task priority/stack map, data flows, and boot sequence |
| [docs/DESIGN_DECISIONS.md](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/DESIGN_DECISIONS.md) | Wokwi simulation reality check, multi-ESP32 architecture, RFID abstraction, and safety-compliant exit schedule analysis |
| [docs/SYNCHRONIZATION.md](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/SYNCHRONIZATION.md) | Shared resource table, 7-level hierarchical mutex lock order, step-by-step race condition timelines, and distributed consistency |
| [docs/PROTOCOL.md](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/PROTOCOL.md) | Inter-terminal message format, sequence numbers, ACK/retry state machine, idempotency, and store-and-forward replay |
| [docs/CLI_REFERENCE.md](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/CLI_REFERENCE.md) | Complete Serial CLI command reference, pre-enrolled RFID tags, default credentials, and sample outputs |
| [docs/TESTING.md](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/TESTING.md) | Reproducible manual and automated (`sim selftest`) test scenarios and expected results |
| [docs/GLOSSARY.md](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/docs/GLOSSARY.md) | Plain-language glossary of all C, FreeRTOS, ESP-IDF, hardware, networking, and security terms |

---

## 4. Module Index & 5-Student Ownership Bundles

Every module consists of a public header in `main/include/<module>.h`, an implementation in `main/src/<module>.c`, and a comprehensive student tutorial guide in `main/modules/<module>/README.md`.

### Bundle 1: Configuration, Hardware Pins, SD Storage & Tamper-Evident Logger (Student 1)
1. **[`config`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/config/README.md)** ([config.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/config.h), [config.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/config.c)) — System-wide constants, timings, limits, and NVS-persisted runtime configuration.
2. **[`board_pins`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/board_pins/README.md)** ([board_pins.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/board_pins.h), [board_pins.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/board_pins.c)) — Complete ESP32 GPIO pin map, LEDC/SPI/I2C assignments, and baseline GPIO initialization.
3. **[`storage_sd`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/storage_sd/README.md)** ([storage_sd.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/storage_sd.h), [storage_sd.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/storage_sd.c)) — SPI2 microSD card driver (`SDSPI` + `FATFS`), graceful fallback when card is absent, log rotation, and boot recovery reader.
4. **[`logger`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/logger/README.md)** ([logger.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/logger.h), [logger.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/logger.c)) — Non-blocking queue-driven CSV logger with SHA-256 tamper-evident hash chain and integrity verifier.

### Bundle 2: Main Entrance RFID, Motorized Doors, Buzzer & Access Engine (Student 2)
5. **[`rfid`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/rfid/README.md)** ([rfid.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/rfid.h), [rfid.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/rfid.c)) — Dual Entry/Exit RFID reader abstraction, GPIO ISR button triggers with 50 ms debounce, and simulated tag injection.
6. **[`door`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/door/README.md)** ([door.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/door.h), [door.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/door.c)) — Entry & Exit 50 Hz servo PWM controllers, status LEDs, auto-relock FreeRTOS software timers, and forced-door/held-open alarms.
7. **[`buzzer`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/buzzer/README.md)** ([buzzer.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/buzzer.h), [buzzer.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/buzzer.c)) — Asynchronous LEDC PWM piezo buzzer pattern synthesizer (Grant, Deny, Anti-Passback, Alarm, Emergency).
8. **[`access_control`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/access_control/README.md)** ([access_control.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/access_control.h), [access_control.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/access_control.c)) — Authoritative Entry/Exit decision pipeline integrating `user_db`, `schedule`, `occupancy_ledger`, anti-passback, and safety overrides.

### Bundle 3: Identity, Access Schedule, Live Occupancy Ledger & Admin Auth (Student 3)
9. **[`user_db`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/user_db/README.md)** ([user_db.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/user_db.h), [user_db.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/user_db.c)) — Enrolled RFID user database (UID, name, role, active flag, visitor expiry) with NVS persistence and 8 default CoCIS personas.
10. **[`schedule`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/schedule/README.md)** ([schedule.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/schedule.h), [schedule.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/schedule.c)) — Day-of-week access windows, after-hours exit policy, supervisor safe-exit passes, and emergency evacuation mode.
11. **[`occupancy_ledger`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/occupancy_ledger/README.md)** ([occupancy_ledger.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/occupancy_ledger.h), [occupancy_ledger.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/occupancy_ledger.c)) — Live "who is inside" tracking, anti-passback state lookup, emergency evacuation roll-call, and SD log boot recovery.
12. **[`auth`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/auth/README.md)** ([auth.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/auth.h), [auth.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/auth.c)) — Salted SHA-256 password authentication (`psa/crypto.h`), constant-time compare, lockout backoff, auto-logout, and 2FA badge check.

### Bundle 4: Lecture Room PIR Sensors, Smart Lighting, I2C LCD & Time Sync (Student 4)
13. **[`pir_sensor`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/pir_sensor/README.md)** ([pir_sensor.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/pir_sensor.h), [pir_sensor.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/pir_sensor.c)) — Dual lecture room PIR motion sensor driver with GPIO interrupts, software debounce, and CLI simulation injection.
14. **[`light`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/light/README.md)** ([light.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/light.h), [light.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/light.c)) — Intelligent room lighting FSM (`VACANT_OFF`, `OCCUPIED_ON`, `GRACE_HOLD`), vacancy timeout, and energy-savings calculator.
15. **[`display`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/display/README.md)** ([display.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/display.h), [display.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/display.c)) — Thread-safe ESP-IDF v6.0 `i2c_master` HD44780 16x2 LCD driver with rotating status screens and instant priority event banners.
16. **[`time_sync`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/time_sync/README.md)** ([time_sync.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/time_sync.h), [time_sync.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/time_sync.c)) — East Africa Time (`EAT-3`, UTC+3) wall-clock manager with `esp_netif_sntp`, deterministic coursework seed, and runtime CLI clock control.

### Bundle 5: Synchronization, Wi-Fi, Inter-Terminal Protocol, Health & Serial CLI (Student 5)
17. **[`sync`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/sync/README.md)** ([sync.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/sync.h), [sync.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/sync.c)) — 7-level hierarchical mutex manager enforcing lock ordering, per-task lock tracking, and system-wide FreeRTOS event group.
18. **[`net_wifi`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/net_wifi/README.md)** ([net_wifi.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/net_wifi.h), [net_wifi.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/net_wifi.c)) — Wi-Fi Station manager (`Wokwi-GUEST`) using `esp_netif`, `esp_event`, lwIP DHCP/DNS, and automatic reconnect backoff.
19. **[`net_protocol`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/net_protocol/README.md)** ([net_protocol.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/net_protocol.h), [net_protocol.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/net_protocol.c)) — Distributed inter-terminal protocol with `seq_id`, ACK/retry, idempotent deduplication, store-and-forward buffer, and lwIP TCP MQTT v3.1.1 client.
20. **[`health`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/health/README.md)** ([health.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/health.h), [health.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/health.c)) — Node heartbeat monitor, offline node alerting, Task Watchdog (`esp_task_wdt`) integration, and heap/stack diagnostics.
21. **[`cli`](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/modules/cli/README.md)** ([cli.h](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/include/cli.h), [cli.c](file:///Users/spooky/Spooky/Programming/Bare_Metal/ESP/Projects/COCIS_Smart_Ctrl/main/src/cli.c)) — Interactive UART0 console, command parser, formatted ASCII tables, admin session guard, and built-in 10-scenario self-test runner (`sim selftest`).
