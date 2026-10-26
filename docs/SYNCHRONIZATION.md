# Distributed Synchronization & Resource Integrity

**Project:** CoCIS Smart Control and Monitoring System (Makerere University, BSE III & CS II 2026/2027)  
**Scope:** Distributed Concurrency Control, Single-Writer Authoritative Consistency & In-Order Replay

---

## 1. Concurrency Challenges in Multi-ESP32 Systems

Section 1.7 of the coursework specification requires the system to prevent **conflicts, inconsistent records, race conditions, and event loss** across distributed ESP32 terminals.

Our distributed architecture addresses four specific synchronization hazards:

| Hazard | Description | Engineering Mitigation |
| :--- | :--- | :--- |
| **Write-Write Conflict** | Two nodes updating shared records simultaneously | **Single Authoritative Writer Pattern**: Only `apps/entrance_node` writes to the SD card and authoritative occupancy records. Room terminals act as telemetry publishers and control subscribers. |
| **Out-of-Order Delivery** | Network latency delivers `Vacancy OFF` before `Motion ON` | **Monotonic Sequence Numbers (`seq_id`) + FIFO Queue**: Events carry timestamps and sequence numbers assigned at generation time; room buffers drain in strict FIFO order. |
| **Network Partition / Hub Outage** | Hub disconnects while occupants enter lecture rooms | **Autonomous Edge FSM + Store-and-Forward**: Room nodes continue local lighting control with zero latency, queueing up to 32 unsent events in local RAM. |
| **Duplicate Message Execution** | Re-sent packets causing double-counted energy or double logs | **Idempotent Duplicate Suppression**: Hub tracks `last_rx_seq` per node; duplicate packets generate ACKs but do not create duplicate SD card logs. |

---

## 2. Mutex Locking Hierarchy (Entrance Hub)

To guarantee that internal FreeRTOS tasks on the Entrance Hub cannot deadlock, locks must be acquired in strictly increasing numerical order and released in reverse order:

```
Level 1: LOCK_LEVEL_CONFIG      (NVS Configuration)
   └── Level 2: LOCK_LEVEL_USER_DB     (Enrolled Users & Credentials)
         └── Level 3: LOCK_LEVEL_SCHEDULE    (Access Windows & Overrides)
               └── Level 4: LOCK_LEVEL_LEDGER      (Who-Is-Inside Roll Call)
                     └── Level 5: LOCK_LEVEL_ROOM        (Room Telemetry Cache)
                           └── Level 6: LOCK_LEVEL_DISPLAY     (I2C LCD Bus)
                                 └── Level 7: LOCK_LEVEL_SD          (SPI SDSPI FATFS VFS)
```

> [!IMPORTANT]
> **Strict Lock Ordering Rule**
> A thread holding lock level $L_i$ may only acquire lock level $L_j$ if $j > i$. Holding `LOCK_LEVEL_SD` while requesting `LOCK_LEVEL_LEDGER` is strictly prohibited and caught by asserts in debug builds.

---

## 3. Distributed Consistency Model

### 3.1 Causal Ordering Guarantee
When a student triggers motion at $t_1$, turns off motion at $t_2$, and triggers motion again at $t_3$:
- The Room terminal assigns `seq_id = 101, 102, 103` respectively.
- Even if the network drops between $t_1$ and $t_3$, events remain sequenced in the 32-entry `room_buffer`.
- When replayed, event $101$ must be received and processed before event $102$, ensuring causal consistency.

### 3.2 Idempotent Duplicate Handling
If Room 1 sends `seq=104`, Hub processes it and publishes `PROTO_MSG_ACK(104)`, but the ACK packet is lost on the network:
- Room 1 times out and re-sends `seq=104`.
- Hub checks `seq=104 <= last_rx_seq[1]` (which is already `104`).
- Hub flags the packet as a duplicate, skips SD card logging to avoid corrupting analytics, and re-transmits `PROTO_MSG_ACK(104)`.
- Room 1 receives the duplicate ACK and clears the packet from its buffer.

