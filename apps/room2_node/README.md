# Application Documentation: Lecture Room 2 Terminal (`apps/room2_node`)

**Node ID:** `2` (`NODE_ROOM_2`)  
**Firmware Binary:** `apps/room2_node/build/room2_node.bin`  
**Wokwi Configuration:** `apps/room2_node/wokwi.toml` & `apps/room2_node/diagram.json`  
**Target Chip:** ESP32-WROOM-32 (Dual-core Xtensa LX6 @ 160MHz)

---

## 1. Executive Summary & Architectural Role

The **Lecture Room 2 Terminal** is structurally and logically equivalent to Room 1, controlling Lecture Room 2 in the CoCIS facility:
1. **Dedicated Peripherals**: Monitored by its own PIR sensor on `GPIO 26` and lighting relay on `GPIO 25`.
2. **Autonomous Edge Operation**: Senses motion, drives the 3-state lighting finite state machine (`OFF_VACANT`, `ON_ACTIVE`, `ON_GRACE_HOLD`), and controls illumination without reliance on the network or central coordinator.
3. **Telemetry & Synchronization**: Publishes HMAC-SHA256 authenticated telemetry frames to MQTT topic `cocis/smart_ctrl_mak/hub/ingress` and listens on `cocis/smart_ctrl_mak/room2/command` for supervisor lighting overrides.
4. **Resilient Buffering**: Maintains its own 32-entry circular FIFO ring buffer (`room_buffer.c`) to survive hub or network dropouts with zero telemetry loss.

---

## 2. Hardware Pinout & Wiring Specifications (`diagram.json`)

| Peripheral / Signal | ESP32 GPIO | Electrical Protocol | Function / Behavior |
| :--- | :--- | :--- | :--- |
| **PIR Motion Sensor** | `GPIO 26` | Active-High GPIO Input | Detects human presence with 100 ms hardware debounce |
| **Light Actuator Relay** | `GPIO 25` | Active-High GPIO Output | Drives yellow indicator LED simulating 240V lighting relay |
| **System Status LED (Blue)**| `GPIO 2` | Active-High GPIO Output | Pulses during Wi-Fi / MQTT communication; solid when online |
| **Console UART0 TX** | `GPIO 1` | UART Serial (115200 bps) | Diagnostic serial monitor output |
| **Console UART0 RX** | `GPIO 3` | UART Serial (115200 bps) | Diagnostic serial console input |

---

## 3. Local CLI Commands
Run these commands in the terminal connected to Room 2:
- `status`: Show current lighting state, relay status, motion sensor level, and network link state.
- `motion on`: Manually trigger simulated PIR motion.
- `motion off`: Stop simulated PIR motion (starts 15s vacancy countdown).
- `buffer`: Inspect store-and-forward queue depth and replayed count.
- `clear`: Clear console screen.
- `help`: Print command menu.
