# CosmoSoft
<<<<<<< HEAD

CosmoSoft is an open-source desktop ground-station application for working with a flight computer and telemetry data.

The project is written in C++20 and uses Qt 6 for the user interface and CMake for builds.

## Current state

The repository currently builds and launches a desktop app with:

- a monitoring page
- a flight data page
- a telemetry chart demo
- a separate settings window

Some backend pieces already exist, but live telemetry integration, parsing, and persistence are still being wired up. At the moment, parts of the UI show placeholder data.

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

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

If you use a multi-config generator such as Visual Studio or Xcode, build a configuration explicitly:

```bash
cmake -S . -B build
cmake --build build --config Release
```

## Start The App

After the build finishes, run the executable.

Single-config generators:

```bash
./build/cosmo-soft
```

Multi-config generators:

```bash
./build/Release/cosmo-soft
```

On Windows the executable name will be:

```powershell
.\build\Release\cosmo-soft.exe
```

## Project structure

- `src/gui` - Qt UI and pages
- `src/comms` - serial communication and port scanning
- `src/telemetry` - telemetry parsing
- `src/persistence` - file/log persistence
- `src/flight` - flight-related calculations
- `assets` - fonts, icons, and images bundled through Qt resources

## Notes

- Assets are bundled with Qt resources, so there is no separate asset copy step.
- The default executable target name is `cosmo-soft`.

## Project board

Our TODO list is tracked in Jira:

<https://rock-team-oln4apuq.atlassian.net/jira/software/projects/KAN/list>

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
=======

Open Source software for connection with flight computer

Our TODO list is on <a href="https://rock-team-oln4apuq.atlassian.net/jira/software/projects/KAN/list">Jira Board<a>
>>>>>>> 4f89ac6 (Jira lins added)
