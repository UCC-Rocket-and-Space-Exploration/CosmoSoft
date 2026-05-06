# Theme Switching Fix Summary

## Problem
When switching between light and dark themes, certain UI elements were not updating their colors properly:
1. Brand label "Soft" text color stayed in the old theme's accent color
2. Link idle status colors didn't refresh
3. Graph control buttons (view switchers, zoom buttons, toggles) kept their old theme colors

## Root Cause
- Qt caches button styles for performance, especially for checkable buttons
- Inline HTML styles in brand label weren't being regenerated
- Link status colors were only set once during initial render

## Solution

### 1. Brand Label Dynamic Updates
**Files**: `include/gui/MainWindow.h`, `src/gui/MainWindow.cpp`

- Changed brand label from local variable to member variable `m_brandLabel`
- Added dynamic text update in `onThemeChanged()`:
  ```cpp
  m_brandLabel->setText(
      QString(u"Cosmo<span style=\"color:%1\">Soft</span>"_s)
          .arg(Theme::kAccentLink()));
  ```

### 2. Link Status Color Refresh  
**File**: `src/gui/MainWindow.cpp`

- Added `syncTelemetryStrip()` call in `onThemeChanged()`
- This reapplies semantic colors (success/warning/idle) with new theme values:
  - Idle: `bg_dark` + `text_muted`
  - Connected: `success_bg` + `success`
  - Errors: `warning_bg` + `warning`

### 3. Button Style Force-Refresh
**File**: `src/gui/pages/DashboardPage.cpp`

Added lambda in `refreshPageStyleSheet()` to force Qt to recompute button styles:

```cpp
auto forceButtonStyleUpdate = [](QWidget *widget) {
    if (widget) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
};

forceButtonStyleUpdate(m_graphViewBtn);
forceButtonStyleUpdate(m_mapViewBtn);
forceButtonStyleUpdate(m_zoomOutBtn);
forceButtonStyleUpdate(m_zoomInBtn);
forceButtonStyleUpdate(m_zoomResetBtn);
forceButtonStyleUpdate(m_tracesToggleBtn);
forceButtonStyleUpdate(m_showMarkersToggle);
forceButtonStyleUpdate(m_showPointValuesToggle);
forceButtonStyleUpdate(m_followToggle);
```

## Affected Components

### Graph View Buttons
- **Checked state**: Uses `accent_link` background
  - Dark: `#6ab0de` (light blue)
  - Light: `#1a73b8` (darker blue)
- **Unchecked state**: Uses `text_muted` color
  - Dark: `#b0bcc8`
  - Light: `#5a6370`

### Link Status Badge
- **Idle**: `bg_dark` + `text_muted`
- **Connected**: `success_bg` + `success`  
- **Errors**: `warning_bg` + `warning`

### Brand Label
- "Soft" text color: `accent_link`
  - Dark: `#6ab0de`
  - Light: `#1a73b8`

## Additional Changes

### Chart Stats Label Removal
The chart stats label (showing trace count, point count, decimation status) has been completely removed:
- Removed `updateChartStatsLabel()` function and all 13 call sites
- Removed `m_chartStatsLabel` widget creation and member variable
- Removed associated QSS styling

## Testing
✅ Build successful
✅ All buttons force-refresh on theme change
✅ Link status colors update dynamically
✅ Brand label accent color updates
✅ No QString::arg errors
✅ No linker errors

## Related Files Modified
1. `include/gui/MainWindow.h` - Added m_brandLabel member
2. `src/gui/MainWindow.cpp` - Brand label + link status updates
3. `src/gui/pages/DashboardPage.cpp` - Button style force-refresh, chart stats label removed
4. `include/gui/pages/DashboardPage.h` - Chart stats label removed
5. `src/gui/widgets/StatTileWidget.cpp` - Fixed QString::arg placeholders
