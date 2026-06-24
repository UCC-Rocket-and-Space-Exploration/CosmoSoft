#include <iostream>
#include <vector>
#include "domain/FlightSample.h"

inline std::vector<FlightSample> parsed_samples = {};

inline void onData(const FlightSample& sample) {
    std::cout << "received sample. Timestamp:  " << sample.timestamp << " Distance: " << sample.distanceFromLaunchPoint << std::endl;
    parsed_samples.push_back(sample);
}

