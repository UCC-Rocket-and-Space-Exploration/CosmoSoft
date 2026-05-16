# AGENTS.md — CosmoSoft

## Who This Is For

- **AI agents** — automate repository tasks with minimal context
- **Contributors** — humans using AI assistants or working directly
- **Maintainers** — ensure assistants follow project conventions and CI rules

---

## Agent Behaviour Policy

AI agents should:

- Make atomic, minimal, and reversible changes
- Verify the build produces zero warnings before proposing commits
- Prefer local analysis (`cmake --build build`, `clang-format`, `cppcheck`) before proposing changes
- Use this file and the `.cursor/rules/` directory as the source of truth for conventions

Agents must **NOT**:

- Modify `CMakeLists.txt`, CI configuration, or `assets/resources.qrc` unless explicitly requested
- Bypass `clang-format` or `cppcheck`
- Introduce new third-party libraries without updating CMake and documenting the reason in a comment
- Generate or commit files from the `build/` directory
- Add platform-specific code (`#ifdef _WIN32`, `#ifdef __APPLE__`) without a matching guard on the other platform and a note in `README.md`
- Use non-deterministic code (unseeded random, `time()`-based logic) in tests

---

## Repository Map

```
include/               # Public headers (domain models, GUI, services)
├── domain/            # FlightSample, FlightSession
├── gateway/comms/     # IComms, ISerialPortScanner, platform headers
├── gui/               # MainWindow, pages, widgets, FlightDataModel
└── services/          # BlockingQueue, RingBuffer, telemetry pipeline, import, persistence

src/                   # Implementations
├── gateway/comms/     # CommsFactory, SerialWorker, Posix/, Windows/
├── gui/               # MainWindow, pages/, widgets/
└── services/
    ├── flight/        # Placeholder (FlightModuleStub)
    ├── import/        # SampleFileLoader (CSV + .telem)
    ├── persistence/   # FlightLogManager, RingBuffer
    └── telemetry/     # Framer, Parser, ParserWorker

assets/                # Qt resources (.qrc, fonts, icons, images)
sample_data/           # Example CSV and .telem files for replay
scripts/               # Build helpers (fresh-cmake.sh)
docs/                  # Architecture docs, protocol spec (docs/protocol.md once agreed)
tests/                 # Catch2 unit tests (to be created)
```

---

## Build & Verify Commands

```bash
# Configure and build
cmake -S . -B build
cmake --build build

# Clean rebuild (uses helper script)
bash scripts/fresh-cmake.sh

# Format check (requires clang-format)
clang-format --dry-run --Werror $(find src include -name "*.cpp" -o -name "*.h")

# Static analysis (requires cppcheck)
cppcheck --enable=all --suppress=missingInclude src/

# Run tests (once Catch2 suite exists)
ctest --test-dir build --output-on-failure
```

---

## Core Development Principles

### 1. Stable Public Interfaces

Do **not** change the following public APIs without opening a GitHub Issue and getting maintainer consensus:

- `IComms` — serial communication interface
- `FlightSample` — core domain type passed through the entire pipeline
- `FlightDataModel` — Qt model consumed by all UI pages

Before changing any exported class:

- Check all usages in `src/` and `include/`
- Use new parameters only as additions; never reorder or remove existing parameters

### 2. Code Quality

- C++20; no raw `new`/`delete`; use `std::unique_ptr` / `std::shared_ptr`
- No bare `catch (...)` — catch specific exception types or `std::exception`
- Proper resource cleanup: serial file descriptors, Qt objects, thread handles
- No `system()`, `popen()`, or shell invocations from application code

```cpp
// ❌ BAD
IComms* c = new SerialCommsPosix(dev, baud);

// ✅ GOOD
auto c = std::make_unique<SerialCommsPosix>(dev, baud);
```

### 3. Testing Requirements

Every new service function must be covered by a Catch2 unit test in `tests/`.

Test structure:

```cpp
TEST_CASE("SampleFileLoader: valid Theseus CSV", "[import]") {
    FlightSession session;
    auto err = SampleFileLoader::loadTheseusCsv("sample_data/theseus_flight_data.csv", session);
    REQUIRE_FALSE(err.has_value());
    REQUIRE(session.samples.size() > 0);
}
```

No network calls or hardware access in unit tests. Hardware-in-the-loop tests go in a separate `tests/box/` directory.

### 4. Security Checklist

Before committing, verify:

- No `system()`, `exec()`, or shell-constructed commands from user input
- No hard-coded secrets, credentials, or absolute device paths
- Proper exception handling — no bare `catch (...)`
- All file handles and socket descriptors are closed in destructors or RAII wrappers
- No deprecated crypto (MD5, SHA-1) in any new serialisation or protocol code
- Serial input is length-checked before processing — never trust `size` fields from untrusted bytes

### 5. Documentation Standards

All public functions and classes must have a Doxygen comment:

```cpp
// ❌ Insufficient
bool open();

// ✅ Complete
/**
 * @brief Open the serial port with the configured device path and baud rate.
 * @return true if the port was opened successfully, false otherwise.
 */
bool open();
```

### 6. UI Theme Implementation Requirements

**CRITICAL:** All UI work MUST be theme-aware and support both light and dark themes.

#### Theme System Architecture

CosmoSoft uses a centralized theme management system:

- **ThemeManager** singleton manages the active theme and emits `themeChanged()` signal
- **Theme::k\*()** accessors provide runtime access to current theme colors
- **ColorPalette** struct in `CosmoTheme.h` defines 25+ semantic color tokens
- **Built-in themes** stored as JSON in `assets/skins/dark/` and `assets/skins/light/`
- **Custom skins** can be imported as `.cosmo` ZIP archives

When creating or modifying GUI widgets and pages:

```cpp
// ❌ BAD - Hardcoded colors that won't adapt to theme changes
setStyleSheet("QLabel { color: #ffffff; background: #1f1f1f; }");

// ✅ GOOD - Uses theme tokens that adapt automatically
setStyleSheet(QString("QLabel { color: %1; background: %2; }")
    .arg(Theme::kTextPrimary())
    .arg(Theme::kBgPanel()));
```

#### Required Theme Integration Pattern

**Every widget that builds QSS stylesheets using Theme::k\*() accessors MUST:**

1. **Extract stylesheet building into a dedicated method**
   ```cpp
   // In MyWidget.h
   private:
       void refreshStyleSheet();
   
   // In MyWidget.cpp
   void MyWidget::refreshStyleSheet() {
       setStyleSheet(QString("color: %1; background: %2;")
           .arg(Theme::kTextPrimary())
           .arg(Theme::kBgPanel()));
   }
   ```

2. **Call the refresh method in the constructor**
   ```cpp
   MyWidget::MyWidget(QWidget *parent) : QWidget(parent) {
       // Widget setup...
       refreshStyleSheet();
   }
   ```

3. **Connect to ThemeManager::themeChanged signal**
   ```cpp
   connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
           this, &MyWidget::refreshStyleSheet);
   ```

4. **Include ThemeManager.h when connecting to the signal**
   ```cpp
   #include "gui/ThemeManager.h"
   ```

**Exception:** Transient dialogs created on-demand (like `AboutDialog`) don't need signal connections since they get fresh theme colors each time they're instantiated.

#### Available Theme Tokens

**Text colors:**
- `Theme::kTextPrimary()` — Primary text (headings, body)
- `Theme::kTextMid()` — Secondary text
- `Theme::kTextMuted()` — Tertiary/disabled text
- `Theme::kTextDim()` — Very subtle text

**Backgrounds:**
- `Theme::kBgBase()` — Base background
- `Theme::kBgDark()` — Darker panels
- `Theme::kBgPanel()` — Panel backgrounds
- `Theme::kBgInput()` — Input field backgrounds
- `Theme::kBgButton()` — Button backgrounds

**Borders:**
- `Theme::kBorderPanel()` — Panel borders
- `Theme::kBorderDefault()` — Standard borders
- `Theme::kBorderLight()` — Light borders
- `Theme::kBorderSubtle()` — Very subtle borders

**Interactive states:**
- `Theme::kBtnHover()` — Button hover state
- `Theme::kBtnPressed()` — Button pressed state
- `Theme::kFocusRing()` — Focus indicator color
- `Theme::kAccentLink()` — Accent/active/checked state
- `Theme::kAccentCheckbox()` — Checkbox accent

**Semantic colors:**
- `Theme::kSuccess()` — Success states (green)
- `Theme::kWarning()` — Warning states (orange)
- `Theme::kDanger()` — Error/danger states (red)
- `Theme::kInfo()` — Info states (blue)

**Design constants:**
- `Theme::kFontMono` — Monospace font family
- `Theme::kFontSizeBase` — Base font size (12px)
- `Theme::kFontSizeSm` — Small font size (11px)
- `Theme::kRadiusSm` — Small border radius (4px)
- `Theme::kRadiusMd` — Medium border radius (6px)

#### Custom Painting with Theme Colors

When using `QPainter` with dynamic colors:

