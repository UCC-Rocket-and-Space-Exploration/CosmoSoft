# Telemetry protocols

## Live CSV telemetry

The Pico ground station receives the FC's 16-field CSV payload and optional
space-separated packet counter. It validates a complete LoRa packet, removes
the counter, and emits one plain CSV row followed by a newline over USB at
9600 baud. No labels, startup messages, RSSI or SNR may share that stream.
CRLF is accepted, as are rows fragmented across USB reads.

The optional exact header is:

```csv
timestamp_ms,temp,pressure,altitude,ax,ay,az,gx,gy,gz,anglex,angley,anglez,latitude,longitude,distance_from_base
```

`timestamp_ms` is the original nonnegative integer FC time since boot, in
milliseconds. It is not a UTC receipt time. Temperature uses degrees Celsius,
pressure pascals, altitude and distance metres, acceleration metres per second
squared, angular velocity radians per second, attitude degrees, and GPS signed
decimal degrees. All 16 fields must be finite numeric values. Timestamps must
be whole milliseconds within the model's integer range; latitude and longitude
must be within +/-90 and +/-180 degrees. Malformed rows are rejected. The
distance field is currently validated but not copied into the live model.

Example USB row (the radio counter is omitted):

```csv
251618,21.67,100227.20,221.46,-0.050,-1.089,10.379,-0.040,0.009,0.018,-6.743,0.523,266.904,51.892754,8.491923,84.02
```

The receiver, live decoder and deterministic demo all use milliseconds. Update
the receiver and application together: headerless live rows have no reliable
unit detection, and the old seconds-based live contract is no longer supported.
Fractional millisecond values are rejected rather than rounded.

For an FC that reverses west-longitude signs, the explicit **West longitude
correction** setting in Live Telemetry's **Link details** normalizes longitude
before real serial samples are recorded and displayed. It is disabled by default
and never changes latitude or guesses the operating country. See
[the GPS workaround guide](rocket-attitude.md#western-longitude-fc-workaround).

`FlightSample` retains these raw millisecond timestamps. Charts and replay
controls convert to seconds for display. CSV exports use an explicit
`timestamp_ms` header, retaining millisecond event spacing and the original FC
time origin. Their other columns use the existing export schema, including
explicit degree attitude labels. Replay import accepts `timestamp_ms` and the
older `time_ms` as milliseconds; `time`, `time_s` and `timestamp` remain seconds
for existing CSV/XLSX logs. No timestamp-unit guessing is used.

## Recorded IMU milestone

The supported input is AltOS type `0x08`, using the [manufacturer protocol specification](https://altusmetrum.org/AltOS/doc/telemetry.html), version 1.9.22.2, sections 2.1, 2.3 and 4. The previous `sample_data/TELEM_FORMAT_EXPLAINED.md` interpretation is unverified and must not be used for decoding.

Packets contain a 32-byte payload. The `TELEM` hexadecimal envelope adds length `0x22`, RSSI, radio status, and an additive checksum seeded with `0x5a`. Validation includes both the envelope checksum and the radio CRC-valid bit. Payload type is at offset 4; offset 2 is a little-endian 16-bit clock in hundredths of a second. Signed temperature at offset 12 uses °C × 100. RSSI uses a signed byte, divided by two, minus 74 dBm. The specification's serial-envelope offset table is off by one relative to its 36-byte example; the implementation checks the complete example layout (length + 32 payload + 2 radio + 1 checksum).

Only these verified fields are emitted. Pressure is unavailable: the manufacturer table says Pa × 10, but the [upstream type-0x08 decoder](https://github.com/ajtowns/altos/blob/master/altoslib/AltosTelemetryMegaSensor.java) passes the field directly to `set_pressure`. This conflict needs resolution against the selected firmware/capture before exposing physical pressure. Accelerometer/gyro scale, calibration, board axes and other IMU variants remain unverified and unavailable (`NaN`), as do altitude, GPS, attitude, battery and distance. Unsupported packet types are counted, never emitted as empty samples.

Each session starts at elapsed zero. Counter rollover advances modulo 65536 ticks. Backward or ambiguous jumps of half the counter period or more are rejected; a capture must have less than 327.68 seconds between accepted IMU packets. Device changes and longer gaps require a new session.

`sample_data/recorded_imu.telem` is a synthetic demonstration, not a hardware capture. It includes rollover and known temperature changes. In Live Telemetry choose **Recorded IMU input…**, select this file, and observe the 250 ms line cadence. Use Disconnect to stop; the completed session remains exportable. Open the exported CSV to replay the same millisecond timestamps and missing fields (blank CSV cells). Open a `.telem` file to import supported sensor packets for replay. The importer validates the envelope, skips unsupported packets and GPS/config placeholders, and normalizes counter rollover to elapsed milliseconds. Missing fields remain unavailable rather than appearing as zero. A capture uses the first accepted sensor device; packets from other devices are ignored. Import cancellation or failure preserves the current session.

The frame queue retains at most 128 packets and drops the oldest on overflow. The existing GUI mailbox bounds delivery separately. Rejected-packet and dropped-data counts are reported at session completion or stop. Stopping closes the input, joins framing, drains parsing, then joins parsing; replacing a source invalidates pending GUI deliveries.

Verification commands, results and remaining checks are recorded in [the verification notes](recorded-imu-verification.md).
