# Recorded IMU milestone

The supported input is AltOS type `0x08`, using the [manufacturer protocol specification](https://altusmetrum.org/AltOS/doc/telemetry.html), version 1.9.22.2, sections 2.1, 2.3 and 4. The previous `sample_data/TELEM_FORMAT_EXPLAINED.md` interpretation is unverified and must not be used for decoding.

Packets contain a 32-byte payload. The `TELEM` hexadecimal envelope adds length `0x22`, RSSI, radio status, and an additive checksum seeded with `0x5a`. Validation includes both the envelope checksum and the radio CRC-valid bit. Payload type is at offset 4; offset 2 is a little-endian 16-bit clock in hundredths of a second. Signed temperature at offset 12 uses °C × 100. RSSI uses a signed byte, divided by two, minus 74 dBm. The specification's serial-envelope offset table is off by one relative to its 36-byte example; the implementation checks the complete example layout (length + 32 payload + 2 radio + 1 checksum).

Only these verified fields are emitted. Pressure is unavailable: the manufacturer table says Pa × 10, but the [upstream type-0x08 decoder](https://github.com/ajtowns/altos/blob/master/altoslib/AltosTelemetryMegaSensor.java) passes the field directly to `set_pressure`. This conflict needs resolution against the selected firmware/capture before exposing physical pressure. Accelerometer/gyro scale, calibration, board axes and other IMU variants remain unverified and unavailable (`NaN`), as do altitude, GPS, attitude, battery and distance. Unsupported packet types are counted, never emitted as empty samples.

Each session starts at elapsed zero. Counter rollover advances modulo 65536 ticks. Backward or ambiguous jumps of half the counter period or more are rejected; a capture must have less than 327.68 seconds between accepted IMU packets. Device changes and longer gaps require a new session.

`sample_data/recorded_imu.telem` is a synthetic demonstration, not a hardware capture. It includes rollover and known temperature changes. In Live Telemetry choose **Recorded IMU input…**, select this file, and observe the 250 ms line cadence. Use Disconnect to stop; the completed session remains exportable. Open the exported CSV to replay the same millisecond timestamps and missing fields (blank CSV cells). The existing `.telem` import path remains unsupported; use the new action to decode recorded packets.

The frame queue retains at most 128 packets and drops the oldest on overflow. The existing GUI mailbox bounds delivery separately. Rejected-packet and dropped-data counts are reported at session completion or stop. Stopping closes the input, joins framing, drains parsing, then joins parsing; replacing a source invalidates pending GUI deliveries.

Verification commands, results and remaining checks are recorded in [the verification notes](recorded-imu-verification.md).
