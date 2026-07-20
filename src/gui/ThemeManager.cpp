#include "gui/ThemeManager.h"
#include "gui/SettingsKeys.h"
#include "gui/SkinLoader.h"
#include "gui/Theme.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>

#include <array>

using namespace cosmo;

namespace {

QPalette applicationPalette(const ColorPalette &palette)
{
    QPalette result;
    const std::array groups{QPalette::Active, QPalette::Inactive};
    for (const QPalette::ColorGroup group : groups) {
        result.setColor(group, QPalette::WindowText, QColor(palette.text_primary));
        result.setColor(group, QPalette::Button, QColor(palette.bg_button));
        result.setColor(group, QPalette::Light, QColor(palette.border_light));
        result.setColor(group, QPalette::Midlight, QColor(palette.border_default));
        result.setColor(group, QPalette::Dark, QColor(palette.bg_dark));
        result.setColor(group, QPalette::Mid, QColor(palette.border_default));
        result.setColor(group, QPalette::Text, QColor(palette.text_primary));
        result.setColor(group, QPalette::BrightText, QColor(palette.danger));
        result.setColor(group, QPalette::ButtonText, QColor(palette.text_primary));
        result.setColor(group, QPalette::Base, QColor(palette.bg_input));
        result.setColor(group, QPalette::Window, QColor(palette.bg_base));
        result.setColor(group, QPalette::Shadow, QColor(palette.bg_dark));
        result.setColor(group, QPalette::Highlight, QColor(palette.select_bg));
        result.setColor(group, QPalette::HighlightedText, QColor(palette.text_primary));
        result.setColor(group, QPalette::Link, QColor(palette.accent_link));
        result.setColor(group, QPalette::LinkVisited, QColor(palette.accent_link));
        result.setColor(group, QPalette::AlternateBase, QColor(palette.bg_panel));
        result.setColor(group, QPalette::ToolTipBase, QColor(palette.bg_panel));
        result.setColor(group, QPalette::ToolTipText, QColor(palette.text_primary));
        result.setColor(group, QPalette::PlaceholderText, QColor(palette.text_dim));
    }

    result.setColor(QPalette::Disabled, QPalette::WindowText, QColor(palette.text_muted));
    result.setColor(QPalette::Disabled, QPalette::Button, QColor(palette.bg_panel));
    result.setColor(QPalette::Disabled, QPalette::Text, QColor(palette.text_muted));
    result.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(palette.text_muted));
    result.setColor(QPalette::Disabled, QPalette::Base, QColor(palette.bg_input));
    result.setColor(QPalette::Disabled, QPalette::Window, QColor(palette.bg_base));
    result.setColor(QPalette::Disabled, QPalette::Highlight, QColor(palette.bg_panel));
    result.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(palette.text_muted));
    result.setColor(QPalette::Disabled, QPalette::Link, QColor(palette.text_muted));
    result.setColor(QPalette::Disabled, QPalette::LinkVisited, QColor(palette.text_muted));
    result.setColor(QPalette::Disabled, QPalette::ToolTipBase, QColor(palette.bg_panel));
    result.setColor(QPalette::Disabled, QPalette::ToolTipText, QColor(palette.text_muted));
    result.setColor(QPalette::Disabled, QPalette::PlaceholderText, QColor(palette.text_dim));
    return result;
}

void applyApplicationTheme(const ColorPalette &palette)
{
    if (!qApp) {
        return;
    }
    qApp->setPalette(applicationPalette(palette));
    qApp->setStyleSheet(ThemeManager::generateQss(palette));
}

} // namespace

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
    applyApplicationTheme(m_active.palette);

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
        applyApplicationTheme(m_active.palette);
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
}

