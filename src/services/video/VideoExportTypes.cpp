#include "services/video/VideoExportTypes.h"

namespace cosmo::video {

VideoExportPreset VideoExportPreset::make(VideoOrientation orientation_value,
                                          VideoQuality quality_value) {
  VideoExportPreset preset;
  preset.orientation = orientation_value;
  preset.quality = quality_value;
  const bool portrait = orientation_value == VideoOrientation::portrait;
  const bool draft = quality_value == VideoQuality::draft_720p;
  if (draft) {
    preset.resolution = portrait ? QSize(720, 1280) : QSize(1280, 720);
    preset.video_bit_rate = 4000000;
  } else {
    preset.resolution = portrait ? QSize(1080, 1920) : QSize(1920, 1080);
    preset.video_bit_rate = 8000000;
  }
  return preset;
}

} // namespace cosmo::video
