#include <iostream>
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QSettings>
#include <QStringList>
#include <QMetaType>
#include <QResource>

#include "domain/FlightSample.h"
#include "gateway/comms/SerialFramerWorker.h"
#include "gateway/comms/windows/SerialCommsWindows.h"
#include "gui/MainWindow.h"
#include "gui/SettingsKeys.h"
#include "gui/ThemeManager.h"
#include "services/RingBuffer.h"
#include "services/SerialWriter.h"
#include "services/telemetry/framers/CsvFramer.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    qRegisterMetaType<FlightSample>();

    Q_INIT_RESOURCE(resources);

    const auto loadFontFamily = [](const QString &resource, const QString &label) -> QString {
        const int fontId = QFontDatabase::addApplicationFont(resource);
        if (fontId >= 0) {
            const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
            if (!families.isEmpty()) {
                return families.first();
            }
        }
        qWarning() << "Failed to load font" << label << "from" << resource;
        return {};
    };

    const QString redHatFamily = loadFontFamily(QStringLiteral(":/fonts/RedHatMono-Regular.ttf"), QStringLiteral("Red Hat Mono"));
    QFont baseFont = app.font();
    baseFont.setFamily(
        !redHatFamily.isEmpty() ? redHatFamily : QStringLiteral("Red Hat Mono"));
    QSettings settings(kSettingsOrg, kSettingsApp);
    if (settings.contains(kSettingsFontSize)) {
        const int savedPt = settings.value(kSettingsFontSize).toInt();
        if (savedPt >= 6 && savedPt <= 48) {
            baseFont.setPointSize(savedPt);
        }
    }
    app.setFont(baseFont);

    const QString workbenchFamily = loadFontFamily(QStringLiteral(":/fonts/Workbench-Regular.ttf"), QStringLiteral("Workbench"));
    if (!workbenchFamily.isEmpty()) {
        app.setProperty("workbenchFontFamily", workbenchFamily);
    }

    cosmo::ThemeManager::instance().loadPersistedSkin(cosmo::ThemeManager::skinsDirectory());

    MainWindow window;
    window.show();

    return app.exec();
}