QMainWindow, QDialog, QMessageBox {
    background-color: @BG_BASE@;
    color: @TEXT_PRIMARY@;
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
}
QPushButton:hover {
    background-color: @BTN_HOVER@;
}
QPushButton:pressed {
    background-color: @BTN_PRESSED@;
}
QPushButton:checked {
    background-color: @SELECT_BG@;
    border-color: @FOCUS_RING@;
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
    min-width: 100px;
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
QTextEdit:focus, QPlainTextEdit:focus, QAbstractSpinBox:focus,
QAbstractItemView:focus, QSlider:focus {
    border: 2px solid @FOCUS_RING@;
}
QCheckBox:focus, QRadioButton:focus {
    border: 1px solid @FOCUS_RING@;
    border-radius: 3px;
}
QTabBar::tab:focus {
    border: 2px solid @FOCUS_RING@;
}

QToolTip {
    background-color: @BG_PANEL@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    padding: 4px 8px;
    border-radius: 4px;
    font-size: 11px;
}

QProgressBar {
    background-color: @BG_INPUT@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    border-radius: 4px;
    min-height: 18px;
    text-align: center;
}
QProgressBar::chunk {
    background-color: @ACCENT_LINK@;
    border-radius: 3px;
}
)");

    qss += QStringLiteral(R"(
QLabel:disabled, QToolButton:disabled, QCheckBox:disabled,
QRadioButton:disabled {
    color: @TEXT_MUTED@;
}

QLineEdit, QTextEdit, QPlainTextEdit, QAbstractSpinBox {
    background-color: @BG_INPUT@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    border-radius: 4px;
    selection-background-color: @SELECT_BG@;
    selection-color: @TEXT_PRIMARY@;
}
QTextEdit, QPlainTextEdit {
    padding: 6px;
}
QAbstractSpinBox {
    padding: 4px 8px;
}
QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled,
QAbstractSpinBox:disabled, QComboBox:disabled {
    background-color: @BG_PANEL@;
    color: @TEXT_MUTED@;
    border-color: @BORDER_SUBTLE@;
}

QAbstractItemView {
    background-color: @BG_INPUT@;
    alternate-background-color: @BG_PANEL@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    selection-background-color: @SELECT_BG@;
    selection-color: @TEXT_PRIMARY@;
}
QAbstractItemView::item:disabled {
    color: @TEXT_MUTED@;
}
QHeaderView::section {
    background-color: @BG_PANEL@;
    color: @TEXT_PRIMARY@;
    border: none;
    border-right: 1px solid @BORDER_DEFAULT@;
    border-bottom: 1px solid @BORDER_DEFAULT@;
    padding: 6px 8px;
}

QMenuBar {
    background-color: @BG_DARK@;
    color: @TEXT_PRIMARY@;
    border-bottom: 1px solid @BORDER_PANEL@;
}
QMenuBar::item {
    background-color: transparent;
    padding: 6px 10px;
}
QMenuBar::item:selected, QMenuBar::item:pressed {
    background-color: @SELECT_BG@;
    color: @TEXT_PRIMARY@;
}
QMenuBar::item:disabled {
    color: @TEXT_MUTED@;
}
QMenu {
    background-color: @BG_PANEL@;
    color: @TEXT_PRIMARY@;
    border: 1px solid @BORDER_DEFAULT@;
    padding: 4px;
}
QMenu::item {
    border-radius: 3px;
    padding: 6px 28px 6px 10px;
}
QMenu::item:selected, QMenu::item:checked {
    background-color: @SELECT_BG@;
    color: @TEXT_PRIMARY@;
}
QMenu::item:disabled {
    color: @TEXT_MUTED@;
}
QMenu::separator {
    height: 1px;
    background-color: @BORDER_DEFAULT@;
    margin: 4px 8px;
}

QStatusBar {
    background-color: @BG_DARK@;
    color: @TEXT_MID@;
    border-top: 1px solid @BORDER_PANEL@;
}
QStatusBar::item {
    border: none;
}

QMessageBox {
    background-color: @BG_BASE@;
}
QMessageBox QLabel {
    color: @TEXT_PRIMARY@;
}
QMessageBox QPushButton, QDialogButtonBox QPushButton {
    min-width: 88px;
}

QRadioButton {
    color: @TEXT_PRIMARY@;
    spacing: 8px;
}

QSlider::groove:horizontal {
    height: 6px;
    background-color: @BG_PANEL@;
    border: 1px solid @BORDER_DEFAULT@;
    border-radius: 3px;
}
QSlider::handle:horizontal {
    width: 16px;
    margin: -6px 0;
    background-color: @ACCENT_CHECKBOX@;
    border: 1px solid @ACCENT_CHECKBOX_BORDER@;
    border-radius: 8px;
}
QSlider::handle:horizontal:hover {
    border: 2px solid @FOCUS_RING@;
}

QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
    background-color: @BORDER_LIGHT@;
}

QProgressBar:disabled {
    color: @TEXT_MUTED@;
    border-color: @BORDER_SUBTLE@;
}
QProgressBar::chunk:disabled {
    background-color: @BORDER_DEFAULT@;
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
    qss.replace(QStringLiteral("@BORDER_PANEL@"), p.border_panel);
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
