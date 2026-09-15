# Recorded IMU verification

Verified on macOS on branch `feat/recorded-imu-gui` (2026-09-12).

- `cmake -S . -B build` and `cmake --build build --clean-first -j 6`: passed, zero compiler warnings.
- `ctest --test-dir build --output-on-failure --timeout 30`: **49/49 passed**. The widget test needs macOS WebEngine IPC outside the restricted agent sandbox; it passed there with the offscreen platform.
- New files and changed C++ ranges pass `clang-format`. The requested whole-repository check was also run: existing formatting violations remain (15,492 diagnostics). Unrelated code was not reformatted.
- `cppcheck --enable=all --suppress=missingInclude --suppress=missingIncludeSystem --std=c++20 --library=qt -I include src/` completed. No new service warnings remain. Existing findings include uninitialized `Frame::format` and `EventMarker::timeSec`, and `invalidContainerLoop` in `SettingsPage::onSkinChanged` (the refresh branch). Other existing style/unused-function findings remain. The plain command without `--library=qt` also reports unknown Qt macros.
- `git diff --check`: passed. No hardcoded hex colors were introduced in GUI code.

Coverage includes truncated/invalid hex packets, envelope and radio checksum failures, unsupported types, fragmented reads and timeouts, bounded noise recovery, rollover and rejected backward time, repeat sessions, partial-read cancellation and destruction, read-only recordings, interruptible pacing, callback failures, exact queue overflow counts/bytes, queued GUI-thread ordering, CSV export/reload with missing values, existing CSV decoding/replay, and mailbox stale-generation behavior.

The separate widget test feeds recorded bytes into the existing mailbox/model, checks readouts and finite chart points, switches between Live Telemetry and Dashboard, switches dark/light/dark themes during reception, checks the new action and its focusability, and stops/destroys reception. Dark/light offscreen previews were visually inspected. HTTP(S) requests are blocked in that test. Native operating-system file-picker interaction and full MainWindow closure were not manually exercised; worker destruction during reception is covered independently.

Try **Live Telemetry → Recorded IMU input… → `sample_data/recorded_imu.telem`**, then export and reopen the CSV. This synthetic example emits temperature, RSSI and elapsed time. Pressure and IMU-axis conversion remain unavailable because protocol/firmware evidence is incomplete or conflicting; see [protocol notes](protocol.md). Physical-device validation remains a separate milestone.
