#include "services/telemetry/FrameDecoderFactory.h"

#include "services/telemetry/frame_decoders/CsvFrameDecoder.h"
#include "services/telemetry/frame_decoders/TeleMegaFrameDecoder.h"
#include "services/telemetry/frame_decoders/TeleMiniFrameDecoder.h"

std::unique_ptr<IFrameDecoder> FrameDecoderFactory::create(FrameFormat frame_format) {
    switch (frame_format) {
        case Csv:
            return std::make_unique<CsvFrameDecoder>();
        case TeleMega:
            return std::make_unique<TeleMegaFrameDecoder>();
        case TeleMini:
            std::make_unique<TeleMiniFrameDecoder>();
        default:
            throw std::logic_error("Unknown frame format");
    }
}
