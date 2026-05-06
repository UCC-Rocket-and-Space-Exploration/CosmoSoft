# Top Bar Redesign - Design Specification

**Date:** 2026-05-06  
**Scope:** Clean up CosmoSoft top bar UI by removing dummy information and improving button functionality  
**Files Modified:** `src/gui/MainWindow.cpp`, `include/gui/MainWindow.h`

## Overview

Redesign the MainWindow top bar area to remove unnecessary UI elements and create a cleaner, more functional interface. The application is focused on flight log monitoring and replay, not live telemetry capture, so the UI should reflect this purpose.

## Current State

The UI currently has three horizontal bars:

1. **Top Toolbar** - Brand (CosmoSoft + timestamp) | "Flight data" page label | Settings button
2. **Connection Bar** - Serial controls (port, baud, connect/disconnect) + File operations (open, clear, export)
3. **Data Bar** - Telemetry strip showing LINK status, RATE, and dropped packet count

### Problems

- "Flight data" page label is redundant (only one page currently exists)
- Data bar shows live telemetry metrics not needed for log replay/analysis
- Serial connection controls clutter the interface for a monitoring tool
- Settings icon doesn't adapt to theme (stays same color in light/dark modes)

## Design Goals

1. Remove dummy/redundant information
2. Create space for future page navigation buttons
3. Improve visual hierarchy (page-specific actions vs global navigation)
4. Fix theme-aware icon rendering
5. Maintain familiar file operation access (Open log, Clear flight, Export session)

## Architecture

### Three-Tier Layout

```
┌─────────────────────────────────────────────────────────────┐
│ TOP TOOLBAR                                                 │
│ [CosmoSoft + timestamp]  [future page nav]  [Settings]     │
├─────────────────────────────────────────────────────────────┤
│ PAGE ACTION BAR                                             │
│ [Breadcrumb/Context]  [📁] [🗑️] [💾]                        │
├─────────────────────────────────────────────────────────────┤
│ PAGE CONTENT                                                │
│ (Dashboard, charts, replay controls, etc.)                  │
└─────────────────────────────────────────────────────────────┘
```

**Top Toolbar:** Global application chrome - branding, page navigation (future), settings  
**Page Action Bar:** Page-specific context and actions - breadcrumbs, file operations  
**Page Content:** The main DashboardPage widget

## Detailed Design

### 1. Top Toolbar Changes

**Remove:**
- `m_toolbarPageLabel` (QLabel showing "Flight data")
- Associated spacing/padding widgets

**Keep:**
- Brand block (`m_brandLabel` + `m_missionMetaLabel`) on the left
- Settings button on the right
- Stretch space in the middle (reserved for future page navigation buttons)

**Fix - Settings Icon Theme Adaptation:**

Current behavior:
- Uses `settings_button.png` for normal state
- Uses `settings_button_black.png` for checked state
- Does not adapt to theme changes

New behavior:
- In **dark mode**: Use `settings_button.png` (light icon on dark background)
- In **light mode**: Use `settings_button_black.png` (dark icon on light background)
- Update icon in `onThemeChanged()` slot

Implementation:
```cpp
void MainWindow::onThemeChanged() {
    // ... existing toolbar stylesheet update ...
    
    if (m_openSettingsAction) {
        QIcon settingsIcon;
        const bool isDark = (Theme::currentSkin() == Theme::Skin::Dark);
        const QString iconPath = isDark 
            ? u":/icons/settings_button.png"_s 
            : u":/icons/settings_button_black.png"_s;
        settingsIcon.addFile(iconPath, QSize(), QIcon::Normal);
        m_openSettingsAction->setIcon(settingsIcon);
    }
}
```

**Layout after changes:**
```
[Brand Block] [                    stretch                    ] [Settings]
```

### 2. Data Bar Removal

**Remove completely:**
- `m_dataBar` widget
- `m_dataStripPageLabel`
- `m_dataLinkStatusLabel`
- `m_dataRateLabel`
- `m_droppedBadgeLabel`
- `m_dataRateTimer`

**Remove methods:**
- `setupDataBar()`
- `buildDataBarStyleSheet()`
- `updateDataRateLabel()`
- `syncTelemetryStrip()`
- Data bar-related code in `onThemeChanged()`
- Data bar widget addition in `setupPages()`

