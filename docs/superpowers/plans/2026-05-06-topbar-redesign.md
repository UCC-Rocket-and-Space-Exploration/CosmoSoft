# Top Bar Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Clean up MainWindow UI by removing dummy information (page label, data bar) and improving button functionality (icon-only file operations, breadcrumbs, theme-aware settings icon).

**Architecture:** Incremental cleanup approach - remove toolbar page label and data bar completely, repurpose connection bar to page action bar with breadcrumbs and icon-only file operation buttons, fix settings icon theme adaptation.

**Tech Stack:** Qt 6, C++20, QWidget-based UI

---

## File Structure

**Modified Files:**
- `include/gui/MainWindow.h` - Remove data bar and toolbar label members, add action button members
- `src/gui/MainWindow.cpp` - Remove data bar methods, transform connection bar, add theme-aware icon switching

**No new files created** - this is a refactoring/cleanup task.

---

### Task 1: Remove Toolbar Page Label

**Files:**
- Modify: `include/gui/MainWindow.h`
- Modify: `src/gui/MainWindow.cpp`

**Goal:** Remove the "Flight data" page label from the top toolbar to create space for future page navigation buttons.

- [ ] **Step 1: Remove member variable from header**

Edit `include/gui/MainWindow.h`:

Remove this line (around line 121):
```cpp
QLabel *m_toolbarPageLabel  = nullptr;
```

- [ ] **Step 2: Remove widget creation from setupToolbar()**

Edit `src/gui/MainWindow.cpp` in the `setupToolbar()` method.

Remove these lines (around line 451-454):
```cpp
m_toolbarPageLabel = new QLabel(u"Flight data"_s, content);
m_toolbarPageLabel->setObjectName(u"missionPageTitle"_s);
contentLayout->addWidget(m_toolbarPageLabel);
contentLayout->addSpacing(8);
```

- [ ] **Step 3: Build to verify compilation**

```bash
cd /Users/hslyusar/Desktop/CosmoSoft
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(sysctl -n hw.ncpu)
```

Expected: Clean build with no errors

- [ ] **Step 4: Test application launches**

```bash
./CosmoSoft
```

Expected: Application launches, top toolbar shows brand + settings button (no page label)

- [ ] **Step 5: Commit**

