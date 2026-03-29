# libaprs

C library for **APRS** (Automatic Packet Reporting System) packet parsing, building, and transport. Provides layered support for TNC2 text, AX.25 UI frames, KISS framing, APRS-IS internet gateway, AFSK1200 modem, and station state tracking.

C99, no external dependencies beyond libc + libm. All layers are independent and composable.

## Features

- **APRS Parser** — TNC2 text parsing for all major packet types: position (uncompressed, compressed, Mic-E), message (with ack/rej), status, object, item, weather, telemetry, query, third-party
- **APRS Builder** — Construct position, message, and status packets with proper APRS formatting (DDmm.mmN/DDDmm.mmW)
- **AX.25 UI Frames** — Real binary AX.25 address packing/unpacking (6-char shifted callsign + SSID byte), control/PID fields, FCS-16. Bidirectional conversion between `aprs_packet_t` and `ax25_ui_frame_t`
- **KISS Framing** — Single-frame encode/decode with FEND/FESC escape handling, plus streaming decoder with partial frame buffering and frame callback
- **APRS-IS Client** — Real TCP connection with proper login line, line-buffered I/O, server comment filtering, callback-driven receive loop, duplicate suppression, and passcode computation
- **Serial Transport** — termios-based serial port backend for hardware TNCs (configurable baud, 8N1)
- **TCP Transport** — Socket-based TCP backend for Dire Wolf or networked TNCs (IPv4/IPv6 via getaddrinfo)
- **AFSK1200 Modem** — Bell 202 modulator and demodulator. I/Q correlator with RRC FIR low-pass, bandpass prefilter, 8-way multi-slicer, per-tone AGC, 32-bit PLL bit timing, single-bit FEC. Matches or beats Dire Wolf decode rates
- **Station Database** — Hash table tracking last-heard time, packet count, position, status, and comment per callsign. Expiration support for stale entries
- **Message Tracker** — Outgoing message sequence numbering with pending/acked/rejected state tracking
- **JSON Export** — Station and packet serialization to JSON strings
- **Frame Dedup** — FNV-1a hash ring buffers for both text (APRS-IS) and binary (RF) duplicate suppression
- **WAV Reader** — PCM WAV file parser (8/16-bit, mono/stereo, any sample rate)

### Supported Configurations

| Parameter | Values |
|-----------|--------|
| Sample rates (modem) | 8000, 9600, 11025, 22050, 44100, 48000 Hz |
| Position formats | Uncompressed, compressed (Base91), Mic-E |
| Packet types | Position, message, status, object, item, weather, telemetry, query |
| KISS ports | 0–15 |
| AX.25 SSIDs | 0–15 |
| Path hops | Up to 8 |
| Audio format | Signed 16-bit linear PCM |

## Building

```bash
./autogen.sh    # generate configure (requires autoconf, automake, libtool)
./configure
make            # builds libaprs.so, libaprs.a, and example programs
make check      # runs the test suite (190 tests)
make install    # installs library, headers, and pkg-config file
```

### Debian Packages

```bash
dpkg-buildpackage -us -uc -b
```

Produces `libaprs0` (shared library), `libaprs-dev` (headers + pkg-config), and `libaprs0-dbgsym` (debug symbols).

## API

All public functions return `aprs_err_t`. Headers are under `include/libaprs/`.

### APRS Parsing

```c
#include <libaprs/aprs.h>

aprs_packet_t pkt;
aprs_err_t rc = aprs_parse_tnc2(
    "N0CALL-9>APRS,WIDE1-1:!4903.50N/07201.75W-PHG2360", &pkt);

if (rc == APRS_OK) {
    printf("type: %s\n", aprs_packet_type_name(pkt.type));
    printf("src:  %s\n", pkt.src.callsign);
    printf("lat:  %.4f\n", pkt.data.position.latitude);
    printf("lon:  %.4f\n", pkt.data.position.longitude);
    printf("cmt:  %s\n", pkt.data.position.comment);
}
```

Supported data type identifiers: `!` `=` `/` `@` (position), `:` (message), `>` (status), `;` (object), `)` (item), `_` (weather), `T` (telemetry), `` ` `` `'` (Mic-E), `?` (query), `}` (third-party).

### APRS Building

```c
aprs_packet_t pkt;
char line[512];
const char *path[] = {"WIDE1-1", "WIDE2-1"};

aprs_build_position(&pkt, "N0CALL-9", "APRS", path, 2,
                    35.35, -97.5167, '/', '>', "OKC test");
aprs_format_tnc2(&pkt, line, sizeof(line));
printf("%s\n", line);
/* N0CALL-9>APRS,WIDE1-1,WIDE2-1:!3521.00N/09731.00W>OKC test */

aprs_build_message(&pkt, "N0CALL", "APRS", NULL, 0,
                   "W3ADO-1", "Hello!", "42");
aprs_format_tnc2(&pkt, line, sizeof(line));
/* N0CALL>APRS::W3ADO-1  :Hello!{42 */
```

