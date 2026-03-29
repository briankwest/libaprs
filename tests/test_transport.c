/*
 * test_transport.c — transport layer tests using pipe loopback
 *
 * We test the generic dispatch and create/destroy lifecycle.
 * Real serial/TCP tests need hardware/network — we use a simple
 * pipe-based transport to verify the interface works end to end.
 */

#include <string.h>
#include <unistd.h>
#include "libaprs/transport.h"
#include "libaprs/kiss.h"

extern void test_begin(const char *name);
extern void test_end(void);
extern void test_assert(int cond, const char *msg);

/* ------------------------------------------------------------------ */
/* pipe transport for testing                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    int rfd;
    int wfd;
} pipe_ctx_t;

static aprs_err_t pipe_open(aprs_transport_t *t)
{
    pipe_ctx_t *ctx = (pipe_ctx_t *)t->ctx;
    int fds[2];
    if (pipe(fds) < 0) return APRS_ERR_IO;
    ctx->rfd = fds[0];
    ctx->wfd = fds[1];
    return APRS_OK;
}

static aprs_err_t pipe_close(aprs_transport_t *t)
{
    pipe_ctx_t *ctx = (pipe_ctx_t *)t->ctx;
    if (ctx->rfd >= 0) { close(ctx->rfd); ctx->rfd = -1; }
    if (ctx->wfd >= 0) { close(ctx->wfd); ctx->wfd = -1; }
    return APRS_OK;
}

static aprs_err_t pipe_read(aprs_transport_t *t, uint8_t *buf,
                             size_t maxlen, size_t *nread)
{
    pipe_ctx_t *ctx = (pipe_ctx_t *)t->ctx;
    ssize_t n = read(ctx->rfd, buf, maxlen);
    if (n < 0) return APRS_ERR_IO;
    *nread = (size_t)n;
    return APRS_OK;
}

static aprs_err_t pipe_write(aprs_transport_t *t, const uint8_t *buf,
                              size_t len, size_t *nwritten)
{
    pipe_ctx_t *ctx = (pipe_ctx_t *)t->ctx;
    ssize_t n = write(ctx->wfd, buf, len);
    if (n < 0) return APRS_ERR_IO;
    *nwritten = (size_t)n;
    return APRS_OK;
}

void test_transport(void)
{
    aprs_transport_t tp;
    pipe_ctx_t ctx;
    aprs_err_t rc;

    /* ------------------------------------------------------------------ */
    /* generic dispatch rejects NULL                                       */
    /* ------------------------------------------------------------------ */

    test_begin("transport dispatch rejects NULL");
    test_assert(aprs_transport_open(NULL) == APRS_ERR_INVALID_ARG,
                "open NULL");
    test_assert(aprs_transport_close(NULL) == APRS_ERR_INVALID_ARG,
                "close NULL");
    {
        uint8_t b;
        size_t n;
        test_assert(aprs_transport_read(NULL, &b, 1, &n)
                    == APRS_ERR_INVALID_ARG, "read NULL");
        test_assert(aprs_transport_write(NULL, &b, 1, &n)
                    == APRS_ERR_INVALID_ARG, "write NULL");
    }
    test_end();

    test_begin("transport dispatch rejects missing function ptrs");
    {
        aprs_transport_t empty;
        uint8_t b;
        size_t n;
        memset(&empty, 0, sizeof(empty));
        test_assert(aprs_transport_open(&empty) == APRS_ERR_INVALID_ARG,
                    "open no fn");
        test_assert(aprs_transport_read(&empty, &b, 1, &n)
                    == APRS_ERR_INVALID_ARG, "read no fn");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* pipe transport: open, write, read, close                            */
    /* ------------------------------------------------------------------ */

    test_begin("pipe transport open/write/read/close");
    {
        uint8_t wbuf[] = "Hello Transport";
        uint8_t rbuf[32];
        size_t nw, nr;

        memset(&ctx, 0, sizeof(ctx));
        ctx.rfd = ctx.wfd = -1;

        tp.ctx = &ctx;
        tp.open = pipe_open;
        tp.close = pipe_close;
        tp.read = pipe_read;
        tp.write = pipe_write;

        rc = aprs_transport_open(&tp);
        test_assert(rc == APRS_OK, "open failed");

        rc = aprs_transport_write(&tp, wbuf, sizeof(wbuf), &nw);
        test_assert(rc == APRS_OK, "write failed");
        test_assert(nw == sizeof(wbuf), "short write");

        rc = aprs_transport_read(&tp, rbuf, sizeof(rbuf), &nr);
        test_assert(rc == APRS_OK, "read failed");
        test_assert(nr == sizeof(wbuf), "short read");
        test_assert(memcmp(rbuf, wbuf, sizeof(wbuf)) == 0,
                    "data mismatch");

        rc = aprs_transport_close(&tp);
        test_assert(rc == APRS_OK, "close failed");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* pipe transport: KISS frame through pipe                            */
    /* ------------------------------------------------------------------ */

    test_begin("KISS frame through pipe transport");
    {
        uint8_t ax_data[] = "AX25 payload";
        uint8_t kiss_out[64], kiss_in[64], decoded[64];
        size_t klen, nw, nr, dlen;
        uint8_t kport;
        kiss_cmd_t kcmd;

        memset(&ctx, 0, sizeof(ctx));
        ctx.rfd = ctx.wfd = -1;
        tp.ctx = &ctx;
        tp.open = pipe_open;
        tp.close = pipe_close;
        tp.read = pipe_read;
        tp.write = pipe_write;

        rc = aprs_transport_open(&tp);
        test_assert(rc == APRS_OK, "open failed");

        /* KISS encode */
        rc = kiss_encode(0, KISS_CMD_DATA_FRAME, ax_data,
                         sizeof(ax_data), kiss_out, sizeof(kiss_out), &klen);
        test_assert(rc == APRS_OK, "kiss encode failed");

        /* write to pipe */
        rc = aprs_transport_write(&tp, kiss_out, klen, &nw);
        test_assert(rc == APRS_OK, "write failed");

        /* read from pipe */
        rc = aprs_transport_read(&tp, kiss_in, sizeof(kiss_in), &nr);
        test_assert(rc == APRS_OK, "read failed");
        test_assert(nr == klen, "read length wrong");

        /* KISS decode */
        rc = kiss_decode(kiss_in, nr, &kport, &kcmd,
                         decoded, sizeof(decoded), &dlen);
        test_assert(rc == APRS_OK, "kiss decode failed");
        test_assert(dlen == sizeof(ax_data), "decoded len wrong");
        test_assert(memcmp(decoded, ax_data, sizeof(ax_data)) == 0,
                    "decoded data wrong");

        aprs_transport_close(&tp);
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* serial transport create/destroy (no device needed)                   */
    /* ------------------------------------------------------------------ */

    test_begin("serial transport create/destroy lifecycle");
    {
        aprs_transport_t st;
        aprs_serial_opts_t opts;
        opts.device = "/dev/null";
        opts.baud = 9600;

        rc = aprs_transport_serial_create(&st, &opts);
        test_assert(rc == APRS_OK, "create failed");
        test_assert(st.open != NULL, "open fn NULL");
        test_assert(st.close != NULL, "close fn NULL");
        test_assert(st.read != NULL, "read fn NULL");
        test_assert(st.write != NULL, "write fn NULL");
        test_assert(st.ctx != NULL, "ctx NULL");

        aprs_transport_serial_destroy(&st);
        test_assert(st.ctx == NULL, "ctx not cleared");
    }
    test_end();

    test_begin("serial transport create rejects NULL");
    {
        aprs_transport_t st;
        rc = aprs_transport_serial_create(&st, NULL);
        test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL opts");
        rc = aprs_transport_serial_create(NULL, NULL);
        test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL tp");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* TCP transport create/destroy (no connection needed)                  */
    /* ------------------------------------------------------------------ */

    test_begin("TCP transport create/destroy lifecycle");
    {
        aprs_transport_t tt;
        aprs_tcp_opts_t opts;
        opts.host = "localhost";
        opts.port = 8001;

        rc = aprs_transport_tcp_create(&tt, &opts);
        test_assert(rc == APRS_OK, "create failed");
        test_assert(tt.open != NULL, "open fn NULL");
        test_assert(tt.ctx != NULL, "ctx NULL");

        aprs_transport_tcp_destroy(&tt);
        test_assert(tt.ctx == NULL, "ctx not cleared");
    }
    test_end();

    test_begin("TCP transport create rejects NULL");
    {
        aprs_transport_t tt;
        rc = aprs_transport_tcp_create(&tt, NULL);
        test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL opts");
    }
    test_end();
}