```bash
git add include/gui/MainWindow.h src/gui/MainWindow.cpp
git commit -m "Remove toolbar page label from top bar

Removes the 'Flight data' label to create space for future page
navigation buttons.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

### Task 2: Remove Data Bar Completely

**Files:**
- Modify: `include/gui/MainWindow.h`
- Modify: `src/gui/MainWindow.cpp`

**Goal:** Remove the entire telemetry data bar (LINK/RATE/dropped packets) as it's not needed for log replay/analysis.

- [ ] **Step 1: Remove member variables from header**

Edit `include/gui/MainWindow.h`:

Remove these lines (around lines 131-137):
```cpp
QWidget *m_dataBar             = nullptr;
QLabel  *m_dataStripPageLabel  = nullptr;
QLabel  *m_dataLinkStatusLabel = nullptr;
QLabel  *m_dataRateLabel       = nullptr;
QLabel  *m_droppedBadgeLabel   = nullptr;
```

Also remove (around line 151):
```cpp
QTimer *m_dataRateTimer                = nullptr;
qint64  m_prevBytesForRate             = 0;
std::size_t m_lastDroppedCount = 0;
```

- [ ] **Step 2: Remove method declarations from header**

Edit `include/gui/MainWindow.h`:

Remove these lines (around lines 83-84):
```cpp
void setupDataBar();
[[nodiscard]] QString buildDataBarStyleSheet();
```

Also remove (around line 69):
```cpp
void updateDataRateLabel();
```

Also remove (around line 94):
```cpp
void syncTelemetryStrip();
```

- [ ] **Step 3: Remove setupDataBar() method implementation**

Edit `src/gui/MainWindow.cpp`:

Remove the entire `setupDataBar()` method (around lines 496-533):
```cpp
void MainWindow::setupDataBar() {
    // ... entire method body ...
}
```

- [ ] **Step 4: Remove buildDataBarStyleSheet() method implementation**

Edit `src/gui/MainWindow.cpp`:

Remove the entire `buildDataBarStyleSheet()` method (around lines 363-399):
```cpp
QString MainWindow::buildDataBarStyleSheet() {
    // ... entire method body ...
}
```

- [ ] **Step 5: Remove updateDataRateLabel() method implementation**

Edit `src/gui/MainWindow.cpp`:

Remove the entire `updateDataRateLabel()` method (around lines 798-822):
```cpp
void MainWindow::updateDataRateLabel() {
    // ... entire method body ...
}
```

- [ ] **Step 6: Remove syncTelemetryStrip() method implementation**

Edit `src/gui/MainWindow.cpp`:

Remove the entire `syncTelemetryStrip()` method (around lines 726-746):
```cpp
void MainWindow::syncTelemetryStrip() {
    // ... entire method body ...
}
```

- [ ] **Step 7: Remove data rate timer from constructor**

Edit `src/gui/MainWindow.cpp` in the constructor (around lines 120-123):

Remove these lines:
```cpp
m_dataRateTimer = new QTimer(this);
m_dataRateTimer->setInterval(1000);
connect(m_dataRateTimer, &QTimer::timeout, this, &MainWindow::updateDataRateLabel);
m_dataRateTimer->start();
```

- [ ] **Step 8: Remove data bar from setupPages()**

Edit `src/gui/MainWindow.cpp` in the `setupPages()` method (around lines 698-703):

Remove these lines:
```cpp
if (!m_dataBar) {
    setupDataBar();
}
if (m_dataBar) {
    centralLayout->addWidget(m_dataBar);
}
```

- [ ] **Step 9: Remove syncTelemetryStrip() call from setupPages()**

Edit `src/gui/MainWindow.cpp` in the `setupPages()` method (around line 715):

Remove this line:
```cpp
connect(m_flightModel.get(), &FlightDataModel::replayModeChanged, this, [this](bool) {
    syncTelemetryStrip();
});
```

Replace with:
```cpp
connect(m_flightModel.get(), &FlightDataModel::replayModeChanged, this, [this](bool) {
    // Replay mode changed - future: update UI state
});
```

- [ ] **Step 10: Remove syncTelemetryStrip() call from updateTopBarsForCurrentPage()**

Edit `src/gui/MainWindow.cpp` in the `updateTopBarsForCurrentPage()` method (around line 722):

Remove this line:
```cpp
syncTelemetryStrip();
```

- [ ] **Step 11: Remove data bar references from onThemeChanged()**

Edit `src/gui/MainWindow.cpp` in the `onThemeChanged()` method (around lines 987-989):

Remove these lines:
```cpp
if (m_dataBar) {
    m_dataBar->setStyleSheet(buildDataBarStyleSheet());
}
```

Also remove the comment (around line 1007):
```cpp
// Refresh telemetry strip to reapply semantic colors with new theme
syncTelemetryStrip();
```

- [ ] **Step 12: Remove updateDataRateLabel() call from updateTopBarsForCurrentPage()**

Edit `src/gui/MainWindow.cpp` in the `updateTopBarsForCurrentPage()` method (around line 723):

Remove this line:
```cpp
updateDataRateLabel();
```

- [ ] **Step 13: Remove m_prevBytesForRate initialization from updateTopBarsForCurrentPage()**

Edit `src/gui/MainWindow.cpp` in the `updateTopBarsForCurrentPage()` method (around lines 719-721):

Remove these lines:
```cpp
if (m_flightModel) {
    m_prevBytesForRate = m_flightModel->totalBytesReceived();
}
```

- [ ] **Step 14: Build to verify compilation**

```bash
cd /Users/hslyusar/Desktop/CosmoSoft/build
make -j$(sysctl -n hw.ncpu)
```

Expected: Clean build with no errors

- [ ] **Step 15: Test application launches**

```bash
./CosmoSoft
```

Expected: Application launches, no data bar visible below top toolbar

- [ ] **Step 16: Commit**

```bash
git add include/gui/MainWindow.h src/gui/MainWindow.cpp
git commit -m "Remove telemetry data bar completely

Removes the data bar (LINK/RATE/dropped packets) as it's not needed
for log replay and analysis. Removes all associated member variables,
methods, and call sites.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

### Task 3: Remove Serial Controls from Connection Bar

**Files:**
- Modify: `include/gui/MainWindow.h`
- Modify: `src/gui/MainWindow.cpp`

**Goal:** Remove serial port controls (port/baud dropdowns, connect/disconnect buttons) from the connection bar, keeping only the container for later transformation.

- [ ] **Step 1: Remove serial member variables from header**

Edit `include/gui/MainWindow.h`:

Remove these lines (around lines 127-130):
```cpp
QWidget  *m_serialControlBlock  = nullptr;
QComboBox *m_portCombo          = nullptr;
QComboBox *m_baudCombo          = nullptr;
```

Also remove (around line 166):
```cpp
QString m_serialPortSummary;
```

- [ ] **Step 2: Remove serial method declarations from header**

Edit `include/gui/MainWindow.h`:

Remove these lines (around lines 70-72):
```cpp
void refreshSerialPorts();
void startSerial(const QString &portName, int baud);
void stopSerial();
```

