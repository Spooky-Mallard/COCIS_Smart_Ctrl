# Serial CLI Reference Manual

**Project:** CoCIS Smart Control and Monitoring System (Makerere University, BSE III & CS II 2026/2027)  
**Interfaces:** Entrance Hub Management Console & Lecture Room Diagnostic Terminals

---

## 1. Entrance Hub Management Console (`apps/entrance_node`)

Connected to Entrance Node UART0 (`TX: GPIO 1`, `RX: GPIO 3`, 115200 baud).

### 1.1 Main Interactive Menu
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

### 1.2 Menu Explanations & Commands

#### Option [1] / `logs [count]`: View Access Logs
Reads the latest append-only access events from `/sdcard/access.csv`:
```
[LOG REPLAY] Last 5 Access Events:
  1. 2026-10-25 08:32:10 EAT | UID: 04A1B2C3 | Dr. John Bosco  | ENTRY_GRANTED | PASS: None
  2. 2026-10-25 08:34:45 EAT | UID: 04B2C3D4 | Sarah Nabatanzi | ENTRY_GRANTED | PASS: None
  3. 2026-10-25 08:40:12 EAT | UID: DEADBEEF | UNREGISTERED    | ENTRY_DENIED  | REASON: Invalid Tag
```

#### Option [2] / `occupancy`: View Current Building Occupancy
Displays real-time headcount and the authoritative "who is inside" ledger:
```
========================================================================
                      CURRENT BUILDING OCCUPANCY
========================================================================
 Total Headcount Inside: 2 persons
------------------------------------------------------------------------
 UID       | Name                 | Role       | Entry Time   | Duration
------------------------------------------------------------------------
 04A1B2C3  | Dr. John Bosco       | FACULTY    | 08:32:10 EAT | 00h 14m
 04B2C3D4  | Sarah Nabatanzi      | STUDENT    | 08:34:45 EAT | 00h 11m
========================================================================
```

#### Option [3] / `rooms`: View Lecture Room Status
Queries live telemetry received over MQTT from Room 1 and Room 2:
```
===================================================================================
                       LECTURE ROOM LIVE TELEMETRY
===================================================================================
 Room | State     | PIR Motion | Light Relay | Energy Saved | Heartbeat | Last Seen
-----------------------------------------------------------------------------------
 1    | ON_ACTIVE | DETECTED   | ON          | 120 mWh      | HEALTHY   | 2s ago
 2    | VACANT    | IDLE       | OFF         | 480 mWh      | HEALTHY   | 1s ago
===================================================================================
```

#### Option [4] / `schedule`: Configure Access Schedule
Requires authenticated administrator login (`login admin Cocis@2026`):
```
cocis(admin)# schedule set 1 08:00 21:00
OK: Monday schedule updated: Permitted access between 08:00 and 21:00 EAT
```

#### Option [5] / `override_light <room_id> <on|off|auto>`: Lighting Override
Sends remote override command over MQTT:
- `override_light 1 on`: Forces Room 1 lighting relay ON regardless of motion.
- `override_light 1 off`: Forces Room 1 lighting relay OFF.
- `override_light 1 auto`: Releases Room 1 back to autonomous PIR FSM control.

#### Option [6] / `door <entry|exit> unlock`: Manual Door Release
Actuates door servo for 3000 ms with audit logging.

#### Option [7] / `status`: System Health & Network Telemetry
Displays node statuses, Wi-Fi IP, and MQTT broker connectivity:
```
============================================================
              SYSTEM HEALTH & NETWORK TELEMETRY
============================================================
 Node 0 (Hub):    ONLINE  | IP: 10.13.37.2 | RSSI: -55 dBm
 Node 1 (Room 1): HEALTHY | Last Ack: 104  | Latency: <1s
 Node 2 (Room 2): HEALTHY | Last Ack: 82   | Latency: <1s
 MQTT Broker:     CONNECTED to broker.hivemq.com:1883
 Free Heap:       294,180 bytes | Uptime: 345s
============================================================
```

#### Option [8] / `sim selftest`: Automated Verification Demo
Executes the comprehensive 10-scenario coursework test suite across access control, anti-passback, smart lighting, store-and-forward buffering, and tamper defense.

---

## 2. Lecture Room Diagnostic Console (`apps/room1_node`, `apps/room2_node`)

Connected to Room Node UART0 (`TX: GPIO 1`, `RX: GPIO 3`, 115200 baud).

### Commands
| Command | Arguments | Description |
| :--- | :--- | :--- |
| `status` | None | Prints local PIR input, light relay state, store-and-forward queue depth, uptime, and Wi-Fi RSSI |
| `motion` | None | Injects simulated PIR motion detection to test autonomous lighting FSM |
| `clear` | None | Injects simulated motion clear to begin grace and vacancy countdown |
| `help` | None | Displays available commands |
