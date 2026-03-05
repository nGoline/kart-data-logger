# 🏎️ ESP32 Tri-Node Kart Telemetry System

A professional-grade, distributed datalogging and telemetry dashboard designed for open-track go-karts. Built on the ESP32-S3/XIAO ecosystem, this system uses a decoupled "Tri-Node" architecture communicating via high-speed ESP-NOW to isolate vehicle dynamics, GPS tracking, and UI rendering.



## 🏗️ The Tri-Node Architecture

To combat high-frequency chassis vibrations, GPS signal blocking (Faraday cages), and internal silicon bus contention, the system is split into three dedicated physical nodes:

1. **The Chassis Node (Seeed XIAO ESP32)**
   * **Role:** Pure Vehicle Dynamics.
   * **Mount:** Rigidly mounted to the kart chassis (seat strut/steering column).
   * **Function:** Runs a high-speed 50Hz FreeRTOS task reading an MPU6050 IMU. Broadcasts raw lateral/longitudinal G-forces and gyro data over ESP-NOW.
   
2. **The Helmet Node (Seeed XIAO ESP32)**
   * **Role:** The Brain, Aggregator & Audio Narrator.
   * **Mount:** Inside the driver's helmet (shock-absorbed by the human body).
   * **Function:** Polls a U-Blox GPS at 5Hz. Listens to the Chassis IMU to run anti-ghosting velocity filters. Aggregates GPS, G-Force, and Battery data into a single `TelemetryMsg` and broadcasts it. Drives an I2S amplifier for real-time audio cues (lap times, satellite counts, system faults).

3. **The Steering Wheel Node (CYD - Cheap Yellow Display)**
   * **Role:** UI Dashboard & Data Logger.
   * **Mount:** Steering wheel.
   * **Function:** Listens for the aggregated 5Hz telemetry stream. Updates the LVGL-based GUI (Speed, G-Force needle, GPS link health) and streams the perfectly synced structs directly to an SD card via a non-blocking queue.

## ✨ Key Technical Features

* **Dual-Core FreeRTOS Architecture:** Sensor polling runs strictly on Core 1 (Priority 3) while Audio/UI/SD Logging runs on Core 0 (Priority 2). This ensures exact 5Hz/50Hz polling without jitter.
* **Auto-Healing I2C Bus:** Custom software wrapper that detects SPI Flash ISR latency crashes (`ESP_ERR_INVALID_STATE [259]`) and automatically nukes and reboots the I2C driver in <40ms without dropping telemetry.
* **Hardware Bus Mutexing:** Implements a global FreeRTOS "Talking Stick" (`hardwareBusMutex`) to prevent `LittleFS` audio loading from colliding with I2C sensor reads on the internal memory matrix.
* **Smart GPS Deadbanding:** Fuses IMU dynamic data with GPS speed to completely eliminate "ghost speeds" caused by multipath interference when the kart is stationary.
* **Automated Build Scripts:** Includes a custom Python pre-build script (`fix_stride.py`) that mathematically calculates and injects `.header.stride` into SquareLine Studio LVGL 9 image exports on the fly.

## 🧰 Hardware Requirements

* 1x ESP32-3428S035 "Cheap Yellow Display" (CYD)
* 2x Seeed Studio XIAO ESP32 (or similar compact ESP32 boards)
* 1x U-Blox GPS Module (e.g., NEO-6M / M8N)
* 1x MPU6050 6-DoF IMU
* 1x I2S Audio Amplifier (e.g., MAX98357A) + Micro Speaker
* SD Card (Formatted FAT32)

## 💻 Software Dependencies

Built using **PlatformIO**. 

**Libraries:**
* `lvgl/lvgl` (v9.x)
* `rzeldent/esp32_smartdisplay`
* `mikalhart/TinyGPSPlus`
* `ElectronicCats/MPU6050`
* `espressif/esp32-audioI2S`

## 🚀 Setup & Installation

1. Clone this repository.
2. Open the project in **VSCode + PlatformIO**.
3. Use the included SquareLine Studio project file to edit the UI. When exporting UI files to `/lib/ui/`, the `fix_stride.py` script will automatically patch them during the PlatformIO build process.
4. Flash the respective environments:
   * `pio run -e cyd_display -t upload`
   * `pio run -e helmet_node -t upload`
   * `pio run -e chassis_node -t upload`
5. Ensure all LittleFS partitions are uploaded (specifically for the Helmet Node to load the `.wav` audio cues).

## 📡 Protocol / ESP-NOW
All nodes are hardcoded to **WiFi Channel 6** to eliminate scanner-hopping latency.

```c
#define MSG_IMU_RAW 0x01
#define MSG_TELEMETRY 0x02
```

See [EspNowProtocol.h](lib/Shared/EspNowProtocol.h) for complete packet struct definitions.