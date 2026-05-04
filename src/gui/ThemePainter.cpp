#include "gui/ThemePainter.h"
#include "gui/ThemeManager.h"

using namespace cosmo;

void ThemePainter::paintBackground(QPainter &painter, const QRect &rect, const QString &region)
{
    const auto &mgr = ThemeManager::instance();
    const QPixmap tex = mgr.texture(region);
    if (tex.isNull()) return;

    painter.save();
    painter.setOpacity(mgr.textureOpacity());

    switch (mgr.textureMode()) {
    case TextureMode::Tile:
        painter.drawTiledPixmap(rect, tex);
        break;
    case TextureMode::Stretch:
        painter.drawPixmap(rect, tex);
        break;
    case TextureMode::Cover: {
        const qreal scale = qMax(
            static_cast<qreal>(rect.width()) / tex.width(),
            static_cast<qreal>(rect.height()) / tex.height());
        const int sw = static_cast<int>(rect.width() / scale);
        const int sh = static_cast<int>(rect.height() / scale);
        const int sx = (tex.width() - sw) / 2;
        const int sy = (tex.height() - sh) / 2;
        painter.drawPixmap(rect, tex, QRect(sx, sy, sw, sh));
        break;
    }
    }

    painter.restore();
}
