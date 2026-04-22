/**
 * @file Theme.h
 * @brief Compile-time design tokens for the CosmoSoft GUI.
 *
 * All colours, font families, font sizes, and border radii used in
 * setStyleSheet() calls must be referenced from this header rather than
 * scattered as inline hex literals throughout the source files.  This
 * ensures a single source of truth and makes future restyling trivial.
 *
 * Usage:
 * @code
 *   #include "gui/Theme.h"
 *   setStyleSheet(QString(u"color: %1; font-size: %2px;"_s)
 *       .arg(Theme::kTextMuted)
 *       .arg(Theme::kFontSizeBase));
 * @endcode
 *
 * Global widget defaults (QPushButton, QComboBox, QScrollBar, QCheckBox,
 * QGroupBox) are applied via assets/theme.qss loaded in main.cpp.
 * Per-widget stylesheets should only contain page-specific overrides.
 */

#ifndef COSMO_SOFT_THEME_H
#define COSMO_SOFT_THEME_H

namespace Theme {

// ── Backgrounds ───────────────────────────────────────────────────────────────

/** @brief Base surface: dialogs, settings root, about dialog chrome. */
constexpr auto kBgBase   = "#1f1f1f";

/** @brief Dark surface: console areas, log views, sysinfo label bg. */
constexpr auto kBgDark   = "#161618";

/** @brief Raised panel: group boxes, combo pop-up views, connection overlay. */
constexpr auto kBgPanel  = "#2b2d33";

/** @brief Input background: QComboBox, QLineEdit, checkbox indicator bg. */
constexpr auto kBgInput  = "#1a1a1a";

/** @brief Default button background. */
constexpr auto kBgButton = "#3d3f47";

// ── Text ──────────────────────────────────────────────────────────────────────

/** @brief Primary foreground text. */
constexpr auto kTextPrimary = "#f8f8f8";

/** @brief Mid-weight text: page titles, group-box titles. */
constexpr auto kTextMid     = "#c8c8c8";

/** @brief Muted secondary labels (tab text, setting descriptions). */
constexpr auto kTextMuted   = "#9aa7b8";

/** @brief Dim captions: telemetry strip page label, replay captions. */
constexpr auto kTextDim     = "#8fa0b0";

// ── Borders ───────────────────────────────────────────────────────────────────

/** @brief Default border for inputs and group boxes. */
constexpr auto kBorderDefault = "#4d4d4d";

/** @brief Lighter border for buttons. */
constexpr auto kBorderLight   = "#6a6a6a";

/** @brief Subtle border for tabs, dividers, scrollbar track. */
constexpr auto kBorderSubtle  = "#3a3a3a";

/** @brief Panel border for stat tiles, chart frame, monitoring tiles. */
constexpr auto kBorderPanel   = "#3b3b45";

// ── Button interaction states ─────────────────────────────────────────────────

/** @brief Button background on hover. */
constexpr auto kBtnHover   = "#4d4f57";

/** @brief Button background when pressed. */
constexpr auto kBtnPressed = "#2d2f37";

// ── Accent / semantic ─────────────────────────────────────────────────────────

/** @brief Hyperlink / about-page link colour. */
constexpr auto kAccentLink     = "#6ab0de";

/** @brief Checked checkbox fill. */
constexpr auto kAccentCheckbox = "#4a7fb5";

/** @brief Checked checkbox border. */
constexpr auto kAccentCheckboxBorder = "#5a9fd5";

/** @brief Danger / packet-drop badge text. */
constexpr auto kDanger = "#ff6b6b";

/** @brief Error row in the event log. */
constexpr auto kError = "#e05555";

/** @brief Selection background in combo pop-up views. */
constexpr auto kSelectBg = "#4b4b4b";

// ── Font families ─────────────────────────────────────────────────────────────

/** @brief Monospace stack used for all data labels and UI controls. */
constexpr auto kFontMono    = R"("Red Hat Mono","Courier New","Roboto Mono",monospace)";

/** @brief Display stack used for the brand logo label. */
constexpr auto kFontDisplay = R"("Workbench","Courier New","Roboto Mono",monospace)";

// ── Font sizes (px) ───────────────────────────────────────────────────────────

/** @brief Small UI text: buttons, captions, badges. */
constexpr int kFontSizeSm   = 11;

/** @brief Base UI text: labels, inputs, checkboxes. */
constexpr int kFontSizeBase = 12;

/** @brief Medium headings: section titles, debug checkbox. */
constexpr int kFontSizeMd   = 13;

// ── Border radii (px) ─────────────────────────────────────────────────────────

/** @brief Standard corner radius for buttons, inputs, popups. */
constexpr int kRadiusSm = 4;

/** @brief Larger corner radius for panels, stat tiles, group boxes. */
constexpr int kRadiusMd = 8;

} // namespace Theme

#endif // COSMO_SOFT_THEME_H
