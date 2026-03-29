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
- **RTL-SDR Monitor** — Live APRS packet decoding from an RTL-SDR dongle on 144.390 MHz (optional, requires librtlsdr)

### Supported Configurations

| Parameter | Values |
|-----------|--------|
| Sample rates (modem) | 8000, 16000, 32000, 48000 Hz |
| Position formats | Uncompressed, compressed (Base91), Mic-E |
| Packet types | Position, message, status, object, item, weather, telemetry, query |
| KISS ports | 0-15 |
| AX.25 SSIDs | 0-15 |
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

Compiles with `-Wall -Wextra -pedantic` in C99 strict mode. Requires only a C99 compiler, libc, and libm.

If librtlsdr is installed, `aprs_sdr` is also built automatically.

### Debian Packages

```bash
dpkg-buildpackage -us -uc -b
```

Produces `libaprs0` (shared library), `libaprs-dev` (headers + pkg-config), and `libaprs0-dbgsym` (debug symbols).

## API

All public functions return `aprs_err_t`. Headers are under `include/libaprs/`, included as `<libaprs/aprs.h>` etc.

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

Supported data type identifiers:

| DTI | Type |
|-----|------|
| `!` | Position (no timestamp) |
| `=` | Position (no timestamp, messaging capable) |
| `/` | Position (with timestamp) |
| `@` | Position (with timestamp, messaging capable) |
| `:` | Message, ack, rej |
| `>` | Status |
| `;` | Object |
| `)` | Item |
| `_` | Weather (positionless) |
| `T` | Telemetry |
| `` ` `` `'` | Mic-E |
| `?` | Query |
| `}` | Third-party |

### APRS Building

```c
aprs_packet_t pkt;
char line[512];
const char *path[] = {"WIDE1-1", "WIDE2-1"};

/* Position packet */
aprs_build_position(&pkt, "N0CALL-9", "APRS", path, 2,
                    35.35, -97.5167, '/', '>', "OKC test");
aprs_format_tnc2(&pkt, line, sizeof(line));
printf("%s\n", line);
/* N0CALL-9>APRS,WIDE1-1,WIDE2-1:!3521.00N/09731.00W>OKC test */

/* Message packet */
aprs_build_message(&pkt, "N0CALL", "APRS", NULL, 0,
                   "W3ADO-1", "Hello!", "42");
aprs_format_tnc2(&pkt, line, sizeof(line));
/* N0CALL>APRS::W3ADO-1  :Hello!{42 */

/* Status packet */
aprs_build_status(&pkt, "N0CALL", "APRS", NULL, 0, "On the air");
aprs_format_tnc2(&pkt, line, sizeof(line));
/* N0CALL>APRS:>On the air */
```

### AX.25 UI Frames

```c
#include <libaprs/ax25.h>

/* Pack/unpack callsign to 7-byte AX.25 address */
uint8_t addr[7];
ax25_pack_address("N0CALL-9", addr, true, false);

char text[APRS_CALLSIGN_MAX];
bool hbit;
ax25_unpack_address(addr, text, &hbit);

/* Encode frame to binary (for KISS) */
ax25_ui_frame_t frame;
ax25_ui_frame_init(&frame);
snprintf(frame.dst.callsign, sizeof(frame.dst.callsign), "APRS");
snprintf(frame.src.callsign, sizeof(frame.src.callsign), "N0CALL-9");
memcpy(frame.info, ">status", 7);
frame.info_len = 7;

uint8_t buf[512];
size_t written;
ax25_encode_ui_frame(&frame, buf, sizeof(buf), &written);

/* Decode binary frame back */
ax25_ui_frame_t decoded;
ax25_decode_ui_frame(buf, written, &decoded);

/* Convert between APRS packets and AX.25 frames */
aprs_packet_t pkt;
ax25_from_aprs(&pkt, &frame);
ax25_to_aprs(&frame, &pkt);

/* FCS-16 checksum */
uint16_t fcs = ax25_fcs16(buf, written);
```