Also remove (around lines 87-89):
```cpp
void loadSerialPrefsToUi();
void persistSerialPrefs();
```

- [ ] **Step 3: Comment out refreshSerialPorts() method**

Edit `src/gui/MainWindow.cpp`:

Comment out the entire `refreshSerialPorts()` method (around lines 824-848):
```cpp
/*
void MainWindow::refreshSerialPorts() {
    // ... method body ...
}
*/
```

Add a comment above:
```cpp
// NOTE: Serial port functionality commented out for future live mode
```

- [ ] **Step 4: Comment out loadSerialPrefsToUi() method**

Edit `src/gui/MainWindow.cpp`:

Comment out the entire method (around lines 656-676):
```cpp
/*
void MainWindow::loadSerialPrefsToUi() {
    // ... method body ...
}
*/
```

- [ ] **Step 5: Comment out persistSerialPrefs() method**

Edit `src/gui/MainWindow.cpp`:

Comment out the entire method (around lines 678-685):
```cpp
/*
void MainWindow::persistSerialPrefs() {
    // ... method body ...
}
*/
```

- [ ] **Step 6: Comment out startSerial() method**

Edit `src/gui/MainWindow.cpp`:

Comment out the entire method (around lines 1039-1112):
```cpp
/*
void MainWindow::startSerial(const QString &portName, int baud) {
    // ... method body ...
}
*/
```

- [ ] **Step 7: Comment out stopSerial() method**

Edit `src/gui/MainWindow.cpp`:

Comment out the entire method (around lines 1114-1133):
```cpp
/*
void MainWindow::stopSerial() {
    // ... method body ...
}
*/
```

- [ ] **Step 8: Remove serial controls from setupConnectionBar()**

Edit `src/gui/MainWindow.cpp` in the `setupConnectionBar()` method.

Remove the serial control block creation (around lines 558-596):
```cpp
m_serialControlBlock = new QWidget(m_connectionBar);
m_serialControlBlock->setVisible(true);
auto *serialRow = new QHBoxLayout(m_serialControlBlock);
serialRow->setContentsMargins(0, 0, 0, 0);
serialRow->setSpacing(12);

auto *portLabel = new QLabel(u"Port"_s, m_serialControlBlock);
portLabel->setStyleSheet(
    QString(u"color: %1; font-family: %2;"_s)
        .arg(Theme::kTextMid())
        .arg(Theme::kFontMono));
m_portCombo = new QComboBox(m_serialControlBlock);
m_portCombo->setEditable(true);
m_portCombo->setMinimumWidth(200);
m_portCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

auto *baudLabel = new QLabel(u"Baud"_s, m_serialControlBlock);
baudLabel->setStyleSheet(portLabel->styleSheet());
m_baudCombo = new QComboBox(m_serialControlBlock);
m_baudCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
const QList<int> bauds = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
for (int b : bauds) {
    m_baudCombo->addItem(QString::number(b), b);
}
m_baudCombo->setCurrentIndex(4);

auto *refreshBtn = new QPushButton(u"Refresh"_s, m_serialControlBlock);
refreshBtn->setAccessibleName(u"Refresh serial ports"_s);
auto *connectBtn = new QPushButton(u"Connect"_s, m_serialControlBlock);
connectBtn->setAccessibleName(u"Connect to serial port"_s);
auto *disconnectBtn = new QPushButton(u"Disconnect"_s, m_serialControlBlock);
disconnectBtn->setAccessibleName(u"Disconnect serial port"_s);
serialRow->addWidget(portLabel);
serialRow->addWidget(m_portCombo);
serialRow->addWidget(baudLabel);
serialRow->addWidget(m_baudCombo);
serialRow->addWidget(refreshBtn);
serialRow->addWidget(connectBtn);
serialRow->addWidget(disconnectBtn);
```

Also remove the serial control block from layout (around line 606):
```cpp
row->addWidget(m_serialControlBlock);
```

Also remove the serial button connections (around lines 613-642):
```cpp
connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshSerialPorts);
connect(connectBtn, &QPushButton::clicked, this, [this]() {
    persistSerialPrefs();
    const QString port = m_portCombo ? m_portCombo->currentText().trimmed() : QString{};
    int baud = 115200;
    if (m_baudCombo) {
        baud = m_baudCombo->currentData().toInt();
        if (baud <= 0) {
            baud = m_baudCombo->currentText().toInt();
        }
        if (baud <= 0) {
            baud = 115200;
        }
    }
    startSerial(port, baud);
});
connect(disconnectBtn, &QPushButton::clicked, this, [this]() {
    if (m_comms && m_comms->isOpen() && !m_logManager->session().samples.empty()) {
        const auto reply = QMessageBox::question(
            this,
            u"Disconnect"_s,
            u"A telemetry session is active. Disconnect anyway?"_s,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }
    stopSerial();
});
```

