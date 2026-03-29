/*
 * transport.c — generic transport + serial and TCP backends
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <termios.h>
#include <stdio.h>

#include "libaprs/transport.h"

/* ------------------------------------------------------------------ */
/* generic dispatch                                                    */
/* ------------------------------------------------------------------ */

aprs_err_t aprs_transport_open(aprs_transport_t *t)
{
    if (!t || !t->open) return APRS_ERR_INVALID_ARG;
    return t->open(t);
}

aprs_err_t aprs_transport_close(aprs_transport_t *t)
{
    if (!t || !t->close) return APRS_ERR_INVALID_ARG;
    return t->close(t);
}

aprs_err_t aprs_transport_read(aprs_transport_t *t, uint8_t *buf,
                                size_t maxlen, size_t *nread)
{
    if (!t || !t->read) return APRS_ERR_INVALID_ARG;
    return t->read(t, buf, maxlen, nread);
}

aprs_err_t aprs_transport_write(aprs_transport_t *t, const uint8_t *buf,
                                 size_t len, size_t *nwritten)
{
    if (!t || !t->write) return APRS_ERR_INVALID_ARG;
    return t->write(t, buf, len, nwritten);
}

/* ================================================================== */
/* serial transport                                                    */
/* ================================================================== */

typedef struct {
    int fd;
    char device[256];
    int baud;
    struct termios orig_termios;
    int have_orig;
} serial_ctx_t;

static speed_t baud_to_speed(int baud)
{
    switch (baud) {
    case 1200:   return B1200;
    case 2400:   return B2400;
    case 4800:   return B4800;
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    default:     return B9600;
    }
}

static aprs_err_t serial_open(aprs_transport_t *t)
{
    serial_ctx_t *ctx = (serial_ctx_t *)t->ctx;
    struct termios tty;
    speed_t speed;

    ctx->fd = open(ctx->device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (ctx->fd < 0) return APRS_ERR_IO;

    /* save original termios so we can restore on close */
    if (tcgetattr(ctx->fd, &ctx->orig_termios) == 0)
        ctx->have_orig = 1;

    memset(&tty, 0, sizeof(tty));
    speed = baud_to_speed(ctx->baud);

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    tty.c_cflag = speed | CS8 | CLOCAL | CREAD;
    tty.c_iflag = IGNPAR;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;  /* 100ms timeout */

    tcflush(ctx->fd, TCIFLUSH);
    if (tcsetattr(ctx->fd, TCSANOW, &tty) < 0) {
        close(ctx->fd);
        ctx->fd = -1;
        return APRS_ERR_IO;
    }

    /* switch back to blocking after setup */
    {
        int flags = fcntl(ctx->fd, F_GETFL, 0);
        if (flags >= 0)
            fcntl(ctx->fd, F_SETFL, flags & ~O_NONBLOCK);
    }

    return APRS_OK;
}

static aprs_err_t serial_close(aprs_transport_t *t)
{
    serial_ctx_t *ctx = (serial_ctx_t *)t->ctx;

    if (ctx->fd < 0) return APRS_OK;

    if (ctx->have_orig)
        tcsetattr(ctx->fd, TCSANOW, &ctx->orig_termios);

    close(ctx->fd);
    ctx->fd = -1;
    return APRS_OK;
}

static aprs_err_t serial_read(aprs_transport_t *t, uint8_t *buf,
                               size_t maxlen, size_t *nread)
{
    serial_ctx_t *ctx = (serial_ctx_t *)t->ctx;
    ssize_t n;

    if (ctx->fd < 0) return APRS_ERR_STATE;

    n = read(ctx->fd, buf, maxlen);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            *nread = 0;
            return APRS_OK;
        }
        return APRS_ERR_IO;
    }
    *nread = (size_t)n;
    return APRS_OK;
}

static aprs_err_t serial_write(aprs_transport_t *t, const uint8_t *buf,
                                size_t len, size_t *nwritten)
{
    serial_ctx_t *ctx = (serial_ctx_t *)t->ctx;
    ssize_t n;

    if (ctx->fd < 0) return APRS_ERR_STATE;

    n = write(ctx->fd, buf, len);
    if (n < 0) return APRS_ERR_IO;
    *nwritten = (size_t)n;
    return APRS_OK;
}

