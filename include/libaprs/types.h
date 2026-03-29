#ifndef LIBAPRS_TYPES_H
#define LIBAPRS_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APRS_CALLSIGN_MAX  10
#define APRS_PATH_MAX      8
#define APRS_TEXT_MAX      256
#define APRS_INFO_MAX      512

typedef struct {
    char callsign[APRS_CALLSIGN_MAX];
} aprs_address_t;

typedef enum {
    APRS_PACKET_UNKNOWN = 0,
    APRS_PACKET_POSITION,
    APRS_PACKET_POSITION_MSGCAP,
    APRS_PACKET_STATUS,
    APRS_PACKET_MESSAGE,
    APRS_PACKET_OBJECT,
    APRS_PACKET_ITEM,
    APRS_PACKET_WEATHER,
    APRS_PACKET_TELEMETRY,
    APRS_PACKET_QUERY,
    APRS_PACKET_THIRDPARTY,
    APRS_PACKET_MIC_E
} aprs_packet_type_t;

typedef struct {
    int day;
    int hour;
    int minute;
    int second;
    char type;         /* 'z'=UTC DHM, '/'=local DHM, 'h'=UTC HMS, 0=none */
} aprs_timestamp_t;

typedef struct {
    double latitude;
    double longitude;
    char symbol_table;
    char symbol_code;
    int course;
    int speed_knots;
    int altitude_ft;
    bool has_course_speed;
    bool has_altitude;
    bool has_timestamp;
    bool messaging;    /* true for = and @ packets */
    aprs_timestamp_t timestamp;
    char comment[APRS_TEXT_MAX];
} aprs_position_t;

typedef struct {
    char addressee[10];
    char text[APRS_TEXT_MAX];
    char msgno[8];
    bool ack;
    bool rej;
} aprs_message_t;

typedef struct {
    char text[APRS_TEXT_MAX];
} aprs_status_t;

typedef struct {
    int wind_dir_deg;
    int wind_speed_mph;
    int wind_gust_mph;
    float temp_c;
    float rain_1h_mm;
    float rain_24h_mm;
    float rain_since_midnight_mm;
    int humidity_pct;
    float pressure_hpa;
    bool valid_wind;
    bool valid_temp;
    bool valid_rain;
    bool valid_humidity;
    bool valid_pressure;
} aprs_weather_t;

typedef struct {
    uint16_t seq;
    int analog[5];
    uint8_t digital;
    char comment[APRS_TEXT_MAX];
} aprs_telemetry_t;

typedef struct {
    char name[10];              /* up to 9 chars */
    bool live;                  /* true = *, false = _ (killed) */
    bool has_timestamp;
    aprs_timestamp_t timestamp;
    aprs_position_t position;
} aprs_object_t;

typedef struct {
    char name[10];              /* 3-9 chars */
    bool live;                  /* true = !, false = _ (killed) */
    aprs_position_t position;
} aprs_item_t;

typedef struct {
    aprs_packet_type_t type;
    aprs_address_t src;
    aprs_address_t dst;
    aprs_address_t path[APRS_PATH_MAX];
    size_t path_len;
    char raw_info[APRS_INFO_MAX];

    union {
        aprs_position_t  position;
        aprs_message_t   message;
        aprs_status_t    status;
        aprs_weather_t   weather;
        aprs_telemetry_t telemetry;
        aprs_object_t    object;
        aprs_item_t      item;
    } data;
} aprs_packet_t;

#ifdef __cplusplus
}
#endif

#endif /* LIBAPRS_TYPES_H */
