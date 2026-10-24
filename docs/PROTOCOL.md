# Inter-Terminal Communication Protocol (`net_protocol`)

**Project:** CoCIS Smart Control and Monitoring System  
**Terminals:** `NODE_ENTRANCE` (0 — Coordinator), `NODE_ROOM_1` (1 — Lecture Room 1), `NODE_ROOM_2` (2 — Lecture Room 2)

---

## 1. Protocol Design Goals

Requirements 1.3 and 1.7 specify that the three ESP32 terminals must coordinate reliably as one integrated solution without inconsistent records, lost events, or duplicate state changes.

Our inter-terminal protocol (`net_protocol`) provides:
1. **Transport Independence**: Identical packet structure (`proto_packet_t`) and JSON wire serialization over both the **in-memory FreeRTOS Virtual Bus** (for single-ESP32 Wokwi multi-terminal simulation) and **MQTT v3.1.1 over lwIP TCP sockets** (for multi-ESP32 Wi-Fi deployment).
2. **At-Least-Once Delivery with Idempotent Deduplication (Effectively-Once Semantics)**: Every state-changing event carries a monotonically increasing 32-bit `seq_id` per source node and requires a `PROTO_MSG_ACK` from the coordinator.
3. **Offline Store-and-Forward Replay**: If a lecture room terminal loses connectivity to the Main Entrance coordinator, it buffers up to `32` timestamped events locally and replays them in strict FIFO order upon reconnection.
4. **Single Authoritative Writer**: Only `NODE_ENTRANCE` writes to the shared occupancy records, energy aggregates, and SD card logs, eliminating distributed write-write conflicts.

---

## 2. Node Addressing & Message Types

### 2.1 Node Identifiers (`node_id_t`)

| Enum Symbol | Numeric ID | Role | Direction |
| :--- | :---: | :--- | :--- |
| `NODE_ID_ENTRANCE` | `0` | Main Entrance Authoritative Coordinator | Receives telemetry; broadcasts config/emergency |
| `NODE_ID_ROOM_1` | `1` | Lecture Room 1 Controller | Sends PIR/Light telemetry & heartbeats; receives config |
| `NODE_ID_ROOM_2` | `2` | Lecture Room 2 Controller | Sends PIR/Light telemetry & heartbeats; receives config |
| `NODE_ID_BROADCAST` | `255` | All Terminals | Used by Entrance for emergency unlock & schedule sync |

### 2.2 Message Types (`proto_msg_type_t`)

| Message Type | ID | Direction | Requires ACK? | Purpose |
| :--- | :---: | :--- | :---: | :--- |
| `PROTO_MSG_HEARTBEAT` | `0x01` | Room -> Entrance | No | Periodic liveness ping with uptime, light state, and free heap |
| `PROTO_MSG_ROOM_EVENT` | `0x02` | Room -> Entrance | **Yes** | Occupancy transition or light ON/OFF/GRACE state change + energy delta |
| `PROTO_MSG_SCHEDULE_SYNC` | `0x03` | Entrance -> Room | **Yes** | Pushes updated building access schedule & vacancy timeout |
| `PROTO_MSG_EMERGENCY` | `0x04` | Entrance -> Broadcast | **Yes** | Activates/clears building-wide Emergency Evacuation Mode |
| `PROTO_MSG_ACK` | `0xF0` | Receiver -> Sender | No | Acknowledges receipt of packet with matching `seq_id` |

---

## 3. Packet Structure & Wire Format

### 3.1 In-Memory C Representation (`proto_packet_t`)

```c
typedef struct {
    uint8_t  version;           /* Protocol version (1) */
    uint8_t  msg_type;          /* proto_msg_type_t */
    uint8_t  src_node;          /* node_id_t of sender */
    uint8_t  dst_node;          /* node_id_t of receiver */
    uint32_t seq_id;            /* Monotonic sequence number per src_node */
    uint32_t ack_seq_id;        /* Sequence number being acknowledged (for PROTO_MSG_ACK) */
    int64_t  timestamp_epoch;   /* EAT UNIX epoch when event occurred */
    uint8_t  room_id;           /* 1 or 2 for room events */
    uint8_t  occupied;          /* 0 = vacant, 1 = occupied */
    uint8_t  light_state;       /* 0 = OFF, 1 = ON, 2 = GRACE_HOLD */
    uint8_t  emergency_active;  /* 0 = normal, 1 = evacuation active */
    uint32_t light_on_sec;      /* Cumulative seconds light was ON */
    uint32_t energy_saved_mwh;  /* Estimated energy saved in milliwatt-hours */
    uint32_t uptime_sec;        /* Sender node uptime in seconds */
} proto_packet_t;
```

### 3.2 JSON Wire Format (MQTT Payload & Serial Debug Trace)

When transmitted over MQTT topics (`cocis/smart_ctrl/node/<src>/tx`), packets are serialized as compact, human-inspectable JSON:

```json
{"v":1,"t":2,"src":1,"dst":0,"seq":104,"ack":0,"ts":1792820415,"rm":1,"occ":1,"lgt":1,"emg":0,"on_s":45,"sav":120,"up":310}
```

---

## 4. Reliability, Retry, Deduplication & Store-and-Forward Rules

### 4.1 Sequence Numbers & ACK Retry State Machine
1. Each node maintains its own atomic 32-bit counter `s_next_seq_id[node_id]` starting at `1`.
2. When a room node emits a `PROTO_MSG_ROOM_EVENT`:
   - It assigns `pkt.seq_id = s_next_seq_id++` and timestamps `pkt.timestamp_epoch = time(NULL)` **at the moment of detection** (so replayed events preserve their true occurrence time).
   - If the node is online, it transmits the packet immediately and places a copy in its **Pending ACK Slot** (`retry_count = 0`, `timeout = 1000 ms`).
   - If `PROTO_MSG_ACK` with `ack_seq_id == pkt.seq_id` is not received within `1000 ms`, the node retransmits up to `PROTO_MAX_RETRIES` (`3` attempts) with backoff (`1000 ms`, `2000 ms`, `4000 ms`).
   - After 3 failed retries (or immediately if link state is `OFFLINE`), the packet is moved into the node's **32-entry Store-and-Forward Ring Buffer**.

### 4.2 Idempotent Duplicate Suppression at the Coordinator
What happens if `NODE_ROOM_1` sends `seq=104`, `NODE_ENTRANCE` processes it and sends `PROTO_MSG_ACK(104)`, but the `ACK` packet is lost on the network?
- `NODE_ROOM_1` times out and retransmits `seq=104`.
- `NODE_ENTRANCE` maintains `s_last_seen_seq[NODE_COUNT]` (and a 16-entry sliding window bitmap of recently processed `seq_id`s per source node).
- Seeing that `seq=104` from `src_node=1` was **already processed**, `NODE_ENTRANCE` **does not** log a duplicate CSV row or double-count energy statistics—it simply re-sends `PROTO_MSG_ACK(104)`!

### 4.3 Store-and-Forward Replay on Reconnect
When a room node transitions from `OFFLINE` back to `ONLINE` (or receives a heartbeat response from the coordinator):
1. It iterates through its **Store-and-Forward Ring Buffer** from oldest (`head`) to newest (`tail`).
2. Each buffered `proto_packet_t` is transmitted in strict sequence order and acknowledged before advancing `head`.
3. Only after the replay buffer is drained do new live events jump straight to transmission, ensuring causal ordering (`Motion ON` at `t=10s` is never delivered after `Vacancy OFF` at `t=25s`).
