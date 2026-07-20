# Architecture & Build Quickstart

1. Install CMake 3.21+, Qt 6 (Core, Gui, Widgets, Charts, Concurrent,
   WebEngineWidgets, WebChannel modules), and Zlib development files.
2. From the repository root run:
   ```
   cmake -S . -B build
   cmake --build build
   ```
3. Launch the demo window:
   ```
   ./build/cosmo-soft
   ```
4. Remove build:
   ```
   rm -rf build
   ```
