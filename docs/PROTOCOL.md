# Inter-Terminal Communication & Wire Protocol (`cocis_protocol`)

**Project:** CoCIS Smart Control and Monitoring System (Makerere University, BSE III & CS II 2026/2027)  
**Terminals:** `NODE_ID_ENTRANCE` (0 — Hub Coordinator), `NODE_ID_ROOM_1` (1 — Lecture Room 1), `NODE_ID_ROOM_2` (2 — Lecture Room 2)  
**Transport:** MQTT v3.1.1 over lwIP TCP sockets (`broker.hivemq.com:1883`) via `Wokwi-GUEST` Virtual Wi-Fi

---

## 1. Protocol Overview & Security Principles

The inter-terminal protocol (`components/cocis_common/include/cocis_protocol.h`) guarantees:
1. **Effectively-Once Delivery Semantics**: Monotonically increasing 32-bit sequence numbers (`seq_id`) per source node paired with coordinator acknowledgments (`PROTO_MSG_ACK`) and sliding-window duplicate suppression.
2. **Cryptographic Authentication & Integrity (HMAC-SHA256)**: Every packet transmitted across the network carries an HMAC-SHA256 signature calculated over the JSON body using a pre-shared secret key (`CFG_SECRET_KEY`). Packets with missing, altered, or forged signatures are discarded with security alerts.
3. **Replay Attack Defense**: Packets with `seq_id <= last_rx_seq` are detected as duplicate/replayed and ignored at the domain layer (with duplicate ACKs sent to clear sender buffers).
4. **Offline Store-and-Forward Replay**: If the Entrance Hub goes offline, room nodes buffer up to 32 events locally in a circular FIFO ring buffer and replay them in strict chronological sequence upon reconnection.

---

## 2. Topic Taxonomy

All MQTT communication uses the project prefix `cocis/smart_ctrl_mak/`:

| Topic | Direction | Content / Purpose |
| :--- | :--- | :--- |
| `cocis/smart_ctrl_mak/room/1/telemetry` | Room 1 $\rightarrow$ Hub | Motion events, lighting transitions, and 5s heartbeats |
| `cocis/smart_ctrl_mak/room/2/telemetry` | Room 2 $\rightarrow$ Hub | Motion events, lighting transitions, and 5s heartbeats |
| `cocis/smart_ctrl_mak/room/1/control` | Hub $\rightarrow$ Room 1 | `PROTO_MSG_ACK`, lighting overrides (`PROTO_MSG_LIGHT_OVERRIDE`), time sync |
| `cocis/smart_ctrl_mak/room/2/control` | Hub $\rightarrow$ Room 2 | `PROTO_MSG_ACK`, lighting overrides, time sync |
| `cocis/smart_ctrl_mak/broadcast` | Hub $\rightarrow$ All | Building-wide emergency evacuation or schedule updates |

---

## 3. Message Types (`proto_msg_type_t`)

| Type Symbol | Hex ID | Direction | Requires ACK? | Description |
| :--- | :---: | :--- | :---: | :--- |
| `PROTO_MSG_HEARTBEAT` | `0x01` | Room $\rightarrow$ Hub | No | Periodic liveness ping with uptime, light state, and energy saved |
| `PROTO_MSG_ROOM_EVENT` | `0x02` | Room $\rightarrow$ Hub | **Yes** | Motion detected/cleared or light FSM state change |
| `PROTO_MSG_SCHEDULE_SYNC` | `0x03` | Hub $\rightarrow$ Room | **Yes** | Synchronizes access hours and vacancy timeouts |
| `PROTO_MSG_EMERGENCY` | `0x04` | Hub $\rightarrow$ Broadcast | **Yes** | Activates or clears building-wide emergency evacuation mode |
| `PROTO_MSG_LIGHT_OVERRIDE` | `0x05` | Hub $\rightarrow$ Room | **Yes** | Admin CLI force ON (1), force OFF (0), or release to AUTO (2) |
| `PROTO_MSG_ACK` | `0xF0` | Receiver $\rightarrow$ Sender | No | Acknowledges receipt of packet with matching `ack_seq_id` |

---

## 4. Wire Format & Cryptographic Signature

### 4.1 JSON Wire Representation
Packets are formatted as compact, human-readable JSON carrying the 64-character hex HMAC signature:

