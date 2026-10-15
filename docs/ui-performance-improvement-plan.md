# UI correctness and performance improvement plan

Reviewed 15 October 2026 on `feat/rocket-attitude-trajectory-ui`, against the
working tree based on `1ced3f2`. All six fixes below are implemented.
Recording-start attitude correction remains removed.

Exact map samples now retain their altitude when coordinates repeat. Empty
replay ranges clear comparison curves. Session replacement and reset clear
event markers, while navigation within a recording preserves them. Packet age
uses monotonic receipt time and catches up after a hidden page becomes visible.
Replay hover shares the preview timeline and uses an active source range; hover
and keyboard reads preserve Qt implicit sharing. Overview selection evaluates
each metric once per sample. Attitude-only map updates reuse path geometries.

## Findings and implemented changes

| Order | Finding and evidence | Smallest useful change | Acceptance check |
| --- | --- | --- | --- |
| 1 | **Selected altitude can be stale when GPS coordinates repeat.** Both `updateEntities()` and `update3DEntities()` in `assets/map/map3d.js` decide whether to include the exact sample using latitude/longitude alone. A probe with a retained point at 100 m and a later exact sample at the same location at 200 m still reported 100 m. The 3D probe also retained the older height. | Include the exact sample using its identity/index and altitude, rather than treating the same horizontal position as the same sample. Keep the original trajectory rendering and vertical scale. | Seek between samples with identical latitude/longitude and changing altitude, including samples omitted from the decimated path. Assert exact altitude in map statistics and the 3D rocket position. Cover backward seeking and GPS gaps. |
| 2 | **Comparison curves can outlive the displayed replay data.** `DashboardPage::rebuildReplayCharts()` clears the ordinary series and returns for a zero trail without clearing the independently copied comparison series. Confirmed by tracing the empty branch and comparison rebuild paths. | Clear comparison telemetry and its hover state at the same empty-data boundary. Check the other empty-data returns too. | Select two traces, enter Compare, then seek or stop at zero. No comparison telemetry points or old hover values remain. Resume and verify that both curves return. |
| 3 | **Event markers survive into another flight.** `onSessionReset()` and replacement-session loading retain `m_eventMarkers`; `configureTracePlots()` deliberately migrates those markers between trace charts. | Clear session annotations on reset and when replacing a recording. Preserve them during navigation and seeking within the same recording. | Add an event to flight A, clear/load flight B, and check ordinary and comparison charts. View switching within A must keep its events. |
| 4 | **Packet freshness is misleading.** `LiveTelemetryPage::updateLastPacketLabel()` displays “Last packet: now” whenever the sample count is positive, including after telemetry stops. There is no elapsed receipt time behind this label. | Store monotonic receipt time, update the age while the page is visible, and distinguish no packets from an old packet. Keep the last age after disconnect; reset it when the session clears. | Check aging without new samples, disconnect/end-of-file behavior, resuming data, hidden-page catch-up, and reset. Use a controllable clock or update path for deterministic tests. |
| 5 | **Large replay chart rebuilds exceed the scheduler budget.** `buildChartFromSampleIndices()` constructs and stable-sorts the complete original-sample hover lookup on each rebuild, despite a bounded plotted-point count. The local probe measured about 24 ms at 100k samples and 213 ms at 1m samples. | First reuse the already-monotonic preview timeline and avoid sorting it again. Reuse immutable hover ordering and update the active range, preserving access to every original sample. Measure remaining costs before changing live-history storage or chart architecture. | Record p50/p95 rebuild time, allocations, and GUI event latency at 25k/100k/1m samples. Keep existing peak, gap, timestamp, and original-sample inspection checks. Compare the same workload before and after in Release. |
| 6 | **The map reallocates geometry even when the trail does not change.** `update3DEntities()` rebuilds the active trail and drop lines; `updateWideLine()` disposes and replaces their geometries. The probe counted two geometry disposals per unchanged update. | Separate pose changes from path changes. Reuse the existing line geometry when only attitude changes; update buffers when the path actually changes. Keep the original path shape and ground projections. | An attitude-only update changes the rocket pose without rebuilding path buffers. Measure allocation counts and frame times during replay, camera movement, and long live sessions in QtWebEngine. |

## Measurements and limits

Temporary probes lived under `/tmp`; build configuration was unchanged.
The Qt probe linked the local libraries before and after the fixes and used the offscreen
platform. Chart timings measure the scheduler callback near the end of a synthetic
recording with three enabled traces, averaged over five rebuilds. They exclude
painting and the timer's waiting interval. The current CMake build has no selected
build type or application optimization flags, so these are local diagnostics,
not Release performance guarantees.

| Workload | Before | After |
| --- | ---: | ---: |
| Chart rebuild, 1,000 samples | 4.6 ms | 4.3 ms |
| Chart rebuild, 25,000 samples | 9.8 ms | 6.0 ms |
| Chart rebuild, 100,000 samples | 24.3 ms | 8.7 ms |
| Chart rebuild, 1,000,000 samples | 212.7 ms | 30.6 ms |
| Attitude update plus software rendering, 260 × 204 pixels | 0.8 ms | unchanged |
| Attitude update while hidden | 0.035 ms | unchanged |

Replay's configured chart interval is 33 ms. The million-sample callback is
about seven times faster in this local workload. Painting and event processing
still need time, so this does not establish a 30 fps guarantee. The attitude
mesh is recreated during painting, but its measured cost does not justify a
renderer rewrite or a new dependency.

The JavaScript probe called the current map functions from a temporary module
copy with added probe entry points. It used the repository's Three.js classes
and synthetic data. At 5,000 retained points, unchanged 3D updates averaged
about 0.4 ms in Node and replaced two line geometries before the fix. After the
fix, the same unchanged-path workload averaged about 0.10 ms and disposed no
line geometries. This excludes WebGL uploads,
browser layout, terrain fetching, and GPU time. Browser profiling is required
before predicting a frame-rate improvement.

Dedicated GUI regressions cover comparison clearing, marker lifecycle,
original-row hover, active hover bounds, implicit sharing and packet-age
lifecycle. Packet-age tests inject a clock and do not sleep. Node regressions
exercise the actual map module for exact repeated-coordinate altitude and
geometry reuse, including updates without GPS.

Release p50/p95, allocation profiling, GUI event latency and long-session GPU
frame measurements remain follow-up measurements; they were not collected by
these local callback probes.

## Implementation and verification sequence

1. Added focused regressions for findings 1–4 and verified the failures before fixing them.
2. Implemented finding 5 using existing preview data and measured the same
   local build before and after. Original-row inspection and peak/gap checks remain.
3. Implemented finding 6 after reproducing redundant geometry disposals.
   The trajectory's shape, vertical scale and ground projections remain unchanged.
4. Latest verification passed: 62 CTest tests, five Node map tests, six native
   layout/attitude cases (220 assertions), and the explicit native WebGL case
   (23 assertions), including Follow during zoom.
   Native captures cover both themes, desktop resizing and empty/reset states.
   The build produced zero compiler warnings. Changed C++ lines pass
   `clang-format`; `git diff --check` passes. Whole-source `cppcheck` reports
   no errors or warnings, with six existing style/performance findings and
   missing-include information.

Keep `FlightSample`, `FlightDataModel`, and `IComms` unchanged. No new libraries,
CMake changes, CI changes, resource changes, or attitude recalibration were introduced.
Existing bounds, render scheduling, and hidden-map suspension should be preserved.
