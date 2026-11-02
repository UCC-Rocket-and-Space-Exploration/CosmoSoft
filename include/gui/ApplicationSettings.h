#pragma once

#include "gui/SettingsKeys.h"
#include <QSettings>

namespace cosmo {
/** @brief Open application preferences using the process-selected storage
 * format. Production uses native settings; tests select INI storage in a
 * temporary path.
 */
inline QSettings applicationSettings() {
  return QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                   kSettingsOrg, kSettingsApp);
}
} // namespace cosmo