```json
{"v":1,"t":2,"src":1,"dst":0,"seq":104,"ack":0,"ts":1792904520,"rm":1,"occ":1,"lgt":1,"emg":0,"on_s":45,"sav":120,"up":310,"hmac":"7c8e9b42a1f056d8e2..."}
```

### 4.2 Field Definitions
| Field | Name | Type | Description |
| :---: | :--- | :---: | :--- |
| `v` | Version | `uint8` | Protocol version (always `1`) |
| `t` | Message Type | `uint8` | `proto_msg_type_t` enum code |
| `src` | Source Node | `uint8` | Node ID of sender (`0` = Hub, `1` = Room 1, `2` = Room 2) |
| `dst` | Destination Node | `uint8` | Target Node ID or `255` for Broadcast |
| `seq` | Sequence ID | `uint32` | Monotonically increasing sequence number |
| `ack` | Ack Sequence ID | `uint32` | Sequence number being acknowledged (for `PROTO_MSG_ACK`) |
| `ts` | Timestamp | `int64` | UNIX epoch in East Africa Time when event occurred |
| `rm` | Room ID | `uint8` | `1` or `2` for lecture room telemetry |
| `occ` | Occupied | `uint8` | `1` = motion detected, `0` = room vacant |
| `lgt` | Light State | `uint8` | `0` = OFF_VACANT, `1` = ON_ACTIVE, `2` = ON_GRACE_HOLD |
| `emg` | Emergency Active | `uint8` | `1` = evacuation active, `0` = normal operation |
| `on_s`| Total Light On | `uint32` | Cumulative seconds lighting fixture was energized |
| `sav` | Energy Saved | `uint32` | Cumulative energy saved in milliwatt-hours (`mWh`) |
| `up` | Uptime | `uint32` | Sender node uptime in seconds |
| `hmac`| HMAC Signature | `string` | 64-hex HMAC-SHA256 computed over the JSON body |

### 4.3 HMAC-SHA256 Signing & Verification Algorithm
1. Senders serialize all packet fields into JSON without the `"hmac"` field:
   `{"v":1,"t":2,"src":1,"dst":0,"seq":104,"ack":0,"ts":1792904520,"rm":1,"occ":1,"lgt":1,"emg":0,"on_s":45,"sav":120,"up":310}`
2. Senders compute:
   $$\text{HMAC} = \text{HMAC-SHA256}(K, \text{body})$$
   where $K$ is the pre-shared secret key (`CFG_SECRET_KEY`).
3. Senders append `,"hmac":"<64 hex characters>"}` to produce the final wire payload.
4. Receivers isolate the received `hmac` hex string, reconstruct the payload body, compute the expected HMAC, and verify equality using constant-time string comparison (`cocis_crypto_verify_hmac`).
5. If verification fails, the packet is discarded and an audit security log is generated.

---

## 5. Sequence Tracking, Deduplication & Store-and-Forward

```mermaid
sequenceDiagram
    autonumber
    participant R as Room 1 Terminal
    participant M as MQTT Broker
    participant H as Entrance Hub Coordinator

    Note over R,H: Scenario 1: Normal Live Telemetry
    R->>M: PUBLISH room/1/telemetry (seq=101, occ=1, lgt=1, HMAC)
    M->>H: DELIVER room/1/telemetry
    H->>H: Verify HMAC & Check seq > last_rx_seq
    H->>H: Append /sdcard/lighting.csv
    H->>M: PUBLISH room/1/control (type=ACK, ack_seq=101, HMAC)
    M->>R: DELIVER room/1/control
    R->>R: Clear pending ack slot

    Note over R,H: Scenario 2: Hub Offline -> Store-and-Forward
    Note over H: Hub Disconnected from Wi-Fi
    R->>R: Motion cleared -> Light FSM enters GRACE_HOLD
    R->>R: Light turns OFF -> Packet pushed to 32-entry room_buffer
    Note over H: Hub Reconnects to Wi-Fi
    R->>M: PUBLISH room/1/telemetry (seq=102, occ=0, lgt=0, original ts, HMAC)
    M->>H: DELIVER room/1/telemetry
    H->>H: Verify HMAC & Log buffered event
    H->>M: PUBLISH room/1/control (ACK ack_seq=102)
    M->>R: DELIVER ACK
    R->>R: Pop seq=102 from room_buffer
```
