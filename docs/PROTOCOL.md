# Protocol notes

This document separates observed behavior from assumptions. The implementation
was derived from USB/HID captures and responses from one COUGAR CFV235-series
LCD; it does not contain decompiled vendor source, firmware, or media assets.

## Confirmed interface

```text
Vendor ID:       0x1D6B
Product ID:      0x0126
Usage page:      0xFF00
Usage:           0x0001
Input report:    1025 bytes
Output report:   1025 bytes
```

The tested status payload reported a Linux device OS, 1920×462 background,
270-degree orientation, app/firmware V1.0.5, SDK V1.2.7, and hardware V2.0.
Serial numbers are deliberately excluded.

## Control frame

Control requests occupy one HID output report. Byte zero is the unnumbered HID
report ID. The application frame begins at byte one:

```text
5A | length_be | escaped_payload | checksum | 5A | zero padding
```

- `length_be` is the decoded frame length, including both delimiters, the
  two-byte length, and checksum.
- Payload/checksum bytes `5A` and `5B` are escaped as `5B 01` and `5B 02`.
- The checksum is the low byte of the sum of both length bytes and every
  unescaped payload byte.

The payload resembles a small HTTP-like request:

```text
POST brightness 1\r\n
SeqNumber=1\r\n
Date=<unix milliseconds>\r\n
ContentType=json\r\n
ContentLength=12\r\n
\r\n
{"value":75}
```

Successful responses begin with `1 200` and use the same frame escaping.

## Confirmed commands

| Command | Body | Use |
|---|---|---|
| `brightness` | `{"value":0..100}` | Set panel brightness |
| `power` | `{"event":"resume"}` | Wake/resume the display |
| `transport` | media metadata | Begin PNG upload |
| `transported` | filename/completion metadata | Commit uploaded PNG |

No other command is part of the live client.

## PNG media transport

1. Send `transport` with `type`, `fileSize`, and a conservative ASCII
   `fileName`.
2. Require a success response containing `"blockMaxSize":1024`.
3. Split the PNG into 1000-byte chunks and send one 1025-byte HID report per
   chunk. The media report uses delimiter `5C`, big-endian frame length,
   message type `13`, block count, block index, and media type `02`; payload
   starts at report offset 25.
4. Wait for the media-block acknowledgement.
5. Send `transported` for the same filename and require
   `{"state":"success"}`.

The tested firmware accepted the completion field `"md5":"todo"`; therefore
the code preserves that captured value rather than claiming an unverified hash
algorithm. This should be revisited if another firmware requires validation.

## Evidence boundary

Known safe behavior is intentionally narrow. Contributions that add a write
must include:

1. A redacted capture showing the request and successful response.
2. The exact tested model and firmware versions.
3. Bounds checks and positive device identity checks.
4. A read-only or local test path where feasible.

Firmware update, delete/erase, arbitrary filesystem, and undocumented feature
reports are out of scope until independently understood and reviewed.

