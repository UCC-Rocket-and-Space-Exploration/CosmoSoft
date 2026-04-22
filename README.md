# CosmoSoft

CosmoSoft is an open-source desktop ground-station application for working with a flight computer and telemetry data.

The project is written in C++20 and uses Qt 6 for the user interface and CMake for builds.

## Current state

The app builds and runs with:

- **Monitoring** — summary of the latest decoded sample (live or replay).
- **Flight data** — interactive telemetry chart: **multiple traces** (checkboxes for altitude, temp, pressure, accel, battery, RSSI, gyro, lat/lon). One trace uses raw Y units; **two or more** use a **normalized 0–1 overlay** (legend shows “(norm)”); **hover** lists elapsed time, sample index, and **engineering values for every enabled trace**. Dotted grid, legend, point markers when ≤400 samples/trace, full-history replay scrubber, zoom, stat tiles.
- **Connection bar** (under the toolbar) — serial port, baud, **Refresh** / **Connect** / **Disconnect**, **Open log…** and **Clear flight**; port, baud, and last replay folder are persisted via **QSettings** (`CosmoSoft` / `cosmo-soft`).
- **Settings** (separate window) — appearance (UI font size) and sound preference flags only.

**Offline replay:** Load **Theseus-style CSV** (see `sample_data/theseus_flight_data.csv`) via *Open log…* on the connection bar. Timestamps are taken from the `time` column (seconds → stored as milliseconds).

**`.telem` files:** Lines are hex-decoded and fed through the telemetry **Framer** / **Parser**. Those layers are still **stubs**, so `.telem` replay will not produce samples until binary framing and decoding are implemented.

**Live serial:** Connect from the connection bar; bytes flow through `ParserWorker` → `Framer` → `Parser`. Until parsing is implemented, decoded live samples may not appear in the UI.

## Requirements

Before building, make sure you have:

- CMake 3.21 or newer
- a C++20 compiler
- Qt 6.2 or newer with these modules:
  - `Core`
  - `Gui`
  - `Widgets`
  - `Charts`
  - `Concurrent`

If CMake cannot find your Qt installation automatically, set `CMAKE_PREFIX_PATH` or `Qt6_DIR` to the Qt install location.

## Build

From the repository root, use an **out-of-tree** build directory (in-source builds are rejected by CMake):

```bash
cmake -S . -B build
cmake --build build
```

If `cmake` fails with a message about the cache or source directory not matching (for example after moving the repo), either delete the `build/` directory and run the commands again, or with **CMake 3.24+** force a clean configure:

```bash
cmake --fresh -S . -B build
cmake --build build
```

Or run the helper (removes `build/`, then configures and builds):

```bash
bash scripts/fresh-cmake.sh
```

If you use a multi-config generator such as Visual Studio or Xcode, build a configuration explicitly:

```bash
cmake -S . -B build
cmake --build build --config Release
```

## Start the app

Single-config generators:

```bash
./build/cosmo-soft
```

Multi-config generators:

```bash
./build/Release/cosmo-soft
```

On Windows:

```powershell
.\build\Release\cosmo-soft.exe
```

## Sample data

The `sample_data/` folder contains example logs:

| File | Use |
|------|-----|
| `theseus_flight_data.csv` | Replay in the app (connection bar → Open log…) |
| `altos_sample_data.telem`, `altos_sample_data2.telem` | Altos-style hex lines; needs Framer/Parser implementation to decode |
| `TELEM_FORMAT_EXPLAINED.md` | Documentation of the `.telem` hex packet format |
| `parse_telem.py` | Python script to convert `.telem` hex lines to human-readable output |
| `parse_telem_detailed.py` | Python script to decode individual sensor fields from `.telem` files |

## Project structure

- `src/gui` — Qt UI (`MainWindow`, `MonitoringPage`, `DashboardPage`, `SettingsPage`, replay controller, `FlightDataModel`, widgets)
- `src/services/import` — `SampleFileLoader` (CSV and `.telem` ingestion)
- `src/services/telemetry` — framing and parsing pipeline (worker thread)
- `src/services/persistence` — `FlightLogManager`
- `src/services/flight` — flight module placeholder
- `src/services/RingBuffer.cpp` — lock-free ring buffer utility
- `src/gateway/comms` — serial communication and port scanning
- `include/domain/` — core data models (`FlightSample`, `FlightSession`)
- `include/` — public headers (GUI, services, gateway)
- `assets` — fonts, icons, and images bundled through Qt resources

## Notes

- Assets are bundled with Qt resources; there is no separate asset copy step.
- The executable output name is `cosmo-soft` (CMake target `cosmo-soft-bin`).
- Optional Qt **Vulkan** lookup is disabled in CMake when the Vulkan SDK is absent, to keep configure output quiet.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
