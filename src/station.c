/*
 * station.c — station database, message tracker, JSON export, frame dedup
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "libaprs/station.h"
#include "libaprs/aprs.h"

/* ================================================================== */
/* station database — simple open-addressing hash table                */
/* ================================================================== */

struct aprs_station_db {
    aprs_station_t *slots;
    bool *occupied;
    size_t capacity;
    size_t count;
};

static uint32_t station_hash(const char *callsign)
{
    uint32_t h = 2166136261u;
    for (; *callsign; callsign++) {
        h ^= (uint8_t)*callsign;
        h *= 16777619u;
    }
    return h;
}

aprs_station_db_t *aprs_station_db_create(size_t max_stations)
{
    aprs_station_db_t *db;

    if (max_stations == 0)
        max_stations = APRS_STATION_DB_DEFAULT_SIZE;

    db = (aprs_station_db_t *)calloc(1, sizeof(*db));
    if (!db) return NULL;

    db->slots = (aprs_station_t *)calloc(max_stations, sizeof(aprs_station_t));
    db->occupied = (bool *)calloc(max_stations, sizeof(bool));
    if (!db->slots || !db->occupied) {
        free(db->slots);
        free(db->occupied);
        free(db);
        return NULL;
    }

    db->capacity = max_stations;
    db->count = 0;
    return db;
}

void aprs_station_db_destroy(aprs_station_db_t *db)
{
    if (!db) return;
    free(db->slots);
    free(db->occupied);
    free(db);
}

static aprs_station_t *db_find_slot(aprs_station_db_t *db,
                                    const char *callsign,
                                    bool create)
{
    uint32_t h = station_hash(callsign);
    size_t idx = h % db->capacity;
    size_t start = idx;

    do {
        if (!db->occupied[idx]) {
            if (!create) return NULL;
            /* claim this slot */
            db->occupied[idx] = true;
            db->count++;
            memset(&db->slots[idx], 0, sizeof(aprs_station_t));
            snprintf(db->slots[idx].callsign,
                     sizeof(db->slots[idx].callsign), "%s", callsign);
            return &db->slots[idx];
        }
        if (strcmp(db->slots[idx].callsign, callsign) == 0)
            return &db->slots[idx];
        idx = (idx + 1) % db->capacity;
    } while (idx != start);

    return NULL; /* table full */
}

aprs_err_t aprs_station_db_update(aprs_station_db_t *db,
                                  const aprs_packet_t *pkt, time_t now)
{
    aprs_station_t *st;

    if (!db || !pkt) return APRS_ERR_INVALID_ARG;
    if (!pkt->src.callsign[0]) return APRS_ERR_INVALID_ARG;

    st = db_find_slot(db, pkt->src.callsign, true);
    if (!st) return APRS_ERR_NOMEM; /* table full */

    st->last_heard = now;
    st->packet_count++;

    switch (pkt->type) {
    case APRS_PACKET_POSITION:
    case APRS_PACKET_POSITION_MSGCAP:
    case APRS_PACKET_MIC_E:
        st->has_position = true;
        st->latitude = pkt->data.position.latitude;
        st->longitude = pkt->data.position.longitude;
        st->symbol_table = pkt->data.position.symbol_table;
        st->symbol_code = pkt->data.position.symbol_code;
        if (pkt->data.position.comment[0])
            snprintf(st->last_comment, sizeof(st->last_comment),
                     "%s", pkt->data.position.comment);
        break;
    case APRS_PACKET_OBJECT:
        st->has_position = true;
        st->latitude = pkt->data.object.position.latitude;
        st->longitude = pkt->data.object.position.longitude;
        st->symbol_table = pkt->data.object.position.symbol_table;
        st->symbol_code = pkt->data.object.position.symbol_code;
        break;
    case APRS_PACKET_STATUS:
        snprintf(st->last_status, sizeof(st->last_status),
                 "%s", pkt->data.status.text);
        break;
    default:
        break;
    }

    return APRS_OK;
}

const aprs_station_t *aprs_station_db_find(const aprs_station_db_t *db,
                                           const char *callsign)
{
    if (!db || !callsign) return NULL;
    return db_find_slot((aprs_station_db_t *)db, callsign, false);
}

size_t aprs_station_db_count(const aprs_station_db_t *db)
{
    return db ? db->count : 0;
}