### KISS Framing

```c
#include <libaprs/kiss.h>

/* Single-frame encode */
uint8_t kiss_buf[256];
size_t kiss_len;
kiss_encode(0, KISS_CMD_DATA_FRAME, ax25_data, ax25_len,
            kiss_buf, sizeof(kiss_buf), &kiss_len);

/* Single-frame decode */
uint8_t port;
kiss_cmd_t cmd;
uint8_t payload[256];
size_t payload_len;
kiss_decode(kiss_buf, kiss_len, &port, &cmd,
            payload, sizeof(payload), &payload_len);

/* Streaming decoder for serial/TCP byte streams */
void on_frame(uint8_t port, kiss_cmd_t cmd,
              const uint8_t *payload, size_t len, void *user) {
    /* complete KISS frame received */
}

kiss_decoder_t dec;
kiss_decoder_init(&dec, on_frame, NULL);

/* Feed arbitrary byte chunks — callback fires when frames complete */
kiss_decoder_feed(&dec, incoming_bytes, nbytes);
kiss_decoder_feed(&dec, more_bytes, more_len);
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

/* Callback-driven receive loop */
void on_line(const char *line, void *user) {
    aprs_packet_t pkt;
    if (aprs_parse_tnc2(line, &pkt) == APRS_OK) {
        printf("%s: %s\n", pkt.src.callsign,
               aprs_packet_type_name(pkt.type));
    }
}
aprsis_set_line_callback(c, on_line, NULL);
aprsis_run(c);  /* blocks until disconnect */

/* Or polling mode */
char line[1024];
size_t nread;
aprsis_read_line(c, line, sizeof(line), &nread);

/* Send a packet upstream */
aprsis_send_line(c, "N0CALL>APRS:>On the air");

/* Duplicate suppression */
aprsis_dedup_t dedup;
aprsis_dedup_init(&dedup);
if (!aprsis_dedup_check(&dedup, line)) {
    /* new packet */
}

/* Passcode computation */
int code = aprsis_passcode("N0CALL");

aprsis_disconnect(c);
aprsis_client_destroy(c);
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
aprs_transport_t tp;
aprs_tcp_opts_t tcp_opts = { .host = "localhost", .port = 8001 };
aprs_transport_tcp_create(&tp, &tcp_opts);
aprs_transport_open(&tp);

/* Generic read/write (works with any backend) */
uint8_t buf[512];
size_t nread, nwritten;
aprs_transport_read(&tp, buf, sizeof(buf), &nread);
aprs_transport_write(&tp, buf, nread, &nwritten);

aprs_transport_close(&tp);
aprs_transport_serial_destroy(&tp);  /* or aprs_transport_tcp_destroy */
```

### AFSK1200 Modem

```c
#include <libaprs/modem.h>

/* Modulate: AX.25 frame bytes to audio samples */
afsk_mod_t *mod = afsk_mod_create(48000);  /* sample rate */
int16_t audio[200000];
size_t nsamples;
afsk_mod_frame(mod, ax25_data, ax25_len, audio, 200000, &nsamples);
afsk_mod_destroy(mod);

/* Demodulate: audio samples to AX.25 frames via callback */
void on_frame(const uint8_t *frame, size_t len, void *user) {
    ax25_ui_frame_t ax;
    aprs_packet_t pkt;
    ax25_decode_ui_frame(frame, len, &ax);
    ax25_to_aprs(&ax, &pkt);
    printf("%s\n", pkt.src.callsign);
}

afsk_demod_t *demod = afsk_demod_create(48000, on_frame, NULL);
afsk_demod_feed(demod, audio, nsamples);  /* may call on_frame */
afsk_demod_destroy(demod);
```

### WAV Reader

