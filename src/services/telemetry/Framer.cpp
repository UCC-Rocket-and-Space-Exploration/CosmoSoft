#include "services/telemetry/Framer.h"

void Framer::ingest(const uint8_t *, std::size_t) {}

Frame *Framer::readData(uint8_t *, std::size_t) { return nullptr; }

bool Framer::readableData() const { return false; }

bool Framer::try_next_frame(Frame &) { return false; }
