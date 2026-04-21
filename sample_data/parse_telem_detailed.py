#!/usr/bin/env python3
"""
Detailed parser for .telem telemetry files
Decodes sensor values and telemetry data
"""

import sys
import struct
import csv
from datetime import datetime


def parse_device_info(data_bytes):
    """Parse device information message"""
    try:
        # Extract ASCII fields
        device_name_start = None
        version_start = None

        # Look for device name (around offset 15-21)
        for i in range(10, 25):
            chunk = data_bytes[i:i+6]
            if all(32 <= b < 127 or b == 0 for b in chunk):
                try:
                    text = chunk.decode('ascii').rstrip('\x00')
                    if text and not device_name_start:
                        device_name_start = i
                        device_name = data_bytes[i:i+10].decode('ascii').rstrip('\x00')
                        break
                except:
                    pass

        # Look for version (after device name)
        if device_name_start:
            version = data_bytes[device_name_start+10:device_name_start+18].decode('ascii').rstrip('\x00')
        else:
            device_name = "UNKNOWN"
            version = "UNKNOWN"

        return {
            'device_name': device_name.strip(),
            'firmware_version': version.strip()
        }
    except Exception as e:
        return {'error': str(e)}


def parse_telemetry_data(data_bytes):
    """Parse telemetry sensor data"""
    try:
        # Assuming little-endian format
        # This is a reverse-engineered structure based on patterns

        result = {}

        # Byte positions (approximate based on hex analysis)
        # Bytes 4-5: Sequence or counter
        if len(data_bytes) >= 6:
            result['sequence'] = struct.unpack('<H', data_bytes[4:6])[0]

        # Bytes 6-9: Temperature (appears to be around 0xb60c to 0xba0c)
        if len(data_bytes) >= 10:
            temp_raw = struct.unpack('<H', data_bytes[6:8])[0]
            result['temp_raw'] = temp_raw
            # Convert to Celsius (assuming hundredths of degrees encoding)
            result['temperature_c'] = round(temp_raw / 100.0, 2)

        # Bytes 10-13: Flags/status
        if len(data_bytes) >= 14:
            status = struct.unpack('<I', data_bytes[10:14])[0]
            result['status_flags'] = f"0x{status:08x}"

        # Bytes 14-17: Counter or measurement
        if len(data_bytes) >= 18:
            counter1 = struct.unpack('<I', data_bytes[14:18])[0]
            result['counter_1'] = counter1

        # Bytes 18-21: Packet number/sequence
        if len(data_bytes) >= 22:
            packet_num = struct.unpack('<H', data_bytes[18:20])[0]
            result['packet_num'] = packet_num

        # Bytes 22-25: Sensor data (might be signed)
        if len(data_bytes) >= 26:
            sensor1 = struct.unpack('<h', data_bytes[22:24])[0]  # signed
            result['sensor_1'] = sensor1

        # Bytes 24-27: More sensor data
        if len(data_bytes) >= 28:
            sensor2 = struct.unpack('<h', data_bytes[24:26])[0]  # signed
            result['sensor_2'] = sensor2

        # Bytes 26-29: Additional data
        if len(data_bytes) >= 30:
            sensor3 = struct.unpack('<h', data_bytes[26:28])[0]  # signed
            result['sensor_3'] = sensor3

        # Bytes 28-31: More values
        if len(data_bytes) >= 32:
            value4 = struct.unpack('<I', data_bytes[28:32])[0]
            result['value_4'] = value4

        # Bytes 32-33: Another counter
        if len(data_bytes) >= 34:
            value5 = struct.unpack('<H', data_bytes[32:34])[0]
            result['value_5'] = value5

        # Last 2 bytes: Checksum/CRC
        if len(data_bytes) >= 36:
            checksum = struct.unpack('<H', data_bytes[34:36])[0]
            result['checksum'] = f"0x{checksum:04x}"

        return result
    except Exception as e:
        return {'error': str(e)}


