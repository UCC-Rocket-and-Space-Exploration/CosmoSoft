/**
 * @file ITerrainProvider.h
 * @brief Asynchronous terrain preflight interface for replay-video export.
 */

#ifndef COSMO_SOFT_ITERRAINPROVIDER_H
#define COSMO_SOFT_ITERRAINPROVIDER_H

#include "services/video/VideoExportTypes.h"

#include <QObject>

namespace cosmo::video {

/** @brief Fetches and validates all terrain assets before video encoding
 * begins. */
class ITerrainProvider : public QObject {
  Q_OBJECT

public:
  using QObject::QObject;
  ~ITerrainProvider() override = default;

  /**
   * @brief Begin a bounded terrain preflight.
   * @param bounds Padded geographic flight bounds.
   * @param texture_size Requested terrain texture size.
   * @param api_key Provider client key; never included in emitted errors.
   */
  virtual void request_terrain(const TerrainBounds &bounds,
                               const QSize &texture_size,
                               const QString &api_key) = 0;

  /** @brief Cancel an active preflight without emitting a failure. */
  virtual void cancel() = 0;

signals:
  /** @brief Emitted after imagery and elevation have both been validated. */
  void ready(const cosmo::video::TerrainPackage &terrain);

  /** @brief Emitted for a user-actionable preflight failure. */
  void failed(const QString &message);
};

} // namespace cosmo::video

#endif // COSMO_SOFT_ITERRAINPROVIDER_H
