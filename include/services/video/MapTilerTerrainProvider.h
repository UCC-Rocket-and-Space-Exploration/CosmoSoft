/**
 * @file MapTilerTerrainProvider.h
 * @brief MapTiler imagery and elevation preflight for replay-video export.
 */

#ifndef COSMO_SOFT_MAPTILERTERRAINPROVIDER_H
#define COSMO_SOFT_MAPTILERTERRAINPROVIDER_H

#include "services/video/ITerrainProvider.h"

#include <QByteArray>

#include <memory>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace cosmo::video {

/** @brief Downloads bounded MapTiler imagery and Terrain RGB tiles. */
class MapTilerTerrainProvider final : public ITerrainProvider {
  Q_OBJECT

public:
  /**
   * @brief Construct a provider.
   * @param network Optional injected manager for deterministic tests; not
   * owned.
   * @param parent Optional QObject owner.
   */
  explicit MapTilerTerrainProvider(QNetworkAccessManager *network = nullptr,
                                   QObject *parent = nullptr);
  /** @brief Cancel active replies and release provider resources. */
  ~MapTilerTerrainProvider() override;

  /** @copydoc ITerrainProvider::request_terrain */
  void request_terrain(const TerrainBounds &bounds, const QSize &texture_size,
                       const QString &api_key) override;

  /** @copydoc ITerrainProvider::cancel */
  void cancel() override;

private:
  void begin_attempt();
  void on_imagery_finished();
  void on_metadata_finished();
  void on_tile_finished(int index);
  void finish_if_ready();
  void fail_or_retry();
  void clear_replies();

  QNetworkAccessManager *m_network = nullptr;
  std::unique_ptr<QNetworkAccessManager> m_owned_network;
  QNetworkReply *m_imagery_reply = nullptr;
  QNetworkReply *m_metadata_reply = nullptr;
  std::vector<QNetworkReply *> m_tile_replies;
  std::vector<QImage> m_tile_images;
  QByteArray m_imagery_bytes;
  QString m_attribution;
  TerrainBounds m_bounds;
  QSize m_texture_size;
  QString m_api_key;
  int m_tile_zoom = 0;
  int m_tile_min_x = 0;
  int m_tile_max_x = 0;
  int m_tile_min_y = 0;
  int m_tile_max_y = 0;
  int m_completed_tiles = 0;
  int m_attempt = 0;
  bool m_active = false;
  bool m_imagery_complete = false;
  bool m_metadata_complete = false;
};

} // namespace cosmo::video

#endif // COSMO_SOFT_MAPTILERTERRAINPROVIDER_H
