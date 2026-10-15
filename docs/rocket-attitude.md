# Embedded rocket attitude

Replay and Live Telemetry each contain a compact rocket attitude panel.
It is a reusable widget, not a separate window.

In Replay, the panel sits below the traces in the existing resizable sidebar.
It stays hidden until a non-empty recording is loaded and hides when cleared.
The **Instruments** control shows or hides that column. The chart/map keeps
its existing main area and the replay transport remains at the bottom.
With no loaded data, the whole instrument column collapses and the placeholder
uses the full content width. Clearing a recording restores this state and
disables the transport. The chart toolbar adapts to the chart column's width;
comparison controls share the second row with display options.
In Live Telemetry, the six metric tiles share one row on wide desktops.
Smaller desktop windows use two metric rows above the map, beside the
instrument column. This keeps full-size readings visible without making the
page taller. The map fills the main area. Attitude, connection controls, and
the monitoring notice share the right column. Port and baud fields sit side by side, with
connection actions and sample counters grouped into compact rows.
At narrow widths, the layout stacks the instrument column below the map.
Scrolling remains available when the window cannot fit the controls.
The instrument is 204 logical pixels high and uses its host column's width.
Its border, rounded corners, background, and monospaced headings follow the
existing panel styles in both themes.

The orange rocket has a rounded nose, tapered tail, and swept fins. Fixed
X/Y/Z axes and reference rings surround it. All surfaces are orange; shading
shows the shape. Both built-in themes update immediately. Qt paints the
instrument only when it needs repainting.

Replay feeds the panel from the selected raw sample, so seeking
backwards and pausing preserve the correct pose. The numeric angle readouts
retain the recorded FC values; their tooltip identifies them. No automatic
recording-start or FC-mounting correction is applied. Live Telemetry feeds its
own panel from live batches; replay updates do not replace its live pose.
Missing angles clear the pose and show dash readouts against the reference
axes. The panel does not show an unavailable message. Live poses become stale
after two seconds. The packet indicator shows elapsed seconds since the last
received batch, keeps aging after disconnect, and clears when the session resets.
It refreshes only while the live page is visible and catches up when reopened.

## Angle inputs

CSV and XLSX imports recognize `angleX_deg`, `angleY_deg`, `angleZ_deg`, the
underscore forms `angle_x_deg` etc., and `attitude_x_deg` etc. Matching is
case-insensitive. Existing recordings therefore retain their angle columns.
CSV export retains angles as `attitude_x_deg`, `attitude_y_deg`,
`attitude_z_deg`. Blank cells remain unavailable.

The live text protocol supplies degree angles in columns 11–13
(`anglex`, `angley`, `anglez`). The built-in Demo supplies synthetic angles
in those fields. Recorded IMU packets currently have no absolute attitude.
Gyro rates are not treated as orientation.

The display convention is right-handed X east, Y up, Z south, matching the
map world. At zero rotation the rocket points along body +Y. Degree-valued
intrinsic XYZ rotations compose as `qX * qY * qZ`. Positive X tilts an upright
nose toward +Z; positive Z tilts it toward −X. Y is longitudinal rotation
when the rocket is upright. This is a display convention, not a verified
hardware calibration or an aircraft pitch/roll/yaw mapping. Check the
sender's axes before interpreting absolute physical orientation.

## Flight map and toolbar

The map uses the same orientation conversion as the embedded instrument.
Replay and Live Telemetry both use the supplied attitude directly.
Attitude updates also work when a sample lacks a new GPS fix; the rocket can
rotate at its last mapped position. Unknown attitude is labelled unavailable.
The new rocket model is entirely orange and has no surrounding glow sphere.

The original trajectory rendering is restored, including its original
3× vertical scale, ground projections and terrain placement. The added
smoothing and Path settings have been removed. An exact replay sample keeps its
altitude even when its GPS coordinates match an earlier point. Attitude-only
updates reuse the existing trail and ground-projection geometry.

Replay and Live Telemetry use the same controls inside the map. Live Telemetry
has no second set of Fit, Center or Follow buttons above it. **Follow** changes
to **Following** while active. In 3D, orbit and zoom remain available while the
camera follows the rocket, preserving the chosen viewing angle and distance.
Panning is disabled until Follow is switched off, so the rocket remains the
camera target. Dragging the 2D map still releases Follow for manual navigation.

If the mission toolbar is hidden, restore it with **View → Mission toolbar**
or **Ctrl+Shift+M**. The native View action always reflects its visibility.

## Verification

Run `cmake --build build`, `ctest --test-dir build --output-on-failure`, and
`node --test tests/Map3DGeometryTests.mjs`.

Catch2 covers angles from the existing CSV and XLSX recordings, CSV round
trips, source separation, forward/backward replay seeking, empty replay,
missing values, staleness, compact layout and both themes. Recorded-pose
checks cover missing angles, seeking, replacement recordings, and attitude
without GPS. Replay UI checks cover the full-width startup placeholder,
clearing and resetting a recording, disabled idle transport, and restoring
traces after changing views with Instruments hidden.
Toolbar checks resize from wide to smaller desktop windows and verify that
button labels fit without forcing a larger window.
Desktop layout
checks use populated velocity and RSSI readings, resize through wide and
narrow modes, and verify that readings and all right-column
panels fit without page scrolling at 920 × 650, 1024 × 650, and 1440 × 800.
Chart checks retain original-sample hover through overview reduction, bound
inspection to the active replay range, and verify comparison clearing and
event-marker lifecycle. Packet-age checks use an injected monotonic clock.
Node checks the actual orange mesh, quaternion validation, repeated-coordinate
altitude and path-geometry reuse. See [the performance plan](ui-performance-improvement-plan.md)
for before/after callback measurements and their limits.

In a desktop graphics session, run these explicit UI checks:

- `build/tests/cosmo-recorded-gui-tests '[attitude-webgl]' --rng-seed 1`
- `build/tests/cosmo-recorded-gui-tests '[toolbar-ui]' --rng-seed 1`

Optional captures use an existing directory in `COSMO_TEST_SCREENSHOTS`.
The attitude widget tests capture both pages at 1440 × 900 and 1024 × 768,
plus the compact instrument in both themes. The WebGL check uses an existing
recording with angle columns. No test opens a serial device.
