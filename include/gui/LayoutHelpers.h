/**
 * @file LayoutHelpers.h
 * @brief Utility functions for consistent spacing and layout margins.
 *
 * These helpers apply spacing tokens from Theme.h to QLayouts, ensuring
 * consistent spacing across all UI components without scattered magic numbers.
 *
 * Usage:
 * @code
 *   #include "gui/LayoutHelpers.h"
 *   auto *layout = new QVBoxLayout(this);
 *   LayoutHelpers::setPageMargins(layout);
 *   LayoutHelpers::setGenerousSpacing(layout);
 * @endcode
 */

#ifndef COSMO_SOFT_LAYOUTHELPERS_H
#define COSMO_SOFT_LAYOUTHELPERS_H

#include "gui/Theme.h"
#include <QLayout>

namespace LayoutHelpers {

/**
 * @brief Apply standard page-level margins (kSpaceXl on all sides).
 */
inline void setPageMargins(QLayout *layout) {
    if (layout) layout->setContentsMargins(Theme::kSpaceXl, Theme::kSpaceXl,
                                           Theme::kSpaceXl, Theme::kSpaceXl);
}

/**
 * @brief Apply panel-level margins (kSpaceMd on all sides).
 */
inline void setPanelMargins(QLayout *layout) {
    if (layout) layout->setContentsMargins(Theme::kSpaceMd, Theme::kSpaceMd,
                                           Theme::kSpaceMd, Theme::kSpaceMd);
}

/**
 * @brief Apply compact margins (kSpaceBase on all sides).
 */
inline void setCompactMargins(QLayout *layout) {
    if (layout) layout->setContentsMargins(Theme::kSpaceBase, Theme::kSpaceBase,
                                           Theme::kSpaceBase, Theme::kSpaceBase);
}

/**
 * @brief Zero margins (for nested layouts).
 */
inline void setZeroMargins(QLayout *layout) {
    if (layout) layout->setContentsMargins(0, 0, 0, 0);
}

/**
 * @brief Standard item spacing (kSpaceBase).
 */
inline void setStandardSpacing(QLayout *layout) {
    if (layout) layout->setSpacing(Theme::kSpaceBase);
}

/**
 * @brief Compact item spacing (kSpaceXs).
 */
inline void setCompactSpacing(QLayout *layout) {
    if (layout) layout->setSpacing(Theme::kSpaceXs);
}

/**
 * @brief Generous item spacing (kSpaceMd).
 */
inline void setGenerousSpacing(QLayout *layout) {
    if (layout) layout->setSpacing(Theme::kSpaceMd);
}

} // namespace LayoutHelpers

#endif // COSMO_SOFT_LAYOUTHELPERS_H