def parse_telem_line(line):
    """Parse a single TELEM line"""
    parts = line.strip().split()
    if len(parts) < 2 or parts[0] != 'TELEM':
        return None

    hex_data = parts[1]
    data_bytes = bytes.fromhex(hex_data)

    result = {
        'raw_hex': hex_data,
        'length': len(data_bytes),
    }

    # Extract timestamp (first 4 bytes)
    if len(data_bytes) >= 4:
        timestamp = struct.unpack('<I', data_bytes[0:4])[0]
        result['timestamp'] = timestamp
        result['timestamp_hex'] = f"0x{timestamp:08x}"

    # Determine message type (bytes 4-5 form a command)
    if len(data_bytes) >= 6:
        msg_byte4 = data_bytes[4]
        msg_byte5 = data_bytes[5]
        result['msg_type'] = f"0x{msg_byte4:02x}{msg_byte5:02x}"
        result['sequence'] = msg_byte4  # Byte 4 appears to be a sequence counter

        # Parse based on message type (byte 5)
        if msg_byte5 == 0x04:
            result['type'] = 'DEVICE_INFO'
            result['data'] = parse_device_info(data_bytes)
        elif msg_byte5 == 0x11:
            result['type'] = 'TELEMETRY'
            result['data'] = parse_telemetry_data(data_bytes)
        else:
            result['type'] = f'UNKNOWN_0x{msg_byte5:02x}'
            result['data'] = {}

    return result


def format_output(parsed_data):
    """Format parsed data for display"""
    if not parsed_data:
        return ""

    output = []
    output.append(f"Type: {parsed_data.get('type', 'UNKNOWN')}")
    output.append(f"Timestamp: {parsed_data.get('timestamp', 0)} ({parsed_data.get('timestamp_hex', '')})")
    output.append(f"Message Type: {parsed_data.get('msg_type', '')}")

    if 'data' in parsed_data and parsed_data['data']:
        output.append("Data:")
        for key, value in parsed_data['data'].items():
            output.append(f"  {key}: {value}")

    return "\n".join(output)


def export_to_csv(parsed_lines, output_file):
    """Export parsed telemetry to CSV"""
    with open(output_file, 'w', newline='') as csvfile:
        # Collect all unique field names
        fieldnames = ['line', 'type', 'timestamp', 'msg_type', 'sequence']

        # Add telemetry-specific fields
        telem_fields = ['temp_raw', 'temperature_c', 'status_flags', 'counter_1',
                       'packet_num', 'sensor_1', 'sensor_2', 'sensor_3',
                       'value_4', 'value_5', 'checksum']

        # Add device info fields
        device_fields = ['device_name', 'firmware_version']

        fieldnames.extend(telem_fields)
        fieldnames.extend(device_fields)

        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()

        for line_num, parsed in parsed_lines:
            if not parsed:
                continue

            row = {
                'line': line_num,
                'type': parsed.get('type', ''),
                'timestamp': parsed.get('timestamp', ''),
                'msg_type': parsed.get('msg_type', ''),
                'sequence': parsed.get('sequence', '')
            }

            # Add data fields
            if 'data' in parsed:
                for key, value in parsed['data'].items():
                    row[key] = value

            writer.writerow(row)


def main():
    if len(sys.argv) < 2:
        print("Usage: python parse_telem_detailed.py <telem_file> [--csv output.csv]")
        sys.exit(1)

    filename = sys.argv[1]
    csv_output = None

    # Check for CSV export option
    if '--csv' in sys.argv:
        csv_idx = sys.argv.index('--csv')
        if csv_idx + 1 < len(sys.argv):
            csv_output = sys.argv[csv_idx + 1]

    print("=" * 80)
    print(f"Detailed Telemetry Parser: {filename}")
    print("=" * 80)
    print()

    parsed_lines = []

    with open(filename, 'r') as f:
        for line_num, line in enumerate(f, 1):
            if not line.strip():
                continue

            parsed = parse_telem_line(line)
            if parsed:
                parsed_lines.append((line_num, parsed))
                print(f"[Line {line_num}] " + "=" * 70)
                print(format_output(parsed))
                print()

    # Export to CSV if requested
    if csv_output:
        export_to_csv(parsed_lines, csv_output)
        print("=" * 80)
        print(f"Exported {len(parsed_lines)} records to: {csv_output}")
        print("=" * 80)


if __name__ == "__main__":
    main()
