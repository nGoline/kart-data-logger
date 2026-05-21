# ESP32 Kart Telemetry System

A distributed telemetry and datalogging system for open-track go-karts. The project uses a helmet-mounted logger node paired with an ESP32 display dashboard over ESP-NOW.

## System Architecture

The current firmware supports two primary targets:

1. **Helmet Logger Node**
   * **Board:** Seeed XIAO ESP32S3
   * **Role:** GPS acquisition, battery monitoring, audio cues, error logging, and telemetry broadcast.
   * **Features:**
     * GPS from u-blox (default) or ATGM336.
     * Battery percentage sampling.
     * Audio playback through I2S.
     * Helmet error log capture and transfer.
     * Telemetry packet composition using GPS, IMU feedback, and battery state.

2. **Display Dashboard Node**
   * **Board:** ESP32-3248S035C
   * **Role:** UI dashboard, SD logging, and optional IMU-based speed filtering.
   * **Features:**
     * Receives telemetry from the helmet logger over ESP-NOW.
     * Displays speed, G-force, GPS satellite count, and helmet battery state.
     * Logs telemetry to SD card.
     * Sends IMU feedback to the helmet logger when `ENABLE_IMU` is enabled.
     * Receives helmet error logs and writes them to SD.

### Build targets in `platformio.ini`

* `env:logger` — helmet logger firmware (`main_logger.cpp`)
* `env:display` — display dashboard firmware (`main_display.cpp`)

## Key Technical Features

* **ESP-NOW telemetry link** between the helmet logger and display dashboard.
* **Modular GPS provider** abstraction in `GpsManager`.
* **`fix_stride.py` prebuild patch** for SquareLine Studio LVGL 9 image exports.
* **Helmet error log transport** from logger to display via structured ESP-NOW messages.
* **Optional wheel IMU support** on the display dashboard to improve speed filtering and live telemetry.
* **SPI bus arbitration** for display rendering and SD card access using a mutex.

## Hardware Requirements

* JC3248W535N display board
* Seeed Studio XIAO ESP32S3 logger board
* GPS module (u-blox or ATGM336)
* Optional MPU6050 IMU for wheel/dashboard module
* I2S audio amplifier and speaker
* microSD card formatted FAT32 (display dashboard only)

## Software Dependencies

Built with PlatformIO.

Libraries used by the current configuration:

* `lvgl/lvgl` (v9.x)
* `rzeldent/esp32_smartdisplay`
* `mikalhart/TinyGPSPlus`
* `ElectronicCats/MPU6050`
* `espressif/esp32-audioI2S`

## GPS Providers

The logger firmware supports two GPS providers through `GpsManager`.

* **u-blox provider** (default)
  * No additional build flag required.
  * Update interval: `200ms` (5Hz).
* **ATGM336 provider**
  * Enable with `-D GPS_PROVIDER_ATGM336`.
  * Update interval: `100ms` (10Hz).

### Changing the GPS provider

In `platformio.ini`, under `[env:logger] -> build_flags`:

* Comment out `-D USE_FAKE_GPS` to use real GPS hardware.
* Uncomment `-D GPS_PROVIDER_ATGM336` to select the ATGM336 provider.
* Leave `-D GPS_PROVIDER_ATGM336` commented to use u-blox.

Telemetry timing is derived from `GpsManager::getUpdateIntervalMs()`, so the logger loop cadence automatically follows the selected GPS provider.

## Filesystem & Storage

### Helmet Logger — LittleFS (internal flash)

The logger uses **LittleFS** on the XIAO ESP32S3's internal flash for audio playback. The `data/` directory in this repository is the filesystem image that gets flashed alongside the firmware.

Upload the audio filesystem with:

```
pio run -e logger -t uploadfs
```

Audio files required in the root of LittleFS:

| File(s) | Purpose |
|---|---|
| `0.wav` – `19.wav` | Individual numbers 0–19 |
| `20.wav`, `30.wav` … `90.wav` | Tens |
| `100.wav`, `100toe.wav`, `200.wav` … `900.wav` | Hundreds (`100toe.wav` = "cento e…") |
| `e.wav` | Conjunction "e" (Portuguese: "and") |
| `ponto.wav` | Decimal separator |
| `silence.wav` | Gap between speech segments |
| `initializing.wav`, `wait.wav`, `ready.wav` | Boot state cues |
| `battery_level.wav` | Battery percentage announcement |
| `radio.wav`, `gps.wav`, `imu.wav` | Peripheral init cues |
| `error.wav` | Error announcement |
| `system_ready.wav` | Final ready cue |
| `lap_time.wav`, `best_lap.wav` | Lap completion cues |
| `up.wav`, `down.wav` | Delta direction (slower / faster) |
| `real_car_ride.csv` | Only required when `USE_FAKE_GPS` is enabled |

### Display Dashboard — SD Card

The display dashboard (`env:display`) reads and writes to a **FAT32 microSD** card. No files need to be pre-populated — the display creates what it needs on first boot — but the files below may be edited on a computer for configuration.

#### `/config.ini` (auto-created)

Stores display preferences. Created with defaults on first boot if absent.

```ini
# Kart Data Logger Configuration
theme=dark          # dark | light
selected_track=0    # index into tracks.ini
```

#### `/tracks.ini` (optional, user-created)

Defines named tracks with GPS finish-line coordinates. The display reads this at boot and populates the track selector in the UI. If absent, no tracks are preloaded.

```ini
# Kart track configurations
count=2

0_name=Kartódromo Granja Viana
0_left_lat=-23.60488969
0_left_lon=-46.83622658
0_right_lat=-23.60493793
0_right_lon=-46.83641597

1_name=My Track
1_left_lat=0.00000000
1_left_lon=0.00000000
1_right_lat=0.00000000
1_right_lon=0.00000000
```

#### `/log_N.csv` (auto-created per session)

Telemetry session logs. A new numbered file is created each time a recording session starts.

```
epoch,speed,totalGForce,gForceX,gForceY,steering_angle,sats,lat,lng
```

#### `/helmet_error.log` (auto-written)

Received from the helmet logger at boot if the logger had stored errors. Overwritten each transfer.

## Setup & Installation

1. Clone this repository.
2. Open it in VSCode with PlatformIO.
3. Edit the UI using the SquareLine Studio project files in `UIDesign/`.
4. Export the UI to `/lib/ui/` and let `fix_stride.py` patch LVGL image metadata during build.
5. Flash the selected firmware and filesystem:
   * `pio run -e logger -t upload` then `pio run -e logger -t uploadfs`
   * `pio run -e display -t upload`
6. Insert a FAT32 microSD card into the display. A default `config.ini` is created on first boot.

## ESP-NOW Protocol

The telemetry protocol is defined in `lib/Shared/EspNowProtocol.h`.

* `MSG_TELEMETRY` — helmet logger broadcast.
* `MSG_IMU_FEEDBACK` — dashboard IMU uplink to logger.
* `MSG_ERROR_LOG_START`, `MSG_ERROR_LOG_LINE`, `MSG_ERROR_LOG_END` — helmet error log transfer.

The current codebase uses `ESPNOW_CHANNEL 1` for all packet exchanges.

## Design files

Design files are included under the `hardware/` folder.

### 3D Models

I've used Fusion to design 3D models for the custom enclosures and mounts. These are located in `hardware/3d_models/` and exported as `.f3d` and `.3mf` files. You can open and modify these in Fusion or any compatible 3D modeling software.

### PCB Designs

The PCB design is created in KiCad and located in the `hardware/pcb/` folder. The helmet logger PCB is designed to fit within the existing enclosure on the MT Stinger 2 helmet (this enclosure was built to fit Universal Communication Systems hardware and can be found on other helmets).

## Visual Reference

The repository includes renderings and module photos in the `images/` folder.

![PCB 2D render](images/smd_pcb_design.png)

![PCB 3D render](images/smd_pcd_3d_design.png)

![Helmet module installed](images/2-helmet_breadboard_installation.jpg)

![Wheel module assembled](images/6-wheel_module_assembled.jpg)

## Notes

* The display firmware is responsible for UI rendering and SD logging.
* The logger firmware is responsible for GPS, battery, audio, and helmet error captures.
* `USE_FAKE_GPS` is available for development without real GPS hardware.