---

## 4. Dual-Tier Mutex Architecture & Namespace Isolation

To prevent symbol collision across shared components and application layers, the system partitions synchronization primitives into two distinct tiers:

### 4.1 Tier 1: Common Distributed Primitives (`components/cocis_common/include/cocis_sync.h`)
Shared by all nodes, using the strict namespace prefix `COCIS_`:
```c
typedef enum {
    COCIS_LOCK_LEVEL_NONE     = 0,
    COCIS_LOCK_LEVEL_NET_BUF  = 1, /* Socket & frame serialization buffers */
    COCIS_LOCK_LEVEL_PEER_TAB = 2, /* Node state & sequence tracking table */
    COCIS_LOCK_LEVEL_STORAGE  = 3, /* NVS / flash state persistence */
    COCIS_LOCK_LEVEL_COUNT    = 4
} cocis_lock_level_t;
```
Event group bits are similarly scoped:
- `COCIS_EVT_WIFI_CONNECTED` (`BIT0`)
- `COCIS_EVT_TIME_SYNCED` (`BIT1`)
- `COCIS_EVT_MQTT_CONNECTED` (`BIT2`)
- `COCIS_EVT_EMERGENCY_ACTIVE` (`BIT3`)

### 4.2 Tier 2: Entrance Hub Local Mutexes (`apps/entrance_node/main/include/sync.h`)
Specific to the central coordinator's complex multi-subsystem environment:
- `LOCK_LEVEL_CONFIG`
- `LOCK_LEVEL_USER_DB`
- `LOCK_LEVEL_SCHEDULE`
- `LOCK_LEVEL_LEDGER`
- `LOCK_LEVEL_ROOM`
- `LOCK_LEVEL_DISPLAY`
- `LOCK_LEVEL_SD`

> [!NOTE]
> **Boot Initialization Order**
> In `apps/entrance_node/main/main.c`, both synchronization tiers must be initialized before any tasks are created:
> ```c
> ESP_ERROR_CHECK(sync_init());       /* Initialize local Hub mutex array */
> ESP_ERROR_CHECK(cocis_sync_init()); /* Initialize shared component mutexes */
> ```

---

## 5. Interrupt Service Routine (ISR) Concurrency Boundaries

Hardware interrupts on the ESP32 (such as pushbuttons on `GPIO 32, 33` or PIR motion sensors on `GPIO 27, 26`) execute with the CPU in interrupt mode (`xPortInIsrContext() == true`). 

### 5.1 What ISRs CANNOT Do:
1. **Never acquire a Mutex**: FreeRTOS mutexes implement priority inheritance, which modifies the Thread Control Block (TCB) of the lock holder. An ISR has no TCB, and calling `xSemaphoreTake` or `xSemaphoreTakeRecursive` inside an ISR causes an immediate `abort()` in `lock_acquire_generic`.
2. **Never call Blocking APIs**: `vTaskDelay`, `xQueueReceive(..., portMAX_DELAY)` cannot block inside hardware interrupt context.
3. **Never call Dynamic Allocations or Standard I/O**: `malloc`, `free`, `printf`, and `ESP_LOGI` acquire internal newlib/heap reentrant locks and will crash the kernel.

### 5.2 The Producer-Consumer Queue Pattern
To process PIR motion events safely, we decouple the interrupt using an unblocking queue:

```c
/* Hardware ISR - Must be fast and unblocking */
static void IRAM_ATTR pir_isr_handler(void *arg) {
    int level = gpio_get_level(s_pin);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_pir_queue, &level, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/* Worker Task - Normal task context with full mutex & logging privileges */
static void pir_task(void *pvParameters) {
    int level = 0;
    while (1) {
        if (xQueueReceive(s_pir_queue, &level, portMAX_DELAY) == pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(100)); /* Debounce */
            if (gpio_get_level(s_pin) == level && s_callback) {
                s_callback(level); /* Safely acquires FSM mutex and logs */
            }
        }
    }
}
```
