#ifndef LIBAPRS_STATION_H
#define LIBAPRS_STATION_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "error.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* station record                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    char callsign[APRS_CALLSIGN_MAX];
    time_t last_heard;
    int packet_count;

    bool has_position;
    double latitude;
    double longitude;
    char symbol_table;
    char symbol_code;

    char last_comment[APRS_TEXT_MAX];
    char last_status[APRS_TEXT_MAX];
} aprs_station_t;

/* ------------------------------------------------------------------ */
/* station database                                                    */
/* ------------------------------------------------------------------ */

#define APRS_STATION_DB_DEFAULT_SIZE  256

typedef struct aprs_station_db aprs_station_db_t;

aprs_station_db_t *aprs_station_db_create(size_t max_stations);
void               aprs_station_db_destroy(aprs_station_db_t *db);

/*
 * Update the database with a received packet.
 * Creates a new station entry if not already present.
 * Updates last_heard, packet_count, position, status, comment.
 */
aprs_err_t aprs_station_db_update(aprs_station_db_t *db,
                                  const aprs_packet_t *pkt, time_t now);

/*
 * Find a station by callsign.  Returns NULL if not found.
 * The returned pointer is valid until the next update or expire call.
 */
const aprs_station_t *aprs_station_db_find(const aprs_station_db_t *db,
                                           const char *callsign);

/* Number of stations currently in the database. */
size_t aprs_station_db_count(const aprs_station_db_t *db);

/*
 * Copy up to max stations into out[].  Returns actual count in *count.
 * Stations are returned in no particular order.
 */
aprs_err_t aprs_station_db_list(const aprs_station_db_t *db,
                                aprs_station_t *out, size_t max,
                                size_t *count);

/*
 * Remove all stations not heard since `older_than`.
 * Returns the number of stations removed.
 */
size_t aprs_station_db_expire(aprs_station_db_t *db, time_t older_than);

/* ------------------------------------------------------------------ */
/* message sequence tracker                                            */
/* ------------------------------------------------------------------ */

#define APRS_MSG_TRACKER_MAX  64

typedef enum {
    APRS_MSG_PENDING = 0,
    APRS_MSG_ACKED,
    APRS_MSG_REJECTED
} aprs_msg_state_t;

typedef struct {
    char addressee[APRS_CALLSIGN_MAX];
    char text[APRS_TEXT_MAX];
    char msgno[8];
    aprs_msg_state_t state;
    time_t sent_at;
    int retries;
} aprs_msg_entry_t;

typedef struct {
    aprs_msg_entry_t entries[APRS_MSG_TRACKER_MAX];
    int count;
    int next_seq;
} aprs_msg_tracker_t;

void aprs_msg_tracker_init(aprs_msg_tracker_t *t);

/* Get the next sequence number as a string (writes into buf, >= 6 bytes). */
int aprs_msg_tracker_next_seq(aprs_msg_tracker_t *t, char *buf, size_t buflen);

/*
 * Record an outgoing message.  Returns the index into entries[],
 * or -1 if the tracker is full.
 */
int aprs_msg_tracker_send(aprs_msg_tracker_t *t, const char *addressee,
                          const char *text, const char *msgno, time_t now);

/*
 * Mark a message as acked or rejected by msgno.
 * Returns true if found and updated, false if not found.
 */
bool aprs_msg_tracker_ack(aprs_msg_tracker_t *t, const char *msgno);
bool aprs_msg_tracker_rej(aprs_msg_tracker_t *t, const char *msgno);

/*
 * Count messages in a given state.
 */
int aprs_msg_tracker_count(const aprs_msg_tracker_t *t, aprs_msg_state_t state);

/* ------------------------------------------------------------------ */
/* JSON export                                                         */
/* ------------------------------------------------------------------ */

aprs_err_t aprs_station_to_json(const aprs_station_t *st,
                                char *buf, size_t buflen);

aprs_err_t aprs_packet_to_json(const aprs_packet_t *pkt,
                                char *buf, size_t buflen);

/* ------------------------------------------------------------------ */
/* frame duplicate detection (for RF)                                  */
/* ------------------------------------------------------------------ */

#define APRS_FRAME_DEDUP_SLOTS 256

typedef struct {
    uint32_t hashes[APRS_FRAME_DEDUP_SLOTS];
    int head;
    int count;
} aprs_frame_dedup_t;

void aprs_frame_dedup_init(aprs_frame_dedup_t *d);

/*
 * Check if a raw frame (binary bytes) was seen recently.
 * Returns true if duplicate, false if new (and adds to ring).
 */
bool aprs_frame_dedup_check(aprs_frame_dedup_t *d,
                            const uint8_t *frame, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* LIBAPRS_STATION_H */