```c
#include <libaprs/wav.h>

wav_reader_t wav;
wav_open(&wav, "recording.wav");
printf("Rate: %d Hz, %d-bit, %d channels\n",
       wav.sample_rate, wav.bits_per_sample, wav.channels);

int16_t buf[4096];
size_t nread;
while (wav_read(&wav, buf, 4096, &nread) == APRS_OK && nread > 0) {
    afsk_demod_feed(demod, buf, nread);
}

wav_close(&wav);
```

Supports 8-bit and 16-bit PCM, mono and stereo (stereo is averaged to mono), any sample rate.

### Station Database

```c
#include <libaprs/station.h>

/* Create database, update with packets */
aprs_station_db_t *db = aprs_station_db_create(256);
aprs_station_db_update(db, &pkt, time(NULL));

/* Look up a station */
const aprs_station_t *st = aprs_station_db_find(db, "N0CALL");
if (st) {
    printf("%s: %d packets, last %.4f/%.4f\n",
           st->callsign, st->packet_count,
           st->latitude, st->longitude);
}

/* List all stations */
aprs_station_t list[256];
size_t count;
aprs_station_db_list(db, list, 256, &count);

/* Expire stations not heard in 30 minutes */
size_t removed = aprs_station_db_expire(db, time(NULL) - 1800);

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

/* Frame dedup (for RF) */
aprs_frame_dedup_t dedup;
aprs_frame_dedup_init(&dedup);
if (!aprs_frame_dedup_check(&dedup, frame_bytes, frame_len)) {
    /* new frame */
}

aprs_station_db_destroy(db);
```

### Streaming Usage

All decoders are designed for streaming. Feed successive buffers of any size and internal state is maintained across calls:

```c
afsk_demod_t *demod = afsk_demod_create(48000, on_frame, NULL);
kiss_decoder_t kiss;
kiss_decoder_init(&kiss, on_kiss_frame, NULL);

while (have_audio()) {
    int16_t frame[160];
    read_audio(frame, 160);
    afsk_demod_feed(demod, frame, 160);
}

while (have_serial_data()) {
    uint8_t buf[64];
    size_t n = read_serial(buf, 64);
    kiss_decoder_feed(&kiss, buf, n);
}
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

Benchmarked against Dire Wolf using `gen_packets -n 100` (100 frames with increasing noise). Cross-validated: Dire Wolf decodes all libaprs-modulated frames, and libaprs decodes all Dire Wolf-modulated frames.

| Sample Rate | Dire Wolf | libaprs | Result |
|-------------|-----------|---------|--------|
| 8000 Hz | 24 | 25 | **104%** |
| 16000 Hz | 38 | 40 | **105%** |
| 32000 Hz | 62 | 64 | **103%** |
| 48000 Hz | 70 | 75 | **107%** |

The demodulator uses:
- I/Q correlator with continuous-phase 256-entry sine/cosine lookup oscillators
- Root Raised Cosine FIR low-pass filter (2.8 symbol width, rolloff 0.20)
- Hamming-windowed FIR bandpass prefilter (1014-2386 Hz)
- 8-way multi-slicer with logarithmically spaced space\_gain (0.5x-4.0x)
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

APRS parse/format/build tests:
  parse ! position packet                            PASS
  parse = position packet (msgcap)                   PASS
  parse / position packet (with DHM timestamp)       PASS
  parse @ position packet (ts + msgcap)              PASS
  parse / position with HMS timestamp                PASS
  parse position south/east hemisphere               PASS
  parse position 0,0                                 PASS
  parse position preserves path                      PASS
  parse digipeated path with *                       PASS
  parse message packet                               PASS
  parse message without msgno                        PASS
  parse message ack                                  PASS
  parse message rej                                  PASS
  parse status packet                                PASS
  parse src and dst callsigns                        PASS
  reject missing > in header                         PASS
  reject missing : delimiter                         PASS
  reject NULL input                                  PASS
  reject empty callsign                              PASS
  max path overflow                                  PASS
  reject truncated position                          PASS
  reject bad latitude direction                      PASS
  reject bad longitude direction                     PASS
  detect object packet                               PASS
  detect item packet                                 PASS
  detect query packet                                PASS
  detect telemetry packet                            PASS
  unknown packet type                                PASS
  format status round-trip                           PASS
  format position round-trip                         PASS
  format message round-trip                          PASS
  format buffer overflow                             PASS
  build position packet                              PASS
  build position then parse round-trip               PASS
  build position rejects bad lat/lon                 PASS
  build message packet                               PASS
  build message without msgno                        PASS
  build status packet                                PASS
  build rejects NULL args                            PASS
  callsign validation                                PASS
  latlon validation                                  PASS
  packet type name                                   PASS
  aprs_strerror coverage                             PASS
  real APRS-IS position: W3ADO-1                     PASS
  real APRS-IS message with path                     PASS
  real APRS-IS status                                PASS
  parse compressed position (!)                      PASS
  parse compressed position (= msgcap)               PASS
  parse compressed with timestamp (@)                PASS
  parse Mic-E packet                                 PASS
  Mic-E with ' DTI                                   PASS
  parse live object                                  PASS
  parse killed object                                PASS
  object name trimmed                                PASS
  parse live item                                    PASS
  parse killed item                                  PASS
  parse positionless weather                         PASS
  weather h00 means 100%                             PASS
  parse telemetry                                    PASS
  telemetry with just sequence                       PASS
  real compressed position from APRS-IS              PASS
  real object from APRS-IS                           PASS

KISS encode/decode/streaming tests:
  KISS encode simple data frame                      PASS
  KISS decode simple data frame                      PASS
  KISS encode with FEND in payload                   PASS
  KISS decode FEND escape                            PASS
  KISS encode with FESC in payload                   PASS
  KISS decode FESC escape                            PASS
  KISS encode port 3 TXDELAY                         PASS
  KISS decode port 3 TXDELAY                         PASS
  KISS encode rejects port > 15                      PASS
  KISS encode rejects tiny buffer                    PASS
  KISS decode rejects missing FEND                   PASS
  KISS decode rejects truncated escape               PASS
  KISS decode rejects NULL args                      PASS
  KISS encode empty payload                          PASS
  KISS decode empty payload                          PASS
  stream: single complete frame                      PASS
  stream: two frames in one chunk                    PASS
  stream: frame split across two feeds               PASS
  stream: split in middle of escape sequence         PASS
  stream: garbage before first FEND ignored          PASS
  stream: consecutive FENDs between frames           PASS
  stream: byte-at-a-time feeding                     PASS
  stream: reset clears state                         PASS
  stream: TXDELAY command frame                      PASS
  stream: feed NULL with len 0 is OK                 PASS
  stream: feed rejects NULL with nonzero len         PASS

AX.25 tests:
  ax25_ui_frame_init defaults                        PASS
  pack address N0CALL (no SSID)                      PASS
  pack address N0CALL-9                              PASS
  pack address with extension bit                    PASS
  pack address with H bit                            PASS
  pack short callsign (AB1) pads with spaces         PASS
  pack address with star sets H bit                  PASS
  unpack address round-trip                          PASS
  unpack address with H bit                          PASS
  unpack SSID 0 has no dash                          PASS
  pack address rejects too-long callsign             PASS
  pack address rejects SSID > 15                     PASS
  encode minimal UI frame (no path, no info)         PASS
  encode UI frame with path and info                 PASS
  decode UI frame round-trip (no path)               PASS
  decode UI frame round-trip (with path)             PASS
  decode preserves H-bit as * on path                PASS
  decode known binary AX.25 frame                    PASS
  decode known binary frame with path                PASS
  encode then verify binary matches expected         PASS
  AX.25 frame through KISS round-trip                PASS
  ax25_from_aprs conversion                          PASS
  ax25_to_aprs conversion                            PASS
  APRS -> AX.25 binary -> APRS full round-trip       PASS
  FCS-16 known vector: 123456789                     PASS
  FCS-16 empty data                                  PASS
  FCS-16 on encoded frame is deterministic           PASS
  encode rejects NULL frame                          PASS
  decode rejects too-short data                      PASS
  decode rejects non-UI control byte                 PASS
  ax25_from_aprs rejects NULL                        PASS
  ax25_to_aprs rejects NULL                          PASS
  encode buffer overflow                             PASS

Transport tests:
  transport dispatch rejects NULL                    PASS
  transport dispatch rejects missing function ptrs   PASS
  pipe transport open/write/read/close               PASS
  KISS frame through pipe transport                  PASS
  serial transport create/destroy lifecycle          PASS
  serial transport create rejects NULL               PASS
  TCP transport create/destroy lifecycle             PASS
  TCP transport create rejects NULL                  PASS

APRS-IS tests:
  passcode for N0CALL                                PASS
  passcode strips SSID                               PASS
  passcode is case insensitive                       PASS
  passcode for NULL returns -1                       PASS
  passcode for WB4APR                                PASS
  format login line with filter                      PASS
  format login line without filter                   PASS
  format login default passcode                      PASS
  format login overflow                              PASS
  format login rejects NULL                          PASS
  client create and destroy                          PASS
  connect rejects NULL params                        PASS
  connect rejects missing host                       PASS
  send_line rejects when not connected               PASS
  read_line rejects when not connected               PASS
  read_line with pipe mock: simple line              PASS
  read_line with pipe: two lines in one write        PASS
  read_line with pipe: line split across reads       PASS
  read_line strips bare LF                           PASS
  read_line returns IO error on close                PASS
  run loop filters comments                          PASS
  run loop passes comments when enabled              PASS
  dedup: first occurrence is not duplicate           PASS
  dedup: second occurrence is duplicate              PASS
  dedup: different lines are not duplicates          PASS
  dedup: ring wraps and evicts old entries           PASS
  dedup: NULL inputs handled                         PASS
  send_line appends CRLF                             PASS

Station state tests:
  station db create and destroy                      PASS
  station db update creates entry                    PASS
  station db update increments count                 PASS
  station db tracks position                         PASS
  station db multiple stations                       PASS
  station db list                                    PASS
  station db expire                                  PASS
  station db rejects NULL                            PASS
  msg tracker init and next_seq                      PASS
  msg tracker send and ack                           PASS
  msg tracker reject                                 PASS
  msg tracker ack unknown msgno                      PASS
  msg tracker multiple messages                      PASS
  station to JSON                                    PASS
  station to JSON without position                   PASS
  packet to JSON                                     PASS
  JSON rejects NULL                                  PASS
  JSON overflow                                      PASS
  frame dedup first is not duplicate                 PASS
  frame dedup second is duplicate                    PASS
  frame dedup different frames                       PASS
  frame dedup ring eviction                          PASS
  frame dedup NULL handling                          PASS

AFSK1200 modem tests:
  modulator create and destroy                       PASS
  modulator rejects bad sample rate                  PASS
  demodulator create and destroy                     PASS
  modulate produces samples                          PASS
  modulate -> demodulate round-trip                  PASS
  round-trip with path                               PASS
  full stack: build -> modulate -> demod -> parse    PASS
  round-trip at 48000 Hz                             PASS
  mod_frame rejects NULL                             PASS
  demod_feed rejects NULL                            PASS

==================
Results: 190/190 passed
```