- [ ] **Step 9: Remove refreshSerialPorts() call from constructor**

Edit `src/gui/MainWindow.cpp` in the constructor (around line 95):

Remove this line:
```cpp
refreshSerialPorts();
```

- [ ] **Step 10: Remove loadSerialPrefsToUi() call from setupConnectionBar()**

Edit `src/gui/MainWindow.cpp` in the `setupConnectionBar()` method (around line 653):

Remove this line:
```cpp
loadSerialPrefsToUi();
```

- [ ] **Step 11: Remove stopSerial() call from destructor**

Edit `src/gui/MainWindow.cpp` in the destructor (around line 136):

Comment out this line:
```cpp
// stopSerial();  // Commented out - serial functionality disabled
```

- [ ] **Step 12: Build to verify compilation**

```bash
cd /Users/hslyusar/Desktop/CosmoSoft/build
make -j$(sysctl -n hw.ncpu)
```

Expected: Clean build with no errors

- [ ] **Step 13: Test application launches**

```bash
./CosmoSoft
```

Expected: Application launches, connection bar visible but no serial controls

- [ ] **Step 14: Commit**

```bash
git add include/gui/MainWindow.h src/gui/MainWindow.cpp
git commit -m "Remove serial controls from connection bar

Comments out serial port functionality (port/baud selection,
connect/disconnect) for future live mode. Removes serial control
widgets from the connection bar UI.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

### Task 4: Add Breadcrumb Functionality

**Files:**
- Modify: `include/gui/MainWindow.h`
- Modify: `src/gui/MainWindow.cpp`

**Goal:** Repurpose the connection page label to show breadcrumbs (e.g., "Flight Monitoring › Session: filename.csv").

- [ ] **Step 1: Add updateBreadcrumb() method declaration to header**

Edit `include/gui/MainWindow.h`:

Add this line in the private section (around line 102):
```cpp
void updateBreadcrumb(const QString &context = QString());
```

- [ ] **Step 2: Implement updateBreadcrumb() method**

Edit `src/gui/MainWindow.cpp`:

Add this method after `setupConnectionBar()` (around line 654):
```cpp
void MainWindow::updateBreadcrumb(const QString &context) {
    if (!m_connectionPageLabel) {
        return;
    }
    
    if (context.isEmpty()) {
        m_connectionPageLabel->setText(u"Flight Monitoring"_s);
    } else {
        m_connectionPageLabel->setText(
            QStringLiteral("Flight Monitoring › %1").arg(context));
    }
}
```

- [ ] **Step 3: Update setupConnectionBar() to set initial breadcrumb**

Edit `src/gui/MainWindow.cpp` in the `setupConnectionBar()` method.

Replace the line setting the label text (around line 548):
```cpp
m_connectionPageLabel = new QLabel(u"Serial — port, baud, Connect. Flight logs — Open log…"_s, m_connectionBar);
```

With:
```cpp
m_connectionPageLabel = new QLabel(u"Flight Monitoring"_s, m_connectionBar);
```

- [ ] **Step 4: Update breadcrumb on successful file load**

Edit `src/gui/MainWindow.cpp` in the `onOpenReplayFile()` method.

Add breadcrumb update after successful load (around line 247):
```cpp
syncTelemetryStrip();
addRecentFile(path);
const QString loadMsg = QStringLiteral("Loaded flight: %1").arg(path);
showStatusMessage(loadMsg, 4000);
appendToLog(false, loadMsg);
```

Change to:
```cpp
addRecentFile(path);
const QString filename = QFileInfo(path).fileName();
updateBreadcrumb(QStringLiteral("Session: %1").arg(filename));
const QString loadMsg = QStringLiteral("Loaded flight: %1").arg(path);
showStatusMessage(loadMsg, 4000);
appendToLog(false, loadMsg);
```

Do the same in the recent files menu lambda (around line 249):
```cpp
syncTelemetryStrip();
showStatusMessage(QStringLiteral("Loaded flight: %1").arg(filePath), 4000);
appendToLog(false, QStringLiteral("Loaded flight: %1").arg(filePath));
```

Change to:
```cpp
const QString filename = QFileInfo(filePath).fileName();
updateBreadcrumb(QStringLiteral("Session: %1").arg(filename));
showStatusMessage(QStringLiteral("Loaded flight: %1").arg(filePath), 4000);
appendToLog(false, QStringLiteral("Loaded flight: %1").arg(filePath));
```

- [ ] **Step 5: Reset breadcrumb on clear flight**

Edit `src/gui/MainWindow.cpp` in the `onClearFlightData()` method (around line 966):

Add breadcrumb reset before the status message:
```cpp
syncTelemetryStrip();
showStatusMessage(u"Cleared flight replay data."_s, 2000);
appendToLog(false, u"Flight data cleared."_s);
```

Change to:
```cpp
updateBreadcrumb();  // Reset to default
showStatusMessage(u"Cleared flight replay data."_s, 2000);
appendToLog(false, u"Flight data cleared."_s);
```

- [ ] **Step 6: Build to verify compilation**

```bash
cd /Users/hslyusar/Desktop/CosmoSoft/build
make -j$(sysctl -n hw.ncpu)
```

Expected: Clean build with no errors

- [ ] **Step 7: Test breadcrumb updates**

```bash
./CosmoSoft
```

Manual test:
1. Launch app - breadcrumb shows "Flight Monitoring"
2. File > Open log > select a file - breadcrumb shows "Flight Monitoring › Session: filename.csv"
3. File > Clear flight - breadcrumb resets to "Flight Monitoring"

Expected: Breadcrumb updates correctly

- [ ] **Step 8: Commit**

```bash
git add include/gui/MainWindow.h src/gui/MainWindow.cpp
git commit -m "Add breadcrumb navigation to connection bar

