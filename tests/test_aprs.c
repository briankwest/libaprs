/*
 * test_aprs.c — APRS parsing, formatting, and builder tests
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "libaprs/aprs.h"

extern void test_begin(const char *name);
extern void test_end(void);
extern void test_assert(int cond, const char *msg);

static int near(double a, double b, double eps)
{
    return fabs(a - b) < eps;
}

/* ------------------------------------------------------------------ */
/* position parsing                                                    */
/* ------------------------------------------------------------------ */

void test_aprs(void)
{
    aprs_packet_t pkt;
    aprs_err_t rc;
    char buf[512];

    /* --- basic position without timestamp: ! --- */

    test_begin("parse ! position packet");
    rc = aprs_parse_tnc2("N0CALL>APRS:!4903.50N/07201.75W-PHG2360", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.type == APRS_PACKET_POSITION, "wrong type");
    test_assert(near(pkt.data.position.latitude, 49.0583333, 0.001),
                "latitude wrong");
    test_assert(near(pkt.data.position.longitude, -72.0291667, 0.001),
                "longitude wrong");
    test_assert(pkt.data.position.symbol_table == '/', "symbol_table wrong");
    test_assert(pkt.data.position.symbol_code == '-', "symbol_code wrong");
    test_assert(strcmp(pkt.data.position.comment, "PHG2360") == 0,
                "comment wrong");
    test_assert(pkt.data.position.has_timestamp == false, "should have no ts");
    test_assert(pkt.data.position.messaging == false, "should not be msgcap");
    test_end();

    /* --- position with messaging: = --- */

    test_begin("parse = position packet (msgcap)");
    rc = aprs_parse_tnc2("W3ADO-1>APRS:=4903.50N/07201.75W-PHG2360", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.type == APRS_PACKET_POSITION_MSGCAP, "wrong type");
    test_assert(pkt.data.position.messaging == true, "should be msgcap");
    test_assert(near(pkt.data.position.latitude, 49.0583333, 0.001),
                "latitude wrong");
    test_end();

    /* --- position with timestamp: / --- */

    test_begin("parse / position packet (with DHM timestamp)");
    rc = aprs_parse_tnc2("N0CALL>APRS:/092345z4903.50N/07201.75W>Test", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.type == APRS_PACKET_POSITION, "wrong type");
    test_assert(pkt.data.position.has_timestamp == true, "should have ts");
    test_assert(pkt.data.position.timestamp.type == 'z', "ts type wrong");
    test_assert(pkt.data.position.timestamp.day == 9, "ts day wrong");
    test_assert(pkt.data.position.timestamp.hour == 23, "ts hour wrong");
    test_assert(pkt.data.position.timestamp.minute == 45, "ts minute wrong");
    test_assert(near(pkt.data.position.latitude, 49.0583333, 0.001),
                "latitude wrong");
    test_assert(strcmp(pkt.data.position.comment, "Test") == 0,
                "comment wrong");
    test_end();

    /* --- position with timestamp and messaging: @ --- */

    test_begin("parse @ position packet (ts + msgcap)");
    rc = aprs_parse_tnc2("N0CALL>APRS:@092345z4903.50N/07201.75W>Test", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.type == APRS_PACKET_POSITION_MSGCAP, "wrong type");
    test_assert(pkt.data.position.messaging == true, "should be msgcap");
    test_assert(pkt.data.position.has_timestamp == true, "should have ts");
    test_end();

    /* --- HMS timestamp --- */

    test_begin("parse / position with HMS timestamp");
    rc = aprs_parse_tnc2("N0CALL>APRS:/234517h4903.50N/07201.75W>", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.data.position.timestamp.type == 'h', "ts type not h");
    test_assert(pkt.data.position.timestamp.hour == 23, "ts hour wrong");
    test_assert(pkt.data.position.timestamp.minute == 45, "ts min wrong");
    test_assert(pkt.data.position.timestamp.second == 17, "ts sec wrong");
    test_end();

    /* --- southern / eastern hemisphere --- */

    test_begin("parse position south/east hemisphere");
    rc = aprs_parse_tnc2("VK2RZA>APRS:!3352.50S/15113.00E#Digipeater", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(near(pkt.data.position.latitude, -33.875, 0.001),
                "latitude wrong");
    test_assert(near(pkt.data.position.longitude, 151.2166667, 0.001),
                "longitude wrong");
    test_end();

    /* --- position at equator/prime meridian --- */

    test_begin("parse position 0,0");
    rc = aprs_parse_tnc2("TEST>APRS:!0000.00N/00000.00E/origin", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(near(pkt.data.position.latitude, 0.0, 0.001),
                "latitude wrong");
    test_assert(near(pkt.data.position.longitude, 0.0, 0.001),
                "longitude wrong");
    test_end();

    /* --- position with path --- */

    test_begin("parse position preserves path");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS,WIDE1-1,WIDE2-1:!4903.50N/07201.75W-", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.path_len == 2, "path_len wrong");
    test_assert(strcmp(pkt.path[0].callsign, "WIDE1-1") == 0,
                "path[0] wrong");
    test_assert(strcmp(pkt.path[1].callsign, "WIDE2-1") == 0,
                "path[1] wrong");
    test_end();

    /* --- digipeated path with * --- */

    test_begin("parse digipeated path with *");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS,RELAY*,WIDE2-1:!4903.50N/07201.75W-", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.path_len == 2, "path_len wrong");
    test_assert(strcmp(pkt.path[0].callsign, "RELAY*") == 0,
                "path[0] wrong");
    test_end();

    /* ------------------------------------------------------------------ */
    /* message parsing                                                     */
    /* ------------------------------------------------------------------ */

    test_begin("parse message packet");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS::N0CALL-5 :Hello World{123", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.type == APRS_PACKET_MESSAGE, "wrong type");
    test_assert(strcmp(pkt.data.message.addressee, "N0CALL-5") == 0,
                "addressee wrong");
    test_assert(strcmp(pkt.data.message.text, "Hello World") == 0,
                "text wrong");
    test_assert(strcmp(pkt.data.message.msgno, "123") == 0,
                "msgno wrong");
    test_end();

    test_begin("parse message without msgno");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS::W3ADO-1  :Testing 1 2 3", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(strcmp(pkt.data.message.addressee, "W3ADO-1") == 0,
                "addressee wrong");
    test_assert(strcmp(pkt.data.message.text, "Testing 1 2 3") == 0,
                "text wrong");
    test_assert(pkt.data.message.msgno[0] == '\0', "msgno should be empty");
    test_end();

    test_begin("parse message ack");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS::N0CALL-5 :ack123", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.data.message.ack == true, "ack flag not set");
    test_assert(strcmp(pkt.data.message.msgno, "123") == 0,
                "ack msgno wrong");
    test_end();

    test_begin("parse message rej");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS::N0CALL-5 :rej456", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.data.message.rej == true, "rej flag not set");
    test_assert(strcmp(pkt.data.message.msgno, "456") == 0,
                "rej msgno wrong");
    test_end();

    /* ------------------------------------------------------------------ */
    /* status parsing                                                      */
    /* ------------------------------------------------------------------ */

    test_begin("parse status packet");
    rc = aprs_parse_tnc2("N0CALL>APRS:>On the air in OKC", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.type == APRS_PACKET_STATUS, "wrong type");
    test_assert(strcmp(pkt.data.status.text, "On the air in OKC") == 0,
                "status text wrong");
    test_end();

    /* ------------------------------------------------------------------ */
    /* header parsing edge cases                                           */
    /* ------------------------------------------------------------------ */

    test_begin("parse src and dst callsigns");
    rc = aprs_parse_tnc2("WB4APR-14>APRS:>status", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(strcmp(pkt.src.callsign, "WB4APR-14") == 0, "src wrong");
    test_assert(strcmp(pkt.dst.callsign, "APRS") == 0, "dst wrong");
    test_end();

    test_begin("reject missing > in header");
    rc = aprs_parse_tnc2("N0CALLAPRS:>test", &pkt);
    test_assert(rc == APRS_ERR_PARSE, "should fail");
    test_end();

    test_begin("reject missing : delimiter");
    rc = aprs_parse_tnc2("N0CALL>APRS>test", &pkt);
    test_assert(rc == APRS_ERR_PARSE, "should fail");
    test_end();

    test_begin("reject NULL input");
    rc = aprs_parse_tnc2(NULL, &pkt);
    test_assert(rc == APRS_ERR_INVALID_ARG, "should fail on NULL");
    test_end();

    test_begin("reject empty callsign");
    rc = aprs_parse_tnc2(">APRS:>test", &pkt);
    test_assert(rc == APRS_ERR_PARSE, "should reject empty src");
    test_end();

    test_begin("max path overflow");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS,A,B,C,D,E,F,G,H,I:>test", &pkt);
    test_assert(rc == APRS_ERR_OVERFLOW, "should overflow at 9 path hops");
    test_end();

    /* ------------------------------------------------------------------ */
    /* position format invalid inputs                                      */
    /* ------------------------------------------------------------------ */

    test_begin("reject truncated position");
    rc = aprs_parse_tnc2("N0CALL>APRS:!4903.50N/072", &pkt);
    test_assert(rc == APRS_ERR_PARSE, "should fail on truncated pos");
    test_end();

    test_begin("reject bad latitude direction");
    rc = aprs_parse_tnc2("N0CALL>APRS:!4903.50X/07201.75W-", &pkt);
    test_assert(rc == APRS_ERR_PARSE, "should fail on bad lat dir");
    test_end();

    test_begin("reject bad longitude direction");
    rc = aprs_parse_tnc2("N0CALL>APRS:!4903.50N/07201.75Z-", &pkt);
    test_assert(rc == APRS_ERR_PARSE, "should fail on bad lon dir");
    test_end();

    /* ------------------------------------------------------------------ */
    /* other packet types (type detection only)                            */
    /* ------------------------------------------------------------------ */

    test_begin("detect object packet");
    rc = aprs_parse_tnc2("N0CALL>APRS:;LEADER   *092345z4903.50N/07201.75W>", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.type == APRS_PACKET_OBJECT, "wrong type");
    test_end();

    test_begin("detect item packet");
    rc = aprs_parse_tnc2("N0CALL>APRS:)ITEM!4903.50N/07201.75W>", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.type == APRS_PACKET_ITEM, "wrong type");
    test_end();

    test_begin("detect query packet");
    rc = aprs_parse_tnc2("N0CALL>APRS:?APRS?", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.type == APRS_PACKET_QUERY, "wrong type");
    test_end();

    test_begin("detect telemetry packet");
    rc = aprs_parse_tnc2("N0CALL>APRS:T#005,199,000,255,073,123,01100001", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.type == APRS_PACKET_TELEMETRY, "wrong type");
    test_end();

    test_begin("unknown packet type");
    rc = aprs_parse_tnc2("N0CALL>APRS:Xrandom data", &pkt);
    test_assert(rc == APRS_OK, "parse returned error");
    test_assert(pkt.type == APRS_PACKET_UNKNOWN, "wrong type");
    test_end();

    /* ------------------------------------------------------------------ */
    /* TNC2 format round-trip                                              */
    /* ------------------------------------------------------------------ */

    test_begin("format status round-trip");
    rc = aprs_parse_tnc2("N0CALL-9>APRS,WIDE1-1:>On the air", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    rc = aprs_format_tnc2(&pkt, buf, sizeof(buf));
    test_assert(rc == APRS_OK, "format failed");
    test_assert(strcmp(buf, "N0CALL-9>APRS,WIDE1-1:>On the air") == 0,
                "round-trip mismatch");
    test_end();

    test_begin("format position round-trip");
    rc = aprs_parse_tnc2("N0CALL>APRS:!4903.50N/07201.75W-PHG2360", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    rc = aprs_format_tnc2(&pkt, buf, sizeof(buf));
    test_assert(rc == APRS_OK, "format failed");
    test_assert(strcmp(buf, "N0CALL>APRS:!4903.50N/07201.75W-PHG2360") == 0,
                "round-trip mismatch");
    test_end();

    test_begin("format message round-trip");
    rc = aprs_parse_tnc2("N0CALL>APRS::N0CALL-5 :Hello{42", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    rc = aprs_format_tnc2(&pkt, buf, sizeof(buf));
    test_assert(rc == APRS_OK, "format failed");
    test_assert(strcmp(buf, "N0CALL>APRS::N0CALL-5 :Hello{42") == 0,
                "round-trip mismatch");
    test_end();

    test_begin("format buffer overflow");
    rc = aprs_parse_tnc2("N0CALL>APRS:>status text", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    rc = aprs_format_tnc2(&pkt, buf, 5);
    test_assert(rc == APRS_ERR_OVERFLOW, "should overflow");
    test_end();

    /* ------------------------------------------------------------------ */
    /* builders                                                            */
    /* ------------------------------------------------------------------ */

    test_begin("build position packet");
    {
        const char *path[] = {"WIDE1-1", "WIDE2-1"};
        rc = aprs_build_position(&pkt, "N0CALL-9", "APRS",
                                 path, 2,
                                 49.058333, -72.029167,
                                 '/', '-', "PHG2360");
        test_assert(rc == APRS_OK, "build failed");
        test_assert(pkt.type == APRS_PACKET_POSITION, "wrong type");
        test_assert(near(pkt.data.position.latitude, 49.058333, 0.001),
                    "latitude wrong");
        test_assert(near(pkt.data.position.longitude, -72.029167, 0.001),
                    "longitude wrong");
        /* verify raw_info starts with ! and contains APRS lat/lon */
        test_assert(pkt.raw_info[0] == '!', "raw_info should start with !");
        test_assert(strstr(pkt.raw_info, "N/") != NULL,
                    "raw_info missing N hemisphere");
    }
    test_end();

    test_begin("build position then parse round-trip");
    {
        const char *path[] = {"WIDE1-1"};
        aprs_packet_t pkt2;
        rc = aprs_build_position(&pkt, "W3ADO", "APRS",
                                 path, 1,
                                 39.1525, -76.7275,
                                 '/', '>',  "Baltimore");
        test_assert(rc == APRS_OK, "build failed");
        rc = aprs_format_tnc2(&pkt, buf, sizeof(buf));
        test_assert(rc == APRS_OK, "format failed");
        rc = aprs_parse_tnc2(buf, &pkt2);
        test_assert(rc == APRS_OK, "re-parse failed");
        test_assert(near(pkt2.data.position.latitude, 39.1525, 0.01),
                    "re-parsed latitude wrong");
        test_assert(near(pkt2.data.position.longitude, -76.7275, 0.01),
                    "re-parsed longitude wrong");
        test_assert(strcmp(pkt2.data.position.comment, "Baltimore") == 0,
                    "re-parsed comment wrong");
    }
    test_end();

    test_begin("build position rejects bad lat/lon");
    rc = aprs_build_position(&pkt, "N0CALL", "APRS", NULL, 0,
                             91.0, 0.0, '/', '-', NULL);
    test_assert(rc == APRS_ERR_INVALID_ARG, "should reject lat>90");
    rc = aprs_build_position(&pkt, "N0CALL", "APRS", NULL, 0,
                             0.0, -181.0, '/', '-', NULL);
    test_assert(rc == APRS_ERR_INVALID_ARG, "should reject lon<-180");
    test_end();

    test_begin("build message packet");
    {
        const char *path[] = {"WIDE1-1"};
        rc = aprs_build_message(&pkt, "N0CALL", "APRS",
                                path, 1,
                                "W3ADO-1", "Hello!", "55");
        test_assert(rc == APRS_OK, "build failed");
        test_assert(pkt.type == APRS_PACKET_MESSAGE, "wrong type");
        test_assert(strcmp(pkt.data.message.addressee, "W3ADO-1") == 0,
                    "addressee wrong");
        test_assert(strcmp(pkt.data.message.text, "Hello!") == 0,
                    "text wrong");
        test_assert(strcmp(pkt.data.message.msgno, "55") == 0,
                    "msgno wrong");
        test_assert(strstr(pkt.raw_info, "{55") != NULL,
                    "raw_info missing msgno");
    }
    test_end();

    test_begin("build message without msgno");
    rc = aprs_build_message(&pkt, "N0CALL", "APRS", NULL, 0,
                            "TEST", "No number", NULL);
    test_assert(rc == APRS_OK, "build failed");
    test_assert(strchr(pkt.raw_info, '{') == NULL,
                "raw_info should have no {");
    test_end();

    test_begin("build status packet");
    rc = aprs_build_status(&pkt, "N0CALL", "APRS", NULL, 0,
                           "On the air");
    test_assert(rc == APRS_OK, "build failed");
    test_assert(pkt.type == APRS_PACKET_STATUS, "wrong type");
    test_assert(strcmp(pkt.data.status.text, "On the air") == 0,
                "text wrong");
    test_assert(strcmp(pkt.raw_info, ">On the air") == 0,
                "raw_info wrong");
    test_end();

    test_begin("build rejects NULL args");
    rc = aprs_build_position(NULL, "N0CALL", "APRS", NULL, 0,
                             0.0, 0.0, '/', '-', NULL);
    test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL pkt");
    rc = aprs_build_message(&pkt, "N0CALL", "APRS", NULL, 0,
                            NULL, "text", NULL);
    test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL addressee");
    rc = aprs_build_status(&pkt, "N0CALL", "APRS", NULL, 0, NULL);
    test_assert(rc == APRS_ERR_INVALID_ARG, "should reject NULL text");
    test_end();

    /* ------------------------------------------------------------------ */
    /* validation helpers                                                  */
    /* ------------------------------------------------------------------ */

    test_begin("callsign validation");
    test_assert(aprs_is_valid_callsign("N0CALL") == true, "N0CALL valid");
    test_assert(aprs_is_valid_callsign("N0CALL-15") == true, "N0CALL-15 valid");
    test_assert(aprs_is_valid_callsign("WIDE1-1") == true, "WIDE1-1 valid");
    test_assert(aprs_is_valid_callsign("RELAY*") == true, "RELAY* valid");
    test_assert(aprs_is_valid_callsign("") == false, "empty invalid");
    test_assert(aprs_is_valid_callsign(NULL) == false, "NULL invalid");
    test_assert(aprs_is_valid_callsign("TOOLONGCALLSIGN") == false,
                "too long invalid");
    test_end();

    test_begin("latlon validation");
    test_assert(aprs_is_valid_latlon(0.0, 0.0) == true, "0,0 valid");
    test_assert(aprs_is_valid_latlon(90.0, 180.0) == true, "90,180 valid");
    test_assert(aprs_is_valid_latlon(-90.0, -180.0) == true, "-90,-180 valid");
    test_assert(aprs_is_valid_latlon(90.1, 0.0) == false, "90.1 invalid");
    test_assert(aprs_is_valid_latlon(0.0, 180.1) == false, "180.1 invalid");
    test_end();

    test_begin("packet type name");
    test_assert(strcmp(aprs_packet_type_name(APRS_PACKET_POSITION),
                       "position") == 0, "position name");
    test_assert(strcmp(aprs_packet_type_name(APRS_PACKET_MESSAGE),
                       "message") == 0, "message name");
    test_assert(strcmp(aprs_packet_type_name(APRS_PACKET_STATUS),
                       "status") == 0, "status name");
    test_assert(strcmp(aprs_packet_type_name(APRS_PACKET_UNKNOWN),
                       "unknown") == 0, "unknown name");
    test_end();

    /* ------------------------------------------------------------------ */
    /* error string                                                        */
    /* ------------------------------------------------------------------ */

    test_begin("aprs_strerror coverage");
    test_assert(strcmp(aprs_strerror(APRS_OK), "ok") == 0, "ok");
    test_assert(strcmp(aprs_strerror(APRS_ERR_PARSE), "parse error") == 0,
                "parse error");
    test_assert(strcmp(aprs_strerror(APRS_ERR_OVERFLOW),
                       "buffer overflow") == 0, "overflow");
    test_assert(strcmp(aprs_strerror((aprs_err_t)99), "unknown error") == 0,
                "unknown");
    test_end();

    /* ------------------------------------------------------------------ */
    /* real-world APRS-IS packet examples                                  */
    /* ------------------------------------------------------------------ */

    test_begin("real APRS-IS position: W3ADO-1");
    rc = aprs_parse_tnc2(
        "W3ADO-1>APDW15,TCPIP*,qAC,T2CAEAST:=3909.15N/07643.65W#PHG2360/W3, Baltimore, MD", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_POSITION_MSGCAP, "wrong type");
    test_assert(near(pkt.data.position.latitude, 39.1525, 0.01),
                "lat wrong");
    test_assert(near(pkt.data.position.longitude, -76.7275, 0.01),
                "lon wrong");
    test_end();

    test_begin("real APRS-IS message with path");
    rc = aprs_parse_tnc2(
        "KJ4ERJ-15>APRS,TCPIP*,qAC,T2ONTARIO::N3LLO-2  :good afternoon{3", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_MESSAGE, "wrong type");
    test_assert(strcmp(pkt.data.message.addressee, "N3LLO-2") == 0,
                "addressee wrong");
    test_assert(strcmp(pkt.data.message.text, "good afternoon") == 0,
                "text wrong");
    test_assert(strcmp(pkt.data.message.msgno, "3") == 0, "msgno wrong");
    test_end();

    test_begin("real APRS-IS status");
    rc = aprs_parse_tnc2(
        "VE3RSB-2>APQTH1,TCPIP*,qAC,VE3RSB-GS:>Prescott, ON iridge/prior", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_STATUS, "wrong type");
    test_assert(strcmp(pkt.data.status.text,
                       "Prescott, ON iridge/prior") == 0,
                "status text wrong");
    test_end();

    /* ================================================================== */
    /* Phase 5: expanded data types                                       */
    /* ================================================================== */

    /* ------------------------------------------------------------------ */
    /* compressed position                                                 */
    /* ------------------------------------------------------------------ */

    test_begin("parse compressed position (!)");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS:!/5L!!<*e7>7P[", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_POSITION, "wrong type");
    test_assert(near(pkt.data.position.latitude, 49.5, 0.5), "lat wrong");
    test_assert(near(pkt.data.position.longitude, -72.75, 0.5), "lon wrong");
    test_assert(pkt.data.position.symbol_table == '/', "sym table wrong");
    test_end();

    test_begin("parse compressed position (= msgcap)");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS:=/5L!!<*e7>7P[", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_POSITION_MSGCAP, "wrong type");
    test_end();

    test_begin("parse compressed with timestamp (@)");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS:@092345z/5L!!<*e7>7P[", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_POSITION_MSGCAP, "wrong type");
    test_assert(pkt.data.position.has_timestamp == true, "should have ts");
    test_assert(pkt.data.position.timestamp.day == 9, "ts day wrong");
    test_end();

    /* ------------------------------------------------------------------ */
    /* Mic-E                                                               */
    /* ------------------------------------------------------------------ */

    test_begin("parse Mic-E packet");
    rc = aprs_parse_tnc2(
        "N0CALL>T2SP0W:`(_fn\"Oj/", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_MIC_E, "wrong type");
    /* just verify lat/lon are in reasonable range */
    test_assert(pkt.data.position.latitude > -90.0 &&
                pkt.data.position.latitude < 90.0, "lat out of range");
    test_assert(pkt.data.position.longitude > -180.0 &&
                pkt.data.position.longitude < 180.0, "lon out of range");
    test_assert(pkt.data.position.has_course_speed == true,
                "should have course/speed");
    test_end();

    test_begin("Mic-E with ' DTI");
    rc = aprs_parse_tnc2(
        "N0CALL>T2SP0W:'(_fn\"Oj/", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_MIC_E, "wrong type");
    test_end();

    /* ------------------------------------------------------------------ */
    /* objects                                                             */
    /* ------------------------------------------------------------------ */

    test_begin("parse live object");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS:;LEADER   *092345z4903.50N/07201.75W>test obj", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_OBJECT, "wrong type");
    test_assert(strcmp(pkt.data.object.name, "LEADER") == 0, "name wrong");
    test_assert(pkt.data.object.live == true, "should be live");
    test_assert(pkt.data.object.has_timestamp == true, "should have ts");
    test_assert(pkt.data.object.timestamp.day == 9, "ts day wrong");
    test_assert(near(pkt.data.object.position.latitude, 49.0583, 0.01),
                "lat wrong");
    test_assert(near(pkt.data.object.position.longitude, -72.0291, 0.01),
                "lon wrong");
    test_assert(strcmp(pkt.data.object.position.comment, "test obj") == 0,
                "comment wrong");
    test_end();

    test_begin("parse killed object");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS:;LEADER   _092345z4903.50N/07201.75W>", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.data.object.live == false, "should be killed");
    test_end();

    test_begin("object name trimmed");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS:;AB       *092345z4903.50N/07201.75W>", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(strcmp(pkt.data.object.name, "AB") == 0,
                "name not trimmed");
    test_end();

    /* ------------------------------------------------------------------ */
    /* items                                                               */
    /* ------------------------------------------------------------------ */

    test_begin("parse live item");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS:)AID#2!4903.50N/07201.75W-item test", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_ITEM, "wrong type");
    test_assert(strcmp(pkt.data.item.name, "AID#2") == 0, "name wrong");
    test_assert(pkt.data.item.live == true, "should be live");
    test_assert(near(pkt.data.item.position.latitude, 49.0583, 0.01),
                "lat wrong");
    test_end();

    test_begin("parse killed item");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS:)AID#2_4903.50N/07201.75W-", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.data.item.live == false, "should be killed");
    test_end();

    /* ------------------------------------------------------------------ */
    /* weather                                                             */
    /* ------------------------------------------------------------------ */

    test_begin("parse positionless weather");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS:_10090556c220s004g005t077r000p000P000h50b10243", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_WEATHER, "wrong type");
    test_assert(pkt.data.weather.wind_dir_deg == 220, "wind dir wrong");
    test_assert(pkt.data.weather.wind_speed_mph == 4, "wind speed wrong");
    test_assert(pkt.data.weather.wind_gust_mph == 5, "gust wrong");
    test_assert(pkt.data.weather.valid_wind == true, "wind not valid");
    test_assert(pkt.data.weather.valid_temp == true, "temp not valid");
    test_assert(near(pkt.data.weather.temp_c, 25.0, 0.5), "temp wrong");
    test_assert(pkt.data.weather.humidity_pct == 50, "humidity wrong");
    test_assert(pkt.data.weather.valid_humidity == true, "humid not valid");
    test_assert(pkt.data.weather.valid_pressure == true, "pres not valid");
    test_assert(near(pkt.data.weather.pressure_hpa, 1024.3, 0.1),
                "pressure wrong");
    test_end();

    test_begin("weather h00 means 100%");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS:_10090556c000s000t070h00b10100", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.data.weather.humidity_pct == 100,
                "h00 should mean 100%");
    test_end();

    /* ------------------------------------------------------------------ */
    /* telemetry                                                           */
    /* ------------------------------------------------------------------ */

    test_begin("parse telemetry");
    rc = aprs_parse_tnc2(
        "N0CALL>APRS:T#005,199,000,255,073,123,01100001", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_TELEMETRY, "wrong type");
    test_assert(pkt.data.telemetry.seq == 5, "seq wrong");
    test_assert(pkt.data.telemetry.analog[0] == 199, "a1 wrong");
    test_assert(pkt.data.telemetry.analog[1] == 0, "a2 wrong");
    test_assert(pkt.data.telemetry.analog[2] == 255, "a3 wrong");
    test_assert(pkt.data.telemetry.analog[3] == 73, "a4 wrong");
    test_assert(pkt.data.telemetry.analog[4] == 123, "a5 wrong");
    test_assert(pkt.data.telemetry.digital == 0x61, "digital wrong");
    test_end();

    test_begin("telemetry with just sequence");
    rc = aprs_parse_tnc2("N0CALL>APRS:T#042", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.data.telemetry.seq == 42, "seq wrong");
    test_end();

    /* ------------------------------------------------------------------ */
    /* real-world Phase 5 packets from APRS-IS                            */
    /* ------------------------------------------------------------------ */

    test_begin("real compressed position from APRS-IS");
    rc = aprs_parse_tnc2(
        "DW9280>APRS,TCPIP*,qAC,T2CZECH:@092345z/5L!!<*e7>7P[", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_POSITION_MSGCAP, "wrong type");
    test_assert(pkt.data.position.has_timestamp == true, "should have ts");
    test_end();

    test_begin("real object from APRS-IS");
    rc = aprs_parse_tnc2(
        "WA4DSY>APX210,TCPIP*,qAC,CORE:;444.350TN*111111z3546.90N/08357.16W-T107 R60m", &pkt);
    test_assert(rc == APRS_OK, "parse failed");
    test_assert(pkt.type == APRS_PACKET_OBJECT, "wrong type");
    test_assert(strcmp(pkt.data.object.name, "444.350TN") == 0, "name wrong");
    test_assert(pkt.data.object.live == true, "should be live");
    test_end();
}