Tests cover:
- TNC2 parse/format round-trips for all packet types (position, message, status, object, item, weather, telemetry)
- Compressed position and Mic-E decoding with known vectors
- Real APRS-IS packet examples from live traffic
- AX.25 address packing/unpacking with SSID, H-bit, extension bit
- AX.25 binary frame encode/decode with hand-crafted test vectors
- Full APRS to AX.25 to KISS to transport to KISS to AX.25 to APRS round-trip
- KISS streaming decoder (split frames, split escapes, garbage tolerance, byte-at-a-time)
- APRS-IS login formatting, passcode computation (known vectors), line buffering (pipe mock server), comment filtering, duplicate suppression
- Station database create/update/find/list/expire, message tracker ack/rej, JSON export
- AFSK1200 modulate/demodulate round-trip at multiple sample rates, full-stack end-to-end
- Cross-validation with Dire Wolf: 5/5 packets decoded at every sample rate in both directions

## Example Programs

| Program | Description |
|---------|-------------|
| `example_parse_packet` | Parse TNC2 lines and print structured fields |
| `example_build_position` | Build position and message packets, format as TNC2 |
| `example_kiss_bridge` | Bridge TNC2 text on stdin/stdout to KISS frames on serial or TCP |
| `aprs_decode` | Decode APRS packets from WAV audio files (comparable to Dire Wolf `atest`) |
| `aprs_sdr` | Live APRS monitor using an RTL-SDR dongle (requires librtlsdr) |