aprs_err_t aprs_station_db_list(const aprs_station_db_t *db,
                                aprs_station_t *out, size_t max,
                                size_t *count)
{
    size_t i, n = 0;

    if (!db || !out || !count) return APRS_ERR_INVALID_ARG;

    for (i = 0; i < db->capacity && n < max; i++) {
        if (db->occupied[i])
            out[n++] = db->slots[i];
    }
    *count = n;
    return APRS_OK;
}

size_t aprs_station_db_expire(aprs_station_db_t *db, time_t older_than)
{
    size_t i, removed = 0;

    if (!db) return 0;

    for (i = 0; i < db->capacity; i++) {
        if (db->occupied[i] && db->slots[i].last_heard < older_than) {
            db->occupied[i] = false;
            memset(&db->slots[i], 0, sizeof(aprs_station_t));
            db->count--;
            removed++;
        }
    }
    return removed;
}

/* ================================================================== */
/* message sequence tracker                                            */
/* ================================================================== */

void aprs_msg_tracker_init(aprs_msg_tracker_t *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->next_seq = 1;
}

int aprs_msg_tracker_next_seq(aprs_msg_tracker_t *t, char *buf, size_t buflen)
{
    int seq;
    if (!t || !buf || buflen < 2) return -1;
    seq = t->next_seq++;
    if (t->next_seq > 99999) t->next_seq = 1;
    snprintf(buf, buflen, "%d", seq);
    return seq;
}

int aprs_msg_tracker_send(aprs_msg_tracker_t *t, const char *addressee,
                          const char *text, const char *msgno, time_t now)
{
    aprs_msg_entry_t *e;

    if (!t || !addressee || !text || !msgno) return -1;
    if (t->count >= APRS_MSG_TRACKER_MAX) return -1;

    e = &t->entries[t->count];
    snprintf(e->addressee, sizeof(e->addressee), "%s", addressee);
    snprintf(e->text, sizeof(e->text), "%s", text);
    snprintf(e->msgno, sizeof(e->msgno), "%s", msgno);
    e->state = APRS_MSG_PENDING;
    e->sent_at = now;
    e->retries = 0;

    return t->count++;
}

bool aprs_msg_tracker_ack(aprs_msg_tracker_t *t, const char *msgno)
{
    int i;
    if (!t || !msgno) return false;

    for (i = 0; i < t->count; i++) {
        if (t->entries[i].state == APRS_MSG_PENDING &&
            strcmp(t->entries[i].msgno, msgno) == 0) {
            t->entries[i].state = APRS_MSG_ACKED;
            return true;
        }
    }
    return false;
}

bool aprs_msg_tracker_rej(aprs_msg_tracker_t *t, const char *msgno)
{
    int i;
    if (!t || !msgno) return false;

    for (i = 0; i < t->count; i++) {
        if (t->entries[i].state == APRS_MSG_PENDING &&
            strcmp(t->entries[i].msgno, msgno) == 0) {
            t->entries[i].state = APRS_MSG_REJECTED;
            return true;
        }
    }
    return false;
}

int aprs_msg_tracker_count(const aprs_msg_tracker_t *t,
                           aprs_msg_state_t state)
{
    int i, n = 0;
    if (!t) return 0;
    for (i = 0; i < t->count; i++) {
        if (t->entries[i].state == state)
            n++;
    }
    return n;
}

/* ================================================================== */
/* JSON export                                                         */
/* ================================================================== */

/* Escape a string for JSON — write into dst, return chars written.
 * Does not NUL-terminate.  Returns -1 on overflow. */
static int json_escape(char *dst, size_t dstlen, const char *src)
{
    size_t pos = 0;
    for (; *src; src++) {
        char c = *src;
        if (c == '"' || c == '\\') {
            if (pos + 2 > dstlen) return -1;
            dst[pos++] = '\\';
            dst[pos++] = c;
        } else if ((unsigned char)c < 0x20) {
            /* control chars — skip */
        } else {
            if (pos + 1 > dstlen) return -1;
            dst[pos++] = c;
        }
    }
    return (int)pos;
}