### AX.25 UI Frames

```c
#include <libaprs/ax25.h>

/* Pack/unpack callsign to 7-byte AX.25 address */
uint8_t addr[7];
ax25_pack_address("N0CALL-9", addr, true, false);

/* Encode frame to binary (for KISS) */
ax25_ui_frame_t frame;
ax25_ui_frame_init(&frame);
/* ... fill in dst, src, path, info ... */
uint8_t buf[512];
size_t written;
ax25_encode_ui_frame(&frame, buf, sizeof(buf), &written);

/* Convert between APRS packets and AX.25 frames */
ax25_from_aprs(&pkt, &frame);   /* aprs_packet_t → ax25_ui_frame_t */
ax25_to_aprs(&frame, &pkt);     /* ax25_ui_frame_t → aprs_packet_t */
```

### KISS Framing

```c
#include <libaprs/kiss.h>

/* Single-frame encode/decode */
uint8_t kiss_buf[256];
size_t kiss_len;
kiss_encode(0, KISS_CMD_DATA_FRAME, ax25_data, ax25_len,
            kiss_buf, sizeof(kiss_buf), &kiss_len);

/* Streaming decoder for serial/TCP data */
kiss_decoder_t dec;
kiss_decoder_init(&dec, on_frame_callback, user_ptr);
kiss_decoder_feed(&dec, incoming_bytes, nbytes);
```

### APRS-IS Client

```c
#include <libaprs/aprsis.h>

aprsis_client_t *c = aprsis_client_create();
aprsis_connect_params_t p = {
    .host = "rotate.aprs2.net",
    .port = 14580,
    .login = "N0CALL",
    .passcode = "12345",
    .filter = "r/35.5/-97.5/100"
};
aprsis_connect(c, &p);

/* Callback-driven receive */
aprsis_set_line_callback(c, on_line, NULL);
aprsis_run(c);  /* blocks until disconnect */

/* Or polling mode */
char line[1024];
size_t nread;
aprsis_read_line(c, line, sizeof(line), &nread);

/* Duplicate suppression */
aprsis_dedup_t dedup;
aprsis_dedup_init(&dedup);
if (!aprsis_dedup_check(&dedup, line)) {
    /* new packet — process it */
}

/* Passcode computation */
int code = aprsis_passcode("N0CALL");
```

### Transport Backends

```c
#include <libaprs/transport.h>

/* Serial port (hardware TNC) */
aprs_transport_t tp;
aprs_serial_opts_t opts = { .device = "/dev/ttyUSB0", .baud = 9600 };
aprs_transport_serial_create(&tp, &opts);
aprs_transport_open(&tp);

/* TCP socket (Dire Wolf, networked TNC) */
aprs_tcp_opts_t tcp = { .host = "localhost", .port = 8001 };
aprs_transport_tcp_create(&tp, &tcp);

/* Generic read/write (works with any backend) */
uint8_t buf[512];
size_t nread;
aprs_transport_read(&tp, buf, sizeof(buf), &nread);
```

### AFSK1200 Modem

```c
#include <libaprs/modem.h>

/* Modulate: AX.25 frame → audio samples */
afsk_mod_t *mod = afsk_mod_create(22050);
int16_t audio[200000];
size_t nsamples;
afsk_mod_frame(mod, ax25_data, ax25_len, audio, 200000, &nsamples);

/* Demodulate: audio samples → AX.25 frames via callback */
afsk_demod_t *demod = afsk_demod_create(22050, on_frame, NULL);
afsk_demod_feed(demod, audio, nsamples);

/* Full stack: build APRS → AX.25 → modulate → demodulate → parse */
```

### Station Database

```c
#include <libaprs/station.h>

aprs_station_db_t *db = aprs_station_db_create(256);
aprs_station_db_update(db, &pkt, time(NULL));

const aprs_station_t *st = aprs_station_db_find(db, "N0CALL");
if (st) {
    printf("%s: %d packets, last %.4f/%.4f\n",
           st->callsign, st->packet_count,
           st->latitude, st->longitude);
}

/* Expire stations not heard in 30 minutes */
aprs_station_db_expire(db, time(NULL) - 1800);

/* JSON export */
char json[1024];
aprs_station_to_json(st, json, sizeof(json));
aprs_packet_to_json(&pkt, json, sizeof(json));

/* Message sequence tracking */
aprs_msg_tracker_t tracker;
aprs_msg_tracker_init(&tracker);
char seq[8];
aprs_msg_tracker_next_seq(&tracker, seq, sizeof(seq));
aprs_msg_tracker_send(&tracker, "W3ADO", "Hello", seq, time(NULL));
aprs_msg_tracker_ack(&tracker, seq);
```