**Remove all call sites:**
- `syncTelemetryStrip()` calls in: `updateTopBarsForCurrentPage()`, `onReplayPositionChanged()`, `applyReplayTelemetrySample()`, `onClearFlightData()`, `startSerial()`, `stopSerial()`, `setupPages()`, `onThemeChanged()`
- `updateDataRateLabel()` calls in: constructor (timer connection), `updateTopBarsForCurrentPage()`

**Rationale:** The data bar shows live telemetry metrics (link status, byte rate, dropped packets) which are not relevant for a flight log monitoring/replay tool. This information was designed for live serial capture, which is being deprioritized.

### 3. Connection Bar Transformation → Page Action Bar

**Purpose:** Repurpose the existing connection bar to show page context (breadcrumbs) and page-specific file operations.

**Remove:**
- All serial control widgets:
  - `m_serialControlBlock` container
  - `m_portCombo` (port dropdown)
  - `m_baudCombo` (baud rate dropdown)
  - Port/baud labels
  - Refresh, Connect, Disconnect buttons
- Serial-related methods:
  - `refreshSerialPorts()`
  - `loadSerialPrefsToUi()`
  - `persistSerialPrefs()`
  - `startSerial()`
  - `stopSerial()`
- Serial-related member variables:
  - `m_serialPortSummary`
  - Serial worker/parser pipeline (if not needed for future live mode)

**Keep & Repurpose:**
- Container widget: `m_connectionBar` (rename semantically to page action bar)
- Container layout: Horizontal box layout
- Context label: `m_connectionPageLabel` → displays breadcrumbs

**Add - Icon-Only Action Buttons:**

Three file operation buttons converted to icon-only style:

| Action | Icon Resource | Tooltip | Keyboard Shortcut |
|--------|--------------|---------|-------------------|
| Open Log | `:/icons/folder-open.png` (dark)<br>`:/icons/folder-open-black.png` (light) | "Open flight log" | Ctrl+O |
| Clear Flight | `:/icons/trash.png` (dark)<br>`:/icons/trash-black.png` (light) | "Clear flight data" | (none) |
| Export Session | `:/icons/export.png` (dark)<br>`:/icons/export-black.png` (light) | "Export session to CSV" | Ctrl+Shift+E |

**Button styling:**
- QPushButton with `kind="actionButton"` property
- Icon size: 20x20 pixels (displayed at 24x24 with padding)
- Button size: 32x32 pixels minimum
- Flat style with hover background
- No text, icon-only

**Breadcrumb/Context Label:**

Displays current page and session state:

| State | Breadcrumb Text |
|-------|----------------|
| No session loaded | "Flight Monitoring" |
| Replay session loaded | "Flight Monitoring › Session: filename.csv" |
| Live session (future) | "Flight Monitoring › Live Session" |

Updated in:
- `onOpenReplayFile()` - Set to "Flight Monitoring › Session: {filename}"
- `onClearFlightData()` - Reset to "Flight Monitoring"
- Session load error - Reset to "Flight Monitoring"

**New layout:**
```
[Breadcrumb Label]  [spacing: 16px]  [📁 Open] [🗑️ Clear] [💾 Export]  [stretch]
```

**Visual grouping:**
- File operation buttons grouped together (no spacing between them)
- 16px spacing before the button group
- Subtle visual separator (optional, via styling or spacer widget)

**Styling:**

The bar should have a lighter background than the top toolbar to create visual separation:

```cpp
QString buildActionBarStyleSheet() {
    return QString(uR"(
        QWidget#pageActionBar {
            background: %1;
            color: %2;
            border-bottom: 1px solid %3;
            padding: 8px 16px;
        }
        QLabel#breadcrumbLabel {
            font-size: %4px;
            color: %5;
            font-family: %6;
            font-weight: 500;
        }
        QPushButton[kind="actionButton"] {
            min-width: 32px;
            min-height: 32px;
            max-width: 32px;
            max-height: 32px;
            border: none;
            border-radius: 4px;
            background-color: transparent;
            padding: 4px;
        }
        QPushButton[kind="actionButton"]:hover {
            background-color: %7;
        }
        QPushButton[kind="actionButton"]:pressed {
            background-color: %8;
        }
    )"_s)
        .arg(Theme::kBgBase())         // %1 - lighter than toolbar
        .arg(Theme::kTextPrimary())    // %2
        .arg(Theme::kBorderSubtle())   // %3
        .arg(Theme::kFontSizeBase)     // %4
        .arg(Theme::kTextMid())        // %5
        .arg(Theme::kFontMono)         // %6
        .arg(Theme::kBtnHover())       // %7
        .arg(Theme::kBgButton());      // %8
}
```

