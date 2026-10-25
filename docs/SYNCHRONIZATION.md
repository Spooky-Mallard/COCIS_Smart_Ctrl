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
