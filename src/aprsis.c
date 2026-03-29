/*
 * aprsis.c — APRS-IS internet gateway client
 *
 * Real TCP connection, proper login line, line-buffered I/O,
 * comment filtering, callback-driven receive loop, and dedup.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "libaprs/aprsis.h"

/* ------------------------------------------------------------------ */
/* client struct                                                       */
/* ------------------------------------------------------------------ */

struct aprsis_client {
    int fd;
    int connected;
    int running;
    int pass_comments;

    aprsis_line_cb cb;
    void *cb_user;

    /* connection params (saved for reconnect) */
    char host[128];
    uint16_t port;
    char login[32];
    char passcode[16];
    char filter[256];

    /* line buffer for partial reads */
    char linebuf[APRSIS_LINE_MAX];
    size_t linelen;
};

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

aprsis_client_t *aprsis_client_create(void)
{
    aprsis_client_t *c = (aprsis_client_t *)calloc(1, sizeof(*c));
    if (c) c->fd = -1;
    return c;
}

void aprsis_client_destroy(aprsis_client_t *c)
{
    if (!c) return;
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    free(c);
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void safe_copy(char *dst, size_t dstsz, const char *src)
{
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, dstsz, "%s", src);
}

/* ------------------------------------------------------------------ */
/* passcode                                                            */
/* ------------------------------------------------------------------ */

int aprsis_passcode(const char *callsign)
{
    int hash = 0x73E2;  /* magic seed */
    int i = 0;
    char upper[16];
    const char *dash;
    size_t len;

    if (!callsign) return -1;

    /* strip SSID */
    dash = strchr(callsign, '-');
    len = dash ? (size_t)(dash - callsign) : strlen(callsign);
    if (len >= sizeof(upper)) len = sizeof(upper) - 1;

    /* uppercase */
    for (i = 0; i < (int)len; i++)
        upper[i] = (char)toupper((unsigned char)callsign[i]);
    upper[len] = '\0';

    for (i = 0; i < (int)len; i += 2) {
        hash ^= (unsigned char)upper[i] << 8;
        if (i + 1 < (int)len)
            hash ^= (unsigned char)upper[i + 1];
    }

    return hash & 0x7FFF;
}

/* ------------------------------------------------------------------ */
/* login line formatting                                               */
/* ------------------------------------------------------------------ */

aprs_err_t aprsis_format_login(const aprsis_connect_params_t *p,
                               char *buf, size_t buflen)
{
    int n;

    if (!p || !buf || !p->login) return APRS_ERR_INVALID_ARG;

    if (p->filter && p->filter[0]) {
        n = snprintf(buf, buflen,
                     "user %s pass %s vers libaprs 0.1.0 filter %s\r\n",
                     p->login,
                     (p->passcode && p->passcode[0]) ? p->passcode : "-1",
                     p->filter);
    } else {
        n = snprintf(buf, buflen,
                     "user %s pass %s vers libaprs 0.1.0\r\n",
                     p->login,
                     (p->passcode && p->passcode[0]) ? p->passcode : "-1");
    }

    if (n < 0 || (size_t)n >= buflen) return APRS_ERR_OVERFLOW;
    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* TCP connection                                                      */
/* ------------------------------------------------------------------ */

static aprs_err_t tcp_connect(aprsis_client_t *c)
{
    struct addrinfo hints, *res, *rp;
    char portstr[8];
    int rv;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(portstr, sizeof(portstr), "%u", c->port);

    rv = getaddrinfo(c->host, portstr, &hints, &res);
    if (rv != 0) return APRS_ERR_IO;

    c->fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        c->fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (c->fd < 0) continue;
        if (connect(c->fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(c->fd);
        c->fd = -1;
    }
    freeaddrinfo(res);

    if (c->fd < 0) return APRS_ERR_IO;
    return APRS_OK;
}

static aprs_err_t send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0) return APRS_ERR_IO;
        sent += (size_t)n;
    }
    return APRS_OK;
}

aprs_err_t aprsis_connect(aprsis_client_t *c, const aprsis_connect_params_t *p)
{
    char loginbuf[512];
    aprs_err_t rc;

    if (!c || !p || !p->host || !p->login) return APRS_ERR_INVALID_ARG;

    /* save params */
    safe_copy(c->host, sizeof(c->host), p->host);
    safe_copy(c->login, sizeof(c->login), p->login);
    safe_copy(c->passcode, sizeof(c->passcode), p->passcode);
    safe_copy(c->filter, sizeof(c->filter), p->filter);
    c->port = p->port ? p->port : APRSIS_PORT;

    /* reset line buffer */
    c->linelen = 0;

    /* connect TCP */
    rc = tcp_connect(c);
    if (rc != APRS_OK) return rc;

    c->connected = 1;

    /* send login */
    rc = aprsis_format_login(p, loginbuf, sizeof(loginbuf));
    if (rc != APRS_OK) {
        aprsis_disconnect(c);
        return rc;
    }

    rc = send_all(c->fd, loginbuf, strlen(loginbuf));
    if (rc != APRS_OK) {
        aprsis_disconnect(c);
        return rc;
    }

    return APRS_OK;
}

aprs_err_t aprsis_disconnect(aprsis_client_t *c)
{
    if (!c) return APRS_ERR_INVALID_ARG;
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    c->connected = 0;
    c->running = 0;
    c->linelen = 0;
    return APRS_OK;
}