aprs_err_t aprs_transport_serial_create(aprs_transport_t *t,
                                        const aprs_serial_opts_t *opts)
{
    serial_ctx_t *ctx;

    if (!t || !opts || !opts->device) return APRS_ERR_INVALID_ARG;

    ctx = (serial_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return APRS_ERR_NOMEM;

    ctx->fd = -1;
    snprintf(ctx->device, sizeof(ctx->device), "%s", opts->device);
    ctx->baud = opts->baud > 0 ? opts->baud : 9600;

    t->ctx = ctx;
    t->open = serial_open;
    t->close = serial_close;
    t->read = serial_read;
    t->write = serial_write;

    return APRS_OK;
}

void aprs_transport_serial_destroy(aprs_transport_t *t)
{
    if (!t) return;
    free(t->ctx);
    t->ctx = NULL;
    t->open = t->close = NULL;
    t->read = NULL;
    t->write = NULL;
}

/* ================================================================== */
/* TCP transport                                                       */
/* ================================================================== */

typedef struct {
    int fd;
    char host[256];
    uint16_t port;
} tcp_ctx_t;

static aprs_err_t tcp_open(aprs_transport_t *t)
{
    tcp_ctx_t *ctx = (tcp_ctx_t *)t->ctx;
    struct addrinfo hints, *res, *rp;
    char portstr[8];
    int rv;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(portstr, sizeof(portstr), "%u", ctx->port);

    rv = getaddrinfo(ctx->host, portstr, &hints, &res);
    if (rv != 0) return APRS_ERR_IO;

    ctx->fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        ctx->fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (ctx->fd < 0) continue;
        if (connect(ctx->fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(ctx->fd);
        ctx->fd = -1;
    }
    freeaddrinfo(res);

    if (ctx->fd < 0) return APRS_ERR_IO;

    return APRS_OK;
}

static aprs_err_t tcp_close(aprs_transport_t *t)
{
    tcp_ctx_t *ctx = (tcp_ctx_t *)t->ctx;

    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
    return APRS_OK;
}

static aprs_err_t tcp_read(aprs_transport_t *t, uint8_t *buf,
                            size_t maxlen, size_t *nread)
{
    tcp_ctx_t *ctx = (tcp_ctx_t *)t->ctx;
    ssize_t n;

    if (ctx->fd < 0) return APRS_ERR_STATE;

    n = read(ctx->fd, buf, maxlen);
    if (n < 0) return APRS_ERR_IO;
    if (n == 0) return APRS_ERR_IO; /* EOF = remote closed */
    *nread = (size_t)n;
    return APRS_OK;
}

static aprs_err_t tcp_write(aprs_transport_t *t, const uint8_t *buf,
                             size_t len, size_t *nwritten)
{
    tcp_ctx_t *ctx = (tcp_ctx_t *)t->ctx;
    ssize_t n;

    if (ctx->fd < 0) return APRS_ERR_STATE;

    n = write(ctx->fd, buf, len);
    if (n < 0) return APRS_ERR_IO;
    *nwritten = (size_t)n;
    return APRS_OK;
}

aprs_err_t aprs_transport_tcp_create(aprs_transport_t *t,
                                     const aprs_tcp_opts_t *opts)
{
    tcp_ctx_t *ctx;

    if (!t || !opts || !opts->host) return APRS_ERR_INVALID_ARG;

    ctx = (tcp_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return APRS_ERR_NOMEM;

    ctx->fd = -1;
    snprintf(ctx->host, sizeof(ctx->host), "%s", opts->host);
    ctx->port = opts->port;

    t->ctx = ctx;
    t->open = tcp_open;
    t->close = tcp_close;
    t->read = tcp_read;
    t->write = tcp_write;

    return APRS_OK;
}

void aprs_transport_tcp_destroy(aprs_transport_t *t)
{
    if (!t) return;
    free(t->ctx);
    t->ctx = NULL;
    t->open = t->close = NULL;
    t->read = NULL;
    t->write = NULL;
}
