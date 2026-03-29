# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

libaprs is a C99 library for APRS (Automatic Packet Reporting System) packet parsing, building, and transport. It targets amateur radio applications. Currently at v1.0.1 with stub implementations in several modules.

## Build

```bash
mkdir build && cd build
cmake .. && cmake --build .
```

Produces static library `libaprs.a` and two example binaries: `example_parse_packet`, `example_build_position`. No test framework is wired up yet.

## Architecture

Layered design — each layer is independent and has its own header/source pair:

1. **aprs** — Semantic APRS packets. TNC2 text parsing (`aprs_parse_tnc2`), formatting (`aprs_format_tnc2`), and builders for position/message/status packets. Packet type is determined by the first character of the info field.
2. **ax25** — AX.25 UI frame encode/decode with FCS-16 checksum. Currently a stub that copies raw bytes rather than producing real AX.25 binary frames.
3. **kiss** — KISS framing with FEND/FESC escape handling for TNC serial/TCP communication.
4. **aprsis** — APRS-IS internet gateway client. Stub — does not open real TCP sockets yet.
5. **transport** — Generic read/write abstraction (function-pointer based) for serial, TCP, pipes, or test harnesses.
6. **error** — Unified `aprs_err_t` error codes and `aprs_strerror()`.
7. **types** — Core structs shared across layers (`aprs_packet_t`, `aprs_position_t`, `aprs_message_t`, etc.).

## Key conventions

- All public functions return `aprs_err_t`. Check error codes, no exceptions.
- `aprs_packet_t.data` is a union — only one variant (position/message/status/weather/telemetry) is valid at a time, selected by `packet.type`.
- Buffer limits: callsigns 10 bytes, path max 8 hops, text/comment 256 bytes, info payload 512 bytes.
- TNC2 format: `SRC>DST,PATH1,PATH2:INFO` — this is the primary text representation.
- Builders validate inputs first, then populate both the structured union fields and the `raw_info` string.
- No external dependencies — only standard C library.
- C99 strict mode: no extensions (`CMAKE_C_EXTENSIONS OFF`).
- Public headers are under `include/libaprs/`, included as `<libaprs/aprs.h>` etc.
