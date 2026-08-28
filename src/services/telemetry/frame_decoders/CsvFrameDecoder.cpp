
#include "services/telemetry/frame_decoders/CsvFrameDecoder.h"

#include <iostream>
#include <string>

FlightSample CsvFrameDecoder::decode(const Frame &frame) {
    FlightSample f_sample;
    int field_pos = 0;
    std::string current_field;
    std::size_t size = frame.data.size();

    for (int i = 0; i < size; i++) {
        auto byte = frame.data[i];
        if (byte != ',') {
            current_field += static_cast<char>(byte);
        }
        else {
            setValueToFlightSampleField(current_field, f_sample, field_pos);
            current_field.clear();
            field_pos++;
        }
    }
    setValueToFlightSampleField(current_field, f_sample, field_pos); //set last field

    // for (auto byte : frame.data) {
    //     if (byte != ',') {
    //         current_field += static_cast<char>(byte);
    //     }
    //     else {
    //         set_value_to_flight_sample_field(current_field, f_sample, field_pos);
    //         field_pos++;
    //     }
    // }
    return f_sample;
}

void CsvFrameDecoder::setValueToFlightSampleField(const std::string& value, FlightSample& sample, int field_position) {
//todo: ensure all types are correct and there will not be any slising
    if (!isNumber(value)) {
        return;
    }
    switch (field_position) {
        case 0:
            sample.timestamp = stol(value);
            break;
        case 1:
            sample.temperature = stod(value);
            break;
        case 2:
            sample.pressure = stod(value);
            break;
        case 3:
            sample.altitude = stod(value);
            break;
        case 4:
            sample.acceleration.x = stod(value);
            break;
        case 5:
            sample.acceleration.y = stod(value);
            break;
        case 6:
            sample.acceleration.z = stod(value);
            break;
        case 7:
            sample.angularVelocity.x = stod(value);
            break;
        case 8:
            sample.angularVelocity.y = stod(value);
            break;
        case 9:
            sample.angularVelocity.z = stod(value);
            break;
        case 10:
            sample.angularRotation.x = stod(value);
            break;
        case 11:
            sample.angularRotation.y = stod(value);
            break;
        case 12:
            sample.angularRotation.z = stod(value);
            break;
        case 13:
            sample.coordinates.latitude = stod(value);
            break;
        case 14:
            sample.coordinates.longitude = stod(value);
            break;
        case 15:
            sample.distanceFromLaunchPoint = stod(value);
            break;
        default:
            // std::cout << "warning: no field with position #" << field_position << std::endl;
            break;
    }
}

inline bool CsvFrameDecoder::isNumber(const std::string& value) {
    for (const unsigned char c: value) {
        int ascii_val = (int)c;
        if (ascii_val == 45 || ascii_val == 46 || (ascii_val >= 48 && ascii_val <= 57)) {
            continue;
        }
        return false;
    }
    return true;
}