/**
 * @file VideoExportTypes.h
 * @brief Shared request, preset, terrain, and timeline types for flight-video
 * export.
 */

#ifndef COSMO_SOFT_VIDEOEXPORTTYPES_H
#define COSMO_SOFT_VIDEOEXPORTTYPES_H

#include "domain/FlightSession.h"
#include "services/video/FlightAnalyzer.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

namespace cosmo::video {

/** @brief Supported output aspect ratios. */
enum class VideoOrientation { landscape, portrait };

/** @brief Supported output resolution tiers. */
enum class VideoQuality { draft_720p, full_hd_1080p };

/** @brief Supported terrain privacy modes. */
enum class TerrainMode { private_scene, maptiler };

/** @brief Fixed encoder and canvas properties for an export. */
struct VideoExportPreset {
  VideoOrientation orientation = VideoOrientation::landscape;
  VideoQuality quality = VideoQuality::full_hd_1080p;
  QSize resolution{1920, 1080};
  int frames_per_second = 30;
  int video_bit_rate = 8000000;

  /** @brief Build a validated preset from orientation and quality. */
  [[nodiscard]] static VideoExportPreset make(VideoOrientation orientation,
                                              VideoQuality quality);
};

/** @brief Geographic bounds for one terrain request. */
struct TerrainBounds {
  double south = 0.0;
  double west = 0.0;
  double north = 0.0;
  double east = 0.0;
};

/** @brief Preflighted imagery and height data consumed without further network
 * access. */
struct TerrainPackage {
  QImage texture;
  QImage height_map;
  TerrainBounds bounds;
  QString attribution;
  double height_span_m = 0.0;
  bool real_terrain = false;
};

/** @brief Complete immutable request passed to VideoExportService. */
struct VideoExportRequest {
  std::shared_ptr<const FlightSession> session;
  FlightAnalysis analysis;
  VideoExportPreset preset;
  TerrainMode terrain_mode = TerrainMode::private_scene;
  TerrainPackage terrain;
  QString output_path;
  bool overwrite_existing = false;
  QString title;
  QString rocket_or_team;
};

} // namespace cosmo::video

#endif // COSMO_SOFT_VIDEOEXPORTTYPES_H
