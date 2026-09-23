#include "gui/widgets/StatTileWidget.h"

#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "gui/ThemePainter.h"

#include <QAccessible>
#include <QColor>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

QString buildTileQss(const QString &accentBorder = QString())
{
    const QString topBorder =
        accentBorder.isEmpty() ? QString() : QStringLiteral("border-left: 2px solid %1;").arg(accentBorder);

    return QString(uR"(
        StatTileWidget {
            background-color: %1;
            border: 1px solid %2;
            %3
            border-radius: %4px;
        }
        StatTileWidget QLabel[kind="statLabel"] {
            font-size: 11px;
            color: %5;
            letter-spacing: 0.04em;
            background: transparent;
            border: none;
            text-transform: uppercase;
            font-weight: 500;
        }
        StatTileWidget QLabel[kind="statValue"] {
            font-size: 20px;
            font-weight: 600;
            font-family: %6;
            color: %7;
            background: transparent;
            border: none;
        }
    )"_s)
        .arg(Theme::kBgPanel())
        .arg(Theme::kBorderSubtle())
        .arg(topBorder)
        .arg(Theme::kRadiusMd)
        .arg(Theme::kTextMuted())
        .arg(Theme::kFontMono)
        .arg(Theme::kTextPrimary());
}

} // namespace

StatTileWidget::StatTileWidget(const QString &label,
                               const QString &initialValue,
                               QWidget *parent)
    : QFrame(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    m_titleLabel = new QLabel(label.toUpper(), this);
    m_titleLabel->setProperty("kind", u"statLabel"_s);
    layout->addWidget(m_titleLabel);

    m_valueLabel = new QLabel(initialValue, this);
    m_valueLabel->setProperty("kind", u"statValue"_s);
    layout->addWidget(m_valueLabel);

    refreshAccessibleName();

    applyThemeStyleSheet();
    connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
            this, &StatTileWidget::applyThemeStyleSheet);
}

void StatTileWidget::setValue(const QString &text)
{
    if (m_valueLabel) m_valueLabel->setText(text);
    refreshAccessibleName();
}

void StatTileWidget::setLabel(const QString &text)
{
    if (m_titleLabel) m_titleLabel->setText(text.toUpper());
    refreshAccessibleName();
}

void StatTileWidget::setAccentColor(const QColor &color)
{
    m_accentColor = color;
    applyThemeStyleSheet();
}

void StatTileWidget::applyThemeStyleSheet()
{
    const QString accentBorder = m_accentColor.isValid()
        ? m_accentColor.name(QColor::HexRgb)
        : QString();
    setStyleSheet(buildTileQss(accentBorder));
}

void StatTileWidget::refreshAccessibleName()
{
    if (!m_titleLabel || !m_valueLabel) {
        return;
    }
    setAccessibleName(
        QStringLiteral("%1: %2").arg(m_titleLabel->text(), m_valueLabel->text()));
}

void StatTileWidget::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
    QPainter p(this);
    cosmo::ThemePainter::paintBackground(p, rect(), QStringLiteral("panel"));
}
