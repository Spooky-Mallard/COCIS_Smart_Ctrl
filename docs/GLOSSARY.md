# Technical Glossary (`GLOSSARY.md`)

**Project:** CoCIS Smart Control and Monitoring System (Makerere University)  
This glossary explains every embedded C, FreeRTOS, ESP-IDF, hardware, networking, and security term used across the codebase and module READMEs in plain, student-friendly language.

---

## A — Access Control & Security Terms

- **Anti-Passback (APB)**: A security rule preventing an RFID card from being used to enter a building twice in a row without an intervening exit (or to exit without first having entered). It stops a student inside CoCIS from passing their ID card through a window to a friend outside.
- **Audit Trail (`audit.csv`)**: A chronological, tamper-evident record of every security-relevant administrative action (logins, failed password attempts, schedule modifications, user enrollments, emergency overrides).
- **Constant-Time Comparison**: Comparing two byte arrays (such as password hashes) by XORing every byte (`diff |= a[i] ^ b[i]`) rather than stopping at the first mismatch (`memcmp`). This prevents an attacker from guessing a password byte-by-byte by measuring microsecond differences in response time.
- **Fail-Closed (Fail-Secure)**: A policy where a door remains locked if a subsystem fails (e.g., if the system clock is invalid, **entry** into CoCIS is denied to protect equipment).
- **Fail-Safe (Safe Egress)**: A life-safety policy ensuring human occupants can always evacuate a building during an emergency (e.g., emergency evacuation mode immediately unlocks the **exit** door).
- **Hash Chain (Tamper-Evident Log)**: A cryptographic technique where each log entry $n$ includes a hash computed over both the previous entry's hash $H_{n-1}$ and the current entry's content: $H_n = \text{SHA256}(H_{n-1} \parallel \text{Entry}_n)$. Modifying or deleting any historical line breaks the chain for all subsequent lines.
- **Role-Based Access Control (RBAC)**: Assigning permissions based on a user's role (`ADMIN`, `STAFF`, `STUDENT`, `VISITOR`) rather than hardcoding rules per individual.
- **Salt (Cryptographic Salt)**: A random byte sequence prepended to a password before hashing (`SHA256(salt || password)`). Ensures that two users with the same password produce completely different hashes and defeats precomputed rainbow-table attacks.
- **SHA-256 (Secure Hash Algorithm 256-bit)**: A one-way cryptographic hash function that transforms arbitrary input data into a fixed 32-byte (256-bit) fingerprint. In ESP-IDF v6.0, accessed via ARM PSA Crypto (`psa_hash_compute`).

---

## B — C Language Constructs

- **`const`**: Promises the compiler that a variable or pointer target will not be modified after initialization. Allows constants to be placed in Flash ROM (`.rodata`) instead of consuming scarce SRAM.
- **Designated Initializer**: Initializing struct fields explicitly by name (e.g., `gpio_config_t cfg = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = ... };`). Any omitted fields are automatically zero-initialized by the C standard.
- **`enum` (Enumeration)**: A user-defined type consisting of named integer constants (e.g., `ROLE_ADMIN = 0, ROLE_STAFF = 1`), replacing magic numbers with self-documenting symbols.
- **Function Pointer / Callback**: A variable that stores the address of executable code (e.g., `typedef void (*rfid_scan_cb_t)(const rfid_event_t *evt)`), allowing a low-level driver (`rfid`) to notify a high-level module (`access_control`) without tight coupling.
- **Header Guard (`#ifndef MODULE_H` / `#define MODULE_H` / `#endif`)**: Preprocessor directives that prevent a header file from being included more than once in the same `.c` translation unit, avoiding "redefinition of struct/type" compiler errors.
- **`static` (File Scope)**: When placed before a global variable or function in a `.c` file, `static` makes that symbol **private** to that `.c` file (internal linkage), preventing namespace collisions across modules.
- **`static` (Local Scope)**: When placed inside a function, `static` allocates the variable in permanent data memory instead of the task stack, so its value persists across function calls.
- **`typedef struct`**: Creates a clean alias name (e.g., `user_record_t`) for a composite structure so callers do not need to write `struct user_record` repeatedly.
- **`uint8_t`, `uint16_t`, `uint32_t`, `int64_t` (`<stdint.h>`)**: Fixed-width integer types guaranteed to be exactly 8, 16, 32, or 64 bits on every CPU architecture, essential for hardware registers and network packets.
- **`volatile`**: Tells the compiler that a variable's value may change asynchronously outside the current execution thread (e.g., modified by a GPIO Interrupt Service Routine or hardware register), preventing the compiler from caching it in a CPU register.

---

## C — FreeRTOS & Concurrency Concepts

