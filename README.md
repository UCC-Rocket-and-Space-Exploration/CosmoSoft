# Cosmo-Soft

Open-source software for connecting to and receiving data from a rocket flight computer.
Inspired by *AltOS*.

## Features
- Connect to flight computers and receive telemetry
- Cross-platform support (Windows, macOS, Linux)
- Friendly UI

## Requirements
- CMake 3.16+
- C++17 compiler
- Qt 6.x
- Platform toolchain (MSVC/MinGW on Windows, clang/gcc on macOS/Linux)

## Build
```bash
cmake -S . -B build
cmake --build build
```

## Run
Launch the built application from the `build` output directory.

## Troubleshooting
**Error 0xc0000135** - missing DLLs on Windows  
Add Qt and MinGW bin paths to `PATH`, for example:
```
PATH=FILE/PATH/TO/QT/6.x.x/mingw_xx/bin
PATH=FILE/PATH/TO/QT/Tools/mingw_xx/bin
```

## Contributing
- Issues: https://github.com/UCC-Rocket-and-Space-Exploration/CosmoSoft/issues
- PRs welcome

## License
MIT