```cpp
// ❌ BAD - Hardcoded RGB
painter.setPen(QPen(QColor(255, 255, 255, 100), 1));

// ✅ GOOD - Theme-aware with alpha
QColor crosshairColor(Theme::kTextPrimary());
crosshairColor.setAlpha(100);
painter.setPen(QPen(crosshairColor, 1));
```

For custom paint events, trigger a repaint when theme changes:

```cpp
connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
        this, [this]() { update(); });  // Forces paintEvent() call
```

#### Stateful Widgets (Checkable Buttons)

For widgets with state-dependent styles (checkable buttons, toggles), force style recomputation:

```cpp
void MyWidget::refreshStyleSheet() {
    setStyleSheet(buildStyleSheet());
    
    // Force Qt to recompute styles for stateful widgets
    m_toggleButton->style()->unpolish(m_toggleButton);
    m_toggleButton->style()->polish(m_toggleButton);
    m_toggleButton->update();
}
```

#### Theme Files Structure

**Built-in theme locations:**
- `assets/skins/dark/theme.json` — Dark theme palette
- `assets/skins/light/theme.json` — Light theme palette
- `assets/resources.qrc` — Qt resource manifest (skins compiled into binary)

**Theme JSON format:**
```json
{
  "name": "Dark",
  "author": "CosmoSoft",
  "version": "1.0",
  "palette": {
    "bg_base": "#0f0f0f",
    "text_primary": "#e8e8e8",
    "accent_link": "#4a9eff",
    ...
  }
}
```

**Custom skin import:**
- Users can import `.cosmo` files (ZIP archives with `theme.json` + optional textures/preview)
- Custom skins stored in `QStandardPaths::AppDataLocation/skins/<name>/`
- Discovered on app startup and added to Settings dropdown

#### Theme System Files

**Core headers:**
- `include/gui/Theme.h` — Runtime accessor functions (`Theme::kTextPrimary()`, etc.)
- `include/gui/ThemeManager.h` — Singleton manager, `themeChanged()` signal
- `include/gui/CosmoTheme.h` — Data structures (`ColorPalette`, `TextureSet`, `CosmoTheme`)
- `include/gui/SkinLoader.h` — JSON parsing and `.cosmo` archive import
- `include/gui/ThemePainter.h` — Texture painting utilities

**Implementations:**
- `src/gui/ThemeManager.cpp` — Global QSS generation (~310 lines), persistence
- `src/gui/SkinLoader.cpp` — JSON parsing, ZIP extraction, discovery
- `src/gui/ThemePainter.cpp` — Texture rendering (tile/stretch/cover modes)

**UI integration:**
- `src/gui/pages/SettingsPage.cpp` — Theme selector, import button

#### Testing Requirements

**Before committing UI changes, verify:**

1. **Visual testing in both themes:**
   - Launch app in Dark theme
   - Navigate to your UI component
   - Open Settings → Appearance → Switch to Light theme
   - Return to your component — verify all colors updated immediately
   - Switch back to Dark — verify again

2. **Contrast checks:**
   - All text must be readable (sufficient contrast)
   - All buttons must be visible and distinguishable
   - No dark-on-dark or light-on-light rendering
   - Icons should adapt (swap images if needed, like `settings_button.png` vs `settings_button_black.png`)

3. **Edge cases:**
   - Theme change while your widget is visible (updates in real-time)
   - Theme change while interactive state is active (hover, pressed, checked)
   - Multiple rapid theme switches (no crashes or visual glitches)

4. **Static analysis:**
   ```bash
   # Check for hardcoded hex colors in GUI code
   grep -rn "#[0-9a-fA-F]\{6\}" src/gui/ include/gui/
   ```

#### Verification Checklist

Before opening a PR with UI changes:

- [ ] No hardcoded hex colors in .cpp or .h files
- [ ] All stylesheets use `Theme::k*()` accessors
- [ ] Widget connects to `themeChanged()` signal (unless transient dialog)
- [ ] Refresh method extracts stylesheet building logic
- [ ] Tested in Dark theme — all colors correct
- [ ] Tested in Light theme — all colors correct
- [ ] Theme switching works without app restart
- [ ] All text has sufficient contrast
- [ ] Buttons and interactive elements are visually distinct
- [ ] Custom paint events use dynamic colors via `Theme::k*()`
- [ ] Stateful widgets repolish if needed

#### Common Mistakes to Avoid

**❌ Forgetting to connect to themeChanged:**
```cpp
// Widget works on startup but doesn't update when theme changes
MyWidget::MyWidget() {
    setStyleSheet(QString("color: %1;").arg(Theme::kTextPrimary()));
    // MISSING: connect to themeChanged signal!
}
```