### Error Codes

| Code | Constant | Meaning |
|------|----------|---------|
| 0 | `APRS_OK` | Success |
| -1 | `APRS_ERR_INVALID_ARG` | Invalid argument |
| -2 | `APRS_ERR_PARSE` | Parse error |
| -3 | `APRS_ERR_FORMAT` | Format error |
| -4 | `APRS_ERR_OVERFLOW` | Buffer overflow |
| -5 | `APRS_ERR_BAD_CHECKSUM` | Bad checksum |
| -6 | `APRS_ERR_UNSUPPORTED` | Unsupported feature |
| -7 | `APRS_ERR_IO` | I/O error |
| -8 | `APRS_ERR_NOMEM` | Out of memory |
| -9 | `APRS_ERR_STATE` | Invalid state |

## AFSK1200 Modem Performance

Benchmarked against Dire Wolf using `gen_packets -n 100` (100 frames with increasing noise):

| Sample Rate | Dire Wolf | libaprs | Result |
|-------------|-----------|---------|--------|
| 8000 Hz | 24 | 25 | **104%** |
| 9600 Hz | 30 | 32 | **107%** |
| 22050 Hz | 50 | 52 | **104%** |
| 44100 Hz | 72 | 70 | 97% |
| 48000 Hz | 70 | 75 | **107%** |

The demodulator uses:
- I/Q correlator with continuous-phase 256-entry sine/cosine lookup oscillators
- Root Raised Cosine FIR low-pass filter (2.8 symbol width, α=0.20)
- Hamming-windowed FIR bandpass prefilter (1014–2386 Hz)
- 8-way multi-slicer with logarithmically spaced space_gain (0.5×–4.0×)
- Per-tone AGC with independent peak/valley tracking (fast attack 0.70, slow decay 0.000090)
- 32-bit PLL with multiplicative inertia nudge for bit timing recovery
- NRZI decode with deferred-1-bit HDLC state machine
- Single-bit FEC (flip each bit on CRC failure)
- Cross-slicer frame dedup via FNV-1a hash ring

## Test Suite

```
$ make check

libaprs test suite
==================

APRS parse/format/build tests:                      62 tests
KISS encode/decode/streaming tests:                  26 tests
AX.25 tests:                                         33 tests
Transport tests:                                       8 tests
APRS-IS tests:                                        28 tests
Station state tests:                                  23 tests
AFSK1200 modem tests:                                 10 tests

==================
Results: 190/190 passed
```

Tests cover:
- TNC2 parse/format round-trips for all packet types (position, message, status, object, item, weather, telemetry)
- Compressed position and Mic-E decoding with known vectors
- Real APRS-IS packet examples from live traffic
- AX.25 address packing/unpacking with SSID, H-bit, extension bit
- AX.25 binary frame encode/decode with hand-crafted test vectors
- Full APRS → AX.25 → KISS → transport → KISS → AX.25 → APRS round-trip
- KISS streaming decoder (split frames, split escapes, garbage tolerance, byte-at-a-time)
- APRS-IS login formatting, passcode computation (known vectors), line buffering (pipe mock server), comment filtering, duplicate suppression
- Station database create/update/find/list/expire, message tracker ack/rej, JSON export
- AFSK1200 modulate/demodulate round-trip at multiple sample rates, full-stack end-to-end

## Example Programs

| Program | Description |
|---------|-------------|
| `example_parse_packet` | Parse TNC2 lines and print structured fields |
| `example_build_position` | Build position and message packets, format as TNC2 |
| `example_kiss_bridge` | Bridge TNC2 text on stdin/stdout to KISS frames on serial or TCP |
| `aprs_decode` | Decode APRS packets from WAV audio files (comparable to Dire Wolf `atest`) |

```bash
# Decode a WAV file
./aprs_decode recording.wav

# Bridge to a TNC on serial
./kiss_bridge serial /dev/ttyUSB0 9600

# Bridge to Dire Wolf on TCP
./kiss_bridge tcp localhost 8001
```

## Project Structure

