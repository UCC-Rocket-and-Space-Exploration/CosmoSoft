#include "gui/ThemeManager.h"
#include "gui/SettingsKeys.h"
#include "gui/SkinLoader.h"
#include "gui/Theme.h"

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
    settings.setValue(kSettingsActiveSkin, m_active.id);

    emit themeChanged();
}

void ThemeManager::loadPersistedSkin(const QString &custom_skins_dir)
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    const auto skin_id = settings.value(kSettingsActiveSkin,
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
    font-family: @FONT_MONO@;
    line-height: 1.4;
}

QPushButton {
    border: 1px solid @BORDER_LIGHT@;
    border-radius: 4px;
    padding: 6px 16px;
    min-height: 32px;
    min-width: 0px;
    background-color: @BG_BUTTON@;
    color: @TEXT_PRIMARY@;
    font-size: 12px;
    line-height: 1.4;
    letter-spacing: 0;
}
QPushButton:hover {
    background-color: @BTN_HOVER@;
}
QPushButton:pressed {
    background-color: @BTN_PRESSED@;
}
QPushButton:disabled {
    color: @TEXT_MUTED@;
    border-color: @BORDER_SUBTLE@;
    background-color: @BG_PANEL@;
}

QComboBox {
    background-color: @BG_INPUT@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 12px;
    line-height: 1.4;
}
QComboBox::drop-down {
    border: none;
    width: 24px;
}
QComboBox QAbstractItemView {
    background-color: @BG_PANEL@;
    color: @TEXT_PRIMARY@;
    selection-background-color: @SELECT_BG@;
    border: 1px solid @BORDER_DEFAULT@;
}

QLineEdit {
    background-color: @BG_INPUT@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 12px;
    line-height: 1.4;
}

QScrollBar:vertical {
    background: @BG_DARK@;
    width: 10px;
    margin: 0;
    border: none;
}
QScrollBar::handle:vertical {
    background: @BORDER_DEFAULT@;
    min-height: 24px;
    border-radius: 4px;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar:horizontal {
    background: @BG_DARK@;
    height: 10px;
    margin: 0;
    border: none;
}
QScrollBar::handle:horizontal {
    background: @BORDER_DEFAULT@;
    min-width: 24px;
    border-radius: 4px;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}

QCheckBox {
    color: @TEXT_PRIMARY@;
    spacing: 8px;
    font-size: 12px;
    line-height: 1.4;
}
QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid @BORDER_LIGHT@;
    border-radius: 3px;
    background-color: @BG_INPUT@;
}
QCheckBox::indicator:checked {
    background-color: @ACCENT_CHECKBOX@;
    border-color: @ACCENT_CHECKBOX_BORDER@;
}

QGroupBox {
    font-weight: 600;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    border-radius: 8px;
    margin-top: 12px;
    padding-top: 12px;
    background-color: @BG_PANEL@;
    font-size: 12px;
    line-height: 1.4;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 12px;
    padding: 0 6px;
    color: @TEXT_MID@;
}

QTabWidget::pane {
    border: 1px solid @BORDER_SUBTLE@;
    background-color: @BG_BASE@;
    border-radius: 0 4px 4px 4px;
}
QTabBar::tab {
    background-color: @BG_DARK@;
    color: @TEXT_MUTED@;
    border: 1px solid @BORDER_SUBTLE@;
    border-bottom: none;
    padding: 8px 16px;
    margin-right: 2px;
    border-radius: 4px 4px 0 0;
    font-size: 12px;
    letter-spacing: 0.02em;
    min-width: 100px;
    line-height: 1.4;
}
QTabBar::tab:selected {
    background-color: @BG_BASE@;
    color: @TEXT_PRIMARY@;
    border-color: @BORDER_LIGHT@;
    border-bottom-color: @BG_BASE@;
}
QTabBar::tab:hover:!selected {
    background-color: @BG_PANEL@;
    color: @TEXT_MID@;
}

QPushButton:focus, QToolButton:focus, QComboBox:focus, QLineEdit:focus,
QCheckBox:focus, QSlider:focus {
    outline: 2px solid @FOCUS_RING@;
    outline-offset: 2px;
    border-color: @FOCUS_RING@;
}

/* High-contrast focus for keyboard navigation */
QPushButton:focus-visible, QToolButton:focus-visible, QComboBox:focus-visible,
QLineEdit:focus-visible, QCheckBox:focus-visible, QSlider:focus-visible {
    outline: 2px solid @FOCUS_RING@;
    outline-offset: 2px;
}

QTabBar::tab:focus {
    outline: 2px solid @FOCUS_RING@;
    outline-offset: -2px;
}

QToolTip {
    background-color: @BG_PANEL@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    padding: 4px 8px;
    border-radius: 4px;
    font-size: 11px;
    line-height: 1.6;
}

QProgressBar {
    background-color: @BG_INPUT@;
    border: 1px solid @BORDER_DEFAULT@;
    border-radius: 4px;
    height: 8px;
    text-align: center;
}
QProgressBar::chunk {
    background-color: @ACCENT_LINK@;
    border-radius: 3px;
}
)");

    qss.replace(QStringLiteral("@FONT_MONO@"), QString::fromUtf8(Theme::kFontMono));
    qss.replace(QStringLiteral("@ACCENT_CHECKBOX_BORDER@"), p.accent_checkbox_border);
    qss.replace(QStringLiteral("@ACCENT_CHECKBOX@"), p.accent_checkbox);
    qss.replace(QStringLiteral("@ACCENT_LINK@"), p.accent_link);
    qss.replace(QStringLiteral("@TEXT_PRIMARY@"), p.text_primary);
    qss.replace(QStringLiteral("@TEXT_MUTED@"), p.text_muted);
    qss.replace(QStringLiteral("@TEXT_MID@"), p.text_mid);
    qss.replace(QStringLiteral("@BG_BASE@"), p.bg_base);
    qss.replace(QStringLiteral("@BG_DARK@"), p.bg_dark);
    qss.replace(QStringLiteral("@BG_BUTTON@"), p.bg_button);
    qss.replace(QStringLiteral("@BG_PANEL@"), p.bg_panel);
    qss.replace(QStringLiteral("@BG_INPUT@"), p.bg_input);
    qss.replace(QStringLiteral("@BORDER_DEFAULT@"), p.border_default);
    qss.replace(QStringLiteral("@BORDER_SUBTLE@"), p.border_subtle);
    qss.replace(QStringLiteral("@BORDER_LIGHT@"), p.border_light);
    qss.replace(QStringLiteral("@BTN_PRESSED@"), p.btn_pressed);
    qss.replace(QStringLiteral("@BTN_HOVER@"), p.btn_hover);
    qss.replace(QStringLiteral("@SELECT_BG@"), p.select_bg);
    qss.replace(QStringLiteral("@SUCCESS@"), p.success);
    qss.replace(QStringLiteral("@SUCCESS_BG@"), p.success_bg);
    qss.replace(QStringLiteral("@WARNING@"), p.warning);
    qss.replace(QStringLiteral("@WARNING_BG@"), p.warning_bg);
    qss.replace(QStringLiteral("@INFO@"), p.info);
    qss.replace(QStringLiteral("@INFO_BG@"), p.info_bg);
    qss.replace(QStringLiteral("@FOCUS_RING@"), p.focus_ring);
    qss.replace(QStringLiteral("@FOCUS_RING_OFFSET@"), p.focus_ring_offset);

    return qss;
}
