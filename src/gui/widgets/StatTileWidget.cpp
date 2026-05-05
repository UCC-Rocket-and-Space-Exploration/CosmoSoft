#include "gui/widgets/StatTileWidget.h"

#include "gui/Theme.h"
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
    const QString topBorder = accentBorder.isEmpty()
        ? QString()
        : QStringLiteral("border-top: 2px solid %1;").arg(accentBorder);

    return QString(uR"(
        StatTileWidget {
            background-color: %1;
            border: 1px solid %2;
            %3
            border-radius: %4px;
        }
        StatTileWidget QLabel[kind="statLabel"] {
            font-size: %5px;
            font-family: %6;
            color: %7;
            letter-spacing: 1px;
            background: transparent;
            border: none;
        }
        StatTileWidget QLabel[kind="statValue"] {
            font-size: 20px;
            font-weight: 700;
            font-family: %6;
            color: %8;
            background: transparent;
            border: none;
        }
    )"_s)
        .arg(Theme::kBgPanel())
        .arg(Theme::kBorderPanel())
        .arg(topBorder)
        .arg(Theme::kRadiusMd)
        .arg(Theme::kFontSizeBase)
        .arg(Theme::kFontMono)
        .arg(Theme::kTextMuted())
        .arg(Theme::kTextPrimary());
}

} // namespace

StatTileWidget::StatTileWidget(const QString &label,
                               const QString &initialValue,
                               QWidget *parent)
    : QFrame(parent)
{
    setStyleSheet(buildTileQss());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(4);

    m_titleLabel = new QLabel(label.toUpper(), this);
    m_titleLabel->setProperty("kind", u"statLabel"_s);
    layout->addWidget(m_titleLabel);

    m_valueLabel = new QLabel(initialValue, this);
    m_valueLabel->setProperty("kind", u"statValue"_s);
    layout->addWidget(m_valueLabel);

    setAccessibleName(QStringLiteral("%1: %2").arg(label, initialValue));
}

void StatTileWidget::setValue(const QString &text)
{
    if (m_valueLabel) m_valueLabel->setText(text);
    if (m_titleLabel) {
        setAccessibleName(QStringLiteral("%1: %2").arg(m_titleLabel->text(), text));
    }
}

void StatTileWidget::setLabel(const QString &text)
{
    if (m_titleLabel) m_titleLabel->setText(text.toUpper());
}

void StatTileWidget::setAccentColor(const QColor &color)
{
    setStyleSheet(buildTileQss(color.name(QColor::HexRgb)));
}

void StatTileWidget::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
    QPainter p(this);
    cosmo::ThemePainter::paintBackground(p, rect(), QStringLiteral("panel"));
}
