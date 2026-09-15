#include "catch2/catch_test_macros.hpp"
#include "services/telemetry/FrameDecoderVault.h"
#include "services/telemetry/frame_decoders/TeleMegaImuFrameDecoder.h"
#include "services/telemetry/frame_decoders/TeleMiniV3FrameDecoder.h"

#include <cmath>

TEST_CASE("Legacy IMU fixture uses verified units") {
  const std::string text = "TELEM "
                           "2234127856082D9600A8610000FAFFD4FE6400F401CEFF2C01F"
                           "A0019007B000000AAAA42";
  const auto sample = TeleMegaImuFrameDecoder{}.decode(
      {AltosFrame, {text.begin(), text.end()}, 8});
  REQUIRE(std::isnan(sample.pressure));
  REQUIRE(sample.timestamp == 221360);
  REQUIRE(std::isnan(sample.acceleration.x));
}

TEST_CASE("Vault rejects a legacy packet without a start offset") {
  const std::string text =
      "TELEM "
      "229830660804270100011a0000f401400049474e4953000000312"
      "e392e31380000ee9235";
  const Frame frame{AltosFrame, {text.begin(), text.end()}, 0};
  REQUIRE_THROWS(FrameDecoderVault{}.select(frame));
}

TEST_CASE("TeleMini V3 rejects a bad checksum") {
  const std::string text = "TELEM "
                           "229830bb1b1102850c2604e8040b2c6581e82469e4e07f71d6"
                           "145d812a4d81dae50335af";
  const Frame frame{AltosFrame, {text.begin(), text.end()}, 8};
  REQUIRE_THROWS(TeleMiniV3FrameDecoder{}.decode(frame));
}
