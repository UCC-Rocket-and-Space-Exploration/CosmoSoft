/**
 * @file StatTileWidget.h
 * @brief A compact theme-aware tile showing a labelled numeric readout.
 *
 * Used in the stats row at the top of DashboardPage.  Each tile shows a small
 * UPPERCASE label (e.g. "ALTITUDE") above a large value string (e.g. "42.0 m").
 * Colors are sourced from the active ThemeManager palette and update
 * automatically on theme changes via the themeChanged signal.
 */

#pragma once

#include <QColor>
#include <QFrame>

class QLabel;

class StatTileWidget : public QFrame {
    Q_OBJECT

public:
    explicit StatTileWidget(const QString &label,
                            const QString &initialValue = QString(u"\u2014"),
                            QWidget *parent = nullptr);

    /** Replaces the large value text. */
    void setValue(const QString &text);

    /** Replaces the small header label text. */
    void setLabel(const QString &text);

    /** @brief Sets a 2px colored accent stripe on the top border of the tile. */
    void setAccentColor(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void applyThemeStyleSheet();

private:
    QLabel *m_titleLabel = nullptr;
    QLabel *m_valueLabel = nullptr;
    QColor m_accentColor;
};