Repurposes connection page label to show breadcrumb navigation
(e.g., 'Flight Monitoring › Session: filename.csv'). Updates on
file load and clears on flight data clear.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

### Task 5: Convert File Operation Buttons to Icon-Only

**Files:**
- Modify: `include/gui/MainWindow.h`
- Modify: `src/gui/MainWindow.cpp`

**Goal:** Convert "Open log", "Clear flight", "Export session" buttons to icon-only style with tooltips.

- [ ] **Step 1: Add action button member variables to header**

Edit `include/gui/MainWindow.h`:

Add these lines in the connection bar section (around line 130):
```cpp
QPushButton *m_openLogBtn     = nullptr;
QPushButton *m_clearFlightBtn = nullptr;
QPushButton *m_exportBtn      = nullptr;
```

- [ ] **Step 2: Create helper function for icon buttons**

Edit `src/gui/MainWindow.cpp` after `setupConnectionBar()` method (around line 670):

Add this helper function:
```cpp
namespace {
QPushButton* createActionButton(QWidget *parent, const QString &tooltip, const QString &iconName) {
    auto *btn = new QPushButton(parent);
    btn->setProperty("kind", "actionButton");
    btn->setToolTip(tooltip);
    btn->setAccessibleName(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::TabFocus);
    
    // Use Qt standard icons as placeholders
    QStyle::StandardPixmap iconType = QStyle::SP_FileIcon;
    if (iconName == u"folder-open"_s) {
        iconType = QStyle::SP_DirOpenIcon;
    } else if (iconName == u"trash"_s) {
        iconType = QStyle::SP_TrashIcon;
    } else if (iconName == u"export"_s) {
        iconType = QStyle::SP_DriveNetIcon;
    }
    
    QIcon icon = btn->style()->standardIcon(iconType);
    btn->setIcon(icon);
    btn->setIconSize(QSize(20, 20));
    
    return btn;
}
} // namespace
```

- [ ] **Step 3: Replace text buttons with icon buttons in setupConnectionBar()**

Edit `src/gui/MainWindow.cpp` in the `setupConnectionBar()` method.

Replace the button creation (around lines 598-603):
```cpp
auto *openLogBtn    = new QPushButton(u"Open log…"_s, m_connectionBar);
openLogBtn->setAccessibleName(u"Open flight log file"_s);
auto *clearFlightBtn = new QPushButton(u"Clear flight"_s, m_connectionBar);
clearFlightBtn->setAccessibleName(u"Clear all flight data"_s);
auto *exportBtn     = new QPushButton(u"Export session…"_s, m_connectionBar);
exportBtn->setAccessibleName(u"Export session to CSV"_s);
```

With:
```cpp
m_openLogBtn = createActionButton(m_connectionBar, u"Open flight log"_s, u"folder-open"_s);
m_clearFlightBtn = createActionButton(m_connectionBar, u"Clear flight data"_s, u"trash"_s);
m_exportBtn = createActionButton(m_connectionBar, u"Export session to CSV"_s, u"export"_s);
```

- [ ] **Step 4: Update layout spacing for button group**

Edit `src/gui/MainWindow.cpp` in the `setupConnectionBar()` method.

Replace the layout additions (around lines 605-610):
```cpp
row->addWidget(m_connectionPageLabel);
row->addWidget(m_serialControlBlock);
row->addSpacing(12);
row->addWidget(openLogBtn);
row->addWidget(clearFlightBtn);
row->addWidget(exportBtn);
row->addStretch(1);
```