**Theme adaptation:**

In `onThemeChanged()`, update action button icons:

```cpp
void MainWindow::onThemeChanged() {
    // ... existing code ...
    
    if (m_connectionBar) {
        m_connectionBar->setStyleSheet(buildActionBarStyleSheet());
    }
    
    // Update action button icons based on theme
    const bool isDark = (Theme::currentSkin() == Theme::Skin::Dark);
    const QString iconSuffix = isDark ? u".png"_s : u"-black.png"_s;
    
    if (m_openLogBtn) {
        m_openLogBtn->setIcon(QIcon(u":/icons/folder-open"_s + iconSuffix));
    }
    if (m_clearFlightBtn) {
        m_clearFlightBtn->setIcon(QIcon(u":/icons/trash"_s + iconSuffix));
    }
    if (m_exportBtn) {
        m_exportBtn->setIcon(QIcon(u":/icons/export"_s + iconSuffix));
    }
}
```

## Icon Resources Required

New icons needed (both dark and light variants):

1. **folder-open.png** / **folder-open-black.png** - Open log file
2. **trash.png** / **trash-black.png** - Clear flight data
3. **export.png** / **export-black.png** - Export session

These should be added to the Qt resource file (`.qrc`) at `:/icons/` path.

If icons don't exist, use:
- Unicode fallback: 📁 (U+1F4C1), 🗑️ (U+1F5D1), 💾 (U+1F4BE)
- Qt standard icons as temporary placeholders
- Design custom icons matching the existing settings button style

## Component Changes

### MainWindow.h

**Remove:**
```cpp
// Top toolbar
QLabel *m_toolbarPageLabel  = nullptr;

// Data bar
QWidget *m_dataBar             = nullptr;
QLabel  *m_dataStripPageLabel  = nullptr;
QLabel  *m_dataLinkStatusLabel = nullptr;
QLabel  *m_dataRateLabel       = nullptr;
QLabel  *m_droppedBadgeLabel   = nullptr;
QTimer  *m_dataRateTimer       = nullptr;
qint64   m_prevBytesForRate    = 0;
std::size_t m_lastDroppedCount = 0;

// Connection bar - serial controls
QWidget    *m_serialControlBlock = nullptr;
QComboBox  *m_portCombo          = nullptr;
QComboBox  *m_baudCombo          = nullptr;
QString     m_serialPortSummary;
```

**Add:**
```cpp
// Page action bar
QPushButton *m_openLogBtn     = nullptr;
QPushButton *m_clearFlightBtn = nullptr;
QPushButton *m_exportBtn      = nullptr;
```

**Rename (conceptually):**
```cpp
// Keep existing member, repurpose for breadcrumbs
QLabel *m_connectionPageLabel = nullptr;  // Now shows breadcrumbs
```

### MainWindow.cpp

**Remove methods:**
- `setupDataBar()`
- `buildDataBarStyleSheet()`
- `updateDataRateLabel()`
- `syncTelemetryStrip()`
- `refreshSerialPorts()`
- `loadSerialPrefsToUi()`
- `persistSerialPrefs()`
- `startSerial()`
- `stopSerial()`
- `applyPendingReplayTelemetryStrip()`
- `onReplayPositionChanged()`

**Add methods:**
```cpp
QString buildActionBarStyleSheet();
void updateBreadcrumb(const QString &context);
```

**Modify methods:**
- `setupToolbar()` - Remove page label
- `setupConnectionBar()` - Transform to page action bar
- `onThemeChanged()` - Add settings icon update + action button icon updates
- `onOpenReplayFile()` - Update breadcrumb on successful load
- `onClearFlightData()` - Reset breadcrumb to default
- Constructor - Remove data rate timer setup

## Future Extensibility

### Adding Page Navigation

When adding more pages (e.g., "Analysis", "Reports", "Settings"):

