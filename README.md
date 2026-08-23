# CosmoSoft

CosmoSoft is an open-source desktop ground-station application for working with a flight computer and telemetry data.

The project is written in C++20 and uses Qt 6 for the user interface and CMake for builds.

## Current state

The app builds and runs with:

- **Dashboard** — interactive telemetry chart: **multiple traces** (checkboxes for altitude, temp, pressure, accel, battery, RSSI, gyro, lat/lon). One trace uses raw Y units; **two or more** use a **normalized 0–1 overlay** (legend shows “(norm)”); **hover** lists elapsed time, sample index, and **engineering values for every enabled trace**. Dotted grid, legend, point markers when ≤400 samples/trace, full-history replay scrubber, zoom, stat tiles.
- **Live Telemetry** — serial device discovery and connection controls, the latest decoded sample, and its mapped flight path.
- **Event Log** — bounded, timestamped application events and errors with live theme updates.
- **Action bar** (under the toolbar) — **Open flight log**, **Clear flight data**, **Export**, and a deterministic fake live transmission; port, baud, and the last replay folder are persisted via **QSettings** (`CosmoSoft` / `cosmo-soft`).
- **Settings** (separate window) — built-in and imported skins, metric/imperial display units, rate-limited error feedback sounds, and developer diagnostics.

**Offline replay:** Load CSV or XLSX logs via *Open log…* on the action bar.
Theseus-style CSV timestamps are taken from the `time` column (seconds →
stored as milliseconds); XLSX import reads supported telemetry columns from the
first worksheet.

**`.telem` files:** Lines are hex-decoded and fed through the telemetry **Framer** / **Parser**. Those layers are still **stubs**, so `.telem` replay will not produce samples until binary framing and decoding are implemented.

**Live serial:** Connect from the Live Telemetry page; the current newline-delimited
CSV stream is decoded off the UI thread by `LineTelemetryDecodeWorker` and
delivered to the model in bounded batches. Complete valid rows appear in the
dashboard and Live Telemetry page. The binary `Framer` / `Parser` path remains
reserved for the future hardware protocol. The live export snapshot retains at
most 1,000,000 samples; if that limit is reached, new samples remain visible
and the Event Log reports that they are no longer retained for export.

Serial discovery has platform-matched implementations: Linux device nodes
(`ttyUSB`, `ttyACM`, `ttyS`), macOS callout/TTY nodes (`cu.*`, `tty.*`), and
Windows registry COM ports. Windows serial changes require validation in
Windows CI and on representative hardware in addition to macOS/Linux testing.

## Theming

CosmoSoft supports **light and dark themes** that can be switched on-the-fly without restarting the application.

**Built-in themes:**
- **Dark theme** — Default, optimized for low-light environments
- **Light theme** — High-contrast option for bright conditions

**Switching themes:**
1. Open **Settings** via the gear icon in the toolbar
2. Go to **Appearance** tab
3. Select your preferred theme from the dropdown
4. All UI elements update immediately

**Custom skins:**
- Import custom `.cosmo` theme files via Settings → Appearance → "Import .cosmo skin..."
- Custom skins are ZIP archives containing a `theme.json` palette definition
- Imported themes persist across app restarts

**Theme persistence:**
- Your selected theme is saved automatically via Qt Settings
- macOS: `~/Library/Preferences/com.CosmoSoft.cosmo-soft.plist`
- Linux: `~/.config/CosmoSoft/cosmo-soft.conf`
- Windows: Registry under `HKEY_CURRENT_USER\Software\CosmoSoft\cosmo-soft`

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
  - `WebEngineWidgets`
  - `WebChannel`
- Zlib

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

On Windows with a single-config generator:

```powershell
.\build\cosmo-soft.exe
```

On Windows with a multi-config generator:

```powershell
.\build\Release\cosmo-soft.exe
```

## Sample data

The `sample_data/` folder contains example logs:

| File | Use |
|------|-----|
| `theseus_flight_data.csv` | Replay in the app (action bar → Open flight log) |
| `flight_2026-05-30_17-34-27.xlsx` | Example XLSX replay log |
| `altos_sample_data.telem`, `altos_sample_data2.telem` | Altos-style hex lines; needs Framer/Parser implementation to decode |
| `TELEM_FORMAT_EXPLAINED.md` | Documentation of the `.telem` hex packet format |
| `parse_telem.py` | Python script to convert `.telem` hex lines to human-readable output |
| `parse_telem_detailed.py` | Python script to decode individual sensor fields from `.telem` files |

## Project structure

- `src/gui` — Qt UI (`MainWindow`, `DashboardPage`, `LiveTelemetryPage`, `EventLogPage`, `SettingsPage`, replay controller, `FlightDataModel`, widgets)
- `src/services/import` — `SampleFileLoader` (CSV, XLSX, and `.telem` ingestion)
- `src/services/telemetry` — framing and parsing pipeline (worker thread)
- `src/services/persistence` — `FlightLogManager`
- `src/services/flight` — flight module placeholder
- `src/services/RingBuffer.tpp` — lock-free ring buffer utility
- `src/gateway/comms` — serial communication and port scanning
- `include/domain/` — core data models (`FlightSample`, `FlightSession`)
- `include/` — public headers (GUI, services, gateway)
- `assets` — fonts, icons, and images bundled through Qt resources

## Notes

- Assets are bundled with Qt resources; there is no separate asset copy step.
- Single-config builds place the `cosmo-soft` executable at `build/cosmo-soft`
  (the internal CMake target remains `cosmo-soft-bin`). Multi-config builds use
  the configuration directory shown above.
- Optional Qt **Vulkan** lookup is disabled in CMake when the Vulkan SDK is absent, to keep configure output quiet.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