bool aprsis_is_connected(const aprsis_client_t *c)
{
    return c && c->connected;
}

/* ------------------------------------------------------------------ */
/* send                                                                */
/* ------------------------------------------------------------------ */

aprs_err_t aprsis_send_line(aprsis_client_t *c, const char *line)
{
    char buf[APRSIS_LINE_MAX + 4];
    int n;

    if (!c || !line) return APRS_ERR_INVALID_ARG;
    if (!c->connected) return APRS_ERR_STATE;

    n = snprintf(buf, sizeof(buf), "%s\r\n", line);
    if (n < 0 || (size_t)n >= sizeof(buf)) return APRS_ERR_OVERFLOW;

    return send_all(c->fd, buf, (size_t)n);
}

/* ------------------------------------------------------------------ */
/* line-buffered read                                                  */
/* ------------------------------------------------------------------ */

/*
 * Try to extract one complete line from the internal buffer.
 * Returns 1 if a line was extracted, 0 if not enough data yet.
 * The line is copied to out (without CRLF) and linebuf is shifted.
 */
static int extract_line(aprsis_client_t *c, char *out, size_t outlen,
                        size_t *olen)
{
    size_t i;

    for (i = 0; i < c->linelen; i++) {
        if (c->linebuf[i] == '\n') {
            size_t llen = i;
            /* strip CR before LF */
            if (llen > 0 && c->linebuf[llen - 1] == '\r')
                llen--;
            if (llen >= outlen) llen = outlen - 1;
            memcpy(out, c->linebuf, llen);
            out[llen] = '\0';
            *olen = llen;

            /* shift remaining data */
            {
                size_t consumed = i + 1;
                size_t remain = c->linelen - consumed;
                if (remain > 0)
                    memmove(c->linebuf, c->linebuf + consumed, remain);
                c->linelen = remain;
            }
            return 1;
        }
    }
    return 0;
}

aprs_err_t aprsis_read_line(aprsis_client_t *c, char *buf, size_t buflen,
                            size_t *nread)
{
    ssize_t n;

    if (!c || !buf || !nread) return APRS_ERR_INVALID_ARG;
    if (!c->connected) return APRS_ERR_STATE;

    *nread = 0;

    /* check buffer for a complete line first */
    if (extract_line(c, buf, buflen, nread))
        return APRS_OK;

    /* read more data from socket */
    for (;;) {
        size_t space = sizeof(c->linebuf) - c->linelen;
        if (space == 0) {
            /* buffer full with no newline — discard and resync */
            c->linelen = 0;
            space = sizeof(c->linebuf);
        }

        n = read(c->fd, c->linebuf + c->linelen, space);
        if (n <= 0) {
            c->connected = 0;
            return APRS_ERR_IO;
        }
        c->linelen += (size_t)n;

        if (extract_line(c, buf, buflen, nread))
            return APRS_OK;
    }
}

/* ------------------------------------------------------------------ */
/* callback and run loop                                               */
/* ------------------------------------------------------------------ */

void aprsis_set_line_callback(aprsis_client_t *c, aprsis_line_cb cb,
                              void *user)
{
    if (!c) return;
    c->cb = cb;
    c->cb_user = user;
}

void aprsis_set_pass_comments(aprsis_client_t *c, bool pass)
{
    if (!c) return;
    c->pass_comments = pass ? 1 : 0;
}

void aprsis_stop(aprsis_client_t *c)
{
    if (c) c->running = 0;
}

aprs_err_t aprsis_run(aprsis_client_t *c)
{
    char line[APRSIS_LINE_MAX];
    size_t nread;
    aprs_err_t rc;

    if (!c) return APRS_ERR_INVALID_ARG;
    if (!c->connected) return APRS_ERR_STATE;

    c->running = 1;

    while (c->running && c->connected) {
        rc = aprsis_read_line(c, line, sizeof(line), &nread);
        if (rc != APRS_OK) {
            c->running = 0;
            return rc;
        }

        if (nread == 0) continue;

        /* filter comments unless pass_comments is set */
        if (line[0] == '#' && !c->pass_comments)
            continue;

        if (c->cb)
            c->cb(line, c->cb_user);
    }

    c->running = 0;
    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* duplicate suppression                                               */
/* ------------------------------------------------------------------ */

void aprsis_dedup_init(aprsis_dedup_t *d)
{
    if (!d) return;
    memset(d, 0, sizeof(*d));
}

/* simple FNV-1a 32-bit hash */
static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= (uint8_t)*s;
        h *= 16777619u;
    }
    return h;
}

bool aprsis_dedup_check(aprsis_dedup_t *d, const char *line)
{
    uint32_t h;
    int i;

    if (!d || !line) return false;

    h = fnv1a(line);

    /* search ring for existing match */
    for (i = 0; i < d->count; i++) {
        int idx = (d->head - 1 - i + APRSIS_DEDUP_SLOTS) % APRSIS_DEDUP_SLOTS;
        if (d->hashes[idx] == h)
            return true; /* duplicate */
    }

    /* not found — add to ring */
    d->hashes[d->head] = h;
    d->head = (d->head + 1) % APRSIS_DEDUP_SLOTS;
    if (d->count < APRSIS_DEDUP_SLOTS)
        d->count++;

    return false;
}
