# CosmoSoft

CosmoSoft is an open-source desktop ground-station application for working with a flight computer and telemetry data.

The project is written in C++20 and uses Qt 6 for the user interface and CMake for builds.

## Current state

The app builds and runs with:

- **Monitoring** — summary of the latest decoded sample (live or replay).
- **Flight data** — dashboard with **three time-based charts** (altitude, temperature, pressure), stat tiles, and **replay controls** (play / pause / stop, scrub slider, speed).
- **Settings** (separate window) — serial port and baud, persisted via **QSettings** (`CosmoSoft` / `cosmo-soft`); **flight replay** section to open logs or clear loaded data.

**Offline replay:** Load **Theseus-style CSV** (see `sample_data/theseus_flight_data.csv`) via *Settings → Open flight log…*. Timestamps are taken from the `time` column (seconds → stored as milliseconds).

**`.telem` files:** Lines are hex-decoded and fed through the telemetry **Framer** / **Parser**. Those layers are still **stubs**, so `.telem` replay will not produce samples until binary framing and decoding are implemented.

**Live serial:** Connect from Settings; bytes flow through `ParserWorker` → `Framer` → `Parser`. Until parsing is implemented, decoded live samples may not appear in the UI.

## Requirements

Before building, make sure you have:

- CMake 3.21 or newer
- a C++20 compiler
- Qt 6.2 or newer with these modules:
  - `Core`
  - `Gui`
  - `Widgets`
  - `Charts`

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
| `theseus_flight_data.csv` | Replay in the app (Settings → Open flight log…) |
| `*.telem` | Altos-style hex lines; needs Framer/Parser implementation to decode |

## Project structure

- `src/gui` — Qt UI (`MainWindow`, `MonitoringPage`, `DashboardPage`, `SettingsPage`, replay controller, `FlightDataModel`)
- `src/services/import` — `SampleFileLoader` (CSV and `.telem` ingestion)
- `src/gateway/comms` — serial communication and port scanning
- `src/services/telemetry` — framing and parsing pipeline (worker thread)
- `src/services/persistence` — flight log helper, `RingBuffer`
- `src/services/flight` — flight module placeholder
- `include/` — public headers (domain models, GUI, services)
- `assets` — fonts, icons, and images bundled through Qt resources

## Notes

- Assets are bundled with Qt resources; there is no separate asset copy step.
- The executable output name is `cosmo-soft` (CMake target `cosmo-soft-bin`).
- Optional Qt **Vulkan** lookup is disabled in CMake when the Vulkan SDK is absent, to keep configure output quiet.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
