/**
 * @file ThemePainter.h
 * @brief Utility to paint skin textures as panel backgrounds.
 *
 * Widgets that want skin-texture backgrounds call ThemePainter::paintBackground()
 * at the start of their paintEvent, passing their rect and the texture region name.
 */

#ifndef COSMO_SOFT_THEMEPAINTER_H
#define COSMO_SOFT_THEMEPAINTER_H

#include <QPainter>
#include <QRect>
#include <QString>

namespace cosmo {

class ThemePainter {
public:
    /**
     * @brief Paint the active skin's texture (if any) over the widget's background.
     *
     * Call at the beginning of paintEvent after filling with the solid background.
     * Does nothing if the active skin has no texture for the given region.
     *
     * @param painter Already-begun QPainter for the widget.
     * @param rect Widget rect to fill.
     * @param region Texture region key: "sidebar", "toolbar", "panel", "chart_bg", "settings_bg".
     */
    static void paintBackground(QPainter &painter, const QRect &rect, const QString &region);
};

} // namespace cosmo

#endif // COSMO_SOFT_THEMEPAINTER_H
