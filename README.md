# Low-Power ESP32 E-Ink Image Display

Battery-powered ESP32 device that periodically wakes from deep sleep, fetches a **pre-rendered bitmap image** over Wi‑Fi, renders it on an e‑ink display, and returns to sleep.

This README documents the project **from the Arduino / ESP32 firmware and hardware perspective only**.

ℹ️ see the server side [list-service supporting repo](https://github.com/razelkayamdev/list-service)

---

## Project Overview

- ESP32 remains in **deep sleep** most of the time
- Wakes up on a **fixed interval** (default: 10 minutes)
- Connects to Wi‑Fi with a timeout
- Fetches data via HTTPS
- Renders a **server-generated PBM (P4) bitmap** on a **4.2" e‑ink display (400×300)**
- Preserves the last successful image in RAM (and optionally NVS / filesystem)
- Returns to deep sleep immediately after refresh

Target use cases:
- Always-on information board
- Long battery life (weeks to months)
- Deterministic rendering (layout handled server-side)

---

## Hardware Components

### Main Board
- **Waveshare Universal e‑Paper Raw Panel Driver Board**
  - ESP32-based
  - Integrated Wi‑Fi + Bluetooth
  - Direct connector for Waveshare raw e‑ink panels

### Display
- **Waveshare 4.2" E‑Ink Raw Panel**
  - Resolution: 400 × 300
  - Black / White (1‑bit)
  - Ultra-low power, no power draw when static

### Power System
- **Li‑Po Battery**
  - 3.7 V nominal
  - ~10 000 mAh recommended for long runtime

- **TP4056 Lithium Battery Charger Module**
  - USB (Micro‑USB / USB‑C variants)
  - With protection (over‑charge / over‑discharge)
  - Handles battery charging only (not regulation)

- **Pololu S7V8F5 Step‑Up / Step‑Down Regulator**
  - Regulated 5 V output
  - Very low quiescent current
  - Suitable for deep‑sleep applications

### Power Topology

Li‑Po Battery
↓
TP4056 (charge + protection)
↓
Pololu S7V8F5 (5 V regulated)
↓
Waveshare ESP32 Driver Board

---

## Firmware Environment

- **Framework:** Arduino (ESP32 core)
- **Supported build tools:**
  - Arduino IDE
  - PlatformIO

### Required Libraries
- ESP32 Arduino core
- WiFi / WiFiClientSecure
- GxEPD2 (Waveshare e‑ink display library)

> Note: JSON parsing is no longer required in the current design. All layout and rendering logic is handled server‑side.

---

## Device Behavior (Current)

1. Boot from deep sleep
2. Initialize e‑ink display (paged mode)
3. Connect to Wi‑Fi (with timeout)
4. Sync time via NTP (required for TLS)
5. Perform HTTPS GET to a configured endpoint
6. Stream and parse a **PBM P4 (image/x‑portable‑bitmap)** response
7. Validate header (P4, width=400, height=300)
8. Extract exactly **15000 bytes** of bitmap data
9. Render bitmap using paged drawing (`firstPage()` / `nextPage()`)
10. Enter deep sleep

If Wi‑Fi or HTTP fails:
- Previous image remains visible on the e‑ink display
- Device returns to deep sleep without crashing

---

## Rendering Model

- The server generates a **final 1‑bit bitmap** (PBM P4)
- ESP32 does **no layout, text wrapping, or font rendering**
- Bitmap is rendered 1:1 at native resolution (400×300)
- Rendering uses paged drawing to minimize RAM usage
- No drawing occurs after the bitmap render, ensuring the image remains visible

This approach:
- Simplifies firmware logic
- Makes rendering deterministic
- Moves layout complexity to the backend

---

## Configuration (Firmware)

All configuration is compile‑time:

- Wi‑Fi SSID / password
- Remote HTTPS URL (PBM endpoint)
- Wake interval (seconds)
- Preferred display rotation
- Debug logging on/off

---

## Power Considerations

- Bluetooth is disabled
- Wi‑Fi is only enabled during fetch
- CPU frequency can be reduced
- Serial logging should be disabled in production
- E‑ink redraws are minimized to reduce ghosting and wake time

Typical active time per cycle: **a few seconds**  
Deep sleep current: **tens of µA (board‑dependent)**

---

## Data Persistence (Optional)

- Last successful bitmap can be stored in:
  - NVS, or
  - SPIFFS / LittleFS
- Used on next boot if the network is unavailable

---

## Optional Extensions

- Conditional refresh (ETag / hash‑based)
- Partial e‑ink refresh overlays
- OTA firmware updates
- Server‑side bitmap caching

---

## Scope Notes

- This project does **not** define:
  - Backend implementation
  - Authentication
  - UI beyond bitmap rendering
- The device is intentionally **headless** and unattended

---

## Status

Hardware validated.  
End‑to‑end HTTPS → PBM streaming → e‑ink rendering working.  
Architecture ready for power‑optimization and feature extensions.
