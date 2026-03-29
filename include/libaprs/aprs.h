#ifndef LIBAPRS_APRS_H
#define LIBAPRS_APRS_H

#include <stddef.h>
#include <stdbool.h>
#include "error.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

void aprs_packet_init(aprs_packet_t *pkt);
void aprs_packet_reset(aprs_packet_t *pkt);

aprs_err_t aprs_parse_tnc2(const char *line, aprs_packet_t *out);
aprs_err_t aprs_format_tnc2(const aprs_packet_t *pkt, char *buf, size_t buflen);

aprs_err_t aprs_build_position(
    aprs_packet_t *pkt,
    const char *src,
    const char *dst,
    const char *path[],
    size_t path_len,
    double lat,
    double lon,
    char symbol_table,
    char symbol_code,
    const char *comment
);

aprs_err_t aprs_build_message(
    aprs_packet_t *pkt,
    const char *src,
    const char *dst,
    const char *path[],
    size_t path_len,
    const char *addressee,
    const char *text,
    const char *msgno
);

aprs_err_t aprs_build_status(
    aprs_packet_t *pkt,
    const char *src,
    const char *dst,
    const char *path[],
    size_t path_len,
    const char *text
);

bool aprs_is_valid_callsign(const char *s);
bool aprs_is_valid_path(const char *s);
bool aprs_is_valid_latlon(double lat, double lon);
const char *aprs_packet_type_name(aprs_packet_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* LIBAPRS_APRS_H */
