#include <stdio.h>
#include "libaprs/aprs.h"

int main(void)
{
    aprs_packet_t pkt;
    char line[512];
    const char *path[] = {"WIDE1-1", "WIDE2-1"};
    aprs_err_t rc;

    /* Build a position packet for Oklahoma City */
    rc = aprs_build_position(
        &pkt,
        "N0CALL-9",
        "APRS",
        path, 2,
        35.35,       /* latitude  (N) */
        -97.5166667, /* longitude (W) */
        '/', '>',
        "OKC test"
    );
    if (rc != APRS_OK) {
        fprintf(stderr, "build failed: %s\n", aprs_strerror(rc));
        return 1;
    }

    rc = aprs_format_tnc2(&pkt, line, sizeof(line));
    if (rc != APRS_OK) {
        fprintf(stderr, "format failed: %s\n", aprs_strerror(rc));
        return 1;
    }

    printf("%s\n", line);

    /* Also build and show a message packet */
    rc = aprs_build_message(
        &pkt,
        "N0CALL-9",
        "APRS",
        path, 2,
        "W3ADO-1",
        "Hello from libaprs!",
        "1"
    );
    if (rc != APRS_OK) {
        fprintf(stderr, "build msg failed: %s\n", aprs_strerror(rc));
        return 1;
    }

    rc = aprs_format_tnc2(&pkt, line, sizeof(line));
    if (rc != APRS_OK) {
        fprintf(stderr, "format failed: %s\n", aprs_strerror(rc));
        return 1;
    }

    printf("%s\n", line);

    return 0;
}
