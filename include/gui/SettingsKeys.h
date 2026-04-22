/**
 * @file SettingsKeys.h
 * @brief Compile-time constants for all QSettings keys used by the GUI.
 *
 * Every key string used with QSettings must be declared here rather than
 * scattered as inline string literals throughout the source files.  This
 * makes refactoring and auditing straightforward and prevents silent
 * key-name mismatches across translation units.
 *
 * Usage:
 * @code
 *   #include "gui/SettingsKeys.h"
 *   QSettings s(kSettingsOrg, kSettingsApp);
 *   s.setValue(kSettingsSerialPort, port);
 * @endcode
 */

#ifndef COSMO_SOFT_SETTINGSKEYS_H
#define COSMO_SOFT_SETTINGSKEYS_H

/** @brief QSettings organisation name shared by all CosmoSoft components. */
constexpr auto kSettingsOrg = "CosmoSoft";

/** @brief QSettings application name shared by all CosmoSoft components. */
constexpr auto kSettingsApp = "cosmo-soft";

// ── Window geometry ───────────────────────────────────────────────────────────

/** @brief Saved geometry of the main window. */
constexpr auto kSettingsWindowMainGeo = "window/mainGeometry";

/** @brief Saved geometry of the floating settings window. */
constexpr auto kSettingsWindowSettingsGeo = "window/settingsGeometry";

// ── Serial connection ─────────────────────────────────────────────────────────

/** @brief Last-used serial port device path. */
constexpr auto kSettingsSerialPort = "serial/port";

/** @brief Last-used serial baud rate (stored as a string). */
constexpr auto kSettingsSerialBaud = "serial/baud";

// ── File paths ────────────────────────────────────────────────────────────────

/** @brief Last directory used for the flight-log file picker. */
constexpr auto kSettingsReplayDir = "paths/replayDir";

// ── UI layout ─────────────────────────────────────────────────────────────────

/** @brief Saved splitter state for the DashboardPage traces/chart splitter. */
constexpr auto kSettingsDashSplitter = "ui/dashboardSplitterState";

// ── Appearance ────────────────────────────────────────────────────────────────

/** @brief UI font point size selected in SettingsPage. */
constexpr auto kSettingsFontSize = "ui/fontPointSize";

/** @brief Whether UI sounds are enabled (reserved for future audio engine). */
constexpr auto kSettingsSoundsEnabled = "ui/soundsEnabled";

/** @brief Whether the developer debug mode is active. */
constexpr auto kSettingsDebugMode = "ui/debugMode";

/** @brief Last active settings tab index. */
constexpr auto kSettingsActiveTab = "ui/settingsActiveTab";

#endif // COSMO_SOFT_SETTINGSKEYS_H
