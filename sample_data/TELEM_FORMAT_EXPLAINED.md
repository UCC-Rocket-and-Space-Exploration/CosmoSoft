# TELEM File Format Explanation

## Overview

The `.telem` file format contains telemetry data in hexadecimal encoding. Each line represents a single telemetry packet with sensor readings or device information.

## File Structure

### Line Format

```
TELEM <hexadecimal_data>
```

- Each line starts with the keyword "TELEM"
- Followed by a space and hexadecimal encoded binary data
- Each packet is exactly 36 bytes (72 hex characters)

## Binary Packet Structure (36 bytes)

### Common Header (Bytes 0-5)

```
Offset  Size  Type        Description
------  ----  ----------  ---------------------------
0-3     4     uint32_le   Timestamp (Unix-style)
4       1     uint8       Sequence/Counter
5       1     uint8       Message Type
```

### Message Types

#### 1. TELEMETRY (Type 0x11)

Contains sensor readings and operational data:

```
Offset  Size  Type        Description
------  ----  ----------  ---------------------------
0-3     4     uint32_le   Timestamp
4       1     uint8       Sequence counter
5       1     uint8       Type (0x11)
6-7     2     uint16_le   Temperature (raw ADC value)
8-9     2     uint16_le   Sequence number
10-13   4     uint32_le   Status flags
14-17   4     uint32_le   Counter 1 (increments)
18-19   2     uint16_le   Packet number
20-21   2     int16_le    Sensor 1 (signed)
22-23   2     int16_le    Sensor 2 (signed)
24-25   2     int16_le    Sensor 3 (signed)
26-29   4     uint32_le   Value 4
30-31   2     uint16_le   Value 5
32-33   2     uint16_le   Reserved/padding
34-35   2     uint16_le   CRC16 Checksum
```

**Fields Explanation:**

- **Temperature**: Raw ADC reading (interpretation depends on sensor calibration)
- **Status flags**: Bit flags for device state
- **Counters**: Increment with each reading, useful for detecting dropped packets
- **Sensors 1-3**: Signed values, possibly acceleration/gyro/magnetometer data
- **Checksum**: CRC-16 for error detection

#### 2. DEVICE_INFO (Type 0x04)

Contains device identification and firmware version:

```
Offset  Size  Type        Description
------  ----  ----------  ---------------------------
0-3     4     uint32_le   Timestamp
4       1     uint8       Sequence counter
5       1     uint8       Type (0x04)
6-14    9     bytes       Header/reserved
15-24   10    ASCII       Device Name (null-padded)
25-32   8     ASCII       Firmware Version (null-padded)
33-35   3     bytes       Reserved/padding
```

**Example Values:**

- Device Name: "IGNIS"
- Firmware Version: "1.9.18"

## Example Decoded Packets

### Telemetry Packet

```
Raw Hex: 22983066081102b90c0100030009880100510a0000000000000a88010031380000ec86c7

Decoded:
- Timestamp: 1714460706 (0x66309822)
- Sequence: 0x08
- Type: 0x11 (TELEMETRY)
- Temperature Raw: 47362
- Packet #: 10
- Sensor 1: 0
- Sensor 2: 2560
- Sensor 3: 392
- Checksum: 0xc786
```

### Device Info Packet

```
Raw Hex: 229830660804270100011a0000f401400049474e4953000000312e392e31380000ee9235

Decoded:
- Timestamp: 1714460706 (0x66309822)
- Sequence: 0x08
- Type: 0x04 (DEVICE_INFO)
- Device Name: "IGNIS"
- Firmware: "1.9.18"
- Checksum: 0x9235
```

## Data Patterns

1. **Regular Telemetry**: Most packets are type 0x11, containing sensor readings
2. **Periodic Device Info**: Type 0x04 packets appear periodically (every ~8 telemetry packets)
3. **Timestamps**: Increment sequentially, representing Unix time or milliseconds since boot
4. **Sequence Counter**: First byte increments to help track packet order

## Reading the Data

### Using the Python Parser

**View detailed output:**

```bash
python parse_telem_detailed.py altos_sample_data2.telem
```

**Export to CSV:**

```bash
python parse_telem_detailed.py altos_sample_data2.telem --csv output.csv
```

The CSV output includes all decoded fields in a spreadsheet-friendly format.

## Notes on Data Interpretation

- **Temperature values** appear high (400-500°C range) - these may be:
  - Raw ADC values requiring calibration formula
  - Encoded in an unknown unit (Kelvin * 10, etc.)
  - Require manufacturer-specific conversion
- **Sensor values** (sensor_1, sensor_2, sensor_3) may represent:
  - Accelerometer X, Y, Z axes
  - Gyroscope readings
  - Magnetometer data
  - Or other mission-specific sensors
- **Status flags** encode multiple boolean states in bit fields

## Application

This appears to be telemetry from an aerospace or rocket flight computer:

- Device name "IGNIS" suggests a propulsion or ignition system
- Regular sampling rate (appears to be ~1Hz based on sequence)
- Multiple sensors tracking orientation, temperature, and system state
- Suitable for flight data logging and post-flight analysis

