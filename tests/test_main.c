/*
 * test_main.c — Test harness for libaprs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_total;
static int g_passed;
static int g_failed;
static int g_in_test;

void test_begin(const char *name)
{
    int extra = 0;
    for (const char *p = name; *p; p++)
        if ((*p & 0xC0) == 0x80) extra++;
    printf("  %-*s ", 50 + extra, name);
    g_total++;
    g_in_test = 1;
}

void test_pass(void)
{
    if (!g_in_test) return;
    printf("PASS\n");
    g_passed++;
    g_in_test = 0;
}

void test_fail(const char *msg)
{
    if (!g_in_test) return;
    printf("FAIL: %s\n", msg);
    g_failed++;
    g_in_test = 0;
}

void test_assert(int cond, const char *msg)
{
    if (!cond)
        test_fail(msg);
}

void test_end(void)
{
    if (g_in_test)
        test_pass();
}

extern void test_aprs(void);
extern void test_kiss(void);
extern void test_ax25(void);
extern void test_transport(void);
extern void test_aprsis(void);
extern void test_station(void);
extern void test_modem(void);

int main(void)
{
    printf("libaprs test suite\n");
    printf("==================\n\n");

    printf("APRS parse/format/build tests:\n");
    test_aprs();

    printf("\nKISS encode/decode/streaming tests:\n");
    test_kiss();

    printf("\nAX.25 tests:\n");
    test_ax25();

    printf("\nTransport tests:\n");
    test_transport();

    printf("\nAPRS-IS tests:\n");
    test_aprsis();

    printf("\nStation state tests:\n");
    test_station();

    printf("\nAFSK1200 modem tests:\n");
    test_modem();

    printf("\n==================\n");
    printf("Results: %d/%d passed", g_passed, g_total);
    if (g_failed > 0)
        printf(", %d FAILED", g_failed);
    printf("\n");

    return g_failed > 0 ? 1 : 0;
}