With:
```cpp
row->addWidget(m_connectionPageLabel);
row->addSpacing(16);
row->addWidget(m_openLogBtn);
row->addWidget(m_clearFlightBtn);
row->addWidget(m_exportBtn);
row->addStretch(1);
```

- [ ] **Step 5: Update button connections to use member variables**

Edit `src/gui/MainWindow.cpp` in the `setupConnectionBar()` method.

Replace the connections (around lines 643-645):
```cpp
connect(openLogBtn,    &QPushButton::clicked, this, &MainWindow::onOpenReplayFile);
connect(clearFlightBtn, &QPushButton::clicked, this, &MainWindow::onClearFlightData);
connect(exportBtn,     &QPushButton::clicked, this, &MainWindow::onExportSession);
```

With:
```cpp
connect(m_openLogBtn, &QPushButton::clicked, this, &MainWindow::onOpenReplayFile);
connect(m_clearFlightBtn, &QPushButton::clicked, this, &MainWindow::onClearFlightData);
connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::onExportSession);
```

- [ ] **Step 6: Build to verify compilation**

```bash
cd /Users/hslyusar/Desktop/CosmoSoft/build
make -j$(sysctl -n hw.ncpu)
```

Expected: Clean build with no errors

- [ ] **Step 7: Test icon buttons**

```bash
./CosmoSoft
```

Manual test:
1. Hover over each button - tooltip appears
2. Click Open log button - file dialog opens
3. Click Clear flight button - confirmation dialog appears
4. Click Export button - save dialog opens

Expected: All buttons work correctly with icons

- [ ] **Step 8: Commit**

```bash
git add include/gui/MainWindow.h src/gui/MainWindow.cpp
git commit -m "Convert file operation buttons to icon-only style

Replaces text buttons (Open log, Clear flight, Export session) with
icon-only buttons using Qt standard icons as placeholders. Adds
tooltips and proper accessibility labels.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

### Task 6: Add Action Bar Styling

**Files:**
- Modify: `include/gui/MainWindow.h`
- Modify: `src/gui/MainWindow.cpp`

**Goal:** Add dedicated stylesheet for the page action bar with proper button styling and theme support.

- [ ] **Step 1: Add buildActionBarStyleSheet() method declaration to header**

Edit `include/gui/MainWindow.h`:

Add this line in the private section (around line 86):
```cpp
[[nodiscard]] QString buildActionBarStyleSheet();
```

- [ ] **Step 2: Implement buildActionBarStyleSheet() method**

Edit `src/gui/MainWindow.cpp`:

Add this method after `buildToolbarStyleSheet()` (around line 361):
```cpp
QString MainWindow::buildActionBarStyleSheet() {
    return QString(uR"(
        QWidget#connectionStrip {
            background: %1;
            color: %2;
            border-bottom: 1px solid %3;
            padding: 8px 16px;
        }
        QLabel#connectionStripContext {
            font-size: %4px;
            color: %5;
            font-family: %6;
            font-weight: 500;
        }
        QPushButton[kind="actionButton"] {
            min-width: 32px;
            min-height: 32px;
            max-width: 32px;
            max-height: 32px;
            border: none;
            border-radius: 4px;
            background-color: transparent;
            padding: 4px;
        }
        QPushButton[kind="actionButton"]:hover {
            background-color: %7;
        }
        QPushButton[kind="actionButton"]:pressed {
            background-color: %8;
        }
    )"_s)
        .arg(Theme::kBgBase())          // %1 - lighter than toolbar
        .arg(Theme::kTextPrimary())     // %2
        .arg(Theme::kBorderSubtle())    // %3
        .arg(Theme::kFontSizeBase)      // %4
        .arg(Theme::kTextMid())         // %5
        .arg(Theme::kFontMono)          // %6
        .arg(Theme::kBtnHover())        // %7
        .arg(Theme::kBgButton());       // %8
}
```

- [ ] **Step 3: Update setupConnectionBar() to use new stylesheet**

Edit `src/gui/MainWindow.cpp` in the `setupConnectionBar()` method.

Replace the stylesheet setting (around lines 647-651):
```cpp
m_connectionBar->setStyleSheet(
    QString(u"QWidget#connectionStrip { background: %1; color: %2; border-bottom: 1px solid %3; }"_s)
        .arg(Theme::kBgPanel())
        .arg(Theme::kTextPrimary())
        .arg(Theme::kBorderSubtle()));
