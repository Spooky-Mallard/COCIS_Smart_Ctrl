# System Verification & Multi-Node Testing Guide

**Project:** CoCIS Smart Control and Monitoring System (Makerere University, BSE III & CS II 2026/2027)  
**Nodes:** `apps/entrance_node` (Hub), `apps/room1_node` (Room 1), `apps/room2_node` (Room 2)

---

## 1. How to Launch and Simulate All 3 Nodes

Because Wokwi runs one simulation per project diagram, each node is an independent application:

### Option A: Three VS Code Windows (Recommended for Side-by-Side View)
1. Open three separate VS Code windows.
2. In Window 1: Open folder `/Users/spooky/.../COCIS_Smart_Ctrl/apps/entrance_node`. Open `diagram.json` and press `Cmd+Shift+P` $\rightarrow$ `Wokwi: Start Simulator`.
3. In Window 2: Open folder `/Users/spooky/.../COCIS_Smart_Ctrl/apps/room1_node`. Open `diagram.json` and start simulator.
4. In Window 3: Open folder `/Users/spooky/.../COCIS_Smart_Ctrl/apps/room2_node`. Open `diagram.json` and start simulator.
5. All three nodes automatically connect to virtual Wi-Fi `Wokwi-GUEST` and link together over the public MQTT broker (`broker.hivemq.com:1883`).

### Option B: Wokwi Web / Browser Tabs
1. Open three browser tabs on [Wokwi.com](https://wokwi.com).
2. Upload the respective `diagram.json` and compiled `.bin` for Entrance Node, Room 1, and Room 2.
3. Start simulation in all three tabs.

---

## 2. The 10 Core Verification Scenarios

| Test ID | Test Scenario | Expected Outcome | Verification Command / Action |
| :---: | :--- | :--- | :--- |
| **TC-01** | **Valid Entry Access** | Green LED lights, door servo opens 90° for 3s, buzzer double-beeps, headcount increments to 1, CSV log written. | Push `btn_entry` on Entrance canvas or run `sim scan entry 04A1B2C3` |
| **TC-02** | **Strict Anti-Passback** | Scanning the same card twice on entry without an exit generates `ACCESS_DENIED`, red alarm LED, and security audit log. | Push `btn_entry` twice in succession |
| **TC-03** | **Valid Exit Access** | Exit servo opens, status LED flashes, headcount decrements to 0, duration recorded on SD card. | Push `btn_exit` on Entrance canvas or run `sim scan exit 04A1B2C3` |
| **TC-04** | **Exit Outside Hours & Override** | Exit scan during restricted night hours is blocked; admin executes `override_exit` allowing safe departure. | Run `time set 2026-10-25 23:30:00`, scan exit, observe deny; run `override_exit 04A1B2C3`, re-scan. |
| **TC-05** | **Emergency Evacuation** | All door servos open to 90°, alarm siren sounds, room lights turn ON building-wide, live roll call printed. | Push `btn_emerg` or run `emergency on` on Entrance CLI |
| **TC-06** | **Autonomous Lighting FSM** | PIR motion immediately turns ON room light; vacancy after 15s turns light OFF and records energy saved. | Type `motion` in Room 1 CLI, observe yellow LED; type `clear`, observe countdown to OFF. |
| **TC-07** | **Hub Offline Buffering** | When Hub is disconnected, Room node continues local lighting and buffers telemetry in circular FIFO queue. | Disconnect Hub; trigger motion in Room 1 CLI (`motion` $\rightarrow$ `clear`); verify buffer depth is 2. |
| **TC-08** | **In-Order Replay** | Reconnecting Hub triggers chronological replay of buffered events; Hub logs events with original timestamps. | Reconnect Hub; observe Room 1 logs `Flushed buffered packet seq=...` and Hub logs `Recorded telemetry`. |
| **TC-09** | **Node Offline Alert** | If Room 1 loses power, Hub flags `ROOM 1: UNREACHABLE` after 15s (3 missed heartbeats) on LCD and CLI. | Halt Room 1 simulator; wait 15s; run `rooms` on Entrance CLI. |
| **TC-10** | **HMAC Tamper Rejection** | Modifying any byte or forging packets with an invalid key results in immediate rejection and security alert. | Run automated selftest Option [8] on Entrance Hub CLI. |

---

## 3. Automated Self-Test Demo

On the Entrance Hub console, enter Option `[8]` (or command `sim selftest`). The coordinator executes an automated script covering all 10 scenarios and prints a formatted verification report:

```
==============================================================================
               AUTOMATED SYSTEM VERIFICATION SUMMARY REPORT
==============================================================================
 Test Scenario                                    | Result
--------------------------------------------------+---------------------------
 1. Normal Valid Entry & Servo Actuation          | PASS
 2. Strict Anti-Passback Enforcement              | PASS
 3. Normal Valid Exit & Ledger Decrement          | PASS
 4. After-Hours Exit Restriction & Safe Override  | PASS
 5. Emergency Evacuation Mode                     | PASS
 6. Room Autonomous Lighting FSM & Energy Logic   | PASS
 7. Monotonic Protocol Sequencing & ACKs          | PASS
 8. Idempotent Coordinator Duplicate Suppression  | PASS
 9. Store-and-Forward Replay on Reconnect         | PASS
 10. Tamper-Evident SHA-256 Hash Chain Integrity   | PASS
--------------------------------------------------+---------------------------
 OVERALL STATUS: ALL 10 TESTS PASSED (100% SPECIFICATION COMPLIANCE)
==============================================================================
```
