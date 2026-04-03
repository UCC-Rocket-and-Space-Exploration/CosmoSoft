#include "gui/widgets/StatTileWidget.h"

#include <QLabel>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

StatTileWidget::StatTileWidget(const QString &label,
                               const QString &initialValue,
                               QWidget *parent)
    : QFrame(parent)
{
    // Self-contained stylesheet so the tile looks correct on any parent.
    setStyleSheet(uR"(
        StatTileWidget {
            background-color: rgba(21, 22, 25, 0.88);
            border: 1px solid #3b3b45;
            border-radius: 8px;
        }
        StatTileWidget QLabel[kind="statLabel"] {
            font-size: 12px;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
            color: #9aa7b8;
            letter-spacing: 1px;
            background: transparent;
            border: none;
        }
        StatTileWidget QLabel[kind="statValue"] {
            font-size: 20px;
            font-weight: 700;
            font-family: "Red Hat Mono", "Courier New", "Roboto Mono", monospace;
            color: #f0f0f0;
            background: transparent;
            border: none;
        }
    )"_s);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(2);

    m_titleLabel = new QLabel(label.toUpper(), this);
    m_titleLabel->setProperty("kind", u"statLabel"_s);
    layout->addWidget(m_titleLabel);

    m_valueLabel = new QLabel(initialValue, this);
    m_valueLabel->setProperty("kind", u"statValue"_s);
    layout->addWidget(m_valueLabel);
}

void StatTileWidget::setValue(const QString &text)
{
    if (m_valueLabel) m_valueLabel->setText(text);
}

void StatTileWidget::setLabel(const QString &text)
{
    if (m_titleLabel) m_titleLabel->setText(text.toUpper());
}
