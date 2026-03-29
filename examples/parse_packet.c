#include <stdio.h>
#include "libaprs/aprs.h"

int main(void)
{
    const char *lines[] = {
        "N0CALL-9>APRS,WIDE1-1,WIDE2-1:!3521.00N/09731.00W>OKC test",
        "W3ADO-1>APRS:=3909.15N/07643.65W#Baltimore digi",
        "KJ4ERJ>APRS::N0CALL-9 :Hello World{42",
        "N0CALL>APRS:>On the air in OKC",
        NULL
    };

    for (int i = 0; lines[i]; i++) {
        aprs_packet_t pkt;
        aprs_err_t rc = aprs_parse_tnc2(lines[i], &pkt);

        printf("--- %s\n", lines[i]);
        if (rc != APRS_OK) {
            printf("    PARSE ERROR: %s\n\n", aprs_strerror(rc));
            continue;
        }

        printf("    type: %s\n", aprs_packet_type_name(pkt.type));
        printf("    src:  %s\n", pkt.src.callsign);
        printf("    dst:  %s\n", pkt.dst.callsign);

        if (pkt.type == APRS_PACKET_POSITION ||
            pkt.type == APRS_PACKET_POSITION_MSGCAP) {
            printf("    lat:  %.4f\n", pkt.data.position.latitude);
            printf("    lon:  %.4f\n", pkt.data.position.longitude);
            printf("    sym:  %c%c\n", pkt.data.position.symbol_table,
                   pkt.data.position.symbol_code);
            if (pkt.data.position.comment[0])
                printf("    cmt:  %s\n", pkt.data.position.comment);
        } else if (pkt.type == APRS_PACKET_MESSAGE) {
            printf("    to:   %s\n", pkt.data.message.addressee);
            printf("    msg:  %s\n", pkt.data.message.text);
            if (pkt.data.message.msgno[0])
                printf("    seq:  %s\n", pkt.data.message.msgno);
        } else if (pkt.type == APRS_PACKET_STATUS) {
            printf("    text: %s\n", pkt.data.status.text);
        }
        printf("\n");
    }

    return 0;
}
