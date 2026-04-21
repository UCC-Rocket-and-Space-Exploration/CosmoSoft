#!/usr/bin/env python3
"""
Parser for .telem telemetry files
Converts hexadecimal telemetry data into human-readable format
"""

import sys
import struct
from datetime import datetime


def hex_to_ascii(hex_str):
    """Convert hex string to ASCII, return None if not valid ASCII"""
    try:
        bytes_data = bytes.fromhex(hex_str)
        # Check if mostly printable ASCII
        if all(32 <= b < 127 or b == 0 for b in bytes_data):
            return bytes_data.decode('ascii').rstrip('\x00')
        return None
    except:
        return None


def parse_telem_line(line):
    """Parse a single TELEM line"""
    parts = line.strip().split()
    if len(parts) < 2 or parts[0] != 'TELEM':
        return None

    hex_data = parts[1]

    result = {
        'raw': hex_data,
        'length': len(hex_data) // 2,  # bytes
        'parsed': {}
    }

    # Try to extract timestamp (first 8 hex chars = 4 bytes)
    if len(hex_data) >= 8:
        timestamp_hex = hex_data[:8]
        result['parsed']['timestamp'] = timestamp_hex

    # Check for ASCII strings (look for common patterns)
    # Scan through the data looking for ASCII sequences
    ascii_strings = []
    i = 0
    while i < len(hex_data) - 6:  # at least 3 chars
        # Try chunks of different sizes
        for chunk_size in [20, 16, 12, 10, 8, 6]:
            if i + chunk_size * 2 <= len(hex_data):
                chunk = hex_data[i:i + chunk_size * 2]
                ascii_str = hex_to_ascii(chunk)
                if ascii_str and len(ascii_str) >= 3:
                    ascii_strings.append({
                        'offset': i // 2,
                        'value': ascii_str,
                        'hex': chunk
                    })
                    i += chunk_size * 2
                    break
        else:
            i += 2

    if ascii_strings:
        result['parsed']['ascii_strings'] = ascii_strings

    # Try to identify message type based on patterns
    if 'IGNIS' in str(ascii_strings):
        result['type'] = 'DEVICE_INFO'
    else:
        result['type'] = 'TELEMETRY_DATA'

    # Extract some common numeric fields
    # Bytes 4-6 might be message type/subtype
    if len(hex_data) >= 12:
        msg_type = hex_data[8:12]
        result['parsed']['msg_type'] = msg_type

    return result


def format_output(parsed_data):
    """Format parsed data for display"""
    if not parsed_data:
        return ""

    output = []
    output.append(f"Type: {parsed_data.get('type', 'UNKNOWN')}")
    output.append(f"Length: {parsed_data['length']} bytes")

    if 'parsed' in parsed_data:
        p = parsed_data['parsed']

        if 'timestamp' in p:
            output.append(f"Timestamp: 0x{p['timestamp']}")

        if 'msg_type' in p:
            output.append(f"Message Type: 0x{p['msg_type']}")

        if 'ascii_strings' in p:
            output.append("ASCII Strings:")
            for s in p['ascii_strings']:
                output.append(f"  [@{s['offset']:02d}] '{s['value']}'")

    output.append(f"Raw: {parsed_data['raw']}")

    return "\n".join(output)


def main():
    if len(sys.argv) < 2:
        print("Usage: python parse_telem.py <telem_file>")
        sys.exit(1)

    filename = sys.argv[1]

    print("=" * 80)
    print(f"Parsing telemetry file: {filename}")
    print("=" * 80)
    print()

    with open(filename, 'r') as f:
        for line_num, line in enumerate(f, 1):
            if not line.strip():
                continue

            parsed = parse_telem_line(line)
            if parsed:
                print(f"Line {line_num}:")
                print(format_output(parsed))
                print("-" * 80)
                print()


if __name__ == "__main__":
    main()
