# System Architecture

**Project:** CoCIS Smart Control and Monitoring System (Makerere University)  
**Target:** ESP32 / ESP-IDF v6.0.2 / FreeRTOS / Wokwi

---

## 1. High-Level Layered Architecture

The firmware is structured into **four strict architectural layers** across **21 single-responsibility C modules**, orchestrated by a minimal `main/main.c`.

```mermaid
flowchart TB
    subgraph Layer4["Layer 4: Presentation, CLI & Distributed Coordination"]
        CLI["cli\n(UART0 Interactive Console)"]
        DISP["display\n(I2C 16x2 LCD Coordinator)"]
        PROTO["net_protocol\n(Inter-Node Packet Router & MQTT)"]
        HEALTH["health\n(Watchdog, Heartbeats, Heap/Stack)"]
    end

    subgraph Layer3["Layer 3: Domain Logic & Security Engines"]
        ACC["access_control\n(Entry/Exit Decision Pipeline)"]
        LIGHT["light\n(Room 1 & 2 Smart Lighting FSM)"]
        SCHED["schedule\n(Day-of-Week Windows & Overrides)"]
        AUTH["auth\n(Salted SHA-256 Admin Sessions)"]
    end

    subgraph Layer2["Layer 2: Authoritative Data Stores & Logging"]
        UDB["user_db\n(Enrolled RFID Tags & Roles)"]
        LEDGER["occupancy_ledger\n(Live Who-Is-Inside & Anti-Passback)"]
        LOG["logger\n(Async Queue + Hash-Chained CSV)"]
        TIME["time_sync\n(EAT UTC+3 Clock & SNTP)"]
    end

    subgraph Layer1["Layer 1: Hardware Abstraction & Core Services"]
        CFG["config\n(NVS Settings & Constants)"]
        PINS["board_pins\n(GPIO / Bus Mapping)"]
        SYNC["sync\n(Hierarchical Mutexes & Event Group)"]
        RFID["rfid\n(Entry/Exit Scan ISR & Sim)"]
        DOOR["door\n(Dual Servo PWM & Status LEDs)"]
        BUZ["buzzer\n(LEDC Tone Pattern Task)"]
        PIR["pir_sensor\n(Room 1 & 2 Motion ISR + Debounce)"]
        SD["storage_sd\n(SPI2 SDSPI + FATFS VFS)"]
        WIFI["net_wifi\n(Wokwi-GUEST STA & esp_event)"]
    end

    CLI --> ACC & LIGHT & SCHED & AUTH & UDB & LEDGER & LOG & SD & TIME & HEALTH & PROTO
    ACC --> UDB & SCHED & LEDGER & DOOR & BUZ & DISP & LOG
    PIR --> LIGHT
    LIGHT --> PROTO & LOG & DISP
    PROTO --> WIFI & LEDGER & SCHED & LOG & HEALTH
    LOG --> SD
```

---

## 2. Distributed Terminal Topology

Even when running on a single ESP32 in Wokwi (`NODE_MODE_ALL_IN_ONE`), the three physical terminals operate as independent state machines communicating exclusively through `net_protocol`:

```mermaid
sequenceDiagram
    participant PIR1 as Room 1 PIR (GPIO 34)
    participant N1 as Node 1 Task (Lecture Room 1)
    participant BUS as net_protocol (Virtual Bus + MQTT)
    participant ENT as Node 0 Coordinator (Main Entrance)
    participant SD as Logger Task (/sdcard)

    PIR1->>N1: Motion Detected (Rising Edge ISR -> Debounced)
    N1->>N1: Turn Room 1 Light ON (GPIO 25 HIGH)<br/>State = LIGHT_ON_OCCUPIED
    N1->>BUS: PROTO_MSG_ROOM_STATUS (src=1, seq=42, occ=1, light=1)
    alt Node 1 Online
        BUS->>ENT: Deliver Frame (seq=42)
        ENT->>ENT: Check Duplicate Window (seq 42 is new)<br/>Update Building Room Table & Heartbeat
        ENT->>SD: Enqueue Occupancy & Lighting CSV Event
        ENT->>BUS: PROTO_MSG_ACK (dst=1, ack_seq=42)
        BUS->>N1: Receive ACK (mark seq=42 complete)
    else Node 1 Offline (Store-and-Forward)
        N1->>N1: Timeout waiting for ACK -> Store in Offline Ring Buffer
        Note over N1,ENT: Later, when link restores...
        N1->>BUS: Replay Buffered Frames in Order (seq=42, 43...)
        BUS->>ENT: Deliver Frame (seq=42)
        ENT->>BUS: PROTO_MSG_ACK (dst=1, ack_seq=42)
    end
```

---

## 3. FreeRTOS Task Map

Every FreeRTOS task has a well-defined responsibility, stack size, priority, and blocking primitive (zero busy-wait loops):