```

With:
```cpp
m_connectionBar->setStyleSheet(buildActionBarStyleSheet());
```

- [ ] **Step 4: Update onThemeChanged() to refresh action bar stylesheet**

Edit `src/gui/MainWindow.cpp` in the `onThemeChanged()` method.

Replace the connection bar theme update (around lines 991-996):
```cpp
if (m_connectionBar) {
    m_connectionBar->setStyleSheet(
        QString(u"QWidget#connectionStrip { background: %1; color: %2; border-bottom: 1px solid %3; }"_s)
            .arg(Theme::kBgPanel())
            .arg(Theme::kTextPrimary())
            .arg(Theme::kBorderSubtle()));
}
```

With:
```cpp
if (m_connectionBar) {
    m_connectionBar->setStyleSheet(buildActionBarStyleSheet());
}
```

- [ ] **Step 5: Build to verify compilation**

```bash
cd /Users/hslyusar/Desktop/CosmoSoft/build
make -j$(sysctl -n hw.ncpu)
```

Expected: Clean build with no errors

- [ ] **Step 6: Test action bar styling**

```bash
./CosmoSoft
```

Manual test:
1. Check action bar has lighter background than top toolbar
2. Hover over icon buttons - background highlights
3. Click button - pressed state visible
4. Switch theme (if theme toggle exists) - styling updates

Expected: Action bar styled correctly, buttons have hover/pressed states

- [ ] **Step 7: Commit**

```bash
git add include/gui/MainWindow.h src/gui/MainWindow.cpp
git commit -m "Add dedicated styling for page action bar

Implements buildActionBarStyleSheet() with lighter background than
toolbar, proper button styling with hover/pressed states, and theme
support. Action bar visually separates from top toolbar.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

### Task 7: Fix Settings Icon Theme Adaptation

**Files:**
- Modify: `src/gui/MainWindow.cpp`

**Goal:** Make the settings icon adapt to theme changes (light icon in dark mode, dark icon in light mode).

- [ ] **Step 1: Update settings icon in onThemeChanged()**

Edit `src/gui/MainWindow.cpp` in the `onThemeChanged()` method.

Add this code at the beginning of the method (around line 976):
```cpp
void MainWindow::onThemeChanged() {
    // Update settings icon based on theme
    if (m_openSettingsAction) {
        QIcon settingsIcon;
        const bool isDark = (Theme::currentSkin() == Theme::Skin::Dark);
        const QString iconPath = isDark 
            ? u":/icons/settings_button.png"_s 
            : u":/icons/settings_button_black.png"_s;
        settingsIcon.addFile(iconPath, QSize(), QIcon::Normal);
        m_openSettingsAction->setIcon(settingsIcon);
    }
    
    // Update toolbar stylesheet
    auto *toolbar = findChild<QToolBar *>(u"missionToolbar"_s);
```

- [ ] **Step 2: Build to verify compilation**

```bash
cd /Users/hslyusar/Desktop/CosmoSoft/build
make -j$(sysctl -n hw.ncpu)
```

Expected: Clean build with no errors

- [ ] **Step 3: Test settings icon theme adaptation**

```bash
./CosmoSoft
```

Manual test (if theme switcher exists):
1. Start in dark mode - settings icon should be light colored
2. Switch to light mode - settings icon should become dark
3. Switch back to dark mode - settings icon should become light again

If no theme switcher, verify icon matches current theme.

Expected: Settings icon adapts to theme correctly

- [ ] **Step 4: Commit**

