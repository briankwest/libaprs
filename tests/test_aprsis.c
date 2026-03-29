/*
 * test_aprsis.c — APRS-IS client tests
 *
 * Tests login line formatting, passcode computation, line buffer
 * parsing via a pipe-based mock server, comment filtering,
 * duplicate suppression, and parameter validation.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "libaprs/aprsis.h"

extern void test_begin(const char *name);
extern void test_end(void);
extern void test_assert(int cond, const char *msg);

/* ------------------------------------------------------------------ */
/* passcode                                                            */
/* ------------------------------------------------------------------ */

void test_aprsis(void)
{
    aprs_err_t rc;

    test_begin("passcode for N0CALL");
    {
        int p = aprsis_passcode("N0CALL");
        test_assert(p >= 0 && p <= 32767, "passcode out of range");
    }
    test_end();

    test_begin("passcode strips SSID");
    {
        int p1 = aprsis_passcode("N0CALL");
        int p2 = aprsis_passcode("N0CALL-9");
        test_assert(p1 == p2, "passcode should be same with/without SSID");
    }
    test_end();

    test_begin("passcode is case insensitive");
    {
        int p1 = aprsis_passcode("n0call");
        int p2 = aprsis_passcode("N0CALL");
        test_assert(p1 == p2, "passcode should ignore case");
    }
    test_end();

    test_begin("passcode for NULL returns -1");
    test_assert(aprsis_passcode(NULL) == -1, "should return -1");
    test_end();

    /* known passcode vectors — these are well-known public values */
    test_begin("passcode for WB4APR");
    {
        int p = aprsis_passcode("WB4APR");
        test_assert(p == 16563, "WB4APR passcode wrong");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* login line formatting                                               */
    /* ------------------------------------------------------------------ */

    test_begin("format login line with filter");
    {
        aprsis_connect_params_t p;
        char buf[256];

        p.host = "rotate.aprs2.net";
        p.port = 14580;
        p.login = "N0CALL";
        p.passcode = "12345";
        p.filter = "r/35.5/-97.5/100";

        rc = aprsis_format_login(&p, buf, sizeof(buf));
        test_assert(rc == APRS_OK, "format failed");
        test_assert(strstr(buf, "user N0CALL") != NULL,
                    "missing user");
        test_assert(strstr(buf, "pass 12345") != NULL,
                    "missing passcode");
        test_assert(strstr(buf, "vers libaprs") != NULL,
                    "missing version");
        test_assert(strstr(buf, "filter r/35.5/-97.5/100") != NULL,
                    "missing filter");
        test_assert(strstr(buf, "\r\n") != NULL, "missing CRLF");
    }
    test_end();

    test_begin("format login line without filter");
    {
        aprsis_connect_params_t p;
        char buf[256];

        p.host = "rotate.aprs2.net";
        p.port = 14580;
        p.login = "N0CALL";
        p.passcode = "-1";
        p.filter = NULL;

        rc = aprsis_format_login(&p, buf, sizeof(buf));
        test_assert(rc == APRS_OK, "format failed");
        test_assert(strstr(buf, "filter") == NULL,
                    "should not have filter");
        test_assert(strstr(buf, "pass -1") != NULL,
                    "missing passcode");
    }
    test_end();

    test_begin("format login default passcode");
    {
        aprsis_connect_params_t p;
        char buf[256];

        p.host = "x";
        p.port = 14580;
        p.login = "N0CALL";
        p.passcode = NULL;
        p.filter = NULL;

        rc = aprsis_format_login(&p, buf, sizeof(buf));
        test_assert(rc == APRS_OK, "format failed");
        test_assert(strstr(buf, "pass -1") != NULL,
                    "should default to -1");
    }
    test_end();

    test_begin("format login overflow");
    {
        aprsis_connect_params_t p;
        char buf[10];

        p.host = "x";
        p.port = 14580;
        p.login = "N0CALL";
        p.passcode = "12345";
        p.filter = NULL;

        rc = aprsis_format_login(&p, buf, sizeof(buf));
        test_assert(rc == APRS_ERR_OVERFLOW, "should overflow");
    }
    test_end();

    test_begin("format login rejects NULL");
    {
        char buf[256];
        rc = aprsis_format_login(NULL, buf, sizeof(buf));
        test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* client create / destroy                                             */
    /* ------------------------------------------------------------------ */

    test_begin("client create and destroy");
    {
        aprsis_client_t *c = aprsis_client_create();
        test_assert(c != NULL, "create returned NULL");
        test_assert(aprsis_is_connected(c) == false,
                    "should not be connected");
        aprsis_client_destroy(c);
    }
    test_end();

    test_begin("connect rejects NULL params");
    {
        aprsis_client_t *c = aprsis_client_create();
        rc = aprsis_connect(c, NULL);
        test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL");
        aprsis_client_destroy(c);
    }
    test_end();

    test_begin("connect rejects missing host");
    {
        aprsis_client_t *c = aprsis_client_create();
        aprsis_connect_params_t p;
        memset(&p, 0, sizeof(p));
        p.login = "N0CALL";
        rc = aprsis_connect(c, &p);
        test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL host");
        aprsis_client_destroy(c);
    }
    test_end();

    test_begin("send_line rejects when not connected");
    {
        aprsis_client_t *c = aprsis_client_create();
        rc = aprsis_send_line(c, "test");
        test_assert(rc == APRS_ERR_STATE, "should reject");
        aprsis_client_destroy(c);
    }
    test_end();

    test_begin("read_line rejects when not connected");
    {
        aprsis_client_t *c = aprsis_client_create();
        char buf[64];
        size_t nr;
        rc = aprsis_read_line(c, buf, sizeof(buf), &nr);
        test_assert(rc == APRS_ERR_STATE, "should reject");
        aprsis_client_destroy(c);
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* pipe-based mock server for line buffering tests                      */
    /* ------------------------------------------------------------------ */

    test_begin("read_line with pipe mock: simple line");
    {
        int sv[2];
        aprsis_client_t *c;
        char buf[256];
        size_t nr;

        test_assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
                    "socketpair failed");

        c = aprsis_client_create();
        /* inject fd directly */
        *(int *)((char *)c) = sv[0]; /* fd is first member */
        *(int *)((char *)c + sizeof(int)) = 1; /* connected flag */

        /* write a line from "server" side */
        {
            const char *line = "N0CALL>APRS:>hello\r\n";
            ssize_t w = write(sv[1], line, strlen(line));
            (void)w;
        }

        rc = aprsis_read_line(c, buf, sizeof(buf), &nr);
        test_assert(rc == APRS_OK, "read_line failed");
        test_assert(strcmp(buf, "N0CALL>APRS:>hello") == 0,
                    "line wrong");
        test_assert(nr == 18, "nread wrong");

        close(sv[1]);
        aprsis_client_destroy(c);
    }
    test_end();

    test_begin("read_line with pipe: two lines in one write");
    {
        int sv[2];
        aprsis_client_t *c;
        char buf[256];
        size_t nr;

        test_assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
                    "socketpair failed");

        c = aprsis_client_create();
        *(int *)((char *)c) = sv[0];
        *(int *)((char *)c + sizeof(int)) = 1;

        {
            const char *data = "line1\r\nline2\r\n";
            ssize_t w = write(sv[1], data, strlen(data));
            (void)w;
        }

        rc = aprsis_read_line(c, buf, sizeof(buf), &nr);
        test_assert(rc == APRS_OK, "read 1 failed");
        test_assert(strcmp(buf, "line1") == 0, "line 1 wrong");

        rc = aprsis_read_line(c, buf, sizeof(buf), &nr);
        test_assert(rc == APRS_OK, "read 2 failed");
        test_assert(strcmp(buf, "line2") == 0, "line 2 wrong");

        close(sv[1]);
        aprsis_client_destroy(c);
    }
    test_end();

    test_begin("read_line with pipe: line split across reads");
    {
        int sv[2];
        aprsis_client_t *c;
        char buf[256];
        size_t nr;

        test_assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
                    "socketpair failed");

        c = aprsis_client_create();
        *(int *)((char *)c) = sv[0];
        *(int *)((char *)c + sizeof(int)) = 1;

        /* write in two chunks */
        {
            ssize_t w;
            w = write(sv[1], "hel", 3);
            (void)w;
            w = write(sv[1], "lo\r\n", 4);
            (void)w;
        }

        rc = aprsis_read_line(c, buf, sizeof(buf), &nr);
        test_assert(rc == APRS_OK, "read_line failed");
        test_assert(strcmp(buf, "hello") == 0, "line wrong");

        close(sv[1]);
        aprsis_client_destroy(c);
    }
    test_end();

    test_begin("read_line strips bare LF");
    {
        int sv[2];
        aprsis_client_t *c;
        char buf[256];
        size_t nr;

        test_assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
                    "socketpair failed");

        c = aprsis_client_create();
        *(int *)((char *)c) = sv[0];
        *(int *)((char *)c + sizeof(int)) = 1;

        {
            const char *line = "bare lf\n";
            ssize_t w = write(sv[1], line, strlen(line));
            (void)w;
        }

        rc = aprsis_read_line(c, buf, sizeof(buf), &nr);
        test_assert(rc == APRS_OK, "read_line failed");
        test_assert(strcmp(buf, "bare lf") == 0, "line wrong");

        close(sv[1]);
        aprsis_client_destroy(c);
    }
    test_end();

    test_begin("read_line returns IO error on close");
    {
        int sv[2];
        aprsis_client_t *c;
        char buf[256];
        size_t nr;

        test_assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
                    "socketpair failed");

        c = aprsis_client_create();
        *(int *)((char *)c) = sv[0];
        *(int *)((char *)c + sizeof(int)) = 1;

        /* close server side immediately */
        close(sv[1]);

        rc = aprsis_read_line(c, buf, sizeof(buf), &nr);
        test_assert(rc == APRS_ERR_IO, "should get IO error on EOF");

        aprsis_client_destroy(c);
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* run loop with comment filtering                                     */
    /* ------------------------------------------------------------------ */

    test_begin("run loop filters comments");
    {
        int sv[2];
        aprsis_client_t *c;

        /* accumulator for callback */
        static int cb_count;
        static char cb_lines[4][128];
        cb_count = 0;

        test_assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
                    "socketpair failed");

        c = aprsis_client_create();
        *(int *)((char *)c) = sv[0];
        *(int *)((char *)c + sizeof(int)) = 1;

        {
            void run_cb(const char *line, void *user) {
                (void)user;
                if (cb_count < 4)
                    snprintf(cb_lines[cb_count], 128, "%s", line);
                cb_count++;
            }
            aprsis_set_line_callback(c, run_cb, NULL);
        }

        /* write server data including comments, then close */
        {
            const char *data =
                "# server banner\r\n"
                "N0CALL>APRS:>hello\r\n"
                "# comment\r\n"
                "W3ADO>APRS:>world\r\n";
            ssize_t w = write(sv[1], data, strlen(data));
            (void)w;
            close(sv[1]);
        }

        rc = aprsis_run(c);
        /* run ends with IO error when pipe closes */
        test_assert(rc == APRS_ERR_IO, "should end with IO");
        test_assert(cb_count == 2, "should get 2 lines (comments filtered)");
        test_assert(strcmp(cb_lines[0], "N0CALL>APRS:>hello") == 0,
                    "line 0 wrong");
        test_assert(strcmp(cb_lines[1], "W3ADO>APRS:>world") == 0,
                    "line 1 wrong");

        aprsis_client_destroy(c);
    }
    test_end();

    test_begin("run loop passes comments when enabled");
    {
        int sv[2];
        aprsis_client_t *c;

        static int cb2_count;
        cb2_count = 0;

        test_assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
                    "socketpair failed");

        c = aprsis_client_create();
        *(int *)((char *)c) = sv[0];
        *(int *)((char *)c + sizeof(int)) = 1;
        aprsis_set_pass_comments(c, true);

        {
            void count_cb(const char *line, void *user) {
                (void)line;
                (void)user;
                cb2_count++;
            }
            aprsis_set_line_callback(c, count_cb, NULL);
        }

        {
            const char *data = "# comment\r\ndata\r\n";
            ssize_t w = write(sv[1], data, strlen(data));
            (void)w;
            close(sv[1]);
        }

        aprsis_run(c);
        test_assert(cb2_count == 2, "should get 2 lines with comments");

        aprsis_client_destroy(c);
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* duplicate suppression                                               */
    /* ------------------------------------------------------------------ */

    test_begin("dedup: first occurrence is not duplicate");
    {
        aprsis_dedup_t d;
        aprsis_dedup_init(&d);
        test_assert(aprsis_dedup_check(&d, "N0CALL>APRS:>test") == false,
                    "first should not be dup");
    }
    test_end();

    test_begin("dedup: second occurrence is duplicate");
    {
        aprsis_dedup_t d;
        aprsis_dedup_init(&d);
        aprsis_dedup_check(&d, "N0CALL>APRS:>test");
        test_assert(aprsis_dedup_check(&d, "N0CALL>APRS:>test") == true,
                    "second should be dup");
    }
    test_end();

    test_begin("dedup: different lines are not duplicates");
    {
        aprsis_dedup_t d;
        aprsis_dedup_init(&d);
        aprsis_dedup_check(&d, "line A");
        test_assert(aprsis_dedup_check(&d, "line B") == false,
                    "different line should not be dup");
    }
    test_end();

    test_begin("dedup: ring wraps and evicts old entries");
    {
        aprsis_dedup_t d;
        char buf[32];
        int i;
        aprsis_dedup_init(&d);

        /* fill ring with APRSIS_DEDUP_SLOTS unique lines */
        for (i = 0; i < APRSIS_DEDUP_SLOTS; i++) {
            snprintf(buf, sizeof(buf), "line %d", i);
            aprsis_dedup_check(&d, buf);
        }
        test_assert(d.count == APRSIS_DEDUP_SLOTS, "count wrong");

        /* line 0 should still be there */
        test_assert(aprsis_dedup_check(&d, "line 0") == true,
                    "line 0 should still be in ring");

        /* add one more to evict line 0 */
        aprsis_dedup_check(&d, "evict trigger");

        /* now line 0 should be gone */
        test_assert(aprsis_dedup_check(&d, "line 0") == false,
                    "line 0 should be evicted");
    }
    test_end();

    test_begin("dedup: NULL inputs handled");
    {
        aprsis_dedup_t d;
        aprsis_dedup_init(&d);
        test_assert(aprsis_dedup_check(&d, NULL) == false,
                    "NULL should not be dup");
        test_assert(aprsis_dedup_check(NULL, "test") == false,
                    "NULL dedup should not crash");
    }
    test_end();

    /* ------------------------------------------------------------------ */
    /* send_line through pipe                                              */
    /* ------------------------------------------------------------------ */

    test_begin("send_line appends CRLF");
    {
        int sv[2];
        aprsis_client_t *c;
        char buf[128];

        test_assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
                    "socketpair failed");

        c = aprsis_client_create();
        *(int *)((char *)c) = sv[0];
        *(int *)((char *)c + sizeof(int)) = 1;

        rc = aprsis_send_line(c, "N0CALL>APRS:>test");
        test_assert(rc == APRS_OK, "send failed");

        {
            ssize_t n = read(sv[1], buf, sizeof(buf));
            test_assert(n > 0, "read failed");
            buf[n] = '\0';
            test_assert(strcmp(buf, "N0CALL>APRS:>test\r\n") == 0,
                        "CRLF not appended");
        }

        close(sv[1]);
        aprsis_client_destroy(c);
    }
    test_end();
}