```
libaprs/
  include/libaprs/
    aprs.h              — APRS parsing, formatting, and building
    aprsis.h            — APRS-IS client
    ax25.h              — AX.25 UI frame encode/decode
    error.h             — Error codes and aprs_strerror()
    kiss.h              — KISS framing and streaming decoder
    modem.h             — AFSK1200 modulator and demodulator
    station.h           — Station database, message tracker, JSON, dedup
    transport.h         — Serial and TCP transport backends
    types.h             — Core structs (packet, position, message, etc.)
    version.h           — Version constants
    wav.h               — WAV file reader
  src/
    aprs.c              — TNC2 parser, all packet type decoders, builders
    aprsis.c            — APRS-IS TCP client, line buffering, passcode, dedup
    ax25.c              — AX.25 address packing, frame encode/decode, FCS-16
    error.c             — Error string table
    kiss.c              — KISS encode/decode, streaming state machine
    modem.c             — AFSK1200 modulator and demodulator (I/Q + RRC + PLL)
    station.c           — Station DB, message tracker, JSON export, frame dedup
    transport.c         — Serial (termios) and TCP (sockets) backends
    wav.c               — PCM WAV file parser
  tests/
    test_main.c         — Test harness (test_begin/test_assert/test_end)
    test_aprs.c         — APRS parser and builder tests
    test_aprsis.c       — APRS-IS client tests (pipe mock server)
    test_ax25.c         — AX.25 binary frame tests
    test_kiss.c         — KISS encode/decode and streaming tests
    test_modem.c        — AFSK1200 round-trip tests
    test_station.c      — Station DB, message tracker, JSON, dedup tests
    test_transport.c    — Transport layer tests (pipe loopback)
  examples/
    parse_packet.c      — Parse and display TNC2 packets
    build_position.c    — Build and format APRS packets
    kiss_bridge.c       — TNC2 ↔ KISS bridge for serial/TCP
    aprs_decode.c       — WAV file AFSK decoder
  testdata/             — gen_packets WAV files for modem benchmarking
  debian/               — Debian packaging files
  configure.ac          — Autoconf configuration
  Makefile.am           — Top-level automake
  autogen.sh            — Bootstrap script
  libaprs.pc.in         — pkg-config template
```

## Technical Details

### APRS Parser
- TNC2 format: `SRC>DST,PATH:INFO` with data type identifier as first char of INFO
- Uncompressed position: `DDmm.mmN/DDDmm.mmW` with N/S and E/W hemisphere
- Compressed position: Base91 lat/lon in 13 bytes with optional course/speed/altitude
- Mic-E: latitude in destination address, longitude/speed/course in info field
- Timestamps: DHM (`z`=UTC, `/`=local) and HMS (`h`=UTC) formats
- Messages: 9-char space-padded addressee, optional `{msgno`, ack/rej detection

### AX.25 Encoding
- 7-byte address fields: 6 chars left-shifted by 1, space-padded, plus SSID byte
- SSID byte: H-bit (7), reserved 11 (6:5), SSID (4:1), extension bit (0)
- Control field: 0x03 (UI), PID: 0xF0 (no layer 3)
- FCS-16: CRC-CCITT polynomial 0x8408 (bit-reversed), initial 0xFFFF, final complement

### KISS Framing
- FEND (0xC0) delimiters, FESC (0xDB) + TFEND (0xDC) / TFESC (0xDD) escaping
- Type byte: port (4 high bits) + command (4 low bits)
- Streaming decoder: 4-state machine (WAIT_FEND, WAIT_CMD, DATA, ESCAPE)

### AFSK1200 Modulator
- Bell 202: mark=1200 Hz, space=2200 Hz, 1200 baud
- Phase-continuous FSK with 32-bit phase accumulator
- NRZI encoding: 0=toggle tone, 1=hold
- HDLC framing: 25 preamble flags, bit stuffing (zero after five ones), FCS, 3 postamble flags
- Fractional bit accumulator for drift-free timing at non-integer samples-per-bit

### AFSK1200 Demodulator
- Quadrature mixing with 256-entry sine/cosine lookup oscillators
- Root Raised Cosine FIR low-pass (2.80 symbols, α=0.20, taps scale with sample rate)
- Hamming-windowed FIR bandpass prefilter (1014–2386 Hz, ~8 symbol width)
- 8 parallel slicers with space_gain from 0.5× to 4.0× (logarithmically spaced)
- Per-tone AGC: independent peak/valley envelope tracking, normalizes mark/space imbalance from FM de-emphasis
- 32-bit PLL with sqrt-scaled inertia (0.50 at 8 kHz, 0.74 at 48 kHz)
- Deferred-1-bit HDLC: 1-bits counted until 0 resolves flags/stuffing/data
- Single-bit FEC: on CRC failure, try flipping each bit
- Cross-slicer dedup: FNV-1a hash ring prevents duplicate frame delivery

### APRS-IS Client
- TCP with getaddrinfo (IPv4/IPv6)
- Login: `user CALL pass CODE vers libaprs 0.1.0 filter FILTER\r\n`
- Passcode: XOR hash of uppercase callsign (strips SSID)
- Line buffer with CRLF/LF handling, server comment filtering
- FNV-1a hash ring for line-level duplicate suppression (128 slots)

## License

MIT — see [LICENSE](LICENSE).
