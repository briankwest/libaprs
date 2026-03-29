/*
 * kiss_bridge — bridge between TNC2 text on stdin/stdout and KISS
 *               frames on a serial port or TCP socket.
 *
 * Usage:
 *   kiss_bridge serial /dev/ttyUSB0 9600
 *   kiss_bridge tcp    localhost    8001
 *
 * Text lines on stdin are parsed as TNC2, encoded to AX.25, wrapped
 * in KISS, and written to the transport.
 *
 * KISS frames read from the transport are decoded to AX.25, converted
 * to TNC2, and printed to stdout.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>

#include "libaprs/aprs.h"
#include "libaprs/ax25.h"
#include "libaprs/kiss.h"
#include "libaprs/transport.h"

/* called by the KISS streaming decoder for each received frame */
static void on_kiss_frame(uint8_t port, kiss_cmd_t cmd,
                          const uint8_t *payload, size_t payload_len,
                          void *user)
{
    ax25_ui_frame_t frame;
    aprs_packet_t pkt;
    char line[1024];

    (void)port;
    (void)user;

    if (cmd != KISS_CMD_DATA_FRAME) return;

    if (ax25_decode_ui_frame(payload, payload_len, &frame) != APRS_OK)
        return;

    if (ax25_to_aprs(&frame, &pkt) != APRS_OK)
        return;

    if (aprs_format_tnc2(&pkt, line, sizeof(line)) != APRS_OK)
        return;

    printf("%s\n", line);
    fflush(stdout);
}

/* read a TNC2 line from stdin, encode as KISS, write to transport */
static int send_line(const char *line, aprs_transport_t *tp)
{
    aprs_packet_t pkt;
    ax25_ui_frame_t frame;
    uint8_t ax_buf[AX25_FRAME_MAX];
    uint8_t kiss_buf[AX25_FRAME_MAX + 10];
    size_t ax_len, kiss_len, nw;

    if (aprs_parse_tnc2(line, &pkt) != APRS_OK) {
        fprintf(stderr, "parse error: %s\n", line);
        return -1;
    }

    if (ax25_from_aprs(&pkt, &frame) != APRS_OK) {
        fprintf(stderr, "ax25 convert error\n");
        return -1;
    }

    if (ax25_encode_ui_frame(&frame, ax_buf, sizeof(ax_buf), &ax_len)
        != APRS_OK) {
        fprintf(stderr, "ax25 encode error\n");
        return -1;
    }

    if (kiss_encode(0, KISS_CMD_DATA_FRAME, ax_buf, ax_len,
                    kiss_buf, sizeof(kiss_buf), &kiss_len) != APRS_OK) {
        fprintf(stderr, "kiss encode error\n");
        return -1;
    }

    if (aprs_transport_write(tp, kiss_buf, kiss_len, &nw) != APRS_OK) {
        fprintf(stderr, "transport write error\n");
        return -1;
    }

    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s serial <device> <baud>\n", prog);
    fprintf(stderr, "  %s tcp    <host>   <port>\n", prog);
}

int main(int argc, char **argv)
{
    aprs_transport_t tp;
    kiss_decoder_t dec;
    int tp_fd;

    if (argc < 4) {
        usage(argv[0]);
        return 1;
    }

    memset(&tp, 0, sizeof(tp));

    if (strcmp(argv[1], "serial") == 0) {
        aprs_serial_opts_t opts;
        opts.device = argv[2];
        opts.baud = atoi(argv[3]);
        if (aprs_transport_serial_create(&tp, &opts) != APRS_OK) {
            fprintf(stderr, "failed to create serial transport\n");
            return 1;
        }
    } else if (strcmp(argv[1], "tcp") == 0) {
        aprs_tcp_opts_t opts;
        opts.host = argv[2];
        opts.port = (uint16_t)atoi(argv[3]);
        if (aprs_transport_tcp_create(&tp, &opts) != APRS_OK) {
            fprintf(stderr, "failed to create TCP transport\n");
            return 1;
        }
    } else {
        usage(argv[0]);
        return 1;
    }

    if (aprs_transport_open(&tp) != APRS_OK) {
        fprintf(stderr, "failed to open transport\n");
        return 1;
    }

    /* get underlying fd for select — it's the first member of both
     * serial_ctx_t and tcp_ctx_t */
    tp_fd = *(int *)tp.ctx;

    kiss_decoder_init(&dec, on_kiss_frame, NULL);

    fprintf(stderr, "kiss_bridge: connected, type TNC2 lines to send\n");

    for (;;) {
        fd_set rfds;
        int nfds, rv;

        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(tp_fd, &rfds);
        nfds = (tp_fd > STDIN_FILENO ? tp_fd : STDIN_FILENO) + 1;

        rv = select(nfds, &rfds, NULL, NULL, NULL);
        if (rv < 0) break;

        /* data from transport — KISS frames */
        if (FD_ISSET(tp_fd, &rfds)) {
            uint8_t rbuf[512];
            size_t nr;

            if (aprs_transport_read(&tp, rbuf, sizeof(rbuf), &nr) != APRS_OK)
                break;
            if (nr > 0)
                kiss_decoder_feed(&dec, rbuf, nr);
        }

        /* text from stdin */
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char line[512];
            if (!fgets(line, sizeof(line), stdin))
                break;
            /* strip newline */
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0])
                send_line(line, &tp);
        }
    }

    aprs_transport_close(&tp);

    /* destroy */
    if (strcmp(argv[1], "serial") == 0)
        aprs_transport_serial_destroy(&tp);
    else
        aprs_transport_tcp_destroy(&tp);

    return 0;
}
