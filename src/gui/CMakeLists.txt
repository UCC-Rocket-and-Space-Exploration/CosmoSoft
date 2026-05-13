file(GLOB SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB pages   CONFIGURE_DEPENDS "pages/*.cpp")
file(GLOB widgets CONFIGURE_DEPENDS "widgets/*.cpp")

# Headers with Q_OBJECT live under include/gui; listing them ensures AUTOMOC runs (fixes MainWindow not being a QObject).
set(GUI_MOC_HEADERS
        "${CMAKE_SOURCE_DIR}/include/gui/MainWindow.h"
        "${CMAKE_SOURCE_DIR}/include/gui/FlightDataModel.h"
        "${CMAKE_SOURCE_DIR}/include/gui/FlightReplayController.h"
        "${CMAKE_SOURCE_DIR}/include/gui/pages/MonitoringPage.h"
        "${CMAKE_SOURCE_DIR}/include/gui/pages/SettingsPage.h"
        "${CMAKE_SOURCE_DIR}/include/gui/pages/DashboardPage.h"
        "${CMAKE_SOURCE_DIR}/include/gui/widgets/StatTileWidget.h"
        "${CMAKE_SOURCE_DIR}/include/gui/widgets/TracesPanel.h"
        "${CMAKE_SOURCE_DIR}/include/gui/widgets/ReplayBar.h"
)

add_library(gui ${SOURCES} ${pages} ${widgets} ${GUI_MOC_HEADERS} "MainWindow.cpp")
target_include_directories(gui PUBLIC ${HEADERS})

target_link_libraries(gui
        PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Charts
        Qt6::Concurrent
#        import
)
target_link_libraries(cosmo-backend gui)
#target_link_libraries(cosmo-backend INTERFACE gui)
