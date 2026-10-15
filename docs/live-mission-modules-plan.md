# Live mission instruments and recording protection

Status: proposed plan, 15 October 2026. No application changes are part of this document.

## Scope

Keep the existing summary, telemetry readings, orange map rocket and trajectory.
Add link health to Live Telemetry, improve existing flight events, add Recovery
mode, protect unsaved recordings, and add a compact missing-field message.
Make a KSP-inspired visual horizon the default attitude display.

Do not add an interval inspector or cross-flight comparison. Existing comparison
of metrics within one recording stays available.

Item 8 is provisionally interpreted as configurable preflight checklists, from
the numbered list in the discussion. Clarification is pending; its design is
listed separately below and does not block the other work.

## 1. KSP-inspired horizon

Use the existing embedded attitude panel in Replay and Live Telemetry. A small
selector switches between **Horizon** and **Rocket**; Horizon is the default.
The orange 3D rocket remains available without creating another panel.

The horizon uses a shaded sphere with blue sky and brown ground, a clear horizon
line, pitch markings, a roll scale and a fixed orange rocket indicator. The
sphere rotates behind the indicator. A vertically ascending rocket looks toward
the sky pole; it must not show a level horizon merely because the input angles
are zero. This follows the navball metaphor described in the
[KSP manual](https://www.kerbalspaceprogram.com/files/KSPedia-XB1.pdf).

Reuse the existing body-to-world quaternion and the rocket's body +Y nose axis.
Do not rename raw X/Y/Z values as pitch/roll/yaw without deriving the display
frame correctly. Do not restore recording-start calibration or integrate gyro
rates into an invented orientation. Document the FC axis convention. A physical
mounting mismatch needs an explicit, documented mapping rather than guessing
from the first sample.

The first version displays attitude only. Compass headings and velocity markers
need a verified earth reference or velocity vector and should appear only when
those inputs exist. Use native Qt painting and the existing update scheduling;
no second WebEngine view or continuous rendering while hidden.

Keep the current compact instrument height initially. Its frame, labels and
controls follow the active theme. Sky/ground colors retain their semantic
meaning, with contrast checked in both themes. Missing angles show neutral dash
readouts without an unavailable banner. Stale live attitude is distinguishable
from fresh attitude. Replay hides the whole instrument without loaded data.

## 2. Link health inside Live Telemetry

Expand the existing connection panel with a compact status row: **Receiving**,
**Delayed**, **No data**, or **Disconnected**. Reuse its packet-age and counters.
An expandable Details area shows decoded samples per second, received bytes per
second, malformed rows and local queue drops. Add a short activity history only
if it fits without increasing the desktop page height.

Distinguish received bytes from accepted samples. Bytes may continue arriving
while every row is malformed. Local decoder/queue losses must not be presented
as radio packet-loss percentage; that needs packet sequence numbers.

Use a monotonic receipt clock and bounded history. Refresh diagnostics at a low
rate while visible, catch up on show, and reset counters at the session boundary.
Demo data is clearly labelled.

## 3. Improve existing flight events

Keep the existing markers and give them one consistent editor: add, rename,
change time and delete. Add a compact event track beside the replay transport
and a collapsible event list. Selecting an event seeks to its exact position;
nearby labels collapse into a count instead of overlapping the chart.

Use the same event collection in ordinary charts, comparison charts and the
event track. The application Event Log remains a diagnostic log. Flight markers
must stay with their recording and survive save/reopen.

Start with manual events. Estimated apogee/launch markers can follow after their
rules are agreed. Label estimates explicitly; do not call a peak in a partial
recording a confirmed apogee or infer parachute deployment from a curve alone.

## 4. Recovery mode in the existing map

Add a **Flight / Recovery** mode selector within the map. Recovery uses the same
map surface and camera controls. A small overlay shows:

- Last valid received coordinates and the time/age of that position.
- Distance and bearing from a clearly named reference.
- Copy coordinates and open the location in an external map.

Initially use a user-selected ground-station point, or the launch point with an
explicit label. Do not imply the laptop's current location is known. Keep the
last valid point when telemetry stops; call it **Last known position**, since
it may not be the landing location. Invalid coordinates never replace it.

Persist a small recovery record locally: session identity, source, coordinates,
receipt time in UTC and reference point. Restore it after reopening with its
original timestamp and an **Earlier session** label. This is separate from saving
the full flight. Demo or replay activity must not overwrite real live recovery
coordinates. Live fixes use receipt age; Replay uses only points through the
selected sample and does not expose future positions.

## 5. Unsaved-flight protection

The current export writes a snapshot asynchronously. There is no close-time
save guard, so add one shared decision flow for window close, menu/keyboard
Quit, clearing data and starting/replacing a session that would discard data.

Prompt only for unsaved captured data or edited annotations:
**Save flight / Discard / Cancel**. Opening an unchanged file does not itself
make a flight unsaved.

Cancel leaves the application and acquisition unchanged. Save prompts for a
destination and closes only after the write succeeds. A canceled destination or
failed write keeps the application open with the data intact. All user exit paths
must reach this guard; the existing direct Quit action must not bypass it.

For Save and close during acquisition, stop acquisition after a destination is
chosen, drain pending decoded samples, save the final snapshot, and wait for
completion. If saving fails, keep the stopped recording available and say that
acquisition has stopped. A normal export while acquisition continues marks only
its captured snapshot as saved; later samples remain unsaved. Session identity
and revision prevent an old export from marking a newer session as saved.

Reuse CSV for telemetry. Store edited flight markers and recovery reference in
an adjacent JSON metadata file when needed. CSV-only Export must not silently
claim that annotations were saved. Report metadata write failures and keep the
affected changes unsaved. Normal exit protection does not promise full flight
recovery after a crash or power failure.

## 6. Small missing-field message

Place one quiet line at the bottom of Live Telemetry, for example:
**Last packet missing: altitude, angle Z**. Clear it when the next complete
valid packet arrives. Separate invalid values from absent fields, and distinguish
an incomplete received packet from no recent packet.

The current text decoder rejects the whole row when a required field is absent
or invalid. Add bounded field diagnostics alongside its existing result; preserve
that validation. Never infer missing fields from zero-valued FlightSample
members. Only expect fields supported by the selected source, since battery and
RSSI are not present in the current text payload. Diagnose a completed row, not a
serial chunk that may contain only half a row. Do not replace a valid pose with
fabricated zero values after a rejected packet.

Keep the footer to one line, with full details available on demand. Deduplicate
repeated messages and update at a bounded rate rather than flashing per packet.

## 7. Configurable preflight checklists — provisional item 8

If this is the intended item, provide named profiles such as Bench Test and
Launch Day. A profile controls checklist items, order, required checks and
thresholds for data that actually exists. Support save, duplicate and reset.

Separate manual confirmations from measured checks. Use **Checks complete**,
not a claim that the rocket is safe to launch. Unknown values remain unknown.
Reset manual confirmations for a new session. Put the editor in Settings and
keep the live checklist collapsed until needed. Do not add arming or firing
controls as part of this work.

If item 8 instead refers to custom instrument settings, use the same Settings
entry for visible instruments, default attitude view, ordering and compact mode;
do not implement both interpretations without clarification.

## Layout and implementation order

Keep the map/chart as the largest region. Preserve the desktop layouts at
920 × 650, 1024 × 650 and 1440 × 800. Horizon occupies the existing attitude slot;
link health expands inside Connection; Recovery replaces the map's normal
overlay; Events expands on demand; the missing-field line stays at the bottom.
Do not add separate windows or repeated map controls. Compact desktop views
must still fit without page scrolling; narrower windows can retain scrolling.

Implement in this order:

1. Recording revisions and save/close guard, then annotation persistence.
2. Shared source-aware diagnostics, link health and the missing-field footer.
3. Navball rendering and orientation verification.
4. Recovery mode and persistent last-known location.
5. Event editor and event-track improvements.
6. The clarified configurable item 8.

Keep computation and persistence in the existing service layer, presentation in
Qt widgets, and FlightSample, FlightDataModel and IComms interfaces stable.
Diagnostics can travel alongside decoded samples. Reuse existing dependencies
and themes. Do not change the original path or introduce attitude recalibration.

## Acceptance checks

- Unsaved close, clear, replacement and every Quit route: Save, Discard, Cancel,
  export failure, canceled file selection and samples arriving during export.
- Save/reopen restores markers and references; a new session cannot inherit
  another session's annotations or an old export's saved state.
- Horizon: upright, horizontal, inverted, compound rotation and the ±180°
  boundary; forward/backward seeking; missing/stale values; hidden pages.
- Link diagnostics: valid data, malformed-only traffic, quiet links, reconnects,
  local overflow and hidden-page catch-up with an injected clock.
- Field messages: missing, invalid, zero-valued, unsupported fields and rows
  split across input chunks. Strict row validation remains intact.
- Recovery: loss of GPS/telemetry, restart, old timestamps, invalid fixes,
  reference changes, Demo isolation and no future fixes in Replay.
- Both themes and the existing desktop sizes: no duplicated controls, clipped
  text, overlapping instruments or unexpected page scrolling.
- Warning-free build, changed-line formatting, cppcheck, focused Catch2/Node
  regressions and the full test suite. Visual checks occur during implementation;
  this plan does not claim they have passed for the proposed features.