aprs_err_t aprs_station_to_json(const aprs_station_t *st,
                                char *buf, size_t buflen)
{
    int n;
    char esc_call[32], esc_cmt[APRS_TEXT_MAX * 2], esc_stat[APRS_TEXT_MAX * 2];
    int clen, cmtlen, statlen;

    if (!st || !buf || buflen == 0) return APRS_ERR_INVALID_ARG;

    clen = json_escape(esc_call, sizeof(esc_call), st->callsign);
    if (clen < 0) return APRS_ERR_OVERFLOW;
    esc_call[clen] = '\0';

    cmtlen = json_escape(esc_cmt, sizeof(esc_cmt), st->last_comment);
    if (cmtlen < 0) cmtlen = 0;
    esc_cmt[cmtlen] = '\0';

    statlen = json_escape(esc_stat, sizeof(esc_stat), st->last_status);
    if (statlen < 0) statlen = 0;
    esc_stat[statlen] = '\0';

    if (st->has_position) {
        n = snprintf(buf, buflen,
            "{\"callsign\":\"%s\",\"last_heard\":%ld,"
            "\"packets\":%d,"
            "\"lat\":%.6f,\"lon\":%.6f,"
            "\"symbol\":\"%c%c\","
            "\"comment\":\"%s\",\"status\":\"%s\"}",
            esc_call, (long)st->last_heard,
            st->packet_count,
            st->latitude, st->longitude,
            st->symbol_table, st->symbol_code,
            esc_cmt, esc_stat);
    } else {
        n = snprintf(buf, buflen,
            "{\"callsign\":\"%s\",\"last_heard\":%ld,"
            "\"packets\":%d,"
            "\"comment\":\"%s\",\"status\":\"%s\"}",
            esc_call, (long)st->last_heard,
            st->packet_count,
            esc_cmt, esc_stat);
    }

    if (n < 0 || (size_t)n >= buflen) return APRS_ERR_OVERFLOW;
    return APRS_OK;
}

aprs_err_t aprs_packet_to_json(const aprs_packet_t *pkt,
                                char *buf, size_t buflen)
{
    int n;
    char esc_src[32], esc_dst[32], esc_info[APRS_INFO_MAX * 2];
    int slen, dlen, ilen;

    if (!pkt || !buf || buflen == 0) return APRS_ERR_INVALID_ARG;

    slen = json_escape(esc_src, sizeof(esc_src), pkt->src.callsign);
    if (slen < 0) return APRS_ERR_OVERFLOW;
    esc_src[slen] = '\0';

    dlen = json_escape(esc_dst, sizeof(esc_dst), pkt->dst.callsign);
    if (dlen < 0) return APRS_ERR_OVERFLOW;
    esc_dst[dlen] = '\0';

    ilen = json_escape(esc_info, sizeof(esc_info), pkt->raw_info);
    if (ilen < 0) return APRS_ERR_OVERFLOW;
    esc_info[ilen] = '\0';

    n = snprintf(buf, buflen,
        "{\"src\":\"%s\",\"dst\":\"%s\","
        "\"type\":\"%s\",\"info\":\"%s\"}",
        esc_src, esc_dst,
        aprs_packet_type_name(pkt->type),
        esc_info);

    if (n < 0 || (size_t)n >= buflen) return APRS_ERR_OVERFLOW;
    return APRS_OK;
}

/* ================================================================== */
/* frame duplicate detection                                           */
/* ================================================================== */

void aprs_frame_dedup_init(aprs_frame_dedup_t *d)
{
    if (!d) return;
    memset(d, 0, sizeof(*d));
}

static uint32_t frame_hash(const uint8_t *data, size_t len)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

bool aprs_frame_dedup_check(aprs_frame_dedup_t *d,
                            const uint8_t *frame, size_t len)
{
    uint32_t h;
    int i;

    if (!d || !frame || len == 0) return false;

    h = frame_hash(frame, len);

    for (i = 0; i < d->count; i++) {
        int idx = (d->head - 1 - i + APRS_FRAME_DEDUP_SLOTS)
                  % APRS_FRAME_DEDUP_SLOTS;
        if (d->hashes[idx] == h)
            return true;
    }

    d->hashes[d->head] = h;
    d->head = (d->head + 1) % APRS_FRAME_DEDUP_SLOTS;
    if (d->count < APRS_FRAME_DEDUP_SLOTS)
        d->count++;

    return false;
}
