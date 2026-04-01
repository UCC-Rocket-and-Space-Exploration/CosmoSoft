# Codebase problems and technical debt

This document lists known gaps and follow-ups. It is not exhaustive runtime QA.

---

## 1. Build and CMake

| Severity | Issue | Details |
|----------|--------|---------|
| Medium | **Stale `build/` cache** | If `CMakeCache.txt` was generated under a different project path, CMake may error (source/cache mismatch). **Fix:** remove the build directory and run `cmake -S . -B <dir>` again from the current repo root. |
| Low | **In-source builds** | CMake is configured to **reject** `cmake .` in the source tree; always use `-B` with a separate directory. |

Root [`CMakeLists.txt`](CMakeLists.txt) also disables optional Qt **WrapVulkanHeaders** lookup when Vulkan is not installed, to reduce configure noise.

---

## 2. Telemetry pipeline (blocks live and `.telem` replay)

[`Framer`](src/services/telemetry/Framer.cpp) and [`Parser`](src/services/telemetry/Parser.cpp) are still **stubs**. Until they implement the real protocol:

- **Live serial** will not produce meaningful `FlightSample` values in the UI.
- **`.telem` replay** ([`SampleFileLoader::loadTelemFile`](src/services/import/SampleFileLoader.cpp)) will report that no samples were decoded.

**CSV replay** ([`SampleFileLoader::loadTheseusCsv`](src/services/import/SampleFileLoader.cpp)) does not depend on Framer/Parser and works for supported CSV layouts.

---

## 3. Other implementation notes

- **`src/services/flight/FlightModuleStub.cpp`** — Placeholder so the `flight` object library builds until real flight logic exists.
- **`RingBuffer`** — Implemented and compiled as part of the `persistence` target; **not thread-safe** unless callers synchronize (see comment in [`include/services/RingBuffer.h`](include/services/RingBuffer.h)).
- **`include/gateway/comms/CommsFactory.h`** — TODO on extra platform-agnostic parameters for `createSerialComms`.

---

## Quick remediation order (suggested)

1. Implement **`Framer::try_next_frame`** and **`Parser::decode`** for your telemetry format (and/or document the binary spec).
2. Extend **live** path testing once decoded samples flow into [`FlightDataModel`](include/gui/FlightDataModel.h).
3. Revisit **CommsFactory** API once serial options (parity, stop bits, etc.) are required cross-platform.
