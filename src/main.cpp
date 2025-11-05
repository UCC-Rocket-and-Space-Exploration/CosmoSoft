#include <QApplication>
<<<<<<< HEAD
#include <QFont>
#include <QFontDatabase>
#include <QDebug>
#include <QResource>
#include <QStringList>

#include "MainWindow.h"   // Our custom UI shell with toolbar and stacked pages.
=======
#include "MainWindow.h"   // Our custom UI shell with toolbar, stacked pages, and a demo chart.
>>>>>>> f77beb0 (UI Skeleton)

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    QPushButton button("Hello world!", nullptr);
    button.resize(200, 100);
    button.show();
    return QApplication::exec();
    QApplication app(argc, argv);    // QApplication owns the event loop and must be created first.

<<<<<<< HEAD
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
    if (!redHatFamily.isEmpty()) {
        app.setFont(QFont(redHatFamily));
    } else {
        app.setFont(QFont("Red Hat Mono"));  // Fall back to installed version if available.
    }

    const QString workbenchFamily = loadFontFamily(QStringLiteral(":/fonts/Workbench-Regular.ttf"), QStringLiteral("Workbench"));
    if (!workbenchFamily.isEmpty()) {
        app.setProperty("workbenchFontFamily", workbenchFamily);
    }

=======
>>>>>>> f77beb0 (UI Skeleton)
    MainWindow window;               // Main window assembles the skeleton UI described in MainWindow.cpp.
    window.show();                   // Display the window before handing control to the event loop.

    return app.exec();               // Hand over control to Qt; finishes when the window closes.
}