```bash
# Decode a WAV file
./aprs_decode recording.wav

# Bridge to a TNC on serial
./kiss_bridge serial /dev/ttyUSB0 9600

# Bridge to Dire Wolf on TCP
./kiss_bridge tcp localhost 8001

# Live SDR monitor on 144.390 MHz
./aprs_sdr
./aprs_sdr -f 144.800            # Europe
./aprs_sdr -r 32000 -s 240000    # custom audio/SDR rates
./aprs_sdr -g 400                # manual gain 40.0 dB
```

## Project Structure

```
libaprs/
  include/libaprs/
    aprs.h              Public API: parsing, formatting, building
    aprsis.h            APRS-IS client
    ax25.h              AX.25 UI frame encode/decode
    error.h             Error codes and aprs_strerror()
    kiss.h              KISS framing and streaming decoder
    modem.h             AFSK1200 modulator and demodulator
    station.h           Station database, message tracker, JSON, dedup
    transport.h         Serial and TCP transport backends
    types.h             Core structs (packet, position, message, etc.)
    version.h           Version constants
    wav.h               WAV file reader
  src/
    aprs.c              TNC2 parser, all packet type decoders, builders
    aprsis.c            APRS-IS TCP client, line buffering, passcode, dedup
    ax25.c              AX.25 address packing, frame encode/decode, FCS-16
    error.c             Error string table
    kiss.c              KISS encode/decode, streaming state machine
    modem.c             AFSK1200 modulator and demodulator (I/Q + RRC + PLL)
    station.c           Station DB, message tracker, JSON export, frame dedup
    transport.c         Serial (termios) and TCP (sockets) backends
    wav.c               PCM WAV file parser
  tests/
    test_main.c         Test harness (test_begin/test_assert/test_end)
    test_aprs.c         APRS parser and builder tests (62 tests)
    test_aprsis.c       APRS-IS client tests with pipe mock server (28 tests)
    test_ax25.c         AX.25 binary frame and address tests (33 tests)
    test_kiss.c         KISS encode/decode and streaming tests (26 tests)
    test_modem.c        AFSK1200 round-trip tests (10 tests)
    test_station.c      Station DB, message tracker, JSON, dedup tests (23 tests)
    test_transport.c    Transport layer tests with pipe loopback (8 tests)
  examples/
    parse_packet.c      Parse and display TNC2 packets
    build_position.c    Build and format APRS packets
    kiss_bridge.c       TNC2 to KISS bridge for serial/TCP
    aprs_decode.c       WAV file AFSK decoder
    aprs_sdr.c          Live RTL-SDR APRS monitor
  debian/               Debian packaging files
  .github/workflows/    CI and release workflows
  configure.ac          Autoconf configuration
  Makefile.am           Top-level automake
  autogen.sh            Bootstrap script
  libaprs.pc.in         pkg-config template
```

## Technical Details

