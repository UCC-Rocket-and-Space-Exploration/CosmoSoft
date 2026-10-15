# Embedded flight instruments

Replay and Live Telemetry each contain separate **Rocket attitude** and
**Visual horizon** modules. Both are embedded, reusable Qt widgets.

In Replay Graph, the modules sit side by side below the traces in the existing
resizable sidebar. In Replay Map, Horizon sits above Rocket attitude and the
graph-only traces hide. Switching views reuses the same instrument widgets.
The instrument column stays hidden until a non-empty recording is loaded and
hides when cleared.
**Instruments → Show instrument sidebar** shows or hides that column. The chart/map keeps
its existing main area and the replay transport remains at the bottom.
With no loaded data, the whole instrument column collapses and the placeholder
uses the full content width. Clearing a recording restores this state and
disables the transport. The chart toolbar adapts to the chart column's width;
comparison controls share the second row with display options.
In Live Telemetry, the six metric tiles share one row on wide desktops.
Smaller desktop windows use two metric rows above the map, beside the
instrument column. This keeps full-size readings visible without making the
page taller. The map fills the main area. The two compact instruments, connection
controls, and the collapsed preflight checklist share the right column. Port and baud fields sit side by side, with
connection actions and sample counters grouped into compact rows.
At narrow widths, the layout stacks the instrument column below the map.
Scrolling remains available when the window cannot fit the controls.
Both instruments are 204 logical pixels high and share the host column's width.
The top menu bar places **Instruments** after **View**. Its actions control the
active Replay or Live Telemetry page, with independent visibility settings.
Replay also offers **Show instrument sidebar** and the existing T shortcut.
There are no Instruments buttons inside the pages. Dropdown selectors and
expand/collapse controls share small, theme-aware open chevrons. The full
control remains clickable; the chevron is not a separate tiny target.
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

## Visual horizon

The navball looks along the rocket's body +Y nose. Blue is sky and brown is
ground. A fixed orange vehicle marker stays in the centre while the sphere,
horizon and elevation rings follow the supplied quaternion. At zero rotation,
the upright rocket looks toward the sky pole; 90° about X places the horizon
at the centre, and 180° points into the ground. The numeric value is nose
elevation, not a renamed raw FC axis. The surrounding roll ticks provide a
fixed reference. No compass or velocity markers are inferred from missing data.

The module uses native painting and caches the sphere until its pose, size or
theme changes. It creates no extra WebEngine view. Both modules show stale live
attitude after two seconds, and both hide when Replay has no recording.

## Connection and packet diagnostics

Connection contains port, baud, Scan and Connect/Disconnect. Demo remains in
the global mission toolbar. Recorded test input is in **Settings → Developer**.
Link status distinguishes Disconnected, No data, Delayed and Receiving. Expand
Details for accepted samples/second, bytes/second, malformed rows and local
queue drops. These counters do not estimate radio packet loss.

On Live Telemetry, the bottom application status line shows the latest
completed text row's missing or invalid fields. Zero is a valid value. Partial
serial chunks do not create missing-field warnings, and rejected rows never
replace the rocket pose with fabricated values. The next valid complete row
clears the message. Replay does not display an old Live packet diagnostic.

## Recovery

Use **Recovery** in the top action bar, immediately before **Open flight log**,
to switch the active page to ground navigation. In Replay, enabling Recovery
also opens the map. Recovery is disabled in empty Replay; Live can still show a
retained fix from an earlier session. Each page keeps its own Recovery state.
The map shows last-known coordinates, receipt age, reference range/bearing,
Copy coordinates, Open map and Reference. Turning Recovery off hides these
details and restores the full map surface. The normal map stats
hide in this mode. The first real fix provides a named launch reference until
an explicit ground reference is chosen. The app does not infer laptop location
or call the last fix a landing position.

The last real-live fix, original UTC receipt time, session/source and reference
are stored locally. Reopening labels the record Earlier session. Receipt age
continues with a monotonic timer in the current process. Hidden panels suspend
periodic refresh. Demo and Replay cannot overwrite this real-live record;
Replay Recovery follows only the selected sample. References edited in Replay
stay in that recording's metadata.

## Events, saving and preflight

**Tools → Manage flight events…** edits manual event names and times, adds
entries and removes entries. A compact clustered track and event selector sit
above the replay transport. Selecting an entry seeks to the nearest original
sample. Event controls rebuild when annotations change, rather than on every
playback tick.

Export writes telemetry CSV plus an adjacent `<filename>.mission.json` file
with event annotations and the ground reference. SHA-256 binds metadata to the
exact telemetry bytes. Keep both files together when moving a flight. Metadata
from a different CSV is rejected and reported in Event Log. CSV and metadata
writes are individually atomic; a metadata failure leaves the recording
unsaved even if the CSV has already succeeded. Metadata validation and hashing
run in the background load/save tasks.

Closing, quitting, clearing or replacing an unsaved recording asks **Save /
Discard / Cancel**. Save during acquisition chooses a destination, stops and
drains acquisition, then waits for CSV and metadata to succeed. Canceling the
chooser leaves acquisition running. A write failure keeps the recording open;
after a save attempt acquisition may be stopped. An ordinary export marks only
its snapshot saved, so newer samples or annotation edits remain unsaved.
Imported files start clean. Recovery persistence is separate from full-flight
export and does not provide crash recovery for the full recording.

The collapsed Live **Preflight** panel offers named profiles. Edit profile
names, items, ordering, required flags and measured thresholds in
**Settings → Data → Preflight profiles**. Save, Duplicate and Reset defaults
store definitions; manual confirmations reset for each session. Missing measured
data stays unknown. Completion means the configured checks are complete and
makes no arming or launch-safety decision.

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

### Implementation verification — 15 October 2026

- `cmake --build build -j6`: successful, no compiler warnings.
- `ctest --test-dir build --output-on-failure`: 70/70 passing.
- `node --test tests/Map3DGeometryTests.mjs`: 5/5 passing, including camera Follow.
- Native Qt capture run: 18 cases / 446 assertions passing. Reviewed both themes,
  empty Replay, navball poses, Recovery, checklist editing and desktop layouts.
  Final native mission/layout checks: 8 cases / 205 assertions passing, including
  a regression that inactive Recovery does not consume map height.
- Changed-line/new-file clang-format and `git diff --check`: passing.
- cppcheck: no errors or warnings; six existing style/performance suggestions
  remain outside this change. Native Qt also emits the existing Workbench font
  alias warning; a capture run emitted a compositor diagnostic.

Checks use recorded or generated data and block network requests in map tests.
No serial hardware or external map launch was exercised. The captures are UI
verification evidence, not a validation of the flight computer's mounting axes.

Toolbar follow-up: the full suite still passes 70/70. The native control-routing
check passes 100 assertions across both themes and 920/1024/1440-pixel layouts.
It checks menu order, Recovery placement, empty Replay, independent page state,
and that switching pages never creates a second Recovery action or dropdown.

Instrument/chevron follow-up, 16 October 2026: Replay Map stacks Horizon above
Rocket attitude; Graph and Live retain the horizontal pair. The full suite passes
70/70 and the final native checks pass 429 assertions in four cases. See the
[researched QoL plan](ui-qol-research-plan.md) for design decisions, source links
and the remaining proposals.
