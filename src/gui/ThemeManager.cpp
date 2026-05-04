#include "gui/ThemeManager.h"
#include "gui/SkinLoader.h"
#include "gui/SettingsKeys.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

using namespace cosmo;

// ─── Singleton ────────────────────────────────────────────────────────────────

ThemeManager &ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

ThemeManager::ThemeManager()
    : QObject(nullptr)
{
}

// ─── Public API ───────────────────────────────────────────────────────────────

QString ThemeManager::skinsDirectory()
{
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/skins");
}

void ThemeManager::setActiveSkin(const CosmoTheme &theme)
{
    m_active = theme;

    // Apply generated QSS globally
    if (qApp) {
        qApp->setStyleSheet(generateQss(m_active.palette));
    }

    // Cache textures
    rebuildTextureCache();

    // Persist choice
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(QStringLiteral("ui/activeSkin"), m_active.id);

    emit themeChanged();
}

void ThemeManager::loadPersistedSkin(const QString &custom_skins_dir)
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    const auto skin_id = settings.value(QStringLiteral("ui/activeSkin"),
                                        QStringLiteral("builtin:dark")).toString();

    std::optional<CosmoTheme> theme;

    if (skin_id == QStringLiteral("builtin:dark")) {
        theme = SkinLoader::loadBuiltin(QStringLiteral(":/skins/dark"));
        if (theme) theme->id = QStringLiteral("builtin:dark");
    } else if (skin_id == QStringLiteral("builtin:light")) {
        theme = SkinLoader::loadBuiltin(QStringLiteral(":/skins/light"));
        if (theme) theme->id = QStringLiteral("builtin:light");
    } else if (skin_id.startsWith(QStringLiteral("custom:"))) {
        const auto name = skin_id.mid(7);
        const auto dir = custom_skins_dir + QStringLiteral("/") + name;
        theme = SkinLoader::loadFromDirectory(dir);
        if (theme) theme->id = skin_id;
    }

    if (!theme) {
        theme = SkinLoader::loadBuiltin(QStringLiteral(":/skins/dark"));
        if (theme) theme->id = QStringLiteral("builtin:dark");
    }

    if (theme) {
        m_active = *theme;
        if (qApp) {
            qApp->setStyleSheet(generateQss(m_active.palette));
        }
        rebuildTextureCache();
        emit themeChanged();
    }
}

QPixmap ThemeManager::texture(const QString &region) const
{
    return m_texture_cache.value(region, QPixmap());
}

// ─── Texture cache ────────────────────────────────────────────────────────────

void ThemeManager::rebuildTextureCache()
{
    m_texture_cache.clear();
    for (auto it = m_active.textures.paths.cbegin(); it != m_active.textures.paths.cend(); ++it) {
        QPixmap pix(it.value());
        if (!pix.isNull()) {
            m_texture_cache.insert(it.key(), pix);
        }
    }
}

// ─── QSS Generation ──────────────────────────────────────────────────────────

QString ThemeManager::generateQss(const ColorPalette &p)
{
    QString qss = QStringLiteral(R"(
/*
 * Auto-generated QSS from active CosmoTheme palette.
 */

QWidget {
    color: @TEXT_PRIMARY@;
    font-family: "Red Hat Mono","Courier New","Roboto Mono",monospace;
}

QPushButton {
    background-color: @BG_BUTTON@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_LIGHT@;
    border-radius: 4px;
    padding: 4px 12px;
    font-size: 11px;
}
QPushButton:hover {
    background-color: @BTN_HOVER@;
    border-color: @ACCENT_LINK@;
}
QPushButton:pressed {
    background-color: @BTN_PRESSED@;
}
QPushButton:disabled {
    color: @TEXT_MUTED@;
    background-color: @BG_PANEL@;
    border-color: @BORDER_SUBTLE@;
}

QComboBox {
    background-color: @BG_INPUT@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    border-radius: 4px;
    padding: 3px 8px;
    font-size: 11px;
}
QComboBox::drop-down {
    border: none;
    width: 18px;
}
QComboBox QAbstractItemView {
    background-color: @BG_PANEL@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    selection-background-color: @SELECT_BG@;
}

QLineEdit {
    background-color: @BG_INPUT@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    border-radius: 4px;
    padding: 3px 6px;
    font-size: 11px;
}

QScrollBar:vertical {
    background: transparent;
    width: 8px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: @BORDER_DEFAULT@;
    border-radius: 4px;
    min-height: 20px;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar:horizontal {
    background: transparent;
    height: 8px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: @BORDER_DEFAULT@;
    border-radius: 4px;
    min-width: 20px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}

QCheckBox {
    spacing: 6px;
    font-size: 12px;
}
QCheckBox::indicator {
    width: 14px;
    height: 14px;
    border-radius: 3px;
    border: 1px solid @BORDER_DEFAULT@;
    background-color: @BG_INPUT@;
}
QCheckBox::indicator:checked {
    background-color: @ACCENT_CHECKBOX@;
    border-color: @ACCENT_CHECKBOX_BORDER@;
}

QGroupBox {
    border: 1px solid @BORDER_DEFAULT@;
    border-radius: 8px;
    margin-top: 14px;
    padding-top: 8px;
    font-size: 12px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 6px;
    color: @TEXT_MID@;
}

QTabWidget::pane {
    border: 1px solid @BORDER_SUBTLE@;
    border-radius: 4px;
}
QTabBar::tab {
    background: transparent;
    color: @TEXT_MUTED@;
    padding: 6px 14px;
    border-bottom: 2px solid transparent;
    font-size: 11px;
}
QTabBar::tab:selected {
    color: @TEXT_PRIMARY@;
    border-bottom-color: @ACCENT_CHECKBOX@;
}
QTabBar::tab:hover:!selected {
    color: @TEXT_MID@;
}

QPushButton:focus, QComboBox:focus, QLineEdit:focus, QCheckBox:focus {
    outline: none;
    border-color: @ACCENT_LINK@;
}

QToolTip {
    background-color: @BG_PANEL@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    padding: 4px;
    font-size: 11px;
}
)");

    qss.replace(QStringLiteral("@TEXT_PRIMARY@"), p.text_primary);
    qss.replace(QStringLiteral("@TEXT_MID@"), p.text_mid);
    qss.replace(QStringLiteral("@TEXT_MUTED@"), p.text_muted);
    qss.replace(QStringLiteral("@BG_BUTTON@"), p.bg_button);
    qss.replace(QStringLiteral("@BG_PANEL@"), p.bg_panel);
    qss.replace(QStringLiteral("@BG_INPUT@"), p.bg_input);
    qss.replace(QStringLiteral("@BORDER_LIGHT@"), p.border_light);
    qss.replace(QStringLiteral("@BORDER_DEFAULT@"), p.border_default);
    qss.replace(QStringLiteral("@BORDER_SUBTLE@"), p.border_subtle);
    qss.replace(QStringLiteral("@BTN_HOVER@"), p.btn_hover);
    qss.replace(QStringLiteral("@BTN_PRESSED@"), p.btn_pressed);
    qss.replace(QStringLiteral("@ACCENT_LINK@"), p.accent_link);
    qss.replace(QStringLiteral("@ACCENT_CHECKBOX@"), p.accent_checkbox);
    qss.replace(QStringLiteral("@ACCENT_CHECKBOX_BORDER@"), p.accent_checkbox_border);
    qss.replace(QStringLiteral("@SELECT_BG@"), p.select_bg);

    return qss;
}