### APRS Parser
- TNC2 format: `SRC>DST,PATH:INFO` with data type identifier as first char of INFO
- Uncompressed position: `DDmm.mmN/DDDmm.mmW` with N/S and E/W hemisphere
- Compressed position: Base91 lat/lon in 13 bytes with optional course/speed/altitude
- Mic-E: latitude encoded in destination address (6 chars), longitude/speed/course in info field bytes
- Timestamps: DHM (z=UTC, /=local) and HMS (h=UTC) formats
- Messages: 9-char space-padded addressee, optional {msgno, ack/rej detection
- Objects: 9-char name, live/killed marker, timestamp, position
- Items: 3-9 char name, live/killed marker, position
- Weather: letter-coded fields (c=wind dir, s=speed, g=gust, t=temp F, r/p/P=rain, h=humidity, b=pressure)
- Telemetry: sequence number, 5 analog channels, 8-bit digital from binary string

### AX.25 Encoding
- 7-byte address fields: 6 chars left-shifted by 1, space-padded, plus SSID byte
- SSID byte: H-bit (7), reserved 11 (6:5), SSID (4:1), extension bit (0)
- Trailing `*` in text callsign sets H-bit (has-been-repeated)
- Control field: 0x03 (UI), PID: 0xF0 (no layer 3)
- FCS-16: CRC-CCITT polynomial 0x8408 (bit-reversed), initial 0xFFFF, final complement

### KISS Framing
- FEND (0xC0) delimiters, FESC (0xDB) + TFEND (0xDC) / TFESC (0xDD) escaping
- Type byte: port (4 high bits) + command (4 low bits)
- Streaming decoder: 4-state machine (WAIT\_FEND, WAIT\_CMD, DATA, ESCAPE)
- Handles split frames, split escape sequences, inter-frame garbage, consecutive FENDs

### AFSK1200 Modulator
- Bell 202: mark=1200 Hz, space=2200 Hz, 1200 baud
- Phase-continuous FSK with fractional bit accumulator for drift-free timing
- NRZI encoding: 0=toggle tone, 1=hold
- HDLC framing: 25 preamble flags, bit stuffing (zero after five ones), FCS, 3 postamble flags

### AFSK1200 Demodulator
- Quadrature mixing with 256-entry sine/cosine lookup oscillators
- Root Raised Cosine FIR low-pass (2.80 symbols, rolloff 0.20, taps scale with sample rate)
- Hamming-windowed FIR bandpass prefilter (1014-2386 Hz, ~8 symbol width)
- 8 parallel slicers with space\_gain from 0.5x to 4.0x (logarithmically spaced)
- Per-tone AGC: independent peak/valley envelope tracking, normalizes mark/space imbalance from FM de-emphasis
- 32-bit PLL with sqrt-scaled inertia (0.50 at 8 kHz to 0.74 at 48 kHz)
- Deferred-1-bit HDLC: 1-bits counted until 0-bit resolves as flag (6 ones), stuffing (5 ones), or data
- Single-bit FEC: on CRC failure, try flipping each bit individually
- Cross-slicer dedup: FNV-1a hash ring prevents duplicate frame delivery

### APRS-IS Client
- TCP with getaddrinfo (IPv4/IPv6 dual-stack)
- Login line: `user CALL pass CODE vers libaprs 0.1.0 filter FILTER\r\n`
- Passcode: XOR hash of uppercase callsign (strips SSID)
- Internal line buffer with CRLF/LF handling, automatic server comment filtering
- FNV-1a hash ring for line-level duplicate suppression (128 slots)

### RTL-SDR Monitor
- RTL-SDR IQ capture at 240 kHz (configurable)
- FM discriminator via atan2(cross, dot) on I/Q samples
- 75 us de-emphasis IIR low-pass filter
- Fractional decimation to audio rate (configurable, default 48000 Hz)
- FM-inverted squelch with debounce for activity detection
- Signal/noise reporting on stderr, decoded packets on stdout

## License

MIT -- see [LICENSE](LICENSE).