- **Binary Semaphore vs. Mutex**:
  - A **Binary Semaphore** is for **signaling** (synchronizing) between an ISR/task and another task ("an event happened!"). It does not have an owner.
  - A **Mutex** (Mutual Exclusion) is for **protecting a shared resource** ("only one task in this critical section at a time"). Only the task that took (`xSemaphoreTake`) the mutex may give (`xSemaphoreGive`) it back, and it supports **Priority Inheritance**.
- **Critical Section (`portENTER_CRITICAL` / `portEXIT_CRITICAL`)**: Temporarily disables preemptive context switches and interrupts on the local core (and acquires a spinlock across both ESP32 cores) for a few microseconds to protect ultra-short hardware register manipulations.
- **Deadlock**: A permanent freeze where two or more tasks wait indefinitely for locks held by each other. Prevented in this project by strict lock-ordering levels (`LOCK_LEVEL_1` through `LOCK_LEVEL_7`).
- **Event Group (`EventGroupHandle_t`)**: A 24-bit shared bitmap managed by FreeRTOS where tasks can atomically set (`xEventGroupSetBits`), clear (`xEventGroupClearBits`), or block waiting for (`xEventGroupWaitBits`) combinations of system state flags (e.g., `SYS_EVT_WIFI_CONNECTED | SYS_EVT_TIME_SYNCED`).
- **ISR (Interrupt Service Routine)**: A short, special C function triggered immediately by hardware when an electrical signal changes (e.g., a button press or PIR motion edge). ISRs run outside normal task context, must be marked `IRAM_ATTR`, must never call blocking functions (`vTaskDelay`, `printf`), and must use `*_FromISR` FreeRTOS APIs (`xQueueSendFromISR`).
- **Priority Inversion & Priority Inheritance**: Priority inversion occurs when a high-priority task waits for a mutex held by a low-priority task that gets preempted by a medium-priority task. FreeRTOS mutexes solve this via **Priority Inheritance**: temporarily raising the low-priority lock holder to the high task's priority until it releases the mutex.
- **Queue (`QueueHandle_t`)**: A thread-safe FIFO buffer managed by FreeRTOS (`xQueueCreate`, `xQueueSend`, `xQueueReceive`). Data is copied **by value** into the queue, decoupling producer tasks (`rfid_task`) from consumer tasks (`access_task`).
- **Race Condition**: A bug where system behavior depends on the unpredictable microsecond timing of task switches or interrupts accessing shared memory without synchronization.
- **Software Timer (`TimerHandle_t`)**: A lightweight timer managed by the FreeRTOS `Tmr Svc` daemon task (`xTimerCreate`, `xTimerStart`) that invokes a callback function when a period expires (used to auto-close the Entry and Exit doors after 3 seconds).
- **Stack High-Water Mark (`uxTaskGetStackHighWaterMark`)**: The minimum amount of free stack space (in bytes on ESP-IDF) that has remained unused since a task started. As it approaches `0`, the task is at risk of a stack overflow crash.
- **Tick & `pdMS_TO_TICKS(ms)`**: FreeRTOS measures time in discrete ticks (configured to `1000 Hz` = `1 ms` per tick in `sdkconfig.defaults`). `pdMS_TO_TICKS(ms)` converts milliseconds into ticks portably regardless of tick frequency.

---

## D — ESP-IDF Hardware & Storage Terms

