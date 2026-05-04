#include "gui/widgets/StatTileWidget.h"

#include "gui/Theme.h"

#include <QAccessible>
#include <QLabel>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

StatTileWidget::StatTileWidget(const QString &label,
                               const QString &initialValue,
                               QWidget *parent)
    : QFrame(parent)
{
    // Self-contained stylesheet so the tile looks correct on any parent.
    setStyleSheet(
        QString(uR"(
        StatTileWidget {
            background-color: rgba(21, 22, 25, 0.88);
            border: 1px solid %1;
            border-radius: %2px;
        }
        StatTileWidget QLabel[kind="statLabel"] {
            font-size: %3px;
            font-family: %4;
            color: %5;
            letter-spacing: 1px;
            background: transparent;
            border: none;
        }
        StatTileWidget QLabel[kind="statValue"] {
            font-size: 20px;
            font-weight: 700;
            font-family: %4;
            color: %6;
            background: transparent;
            border: none;
        }
    )"_s)
            .arg(Theme::kBorderPanel)
            .arg(Theme::kRadiusMd)
            .arg(Theme::kFontSizeBase)
            .arg(Theme::kFontMono)
            .arg(Theme::kTextMuted)
            .arg(Theme::kTextPrimary));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(2);

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
