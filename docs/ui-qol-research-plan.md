# Instrument layout and control usability

Research dates: 15–16 October 2026. Baseline: `2cf1733` on
`feat/rocket-attitude-trajectory-ui`.

The requested direction is a compact desktop ground station using the existing
CosmoSoft theme. Keep the chart/map as the main working surface and keep Horizon
and Rocket attitude as separate embedded modules.

## Requested changes

| Context | Instrument arrangement | Reason |
| --- | --- | --- |
| Live Telemetry | Horizon and Attitude side by side | Both belong to the same quick scan, above Connection. Stacking them would lengthen the already dense live column. |
| Replay Graph | Side by side, below Traces | Preserve vertical room for trace selection and comparison. |
| Replay Map | Horizon above Attitude | Use a consistent vertical instrument column while the map occupies the main area. Hide graph-only trace controls. |
| Empty Replay | Both hidden | Keep the existing full-width placeholder and disabled transport. |

Use the same widget instances when changing view. Change the existing
`QBoxLayout` direction, preserving supplied pose, stale state and independent
visibility. Qt supports changing layout direction without constructing a new
panel. The specific arrangement above is the user's preference; external
examples do not prove an optimal layout for rocket telemetry.
[Qt QBoxLayout](https://doc.qt.io/qt-6/qboxlayout.html).

Replace the large native arrow shapes with one compact open chevron: a
10-logical-pixel span and a 1.5-pixel rounded stroke. Use a right chevron for
collapsed sections and down for expanded sections and dropdown selectors.
Draw from the active widget palette, including disabled colors. The icon stays
small while the whole button remains clickable.

Use a small `QProxyStyle` override for arrow drawing and retain native widget
interaction. Qt documents proxy styles as its supported mechanism for adjusting
platform style behavior. System-owned menus retain their platform behavior.
[Qt QProxyStyle](https://doc.qt.io/qt-6/qproxystyle.html).

An existing stylesheet rule gives combo-box dropdowns a border declaration but
no arrow image. In Qt's stylesheet drawing path, this selects custom dropdown
painting and can leave the arrow blank. Remove that decoration while retaining
the dropdown width, so the shared chevron painter can draw the indicator.
This finding comes from the native captures and inspection of
[Qt's stylesheet implementation](https://github.com/qt/qtbase/blob/6.9/src/widgets/styles/qstylesheetstyle.cpp).

## Research and design decisions

**Use familiar control semantics.** NN/g's accordion study found that a caret
communicated expansion more clearly than alternatives such as plus signs.
Apply that finding to expand/collapse controls; it does not justify replacing
every action with an arrow. Recovery remains a checked mode button with no
dropdown indicator. Keep labels beside icons and preserve keyboard focus.
[Accordion icon research](https://www.nngroup.com/articles/accordion-icons/).

**Change structure for the task.** QGroundControl offers several instrument
arrangements, including horizontal and vertical layouts. Its telemetry panel
also permits configuration. Mission Planner keeps status/HUD and a large map
as distinct working areas. These are useful precedents for view-specific
placement, rather than a reason to copy either application's full interface.
[QGroundControl layouts](https://docs.qgroundcontrol.com/master/en/qgc-user-guide/settings_view/fly_view.html),
[telemetry configuration](https://docs.qgroundcontrol.com/Stable_V5.0/en/qgc-user-guide/fly_view/hud.html),
[Mission Planner flight-data view](https://ardupilot.ardupilot.org/planner/docs/mission-planner-flight-data.html).

**Keep frequent actions visible.** Open, Save and the active Recovery mode
deserve immediate access. Rare developer actions and destructive cleanup can
use secondary menus. This follows progressive disclosure: expose the normal
workflow first and make advanced options easy to find when needed.
[NN/g progressive disclosure](https://www.nngroup.com/articles/progressive-disclosure/).

**Small icon does not mean small target.** Keep practical desktop hit areas
and visible focus even after reducing arrow size. W3C's web guidance provides
a useful minimum-target reference, although CSS pixels and native Qt logical
pixels are not a formal accessibility equivalence. This plan does not claim
WCAG conformance from an icon-size or screenshot check.
[W3C target-size guidance](https://www.w3.org/WAI/WCAG22/Understanding/target-size-minimum).

## Additional QoL work — proposed, not implemented here

Priority is based on current code findings and task frequency, not a user study.

| Order | Change | Concrete behavior and rationale | Relative effort |
| --- | --- | --- | --- |
| 1 | Simplify the top action bar | Keep `Recovery` immediately before `Open…`, followed by `Save flight…`. Move Clear flight and Demo into a labelled More menu; retain their standard menu access. Rename the existing export action consistently because it already saves CSV plus metadata. Reuse the same actions across entry points. | Small |
| 2 | Show recording state before close | Place `Unsaved`, `Saving…`, `Saved` or `Save failed` beside the session name. Read the existing recording revision/save state; do not create a second dirty flag. Keep the existing close guard. | Small–medium |
| 3 | Make unsupported measurements explicit | Battery availability currently affects preflight checks, but the metric tiles still format battery/RSSI sample members directly. Carry source capability into those readouts so unsupported data shows a dash with an explanation. A supported field with a real zero must remain zero. | Medium |
| 4 | Remember layouts separately | Persist Graph/Map splitter widths and Replay/Live module visibility. Offer `Reset layout` in Instruments. Restore useful bounds when a saved layout no longer fits the screen. Map can retain a narrower column without changing the user's Graph width. | Medium |
| 5 | Improve replay navigation | Add an on-demand `Go to time…` entry using the existing preview timeline and event seeking. Space, Left/Right and Home/End already exist: improve their discoverability instead of adding duplicate shortcuts. Reject invalid times and clamp/resolve valid input to original samples. | Small–medium |
| 6 | Clarify compact controls and messages | Show full file/device names in tooltips; explain why disabled actions cannot run; make missing-field diagnostics copyable; keep Follow state clear at compact widths without duplicating map controls. | Small |
| 7 | Reclaim Map header space | Native captures show a taller view-switcher/header area in Map than Graph. Check layout stretch and empty graph-only rows, then keep the switcher compact so the reclaimed space goes to the map. | Small |

Recommended top-bar hierarchy after the first two proposals:

```text
[session name · unsaved]       [Recovery]  [Open…] [Save flight…] [More…]
```

Instruments stays after View in the top menu, as previously requested. Connection
actions stay inside Live Telemetry. Any reorganization must retain save warnings,
source isolation, existing keyboard behavior and access to hidden instruments.
There is no need for a new free-form docking system or another settings window.

## Skills and implementation constraints

Use the installed Qt UI Design skill for desktop conventions, Impeccable for
consistency and bounded visual review, and Ponytail for the smallest native Qt
implementation. Qt's official skill catalogue was checked; installing another
skill is unnecessary for these Widgets layout/style changes. QML-specific
skills would not match this implementation.
[Qt's official skill catalogue](https://github.com/TheQtCompanyRnD/agent-skills).

Keep existing semantic theme tokens and fonts. No new rendering dependency,
image generation, CMake edit or resource-manifest change is required. Preserve
raw FC orientation and the original trajectory.

## Acceptance checks

- Graph → Map → Graph produces horizontal → vertical → horizontal instruments
  without recreating widgets, changing pose or losing individual visibility.
- Empty/reset Replay hides both modules. Live remains horizontal.
- Dropdown selectors and both expansion states display a compact open chevron
  in light/dark themes. Disabled colors, focus and keyboard interaction remain
  usable. No action-only button acquires a dropdown arrow.
- At 920 × 650, 1024 × 650 and 1440 × 800, the instrument column fits the window
  and the main view retains useful space. Internal trace/chart scrolling remains
  distinct from an unwanted whole-page scroll.
- Arrow rendering and actual selector visibility have regression coverage.
  Run the full suite, warning-free build, changed-line formatting and cppcheck.
- Inspect one batch of native captures, fix findings together, then use at most
  one confirmation batch. Screenshot tests block map network requests; they do
  not validate online tiles or serial hardware.

The seven additional QoL proposals require implementation after this pass;
their inclusion in this document is not a claim that they are complete.

## Completed verification — 16 October 2026

The requested chevron and instrument-arrangement changes are implemented in the
working tree. The seven additional proposals above remain planned.

- Full CTest suite: 70/70 passed.
- Final native checks: four cases / 429 assertions passed across both themes
  and the three desktop sizes. Captures confirmed visible selector arrows,
  collapsed/expanded chevrons, horizontal Graph/Live and vertical Replay Map.
- Regressions first reproduced the horizontal Map row, filled arrow glyphs and
  completely missing selector arrow. The final tests cover those failures.
- The build completed without compiler warnings. Changed-line clang-format,
  `git diff --check` and cppcheck passed; cppcheck retains the same six existing
  style/performance suggestions. The existing native Workbench font-alias
  diagnostic remains.
- Focused review found no material issues with style ownership, theme handling
  or instrument lifecycle. Native captures checked control/layout appearance;
  they did not validate online map content or serial hardware.
