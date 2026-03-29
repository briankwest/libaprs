/*
 * test_station.c — station database, message tracker, JSON, frame dedup
 */

#include <stdio.h>
#include <string.h>
#include "libaprs/aprs.h"
#include "libaprs/station.h"

extern void test_begin(const char *name);
extern void test_end(void);
extern void test_assert(int cond, const char *msg);

void test_station(void)
{
    /* ================================================================ */
    /* station database                                                  */
    /* ================================================================ */

    test_begin("station db create and destroy");
    {
        aprs_station_db_t *db = aprs_station_db_create(0);
        test_assert(db != NULL, "create returned NULL");
        test_assert(aprs_station_db_count(db) == 0, "should be empty");
        aprs_station_db_destroy(db);
    }
    test_end();

    test_begin("station db update creates entry");
    {
        aprs_station_db_t *db = aprs_station_db_create(64);
        aprs_packet_t pkt;
        const aprs_station_t *st;

        aprs_parse_tnc2("N0CALL>APRS:>on the air", &pkt);
        aprs_station_db_update(db, &pkt, 1000);

        test_assert(aprs_station_db_count(db) == 1, "count should be 1");
        st = aprs_station_db_find(db, "N0CALL");
        test_assert(st != NULL, "should find N0CALL");
        test_assert(st->last_heard == 1000, "last_heard wrong");
        test_assert(st->packet_count == 1, "packet_count wrong");
        test_assert(strcmp(st->last_status, "on the air") == 0,
                    "status wrong");
        aprs_station_db_destroy(db);
    }
    test_end();

    test_begin("station db update increments count");
    {
        aprs_station_db_t *db = aprs_station_db_create(64);
        aprs_packet_t pkt;
        const aprs_station_t *st;

        aprs_parse_tnc2("N0CALL>APRS:>first", &pkt);
        aprs_station_db_update(db, &pkt, 1000);

        aprs_parse_tnc2("N0CALL>APRS:>second", &pkt);
        aprs_station_db_update(db, &pkt, 2000);

        test_assert(aprs_station_db_count(db) == 1, "still 1 station");
        st = aprs_station_db_find(db, "N0CALL");
        test_assert(st->packet_count == 2, "should be 2 packets");
        test_assert(st->last_heard == 2000, "should update time");
        test_assert(strcmp(st->last_status, "second") == 0,
                    "status should update");
        aprs_station_db_destroy(db);
    }
    test_end();

    test_begin("station db tracks position");
    {
        aprs_station_db_t *db = aprs_station_db_create(64);
        aprs_packet_t pkt;
        const aprs_station_t *st;

        aprs_parse_tnc2("N0CALL>APRS:!4903.50N/07201.75W-PHG2360", &pkt);
        aprs_station_db_update(db, &pkt, 1000);

        st = aprs_station_db_find(db, "N0CALL");
        test_assert(st != NULL, "should find");
        test_assert(st->has_position == true, "should have position");
        test_assert(st->latitude > 49.0 && st->latitude < 49.1,
                    "lat wrong");
        test_assert(st->longitude < -72.0 && st->longitude > -72.1,
                    "lon wrong");
        test_assert(strcmp(st->last_comment, "PHG2360") == 0,
                    "comment wrong");
        aprs_station_db_destroy(db);
    }
    test_end();

    test_begin("station db multiple stations");
    {
        aprs_station_db_t *db = aprs_station_db_create(64);
        aprs_packet_t pkt;

        aprs_parse_tnc2("N0CALL>APRS:>hello", &pkt);
        aprs_station_db_update(db, &pkt, 1000);

        aprs_parse_tnc2("W3ADO>APRS:>world", &pkt);
        aprs_station_db_update(db, &pkt, 2000);

        test_assert(aprs_station_db_count(db) == 2, "should be 2");
        test_assert(aprs_station_db_find(db, "N0CALL") != NULL,
                    "should find N0CALL");
        test_assert(aprs_station_db_find(db, "W3ADO") != NULL,
                    "should find W3ADO");
        test_assert(aprs_station_db_find(db, "NOBODY") == NULL,
                    "should not find NOBODY");
        aprs_station_db_destroy(db);
    }
    test_end();

    test_begin("station db list");
    {
        aprs_station_db_t *db = aprs_station_db_create(64);
        aprs_packet_t pkt;
        aprs_station_t list[10];
        size_t count;

        aprs_parse_tnc2("AA1BB>APRS:>a", &pkt);
        aprs_station_db_update(db, &pkt, 100);
        aprs_parse_tnc2("CC2DD>APRS:>b", &pkt);
        aprs_station_db_update(db, &pkt, 200);

        aprs_station_db_list(db, list, 10, &count);
        test_assert(count == 2, "should list 2");
        aprs_station_db_destroy(db);
    }
    test_end();

    test_begin("station db expire");
    {
        aprs_station_db_t *db = aprs_station_db_create(64);
        aprs_packet_t pkt;
        size_t removed;

        aprs_parse_tnc2("OLD>APRS:>old", &pkt);
        aprs_station_db_update(db, &pkt, 100);

        aprs_parse_tnc2("NEW>APRS:>new", &pkt);
        aprs_station_db_update(db, &pkt, 500);

        test_assert(aprs_station_db_count(db) == 2, "should be 2");

        removed = aprs_station_db_expire(db, 300);
        test_assert(removed == 1, "should remove 1");
        test_assert(aprs_station_db_count(db) == 1, "should be 1 left");
        test_assert(aprs_station_db_find(db, "OLD") == NULL,
                    "OLD should be gone");
        test_assert(aprs_station_db_find(db, "NEW") != NULL,
                    "NEW should remain");
        aprs_station_db_destroy(db);
    }
    test_end();

    test_begin("station db rejects NULL");
    {
        test_assert(aprs_station_db_update(NULL, NULL, 0)
                    == APRS_ERR_INVALID_ARG, "should reject");
        test_assert(aprs_station_db_find(NULL, "X") == NULL,
                    "should return NULL");
        test_assert(aprs_station_db_count(NULL) == 0,
                    "should return 0");
    }
    test_end();

    /* ================================================================ */
    /* message tracker                                                   */
    /* ================================================================ */

    test_begin("msg tracker init and next_seq");
    {
        aprs_msg_tracker_t t;
        char seq[8];
        int n;

        aprs_msg_tracker_init(&t);
        n = aprs_msg_tracker_next_seq(&t, seq, sizeof(seq));
        test_assert(n == 1, "first seq should be 1");
        test_assert(strcmp(seq, "1") == 0, "seq string wrong");

        n = aprs_msg_tracker_next_seq(&t, seq, sizeof(seq));
        test_assert(n == 2, "second seq should be 2");
    }
    test_end();

    test_begin("msg tracker send and ack");
    {
        aprs_msg_tracker_t t;
        int idx;

        aprs_msg_tracker_init(&t);
        idx = aprs_msg_tracker_send(&t, "W3ADO", "Hello", "1", 1000);
        test_assert(idx == 0, "first entry index should be 0");
        test_assert(aprs_msg_tracker_count(&t, APRS_MSG_PENDING) == 1,
                    "should be 1 pending");

        test_assert(aprs_msg_tracker_ack(&t, "1") == true,
                    "ack should succeed");
        test_assert(aprs_msg_tracker_count(&t, APRS_MSG_PENDING) == 0,
                    "should be 0 pending");
        test_assert(aprs_msg_tracker_count(&t, APRS_MSG_ACKED) == 1,
                    "should be 1 acked");
    }
    test_end();

    test_begin("msg tracker reject");
    {
        aprs_msg_tracker_t t;

        aprs_msg_tracker_init(&t);
        aprs_msg_tracker_send(&t, "W3ADO", "Test", "5", 1000);

        test_assert(aprs_msg_tracker_rej(&t, "5") == true,
                    "rej should succeed");
        test_assert(aprs_msg_tracker_count(&t, APRS_MSG_REJECTED) == 1,
                    "should be 1 rejected");
    }
    test_end();

    test_begin("msg tracker ack unknown msgno");
    {
        aprs_msg_tracker_t t;
        aprs_msg_tracker_init(&t);
        test_assert(aprs_msg_tracker_ack(&t, "999") == false,
                    "should not find unknown");
    }
    test_end();

    test_begin("msg tracker multiple messages");
    {
        aprs_msg_tracker_t t;
        aprs_msg_tracker_init(&t);

        aprs_msg_tracker_send(&t, "AA", "msg1", "10", 100);
        aprs_msg_tracker_send(&t, "BB", "msg2", "11", 200);
        aprs_msg_tracker_send(&t, "CC", "msg3", "12", 300);

        test_assert(aprs_msg_tracker_count(&t, APRS_MSG_PENDING) == 3,
                    "should be 3 pending");

        aprs_msg_tracker_ack(&t, "11");
        test_assert(aprs_msg_tracker_count(&t, APRS_MSG_PENDING) == 2,
                    "should be 2 pending");
        test_assert(aprs_msg_tracker_count(&t, APRS_MSG_ACKED) == 1,
                    "should be 1 acked");
    }
    test_end();

    /* ================================================================ */
    /* JSON export                                                       */
    /* ================================================================ */

    test_begin("station to JSON");
    {
        aprs_station_db_t *db = aprs_station_db_create(64);
        aprs_packet_t pkt;
        const aprs_station_t *st;
        char json[1024];

        aprs_parse_tnc2("N0CALL>APRS:!4903.50N/07201.75W-Test", &pkt);
        aprs_station_db_update(db, &pkt, 1000);

        st = aprs_station_db_find(db, "N0CALL");
        test_assert(st != NULL, "should find");

        test_assert(aprs_station_to_json(st, json, sizeof(json)) == APRS_OK,
                    "json failed");
        test_assert(strstr(json, "\"callsign\":\"N0CALL\"") != NULL,
                    "missing callsign");
        test_assert(strstr(json, "\"lat\":") != NULL,
                    "missing lat");
        test_assert(strstr(json, "\"lon\":") != NULL,
                    "missing lon");
        test_assert(strstr(json, "\"packets\":1") != NULL,
                    "missing packets");
        aprs_station_db_destroy(db);
    }
    test_end();

    test_begin("station to JSON without position");
    {
        aprs_station_db_t *db = aprs_station_db_create(64);
        aprs_packet_t pkt;
        const aprs_station_t *st;
        char json[1024];

        aprs_parse_tnc2("N0CALL>APRS:>status only", &pkt);
        aprs_station_db_update(db, &pkt, 1000);

        st = aprs_station_db_find(db, "N0CALL");
        test_assert(aprs_station_to_json(st, json, sizeof(json)) == APRS_OK,
                    "json failed");
        test_assert(strstr(json, "\"lat\":") == NULL,
                    "should not have lat");
        test_assert(strstr(json, "\"status\":\"status only\"") != NULL,
                    "missing status");
        aprs_station_db_destroy(db);
    }
    test_end();

    test_begin("packet to JSON");
    {
        aprs_packet_t pkt;
        char json[1024];

        aprs_parse_tnc2("N0CALL>APRS:>hello world", &pkt);
        test_assert(aprs_packet_to_json(&pkt, json, sizeof(json)) == APRS_OK,
                    "json failed");
        test_assert(strstr(json, "\"src\":\"N0CALL\"") != NULL,
                    "missing src");
        test_assert(strstr(json, "\"type\":\"status\"") != NULL,
                    "missing type");
        test_assert(strstr(json, "\"info\":\">hello world\"") != NULL,
                    "missing info");
    }
    test_end();

    test_begin("JSON rejects NULL");
    {
        char buf[64];
        test_assert(aprs_station_to_json(NULL, buf, sizeof(buf))
                    == APRS_ERR_INVALID_ARG, "should reject");
        test_assert(aprs_packet_to_json(NULL, buf, sizeof(buf))
                    == APRS_ERR_INVALID_ARG, "should reject");
    }
    test_end();

    test_begin("JSON overflow");
    {
        aprs_packet_t pkt;
        char buf[10];
        aprs_parse_tnc2("N0CALL>APRS:>test", &pkt);
        test_assert(aprs_packet_to_json(&pkt, buf, sizeof(buf))
                    == APRS_ERR_OVERFLOW, "should overflow");
    }
    test_end();

    /* ================================================================ */
    /* frame dedup                                                       */
    /* ================================================================ */

    test_begin("frame dedup first is not duplicate");
    {
        aprs_frame_dedup_t d;
        uint8_t frame[] = {0x01, 0x02, 0x03};
        aprs_frame_dedup_init(&d);
        test_assert(aprs_frame_dedup_check(&d, frame, 3) == false,
                    "first should not be dup");
    }
    test_end();

    test_begin("frame dedup second is duplicate");
    {
        aprs_frame_dedup_t d;
        uint8_t frame[] = {0x01, 0x02, 0x03};
        aprs_frame_dedup_init(&d);
        aprs_frame_dedup_check(&d, frame, 3);
        test_assert(aprs_frame_dedup_check(&d, frame, 3) == true,
                    "second should be dup");
    }
    test_end();

    test_begin("frame dedup different frames");
    {
        aprs_frame_dedup_t d;
        uint8_t f1[] = {0x01, 0x02};
        uint8_t f2[] = {0x03, 0x04};
        aprs_frame_dedup_init(&d);
        aprs_frame_dedup_check(&d, f1, 2);
        test_assert(aprs_frame_dedup_check(&d, f2, 2) == false,
                    "different should not be dup");
    }
    test_end();

    test_begin("frame dedup ring eviction");
    {
        aprs_frame_dedup_t d;
        uint8_t f[2];
        int i;
        aprs_frame_dedup_init(&d);

        /* fill ring */
        for (i = 0; i < APRS_FRAME_DEDUP_SLOTS; i++) {
            f[0] = (uint8_t)(i >> 8);
            f[1] = (uint8_t)(i & 0xFF);
            aprs_frame_dedup_check(&d, f, 2);
        }

        /* first entry should still be there */
        f[0] = 0; f[1] = 0;
        test_assert(aprs_frame_dedup_check(&d, f, 2) == true,
                    "should still be in ring");

        /* add one more to evict */
        f[0] = 0xFF; f[1] = 0xFF;
        aprs_frame_dedup_check(&d, f, 2);

        /* now first should be gone */
        f[0] = 0; f[1] = 0;
        test_assert(aprs_frame_dedup_check(&d, f, 2) == false,
                    "should be evicted");
    }
    test_end();

    test_begin("frame dedup NULL handling");
    {
        aprs_frame_dedup_t d;
        aprs_frame_dedup_init(&d);
        test_assert(aprs_frame_dedup_check(&d, NULL, 0) == false,
                    "NULL should not crash");
        test_assert(aprs_frame_dedup_check(NULL, (uint8_t*)"x", 1) == false,
                    "NULL dedup should not crash");
    }
    test_end();
}
