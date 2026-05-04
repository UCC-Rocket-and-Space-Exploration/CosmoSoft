/**
 * @file Theme.h
 * @brief Runtime design-token accessors for the CosmoSoft GUI.
 *
 * All colours used in setStyleSheet() calls or QPainter code must be
 * referenced via this header.  Values are resolved at runtime from the
 * active CosmoTheme managed by ThemeManager.
 *
 * Usage:
 * @code
 *   #include "gui/Theme.h"
 *   setStyleSheet(QString(u"color: %1; font-size: %2px;"_s)
 *       .arg(Theme::kTextMuted())
 *       .arg(Theme::kFontSizeBase));
 * @endcode
 *
 * Global widget defaults are generated from the active palette by
 * ThemeManager::generateQss() and applied via qApp->setStyleSheet().
 */

#ifndef COSMO_SOFT_THEME_H
#define COSMO_SOFT_THEME_H

#include "gui/ThemeManager.h"

namespace Theme {

// ── Backgrounds ───────────────────────────────────────────────────────────────

/** @brief Base surface: dialogs, settings root, about dialog chrome. */
inline const QString &kBgBase()   { return cosmo::ThemeManager::instance().palette().bg_base; }

/** @brief Dark surface: console areas, log views, sysinfo label bg. */
inline const QString &kBgDark()   { return cosmo::ThemeManager::instance().palette().bg_dark; }

/** @brief Raised panel: group boxes, combo pop-up views, connection overlay. */
inline const QString &kBgPanel()  { return cosmo::ThemeManager::instance().palette().bg_panel; }

/** @brief Input background: QComboBox, QLineEdit, checkbox indicator bg. */
inline const QString &kBgInput()  { return cosmo::ThemeManager::instance().palette().bg_input; }

/** @brief Default button background. */
inline const QString &kBgButton() { return cosmo::ThemeManager::instance().palette().bg_button; }

// ── Text ──────────────────────────────────────────────────────────────────────

/** @brief Primary foreground text. */
inline const QString &kTextPrimary() { return cosmo::ThemeManager::instance().palette().text_primary; }

/** @brief Mid-weight text: page titles, group-box titles. */
inline const QString &kTextMid()     { return cosmo::ThemeManager::instance().palette().text_mid; }

/** @brief Muted secondary labels (tab text, setting descriptions). */
inline const QString &kTextMuted()   { return cosmo::ThemeManager::instance().palette().text_muted; }

/** @brief Dim captions: telemetry strip page label, replay captions. */
inline const QString &kTextDim()     { return cosmo::ThemeManager::instance().palette().text_dim; }

// ── Borders ───────────────────────────────────────────────────────────────────

/** @brief Default border for inputs and group boxes. */
inline const QString &kBorderDefault() { return cosmo::ThemeManager::instance().palette().border_default; }

/** @brief Lighter border for buttons. */
inline const QString &kBorderLight()   { return cosmo::ThemeManager::instance().palette().border_light; }

/** @brief Subtle border for tabs, dividers, scrollbar track. */
inline const QString &kBorderSubtle()  { return cosmo::ThemeManager::instance().palette().border_subtle; }

/** @brief Panel border for stat tiles, chart frame, monitoring tiles. */
inline const QString &kBorderPanel()   { return cosmo::ThemeManager::instance().palette().border_panel; }

// ── Button interaction states ─────────────────────────────────────────────────

/** @brief Button background on hover. */
inline const QString &kBtnHover()   { return cosmo::ThemeManager::instance().palette().btn_hover; }

/** @brief Button background when pressed. */
inline const QString &kBtnPressed() { return cosmo::ThemeManager::instance().palette().btn_pressed; }

// ── Accent / semantic ─────────────────────────────────────────────────────────

/** @brief Hyperlink / about-page link colour. */
inline const QString &kAccentLink()     { return cosmo::ThemeManager::instance().palette().accent_link; }

/** @brief Checked checkbox fill. */
inline const QString &kAccentCheckbox() { return cosmo::ThemeManager::instance().palette().accent_checkbox; }

/** @brief Checked checkbox border. */
inline const QString &kAccentCheckboxBorder() { return cosmo::ThemeManager::instance().palette().accent_checkbox_border; }

/** @brief Danger / packet-drop badge text. */
inline const QString &kDanger() { return cosmo::ThemeManager::instance().palette().danger; }

/** @brief Error row in the event log. */
inline const QString &kError() { return cosmo::ThemeManager::instance().palette().error; }

/** @brief Selection background in combo pop-up views. */
inline const QString &kSelectBg() { return cosmo::ThemeManager::instance().palette().select_bg; }

// ── Font families (unchanged — not palette-dependent) ─────────────────────────

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
