#include <QApplication>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QSettings>
#include <QStringList>
#include <QMetaType>
#include <QResource>

#include "domain/FlightSample.h"
#include "gui/MainWindow.h"

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
    QFont baseFont = !redHatFamily.isEmpty() ? QFont(redHatFamily) : QFont(QStringLiteral("Red Hat Mono"));
    QSettings settings(QStringLiteral("CosmoSoft"), QStringLiteral("cosmo-soft"));
    const int savedPt = settings.value(QStringLiteral("ui/fontPointSize"), 12).toInt();
    if (savedPt >= 6 && savedPt <= 48) {
        baseFont.setPointSize(savedPt);
    }
    app.setFont(baseFont);

    const QString workbenchFamily = loadFontFamily(QStringLiteral(":/fonts/Workbench-Regular.ttf"), QStringLiteral("Workbench"));
    if (!workbenchFamily.isEmpty()) {
        app.setProperty("workbenchFontFamily", workbenchFamily);
    }

    QFile qssFile(QStringLiteral(":/styles/theme.qss"));
    if (qssFile.open(QFile::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(qssFile.readAll()));
    } else {
        qWarning() << "Failed to load global stylesheet :/styles/theme.qss";
    }

    MainWindow window;
    window.show();

    return app.exec();
}
