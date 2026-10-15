# CosmoSoft session report — 15 October 2026

This session improved flight orientation displays, replay correctness, live
telemetry layout, recording protection and mission controls. Work is on branch
`feat/rocket-attitude-trajectory-ui`.

This report uses the Git log and commit diffs, the current working tree, project
documentation and the verification logs retained from the session. Committed
work and uncommitted implementation are listed separately. Temporary design
experiments are not presented as shipped features.

## Work recorded in Git

The session's three commits follow baseline `1ced3f2`. Times below are local
commit times, UTC+01:00.

| Commit | Time | Git subject | Verified scope |
| --- | --- | --- | --- |
| `d5f6fc2` | 17:30 | feat: add rocket attitude panel and related functionality | Embedded Replay/Live attitude panels, shared angle-to-quaternion conversion, orange rocket rendering, angle import/export, responsive UI fixes, documentation and regression tests. |
| `32361f3` | 19:27 | Refactor code structure for improved readability and maintainability | Replay and chart correctness fixes, packet-age handling, chart and map optimizations, removal of duplicate map controls, and camera Follow behavior during orbit/zoom. |
| `7845cb4` | 19:41 | feat: add font styling and update button labels in map UI | Map font alignment, compact Follow button presentation and the initial live mission modules plan. |

The second commit contains functional fixes as well as refactoring; its subject
alone does not describe the full change.

### Rocket attitude and replay

- Added an embedded rocket attitude module to Replay and Live Telemetry, with
  fixed axes, reference rings and an entirely orange rocket.
- Connected the map rocket and instrument to supplied orientation, including
  orientation changes without a new GPS fix.
- Preserved explicit degree-valued attitude columns through CSV/XLSX import
  and CSV export. Gyro rates are not treated as absolute orientation.
- Documented the display axis convention and removed automatic recording-start
  calibration from the final behavior.
- Kept attitude hidden in empty Replay, repaired the startup/cleared placeholder
  layout and removed the instrument's unavailable banner.
- Restored the original trajectory rendering and removed the experimental
  smoothing and Path settings. The original 3× vertical scale remains.

### Correctness, navigation and performance

- Fixed replay altitude when consecutive samples share GPS coordinates.
- Cleared stale comparison curves and hover state at empty replay boundaries.
- Prevented flight markers from leaking into replacement recordings, while
  preserving them during navigation within a flight.
- Replaced the constant packet freshness label with monotonic receipt age,
  including disconnect aging and hidden-page catch-up.
- Reused the preview timeline for original-sample chart inspection and avoided
  repeated full-history hover sorting/copying.
- Reused map path geometry for attitude-only updates.
- Removed duplicate Live map controls. Camera orbit and zoom now preserve
  Follow; panning is disabled during 3D Follow.
- Preserved a View-menu action and shortcut for restoring the mission toolbar.

The committed [performance notes](ui-performance-improvement-plan.md) record
local chart callback measurements of **24.3 → 8.7 ms at 100,000 samples** and
**212.7 → 30.6 ms at 1,000,000 samples**. The unchanged-path Node probe measured
approximately **0.4 → 0.10 ms**, with geometry replacements reduced from two
to zero per update. These are earlier diagnostic measurements, not new
measurements for this report. They exclude painting/GPU costs and do not
establish a Release frame-rate guarantee.

## Implemented in the current working tree

These changes are implemented and tested, but are **not included in the three
commits above**. HEAD remains `7845cb4`.

1. **Separate visual horizon.** Added a KSP-inspired, native-painted navball
   beside Rocket attitude in Replay and Live. It shares validated orientation,
   shows sky/ground, elevation markings and stale state, and hides in empty
   Replay. It adds no WebEngine view or external dependency.
2. **Connection and diagnostics.** Simplified Connection, retained global Demo,
   moved recorded test input to Developer settings, and added link health,
   rates and local loss counters. The existing bottom status line now identifies
   missing/invalid fields in completed text rows without treating zero as missing.
3. **Flight events.** Added an editor, clustered timeline and event selector.
   Event selection seeks to original samples. Playback no longer rebuilds the
   event list on every tick.
4. **Recovery.** Added last-known coordinates, receipt age, reference
   range/bearing, coordinate copying and external map access. Real-live fixes
   persist across restarts and remain isolated from Demo and Replay. Replay
   Recovery uses only the selected sample; references remain scoped to their
   recording.
5. **Recording protection.** Added Save/Discard/Cancel before unsaved close,
   Quit, clear or replacement. Save during acquisition stops and drains input
   after a destination is chosen, then waits for completion. Revision/session
   tracking prevents an older save or discard decision from clearing newer data.
6. **Metadata persistence.** CSV exports now include an adjacent
   `.mission.json` file for events and reference coordinates, bound to the CSV
   by SHA-256. Failed metadata writes leave the recording unsaved. Loading and
   hashing run in background tasks.
7. **Preflight profiles.** Added editable named checklists with manual and
   measured checks, required flags and thresholds. Definitions persist;
   confirmations reset per session and unavailable measurements stay unknown.

The final navigation adjustment moves **Instruments after View in the top menu
bar** and makes it control the active Replay or Live page. **Recovery sits
immediately before Open flight log** in the top action bar. The previous
in-page controls were removed. Each page retains independent state, and
switching pages does not create extra Recovery actions or a dropdown. Enabling
Recovery in Replay opens its map; empty Replay disables Recovery.

Both themes use the existing theme tokens. Compact layouts preserve useful map
space at 920 × 650, 1024 × 650 and 1440 × 800. Recovery details take space only
while active, and hidden Recovery panels stop their refresh timer.

## Verification and limits

- Final full CTest run: **70/70 passed**, including the GUI regression executable.
- Node map regressions: **5/5 passed**, including rocket geometry, exact replay
  altitude, geometry reuse and camera Follow.
- Native mission-module capture run: **18 cases / 446 assertions passed**.
  A later native mission/layout check passed **8 cases / 205 assertions**.
- Final native control-placement/routing check: **100 assertions passed**,
  covering both themes and the three desktop widths.
- Final build completed with **no compiler warnings**. Changed/new C++ formatting
  and `git diff --check` passed. cppcheck reported no errors or warnings; six
  existing style/performance suggestions remain.
- Review findings about save races, reference ownership, diagnostic ordering,
  diagnostic-only delivery and queued discard identity were addressed.

Verification used recorded/generated data. It did not validate serial hardware,
physical FC mounting axes or external map launch. Release p50/p95 latency and
long-session GPU measurements remain outstanding. The CSV and metadata writes
are individually atomic, not a single two-file transaction. Local Recovery
state does not provide crash recovery for the complete flight recording.

No commits or pull requests were created for the latest working-tree changes.
The report does not attribute unrelated pre-existing working-tree edits to the
assistant. See [the instrument guide](rocket-attitude.md) for current behavior
and [the mission modules plan](live-mission-modules-plan.md) for agreed scope.

## Git evidence

The history and state were inspected with:

```sh
git log -15 --date=iso-local --pretty=format:'%h %ad %s'
git show --stat d5f6fc2
git show --stat 32361f3
git show --stat 7845cb4
git show 32361f3 -- assets/map/map3d.js include/gui/widgets/ChartSamples.h src/gui/widgets/TelemetryChartView.cpp
git status --short
git diff --stat
```

`git diff --stat` covers tracked changes only. The new, untracked module files
listed by `git status` are also part of the uncommitted implementation above.