- **`ESP_ERROR_CHECK(x)`**: A macro that evaluates an `esp_err_t` expression; if the return value is not `ESP_OK` (`0`), it prints the error name, file, line number, and stack backtrace, then aborts the CPU. Used only during fatal boot initialization; recoverable runtime errors use `ESP_RETURN_ON_ERROR` or `if (err != ESP_OK)`.
- **FATFS**: The industry-standard FAT16/FAT32 file system library (ChaN's FatFs) integrated into ESP-IDF so SD cards can be read on any laptop.
- **GPIO Pull-Up / Pull-Down**: Internal ESP32 resistors (`~45 kΩ`) that hold an input pin at a known logic level (`3.3V` HIGH for pull-up, `0V` LOW for pull-down) when a mechanical switch is open, preventing floating-pin electrical noise.
- **I2C (Inter-Integrated Circuit)**: A 2-wire synchronous serial bus (`SDA` data + `SCL` clock) with open-drain lines and 7-bit device addresses (`0x27` for our 16x2 LCD).
- **`IRAM_ATTR`**: An ESP-IDF linker attribute that places a function in Internal Instruction RAM (`IRAM`) instead of external SPI Flash cache, allowing GPIO ISRs to execute safely even while Flash is disabled during NVS writes.
- **LEDC (LED PWM Controller)**: An ESP32 hardware peripheral that generates precise Pulse-Width Modulation (PWM) square waves without CPU intervention—used in this project both to position the **50 Hz door servo motors** and to drive variable-frequency tones on the **piezo buzzer**.
- **NVS (Non-Volatile Storage)**: An ESP-IDF wear-leveled key-value store in internal SPI Flash memory that retains configuration, user accounts, and schedules across power loss.
- **PIR (Passive Infrared) Sensor**: An occupancy sensor that detects changes in infrared heat radiation emitted by moving human bodies, driving its digital `OUT` pin HIGH (`3.3V`).
- **PWM (Pulse-Width Modulation)**: Controlling analog-like actuators from a digital pin by varying the **duty cycle** (fraction of each period the signal stays HIGH) or **frequency**. A standard hobby servo expects a `50 Hz` (`20 ms` period) signal where a `1.0 ms` pulse = `0°` (Closed) and a `1.5 ms–2.0 ms` pulse = `90°–180°` (Open).
- **SDSPI (SD over SPI)**: Communicating with a microSD card using the 4-wire SPI bus (`CS`, `SCK`, `MOSI`, `MISO`) instead of the 4-bit SDIO bus.
- **SPI (Serial Peripheral Interface)**: A high-speed, full-duplex 4-wire synchronous bus (`SCK` clock, `MOSI` Master-Out Slave-In, `MISO` Master-In Slave-Out, and `CS` Chip Select per device).
- **Task Watchdog Timer (`esp_task_wdt`)**: A hardware/software supervisory timer that resets the ESP32 or logs a panic if any registered task freezes (hangs in an infinite loop or deadlock) and fails to call `esp_task_wdt_reset()` within the timeout (`10 s`).
- **VFS (Virtual File System)**: An abstraction layer in ESP-IDF that maps POSIX path prefixes (like `/sdcard/access.csv`) to underlying storage drivers (`FATFS` on `SDSPI`), allowing standard C library calls (`fopen`, `fprintf`, `fgets`, `fclose`) to work seamlessly.

---

## E — Networking & Distributed Systems Terms

- **DHCP (Dynamic Host Configuration Protocol)**: The network protocol by which the Wi-Fi router (`Wokwi-GUEST`) automatically assigns an IPv4 address (e.g., `10.13.37.2`), subnet mask, gateway, and DNS server to the ESP32.
- **DNS (Domain Name System)**: Translates human-readable hostnames (like `broker.hivemq.com` or `pool.ntp.org`) into IPv4 addresses via UDP port 53.
- **`esp_event` (Event Loop)**: ESP-IDF's publish/subscribe event bus inside the MCU. Drivers (like `esp_wifi` and `esp_netif`) post asynchronous events (`WIFI_EVENT_STA_DISCONNECTED`, `IP_EVENT_STA_GOT_IP`) to the default event loop task, which invokes registered C callbacks without polling.
- **`esp_netif` (Network Interface Abstraction)**: The glue layer in ESP-IDF connecting a physical link driver (Wi-Fi STA/AP or Ethernet) to the TCP/IP stack (`lwIP`).
- **Idempotency**: The property where applying the same operation or message multiple times produces the exact same system state as applying it once (achieved in `net_protocol` by tracking `seq_id` per node and ignoring duplicates).
- **lwIP (Lightweight IP)**: The open-source TCP/IP protocol stack built into ESP-IDF implementing IPv4/IPv6, ICMP (ping), UDP, TCP, DHCP, DNS, and BSD sockets (`socket()`, `connect()`, `send()`, `recv()`).
- **MQTT (Message Queuing Telemetry Transport)**: A lightweight publish/subscribe messaging protocol running over TCP/IP (port 1883) designed for IoT devices. Clients publish messages to hierarchical **topics** (e.g., `cocis/smart_ctrl/node/1/tx`) and a central **broker** forwards them to subscribed clients.
- **QoS (Quality of Service in MQTT)**:
  - **QoS 0 (At most once)**: Fire-and-forget; no application ACK.
  - **QoS 1 (At least once)**: Receiver sends `PUBACK`; sender retries until acknowledged (may deliver duplicates).
  - **QoS 2 (Exactly once)**: 4-step handshake (`PUBLISH` -> `PUBREC` -> `PUBREL` -> `PUBCOMP`); highest overhead.
- **SNTP (Simple Network Time Protocol)**: A UDP protocol (port 123) used by the ESP32 to synchronize its internal Real-Time Clock (`RTC`) with atomic internet time servers (`pool.ntp.org`).
- **Store-and-Forward**: A reliability pattern where a remote terminal (`NODE_ROOM_1`) buffers telemetry locally in a ring buffer while disconnected from the network and replays the buffered packets in order once connectivity returns.
