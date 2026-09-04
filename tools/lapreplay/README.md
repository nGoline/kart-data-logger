# Lap replay harness

Offline test rig for finish-line detection. Turns a GoPro's telemetry into the
logger's own CSV format and replays it through the **real** `lib/LapManager`
compiled for the host — so lap detection can be validated without going to the
track.

Nothing here is part of the firmware build. `tools/` is outside `build_src_filter`
and `lib/`, so PlatformIO never sees it.

`make` also builds **`delta`**, which checks the *live* lap delta: the number
the dash hero panel shows continuously, from matching each fix against a
reference lap's recorded trace. See "Testing the live delta" below.

`make` also builds **`navpvt`**, an offset-level check on the real
`UbloxGpsProvider::update()` — it feeds synthetic `UBX-NAV-PVT` frames through a
byte-queue serial stub and asserts what the provider decodes. Run it after touching
that provider: it decodes by explicit byte offset, and a wrong offset does not crash,
it produces plausible telemetry that is silently wrong. It covers the field offsets,
the `hasFix()` gates (2D rejected, `hAcc` bound), checksum rejection, mm/s→km/h
scaling, and a frame split at every byte boundary — that last one being the real 25Hz
failure mode, where a message straddles two `update()` calls because the UART buffer
drained mid-frame.

## Why a GoPro

GoPro MP4s carry a GPMF metadata track alongside the video. On a HERO8 that is
GPS at ~18 Hz and accelerometer/gyro at ~200 Hz — denser than the ATGM336's
10 Hz, already timestamped in UTC, and free if you were filming anyway.

## Usage

```sh
# 1. extract telemetry -> CSV (no ffmpeg/exiftool needed; parses the MP4 directly)
python3 gpmf_to_csv.py ../../video/GH011973.MP4 GH011973.csv

# 2. build the harness
make

# 3. replay against a finish line: <csv> <leftLat> <leftLng> <rightLat> <rightLng>
./replay GH011973.csv -23.60488969 -46.83622658 -23.60493793 -46.83641597
```

`make VERBOSE=1` surfaces `LapManager`'s own `log_d()` gate diagnostics
("Approaching Gate", "Gate Missed!", "VALID CROSSING") which are compiled out by
default.

### Testing lap *timing*

A single crossing only starts the clock — it produces no lap time. If the clip is
a near-closed loop, stitch it into synthetic laps:

```sh
python3 gpmf_to_csv.py ../../video/GH011973.MP4 looped.csv --repeat 4
./replay looped.csv -23.60488969 -46.83622658 -23.60493793 -46.83641597
```

Each reported lap should equal the printed span exactly; any drift is a bug in
the crossing interpolation.

### Testing the live delta

The hero panel's delta comes from `LapManager`'s reference trace: the lap in
progress is recorded as position plus elapsed time, the session's best lap is
kept as the reference, and every fix is matched to the nearest point on it. A
wrong match does not crash, it produces a plausible number that is quietly
attached to the wrong piece of track, which is the worst way to find out at a
circuit. `delta` catches that offline:

```sh
python3 gps_raw_to_log.py --out /tmp/session.csv ../data/gps_raw_GH0*1974.csv
./delta /tmp/session.csv -23.605392 -46.836045 -23.605442 -46.836251
```

The `err` column is the check. At the line the live delta must agree with the
arithmetic one (this lap's time minus the reference lap's), because the two are
the same comparison reached by completely different routes: one by matching
1,000-odd fixes against a polyline, the other by subtracting two crossing
timestamps. On the 12-lap fixture they agree to within 22 ms on every lap, and
the harness exits non-zero if any lap drifts past 250 ms.

`moves` and `held` say how many fixes changed the number and how many repeated
it. A delta that "moves" once per lap would be the old behaviour, and the
point of the whole feature is that it does not. `--dump` emits
`epoch,lap,elapsed_ms,delta_ms` per fix for plotting the trace of a lap.

The trace buffers are allocated by the caller, here and in the firmware, which
is what lets `LapManager` stay host-clean: on the dash they come from PSRAM, in
this harness from `new[]`.

## Column mapping

Output matches `LogManager::task`'s `/log_N.csv` exactly:

```
epoch,speed,totalGForce,gForceX,gForceY,steering_angle,sats,lat,lng
```

| column | source | notes |
|---|---|---|
| `epoch` | `GPSU` | absolute UTC, 1 Hz; the 18 Hz `GPS5` samples are interpolated between stamps |
| `speed` | `GPS5` 2D speed | m/s → km/h |
| `totalGForce` | `ACCL` magnitude | orientation-independent, always correct |
| `gForceX/Y` | `ACCL` axes | **see caveat below** |
| `steering_angle` | — | always `0.0`; a camera cannot know this |
| `sats` | `GPSF` | GPMF has no satellite count, only fix type and DOP. Emitted as `12` on a 3D fix, else `0`, because `LogManager` and `LapManager` treat `sats == 0` as unusable |
| `lat`/`lng` | `GPS5` | 6 dp, ~0.11 m resolution |

**Axis caveat:** GPMF's accelerometer axis order depends on camera mount
orientation (see the `ORIN`/`ORIO` tags). The X/Y assignment is a starting guess —
verify against a known corner before trusting `gForceX`/`gForceY`. Raw accel also
peaks around 6 g from chassis vibration, so samples are averaged into each GPS
interval to damp it.