```bash
git add src/gui/MainWindow.cpp
git commit -m "Fix settings icon theme adaptation

Updates settings icon in onThemeChanged() to use light icon in dark
mode and dark icon in light mode. Improves visual consistency across
theme switches.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

### Task 8: Final Integration Testing

**Files:**
- No file changes

**Goal:** Comprehensive testing of all UI changes together.

- [ ] **Step 1: Build clean**

```bash
cd /Users/hslyusar/Desktop/CosmoSoft/build
make clean
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(sysctl -n hw.ncpu)
```

Expected: Clean build with no warnings

- [ ] **Step 2: Visual inspection test**

```bash
./CosmoSoft
```

Visual checks:
1. Top toolbar:
   - Shows brand (CosmoSoft + timestamp) on left
   - Shows settings icon on right
   - No "Flight data" label
   - Stretch space in middle for future page nav
2. No data bar visible below toolbar
3. Action bar (connection bar):
   - Shows "Flight Monitoring" breadcrumb on left
   - Shows three icon buttons (folder, trash, export)
   - Buttons have proper spacing
   - Lighter background than top toolbar

Expected: All visual elements correct

- [ ] **Step 3: Functional test - Open log**

Actions:
1. Click Open log icon button
2. Select a .csv or .telem file
3. Verify breadcrumb updates to "Flight Monitoring › Session: filename.csv"
4. Verify status bar shows success message

Expected: File loads correctly, breadcrumb updates

- [ ] **Step 4: Functional test - Clear flight**

Actions:
1. With a file loaded, click Clear flight icon button
2. Confirm in dialog
3. Verify breadcrumb resets to "Flight Monitoring"
4. Verify status bar shows cleared message

Expected: Flight data clears, breadcrumb resets

- [ ] **Step 5: Functional test - Export session**

Actions:
1. Load a file
2. Click Export session icon button
3. Choose save location
4. Verify file exports successfully

Expected: Export works correctly

- [ ] **Step 6: Theme switching test (if available)**

Actions:
1. If theme switcher exists, switch between light/dark
2. Verify settings icon changes color
3. Verify action bar styling updates
4. Verify button hover states work in both themes

Expected: Theme adaptation works correctly

- [ ] **Step 7: Keyboard navigation test**

Actions:
1. Press Tab to navigate through UI
2. Verify icon buttons are focusable
3. Verify tooltips appear on hover
4. Verify Enter key activates focused button

Expected: Full keyboard accessibility

- [ ] **Step 8: Window resize test**

Actions:
1. Resize window smaller
2. Resize window larger
3. Verify layout adapts correctly
4. Verify buttons remain visible

Expected: Responsive layout

- [ ] **Step 9: Recent files menu test**

Actions:
1. File > Open Recent > select recent file
2. Verify file loads
3. Verify breadcrumb updates correctly

Expected: Recent files work with breadcrumb updates

- [ ] **Step 10: Document test results**

Create a simple test report:
```bash
cat > /Users/hslyusar/Desktop/CosmoSoft/docs/superpowers/plans/2026-05-06-topbar-redesign-test-results.md << 'EOF'
# Top Bar Redesign - Test Results

**Date:** $(date +%Y-%m-%d)
**Build:** Debug

## Visual Tests
- [ ] Top toolbar layout correct
- [ ] No data bar visible
- [ ] Action bar styling correct
- [ ] Settings icon visible

## Functional Tests
- [ ] Open log works
- [ ] Clear flight works
- [ ] Export session works
- [ ] Breadcrumb updates correctly

## Theme Tests
- [ ] Settings icon adapts to theme
- [ ] Action bar styling updates on theme change

## Accessibility Tests
- [ ] Keyboard navigation works
- [ ] Tooltips appear
- [ ] Focus indicators visible

## Notes:
[Add any observations or issues found]

EOF
```

Fill in the checklist manually based on test results.

- [ ] **Step 11: Final commit**

```bash
git add docs/superpowers/plans/2026-05-06-topbar-redesign-test-results.md
git commit -m "Add integration test results for top bar redesign

Documents comprehensive testing of all UI changes including visual
inspection, functional testing, theme adaptation, and accessibility.

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"
```

---

## Self-Review Checklist

**Spec Coverage:**
- [x] Remove toolbar page label - Task 1
- [x] Remove data bar completely - Task 2
- [x] Remove serial controls - Task 3
- [x] Add breadcrumb functionality - Task 4
- [x] Convert buttons to icon-only - Task 5
- [x] Add action bar styling - Task 6
- [x] Fix settings icon theme - Task 7
- [x] Integration testing - Task 8

**Placeholder Scan:**
- [x] No TBD/TODO placeholders
- [x] All code blocks complete
- [x] All file paths exact
- [x] All commands with expected output

**Type Consistency:**
- [x] `m_connectionPageLabel` used consistently for breadcrumb
- [x] `m_openLogBtn`, `m_clearFlightBtn`, `m_exportBtn` used consistently
- [x] `buildActionBarStyleSheet()` signature matches usage
- [x] `updateBreadcrumb()` signature matches calls

**Implementation Completeness:**
- [x] All member variables added/removed correctly
- [x] All method declarations match implementations
- [x] All connections updated for new button pointers
- [x] All theme-related updates included
- [x] All cleanup of removed code complete

---

## Notes for Implementation

**Build System:**
- CMake-based Qt 6 project
- Use `make -j$(sysctl -n hw.ncpu)` for parallel builds on macOS
- Debug builds recommended for development

**Git Workflow:**
- Frequent commits after each task
- Descriptive commit messages with Co-Authored-By line
- Build verification before each commit

**Testing Strategy:**
- Build verification after each task
- Manual functional testing for UI changes
- Comprehensive integration testing at end
- Document test results

**Icon Resources:**
- Using Qt standard icons as placeholders
- Future: Replace with custom icons matching app style
- Icon paths: `:/icons/settings_button.png` and `:/icons/settings_button_black.png` exist

**Future Extensions:**
- Page navigation buttons will go in top toolbar center stretch
- Serial controls can be re-enabled for live mode
- Action bar can be customized per-page when multiple pages exist