**✅ Correct pattern:**
```cpp
MyWidget::MyWidget() {
    refreshStyleSheet();
    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &MyWidget::refreshStyleSheet);
}

void MyWidget::refreshStyleSheet() {
    setStyleSheet(QString("color: %1;").arg(Theme::kTextPrimary()));
}
```

**❌ Mixing hardcoded and dynamic colors:**
```cpp
// Inconsistent — some colors adapt, some don't
setStyleSheet(QString("color: %1; border: 1px solid #cccccc;")
    .arg(Theme::kTextPrimary()));
```

**✅ All colors dynamic:**
```cpp
setStyleSheet(QString("color: %1; border: 1px solid %2;")
    .arg(Theme::kTextPrimary())
    .arg(Theme::kBorderLight()));
```

#### Examples of Correct Implementation

**Simple widget:**
```cpp
// ReplayBar.h
class ReplayBar : public QWidget {
private:
    void applyThemeStyleSheet();
};

// ReplayBar.cpp
ReplayBar::ReplayBar(QWidget *parent) : QWidget(parent) {
    // Setup widgets...
    applyThemeStyleSheet();
    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &ReplayBar::applyThemeStyleSheet);
}

void ReplayBar::applyThemeStyleSheet() {
    setStyleSheet(QString(R"(
        QPushButton {
            background-color: %1;
            color: %2;
        }
        QPushButton:hover { background-color: %3; }
    )").arg(Theme::kBgButton())
       .arg(Theme::kTextPrimary())
       .arg(Theme::kBtnHover()));
}
```

**Widget with custom painting:**
```cpp
// TelemetryChartView.cpp
TelemetryChartView::TelemetryChartView(QChart *chart, QWidget *parent)
    : QChartView(chart, parent) {
    
    m_hoverOverlay = new QLabel(viewport());
    refreshHoverOverlayStyleSheet();
    
    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &TelemetryChartView::refreshHoverOverlayStyleSheet);
}

void TelemetryChartView::refreshHoverOverlayStyleSheet() {
    if (!m_hoverOverlay) return;
    
    m_hoverOverlay->setStyleSheet(
        QString("background-color: %1; color: %2; border: 1px solid %3;")
            .arg(Theme::kBgDark())
            .arg(Theme::kTextPrimary())
            .arg(Theme::kBorderPanel()));
    
    // Force custom paint overlays to repaint with new colors
    if (m_crosshairOverlay) {
        m_crosshairOverlay->update();
    }
}
```

---

## AI-Generated Code Policy

### Disclosure

Disclose AI assistance via a git commit trailer:

```
feat(telemetry): implement Framer sync-byte detection

Assisted-by: Cursor
```

Trivial AI use (variable-name autocomplete, docstring generation) does not require disclosure.

### Accountability

- Pure agent PRs are not permitted — a human contributor must understand and be able to defend every changed line
- AI-generated code is held to the same review standard as hand-written code
- The submitting human is responsible for running the relevant tests and verifying correctness

### License Compliance

AI tools may inadvertently reproduce copyrighted or licence-restricted code.

- Do not submit AI output that you cannot trace back to an Apache 2.0 / MIT / GPL-compatible source
- If in doubt, rewrite the generated snippet rather than submitting it verbatim
- Red Hat's guidance: marking AI assistance with `Assisted-by:` preserves legal clarity for downstream consumers

---

## Coordination Before Coding

Before opening any PR:

1. Check whether a GitHub Issue already covers the work; coordinate on it first
2. Check for overlapping open PRs:
  ```bash
   gh pr list --repo <org>/cosmo-soft --state open --search "<keywords>"
  ```
3. Do not work on another contributor's issue without explicit approval in the issue thread
4. Do not open one-off PRs for trivial edits (single typo, isolated lint cleanup)

**Fail-closed rule:** if coordination evidence cannot be found, do not open a PR — explain what is missing instead.

---

## Component Ownership


| Component          | Path                        | Notes                                              |
| ------------------ | --------------------------- | -------------------------------------------------- |
| Telemetry pipeline | `src/services/telemetry/`   | Protocol-sensitive; changes need Issue discussion  |
| Serial gateway     | `src/gateway/comms/`        | Platform-specific; test on macOS + Linux           |
| Qt GUI             | `src/gui/`                  | No business logic; delegate to services            |
| Domain types       | `include/domain/`           | Stable API; changes propagate everywhere           |
| Import / replay    | `src/services/import/`      | Only CSV is fully implemented for v1               |
| Persistence        | `src/services/persistence/` | `FlightLogManager` not yet wired into `MainWindow` |


