# ESP32 Solar Energy Management System

Production-oriented Arduino Framework firmware for a solar-only smart energy management system.

## Hardware
- ESP32
- Solar voltage divider on GPIO 35
- Per-appliance INA219 sensors, for example Fridge `0x40`, Pump `0x41`, Lights `0x44`
- GPIO relays per appliance

## Communications
The firmware architecture is HTTP REST plus WebSocket only. REST endpoints are defined for `/api/system`, `/api/loads`, `/api/solar`, `/api/battery`, `/api/recommendations`, `POST /api/load/{id}`, and `POST /api/preferences`. WebSocket events carry live sensor updates, relay changes, A* decisions, alarms, and recommendations.

Unsupported communication stacks have been removed; the communication surface is REST and WebSocket only.