| Task Name | Module | Priority | Stack (Bytes) | Blocking Primitive | Primary Responsibility |
| :--- | :--- | :---: | :---: | :--- | :--- |
| `rfid_task` | `rfid` | **7** (High) | `4096` | `xQueueReceive(s_rfid_isr_queue)` | Debounces physical button/reader interrupts and dispatches `rfid_scan_event_t` to `access_control`. |
| `access_task` | `access_control` | **6** | `6144` | `xQueueReceive(s_scan_queue)` | Evaluates RFID scans against `user_db`, `schedule`, and `occupancy_ledger`; triggers `door`, `buzzer`, `display`, and `logger`. |
| `pir_light_task` | `pir_sensor` / `light` | **5** | `4096` | `xQueueReceive(s_pir_queue, 200ms)` | Processes Room 1 & Room 2 PIR edges, runs the smart-lighting grace/vacancy FSM, updates energy metrics, and emits room packets. |
| `proto_task` | `net_protocol` | **5** | `6144` | `xQueueReceive(s_proto_rx_queue, 250ms)` | Routes inter-terminal frames, handles ACK timeouts, replays store-and-forward buffers, and services MQTT TCP socket I/O. |
| `buzzer_task` | `buzzer` | **5** | `3072` | `xQueueReceive(s_buzzer_queue)` | Plays multi-step LEDC PWM acoustic patterns without blocking caller tasks. |
| `display_task` | `display` | **4** | `4096` | `vTaskDelay(250ms)` / Override Event | Renders rotating telemetry screens and instant access/alarm alert banners on the 16x2 I2C LCD (`0x27`). |
| `cli_task` | `cli` | **4** | `8192` | `uart_read_bytes(..., 100ms)` | Reads UART0 input, parses monitoring/admin/simulation commands, enforces session auto-logout, and prints formatted tables. |
| `logger_task` | `logger` | **3** | `6144` | `xQueueReceive(s_log_queue, 1000ms)` | Computes SHA-256 hash chain and writes CSV records to `/sdcard/*.csv` (or RAM fallback). |
| `health_task` | `health` | **2** (Low) | `4096` | `vTaskDelay(1000ms)` | Feeds Task Watchdog (`esp_task_wdt`), checks remote node heartbeat age, and tracks free heap / stack high-water marks. |

*(Note: Door auto-close timing uses lightweight FreeRTOS Software Timers via the `Tmr Svc` daemon task rather than dedicating a separate task per door.)*

---

## 4. Deterministic System Initialization Order (`main/main.c`)

To prevent any task from accessing an uninitialized queue or mutex at boot, `app_main()` initializes modules strictly bottom-up before starting task loops:

1. **Core Infrastructure**: `sync_init()` (creates all hierarchical mutexes and the system event group) -> `config_init()` (initializes NVS flash and loads persistent settings) -> `board_pins_init()` (configures baseline safe GPIO states).
2. **Storage & Time**: `time_sync_init()` (sets `EAT-3` timezone and seeds RTC) -> `storage_sd_init()` (mounts `/sdcard` over SPI2) -> `logger_init()` (starts background hash-chained CSV logger).
3. **Authoritative Databases**: `user_db_init()` (loads enrolled users or seeds 8 default CoCIS tags) -> `schedule_init()` (loads weekly schedule windows) -> `occupancy_ledger_init()` (recovers "who is inside" from `/sdcard/access.csv` if rebooted mid-day) -> `auth_init()` (initializes salted SHA-256 admin credentials).
4. **Peripherals & Domain Controllers**: `buzzer_init()` -> `display_init()` -> `door_init()` -> `access_control_init()` -> `rfid_init()` -> `light_init()` -> `pir_sensor_init()`.
5. **Networking, Health & Console**: `net_wifi_init()` -> `net_protocol_init()` -> `health_init()` -> `cli_init()`.

---

## 5. Module Ownership Matrix (5-Student Group Division)

The 21 modules are partitioned into **5 balanced bundles** so each student in a 5-member group owns ~4 cohesive modules and can explain every line of their subsystem:

| Student | Bundle Title | Assigned Modules (`include/`, `src/`, `modules/<m>/README.md`) |
| :---: | :--- | :--- |
| **Student 1** | **Config, Pins, SD Storage & Hash-Chained Logger** | `config`, `board_pins`, `storage_sd`, `logger` |
| **Student 2** | **Entrance RFID, Motorized Doors, Buzzer & Access Engine** | `rfid`, `door`, `buzzer`, `access_control` |
| **Student 3** | **User DB, Schedule, Occupancy Ledger & Admin Auth** | `user_db`, `schedule`, `occupancy_ledger`, `auth` |
| **Student 4** | **Lecture Room PIR, Smart Lighting, I2C LCD & Time Sync** | `pir_sensor`, `light`, `display`, `time_sync` |
| **Student 5** | **Synchronization, Wi-Fi, Distributed Protocol, Health & CLI** | `sync`, `net_wifi`, `net_protocol`, `health`, `cli` |