1. Create navigation buttons in `setupToolbar()` after the brand block
2. Style them similarly to current nav button style (checkable, border, hover states)
3. Place them in the center stretch space
4. Use QActionGroup to make them mutually exclusive
5. Connect to page switching logic in `m_pages` stacked widget

Example future layout:
```
[Brand] [Flight Monitoring] [Analysis] [Reports]        [Settings]
```

### Per-Page Action Bars

Each page can have its own action bar:

- **Flight Monitoring page**: Open log, Clear flight, Export session (current design)
- **Analysis page**: Run analysis, Generate report, Export results
- **Reports page**: New report, Open report, Print

Implementation:
- Keep `m_connectionBar` as a shared container
- Clear and rebuild buttons when switching pages
- Or create per-page action bars and swap them in stacked widget

## Error Handling

### Missing Icons

If icon resources are missing:
- Fall back to text buttons temporarily
- Log warning to console
- Don't crash, show placeholder or empty icon

### Session State Transitions

Breadcrumb updates must handle:
- File load errors - Reset to "Flight Monitoring"
- Cleared session - Reset to "Flight Monitoring"  
- Replay mode vs live mode - Different breadcrumb text
- No filename available - Show "Unknown Session"

## Testing Checklist

**Visual Testing:**
- [ ] Top toolbar shows brand + settings, no "Flight data" label
- [ ] Settings icon adapts to theme (light icon in dark mode, dark icon in light mode)
- [ ] No data bar visible below toolbar
- [ ] Page action bar shows breadcrumb + three icon buttons
- [ ] Icon buttons have correct icons in dark/light modes
- [ ] Icon buttons show tooltips on hover
- [ ] Button hover states work correctly
- [ ] Visual grouping clear between file ops and other elements

**Functional Testing:**
- [ ] Open log button works (Ctrl+O shortcut)
- [ ] Clear flight button works
- [ ] Export session button works (Ctrl+Shift+E shortcut)
- [ ] Settings button still opens settings window
- [ ] Breadcrumb updates when loading a file
- [ ] Breadcrumb shows filename correctly
- [ ] Breadcrumb resets when clearing flight
- [ ] Theme switching updates all icons correctly

**Regression Testing:**
- [ ] Replay functionality still works
- [ ] Recent files menu still works
- [ ] File dialogs open correctly
- [ ] Export to CSV still works
- [ ] Clear flight confirmation dialog still works
- [ ] Settings window opens/closes correctly
- [ ] Window geometry save/restore still works

## Implementation Notes

### Migration Strategy

1. **Phase 1:** Remove data bar (low risk, high visibility cleanup)
2. **Phase 2:** Remove toolbar page label (trivial)
3. **Phase 3:** Transform connection bar (moderate complexity)
   - Remove serial controls first
   - Convert buttons to icon-only
   - Add breadcrumb logic
4. **Phase 4:** Fix settings icon theming (small polish)

### Code Organization

Keep styling methods organized:
```cpp
// Styling
QString buildToolbarStyleSheet();
QString buildActionBarStyleSheet();  // New

// Setup
void setupToolbar();
void setupConnectionBar();  // Heavily modified
void setupPages();

// Theme
void onThemeChanged();  // Modified
```

### Performance Considerations

- Icon loading: Cache QIcon objects, don't reload on every theme change
- Breadcrumb updates: Only update text when session state actually changes
- Stylesheet rebuilding: Only rebuild on theme change, not on every repaint

## Open Questions

None - all design decisions confirmed with user.

## Acceptance Criteria

1. ✅ "Flight data" page label removed from top toolbar
2. ✅ Data bar completely removed (no LINK/RATE/dropped packet display)
3. ✅ Settings icon adapts to light/dark theme
4. ✅ Serial connection controls removed from connection bar
5. ✅ Connection bar repurposed to show breadcrumbs + file operations
6. ✅ File operation buttons converted to icon-only style
7. ✅ Breadcrumb shows current page and session context
8. ✅ Icon buttons grouped visually with proper spacing
9. ✅ All icons adapt to theme changes
10. ✅ Layout creates space for future page navigation buttons
11. ✅ Code changes isolated to `src/gui/MainWindow.*` files only
12. ✅ No functionality regressions in file operations or settings
