# ESP32 Kart Telemetry System

A telemetry and datalogging system for open-track go-karts, built around a single
steering-wheel-mounted ESP32 display board that handles everything: GPS
acquisition, lap timing, dashboard rendering and SD logging.

> **Note:** earlier revisions of this project split the work between a
> helmet-mounted logger node and the display, linked over ESP-NOW. That helmet
> node proved unreliable at the track and has been removed — the display is now
> the only node.

## System Architecture

Single target: the **Display Dashboard Node**.

* **Board:** JC3248W535N (ESP32-S3, AXS15231B QSPI panel)
* **Role:** GPS acquisition, lap detection, UI dashboard, and SD logging.
* **Features:**
  * GPS from ATGM336 (default) or u-blox M9, wired directly to the board.
  * Live speed, G-force, satellite count and display battery state.
  * Finish-line based lap detection with best/previous lap deltas.
  * Telemetry logged to SD card per recording session.
  * Optional MPU6050 IMU for speed filtering and steering angle.

### Build targets in `platformio.ini`

Both targets build the same application (`src/main_display.cpp`) and differ only
in the panel driver backend:

* `env:display_gfx` — Arduino_GFX backend (`lib/DisplayGFX`). **Default.**
* `env:display` — esp_lcd backend (`lib/DisplayBSP`). Fallback.

The Arduino_GFX backend is the default because it resolves a QSPI panel wedge
seen with the esp_lcd path.

## Key Technical Features

* **Modular GPS provider** abstraction in `GpsManager`.
* **Finish-line lap detection** in `LapManager`, driven by GPS position.
* **`fix_stride.py` prebuild patch** for SquareLine Studio LVGL 9 image exports.
* **Display-wedge watchdog** — a 5s task watchdog on `loopTask` turns a panel
  wedge into a fast auto-reset instead of a permanent freeze.
* **Charge mode** to keep the board from eating the charger's current budget.
* **Optional IMU support** to improve speed filtering and live telemetry.

## Charge mode

The charger's current is shared between the board and the cell, so a running
dashboard slows charging considerably. Charge mode parks the device:

* Stops the recording session and closes the GPS UART.
* Drops the CPU to 80 MHz (the floor for stable USB-CDC — don't go lower).
* Swaps to a minimal charge screen and blanks the backlight after 15s.

It is **only ever entered manually**, from the `CHARGE MODE` button on the
config screen. Nothing enters it automatically, so bench testing and debugging
are unaffected.

While parked: tap anywhere to wake the backlight and read the battery for
another 15s; press `RESUME` to leave. Resuming re-probes the GPS, which takes a
couple of seconds.

> **Whether the GPS actually sleeps depends on which one is fitted.** Charge mode
> calls `gps.standby()` and leaves the UART open so `wake()` can talk to it.
> On the **ATGM336** that is a quiesce, not a sleep: the RF front end has no
> software power-down at all, so it keeps drawing ~25-40 mA and standby only cuts
> it to GPS-only at 1Hz with NMEA muted. On the **u-blox M9** it is a real
> power-down — `UBX-RXM-PMREQ` software backup, woken by traffic on the module's
> RX pin. Coming back may cost a cold start if the module's V_BCKP does not hold
> the BBR, but the configuration itself is in flash and survives either way.

> **The percentage reads high while charging.** With no power-path charger the
> sense divider sits on the charger rail, so it measures the cell terminal
> voltage *under charge*, which is well above its resting value — expect a
> jump downward when you unplug. It's shown as `~94%` to mark it approximate;
> the voltage below it is the number to trust.

The charge screen and the config-screen button are both built at runtime in
`lib/UiHelper/uiHelper.cpp` rather than exported from SquareLine, so
re-exporting `lib/ui/` will not wipe them.

## Hardware Requirements

* JC3248W535N display board
* GPS module wired to `RX=6` / `TX=7` — ATGM336, or a u-blox M9 such as the Matek
  M9N-5883. The ATGM336 runs on 3V3; the M9N-5883 wants 4~5.5V, so its VCC belongs
  on the IP5306 5V rail, not the board's 3V3.
* Optional MPU6050 IMU
* microSD card formatted FAT32

## Software Dependencies

Built with PlatformIO.

* `lvgl/lvgl` (v9.x)
* `moononournation/GFX Library for Arduino` (`display_gfx` only)
* `mikalhart/TinyGPSPlus`
* `ElectronicCats/MPU6050`

## GPS Providers

`GpsManager` supports two providers. Nothing older than these is supported — the
NEO-6M-era 9600-baud fallback is gone.

* **ATGM336 provider** (current default)
  * Enabled with `-D GPS_PROVIDER_ATGM336`.
  * 10Hz.
  * Configured over CASIC text commands (`$PCAS`), which are never acked, so the
    module cannot report whether a setting took. It also cannot be put to sleep:
    `$PCAS12` standby is a 5L-only feature and this is a 5N.
* **u-blox M9 provider** — targets the M9 generation (NEO-M9N), e.g. the Matek
  M9N-5883.
  * Leave `-D GPS_PROVIDER_ATGM336` out of the build flags.
  * 25Hz.
  * Probes 115200 then 38400 (the M9 default), and moves the port to 115200.
  * Sleeps for real in charge mode — `UBX-RXM-PMREQ` software backup, armed to
    wake on UART RX traffic.
  * Configured over binary UBX, which *is* acked, and the provider reads the
    dynamic model / rate / power mode back at boot and logs `BEFORE:` and
    `AFTER:` lines. Legacy `UBX-CFG-*` messages are deprecated on generation 9
    but still supported; they are removed on generation 10, so a future module
    means porting `configureUblox()` to `UBX-CFG-VALSET`.
  * **Speaks `UBX-NAV-PVT`, not NMEA.** All NMEA output is disabled. One 92-byte
    frame per epoch carries position at 1e-7° (~1.1cm), Doppler ground speed,
    UTC, `numSV`, `fixType`, `pDOP`, `hAcc` and `sAcc` — less traffic than
    GGA+RMC and strictly more information, as one coherent snapshot instead of a
    position from one sentence stitched to a speed from another. It also makes the
    four-constellation problem vanish: per-system duplication of GSA/GSV was an
    NMEA artefact, and UBX has no talker IDs to multiply.
  * `hasFix()` therefore uses the receiver's own verdict — `gnssFixOK`,
    `fixType == 3`, and `hAcc` within bounds — rather than the `sats >= 4 &&
    HDOP <= 3` heuristic the NMEA path had to guess with.
  * **TinyGPSPlus is unused on this path.** It stays in `lib_deps` for the
    ATGM336, and static-archive linking drops it (and the unreferenced provider)
    from the image. It is still compiled every build, so a break in either
    provider still fails the build.

The flag lives in `[display_common] -> build_flags_base`, so it applies to both
build targets at once.

### Fix rate

ATGM336 runs at **10Hz**, u-blox M9 at **25Hz** — the receiver's ceiling with all
four constellations. GGA+RMC at 25Hz is ~36kbaud of the 115200 the port is set to,
so the link has room to spare. 40ms epochs also land on exact centisecond values
(`.00`/`.04`/`.08`…), which matters because NMEA timestamps are centiseconds and
anything finer would round.

The rate buys **log resolution**, which is the point — at 100 km/h, 25Hz puts a
sample every 1.1m instead of every 2.8m. It buys much less for lap times, because
`LapManager` intersects the finish gate with the segment between consecutive fixes
and interpolates the crossing millisecond along it, so the reported time never
quantised to the fix interval in the first place.

To sustain 25Hz, `LogManager` holds the log file open for the whole session and
`flush()`es once a second, rather than reopening and closing it per row (25 FAT
directory updates a second is where a slow card starts stalling). The trade is
durability: an abrupt power-off now costs up to a second of the lap instead of a
single sample. The telemetry queue is 40 frames for the same reason — ~1.6s of
headroom at 25Hz, so a card stall doesn't drop rows.

### GPS wiring

The GPS UART pins are set by `GPS_STANDALONE_RX` / `GPS_STANDALONE_TX` in
`[display_common]`, currently `6` and `7`.

Watch for collisions when changing these: `GPIO5` carries the battery sense
divider (`DISPLAY_BATT_ADC` in `src/main_display.cpp`), and `GPIO18`/`GPIO17`
are the IMU's I2C pins when `ENABLE_IMU` is set.

## Session button

A momentary switch wired between `SESSION_BUTTON_PIN` and GND starts and stops
recording without touching the screen — the dash is on the steering wheel and the
touch target is small with gloves on.

* **Tap** — runs the same start gesture as the on-screen START button: the
  full-screen 3-2-1 overlay with the track name and a CANCEL, then the session
  begins (GoPro rolls, active track's finish line re-applied).
* **Hold 3 s** — stops it, counting `3 - 2 - 1` on the dashboard. Let go early and
  nothing happens.

The start overlay is parented to whatever screen is showing rather than to the
config screen, so it is visible when the physical button fires it from the
dashboard. Tapped from the config screen those are the same object, so the
on-screen button is unchanged.

The button is inert in charge mode — `loop()` returns before the display lock is
taken there, which is the point of that mode.

A tap while a session is already running does nothing: stopping is deliberately
the harder gesture, since an accidental stop costs the rest of the session.

Configured in `platformio.ini`:

| Flag | Default | Notes |
|---|---|---|
| `SESSION_BUTTON_PIN` | `6` | Comment out to build without a button |
| `SESSION_BUTTON_HOLD_MS` | `3000` | Hold time for the stop |
| `SESSION_BUTTON_ACTIVE_HIGH` | off | Set when the switch goes to 3V3 rather than GND |
| `SESSION_BUTTON_DEBOUNCE_MS` | `25` | Raise it if a long lead registers phantom taps |

Wire the switch between the pin and **GND** by default. If the spare wire in the
loom is a 3V3 instead, wire it pin-to-3V3 and set `SESSION_BUTTON_ACTIVE_HIGH` —
that swaps the internal pull-up for a pull-down, so the idle level is still the
released one. The pin is pulled to the idle rail either way and never left
floating: a floating input on a wire run out to a steering wheel picks up enough
to toggle on its own, and a broken wire then reads as *released* rather than as a
stuck press.

GPIO 6 is the ATGM336's old RX pad, already broken out and in the loom now that
the GPS is on 17/18. Other free pins: 7, 9, 10, 14, 15, 16, 41, 42. Avoid 0, 3,
45, 46 (strapping), 19/20 (native USB), 43/44 (UART0) and 26-37 (flash/PSRAM).

The state machine (`lib/SessionButton`) holds no GPIO or LVGL state, so it is
replayed on the host by `tools/lapreplay/sessionbutton.cpp` — bounce windows, a
release a millisecond short of the hold, and the `millis()` wrap, none of which
can be produced reliably with a thumb.

## Filesystem & Storage

The display reads and writes to a **FAT32 microSD** card. No files need to be
pre-populated — the display creates what it needs on first boot — but the files
below may be edited on a computer for configuration.

#### `/config.ini` (auto-created)

Stores display preferences. Created with defaults on first boot if absent.

```ini
# Kart Data Logger Configuration
theme=dark          # dark | light
selected_track=0    # index into tracks.ini
```

#### `/health.csv` (auto-created, only when something goes wrong)

Appended across every session, one row per second in which the telemetry queue
had no room for a frame:

```
epoch,log,dropped_total,dropped_delta
```

**An empty or absent file is the good news** — it means no row was ever lost. Any
row in it means the card could not keep up with the fix rate and the matching
`log_N.csv` is missing samples, so the file is worth checking after a race rather
than watching the serial log, which is unreachable with the dash on a wheel.

`epoch` comes from the GPS-synced system clock, so it lines up with the `epoch`
column in the telemetry file; before the first fix it degrades to uptime.

#### `/tracks.ini` (optional, user-created)

Defines named tracks with GPS finish-line coordinates. The display reads this at
boot and populates the track selector in the UI. If absent, no tracks are
preloaded.

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

Telemetry session logs. A new numbered file is created each time a recording
session starts.

```
epoch,speed,totalGForce,gForceX,gForceY,steering_angle,sats,lat,lng,fix,pdop,hacc,sacc
```

The last four are the receiver's report on its own fix, and are **zero on the
ATGM336** — NMEA carries no equivalent, so zero means "not reported", not "perfect".

| column | meaning |
| --- | --- |
| `fix`  | `0` no fix, `2` 2D, `3` 3D, `5` time only. Never `1`/`4` (dead reckoning) — that is NEO-M9V silicon, not M9N. |
| `pdop` | position dilution of precision |
| `hacc` | horizontal accuracy estimate, metres, ~1σ |
| `sacc` | speed accuracy estimate, m/s |

`hacc` is the useful one for analysis: it does not make a position better, it tells
you which samples to distrust. Around 0.7-1.5m under open sky, climbing near
buildings and in the first seconds after a cold start. A lap time interpolated
between two 12m samples is not a lap time to trust to hundredths.

These columns are **appended**, never inserted, so everything in `tools/` keeps
working — those read by column name or by a fixed `sscanf` prefix. A CSV derived
from GoPro footage simply lacks them.

Note `lat`/`lng` are written at 7 decimal places (~1.1cm) rather than 6 (~11cm),
which is what `UBX-NAV-PVT` actually resolves.

## Setup & Installation

1. Clone this repository.
2. Open it in VSCode with PlatformIO.
3. Edit the UI using the SquareLine Studio project files in `UIDesign/`.
4. Export the UI to `/lib/ui/` and let `fix_stride.py` patch LVGL image metadata during build.
5. Flash the firmware: `tools/flash.sh` (see below).
6. Insert a FAT32 microSD card into the display. A default `config.ini` is created on first boot.

### Flashing

`upload_port` in `platformio.ini` is pinned to `/dev/cu.usbmodem1101`, which is one
machine's enumeration — the same board comes up under a different name elsewhere and
`pio run -t upload` then fails on a port that isn't there. `tools/flash.sh` finds the
board by USB `VID:PID` instead and passes it with `--upload-port`, which overrides the
pinned value:

```bash
tools/flash.sh              # default env, confirm, then upload
tools/flash.sh -m           # ...and open the serial monitor afterwards
tools/flash.sh -e display   # the esp_lcd fallback env
tools/flash.sh -p /dev/cu.usbmodem101   # skip discovery
tools/flash.sh -y           # no confirmation prompt (also FLASH_YES=1)
tools/flash.sh -- -v        # anything after -- goes to `pio run`
```

Before uploading it prints the port, env and `FW_VERSION`, and warns if the tree is
dirty — `FW_VERSION` is `git describe --tags --always --dirty` and shows on the setup
screen, so it is easy to flash uncommitted edits by accident. If more than one board is
attached it lists the candidates and asks for `-p`.

`pio run -e display_gfx -t upload` still works if the pinned port happens to be right.

## Dormant libraries

These libraries are no longer part of the build — nothing includes them — but
they are kept in `lib/` because they may be useful again:

* `EspNowManager` + `Shared/EspNowProtocol.h` message types — the board-to-board radio link.
* `ErrorLogManager` — LittleFS error capture and transfer.
* `AudioManager`, `VoiceParser` — I2S playback and spoken number/lap announcements.
* `PowerManager` — button-driven hibernate and power gating.
* `FakeGps` — CSV replay for development without GPS hardware.

`Shared/EspNowProtocol.h` is still compiled: `TelemetryMsg` is the record the
display passes between the UI, the lap manager and the SD log task.

## Design files

Design files are included under the `hardware/` folder.

### 3D Models

I've used Fusion to design 3D models for the custom enclosures and mounts. These
are located in `hardware/3d-models/` and exported as `.f3d` and `.3mf` files.
You can open and modify these in Fusion or any compatible 3D modeling software.

## Visual Reference

The repository includes module photos in the `images/` folder.

![Wheel module assembled](images/6-wheel_module_assembled.jpg)

## Notes

* The display firmware owns the whole pipeline: GPS, lap timing, UI and SD logging.
* Lap detection only runs while a recording session is active.
