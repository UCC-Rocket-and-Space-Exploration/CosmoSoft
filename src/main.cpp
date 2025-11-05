#include <QApplication>
#include "MainWindow.h"   // Our custom UI shell with toolbar, stacked pages, and a demo chart.

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);    // QApplication owns the event loop and must be created first.

    MainWindow window;               // Main window assembles the skeleton UI described in MainWindow.cpp.
    window.show();                   // Display the window before handing control to the event loop.

    return app.exec();               // Hand over control to Qt; finishes when the window closes.
}
