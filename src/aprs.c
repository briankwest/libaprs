/*
 * aprs.c — TNC2 parsing, formatting, and packet builders for libaprs
 *
 * Phase 1: uncompressed position, message, status
 * Phase 5: compressed position, Mic-E, objects, items, weather, telemetry
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "libaprs/aprs.h"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void copy_str(char *dst, size_t dstsz, const char *src)
{
    size_t len;
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    len = strlen(src);
    if (len >= dstsz) len = dstsz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void lat_to_aprs(double lat, char *buf)
{
    char ns = (lat >= 0.0) ? 'N' : 'S';
    double a = fabs(lat);
    int deg = (int)a;
    double min = (a - deg) * 60.0;
    snprintf(buf, 9, "%02d%05.2f%c", deg, min, ns);
}

static void lon_to_aprs(double lon, char *buf)
{
    char ew = (lon >= 0.0) ? 'E' : 'W';
    double a = fabs(lon);
    int deg = (int)a;
    double min = (a - deg) * 60.0;
    snprintf(buf, 10, "%03d%05.2f%c", deg, min, ew);
}

static bool parse_lat(const char *s, double *out)
{
    int deg, r;
    double min;
    char ns;

    if (strlen(s) < 8) return false;
    ns = s[7];
    if (ns != 'N' && ns != 'S') return false;

    r = sscanf(s, "%2d%5lf", &deg, &min);
    if (r != 2) return false;
    if (deg < 0 || deg > 90 || min < 0.0 || min >= 60.0) return false;

    *out = (double)deg + min / 60.0;
    if (ns == 'S') *out = -(*out);
    return true;
}

static bool parse_lon(const char *s, double *out)
{
    int deg, r;
    double min;
    char ew;

    if (strlen(s) < 9) return false;
    ew = s[8];
    if (ew != 'E' && ew != 'W') return false;

    r = sscanf(s, "%3d%5lf", &deg, &min);
    if (r != 2) return false;
    if (deg < 0 || deg > 180 || min < 0.0 || min >= 60.0) return false;

    *out = (double)deg + min / 60.0;
    if (ew == 'W') *out = -(*out);
    return true;
}

static bool parse_timestamp(const char *s, aprs_timestamp_t *ts)
{
    int a, b, c;
    char t;

    if (strlen(s) < 7) return false;
    t = s[6];
    if (t != 'z' && t != 'h' && t != '/') return false;

    if (sscanf(s, "%2d%2d%2d", &a, &b, &c) != 3) return false;

    ts->type = t;
    if (t == 'h') {
        ts->day = 0;
        ts->hour = a;
        ts->minute = b;
        ts->second = c;
    } else {
        ts->day = a;
        ts->hour = b;
        ts->minute = c;
        ts->second = 0;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* compressed position                                                 */
/* ------------------------------------------------------------------ */

static int b91(char c) { return (int)c - 33; }

/*
 * Detect compressed position: the byte after DTI (and optional timestamp)
 * is a printable char that is NOT a digit.  Uncompressed positions always
 * start with a latitude digit (0-9).
 */
static bool is_compressed(const char *p)
{
    /* compressed: symbol_table char followed by 4 base91 lat bytes */
    if (!p || strlen(p) < 13) return false;
    /* compressed starts with symbol table id (/ \ or uppercase letter) */
    if (p[0] == '/' || p[0] == '\\' ||
        (p[0] >= 'A' && p[0] <= 'Z') ||
        (p[0] >= 'a' && p[0] <= 'j')) {
        /* uncompressed always has digit at position 0 of lat field */
        /* compressed: first lat byte is base91 (33-124) */
        if (!isdigit((unsigned char)p[0]))
            return true;
    }
    return false;
}

/*
 * Parse compressed position: /YYYYXXXX$csT (13 bytes)
 *   p[0]    = symbol table
 *   p[1..4] = base91 latitude
 *   p[5..8] = base91 longitude
 *   p[9]    = symbol code
 *   p[10]   = cs byte (course/speed or altitude)
 *   p[11]   = s byte
 *   p[12]   = compression type T
 */
static aprs_err_t parse_compressed_position(const char *p,
                                            aprs_position_t *pos)
{
    long lat91, lon91;
    int cs, se, ctype;

    if (strlen(p) < 13) return APRS_ERR_PARSE;

    pos->symbol_table = p[0];

    lat91 = (long)b91(p[1]) * 91 * 91 * 91
          + (long)b91(p[2]) * 91 * 91
          + (long)b91(p[3]) * 91
          + (long)b91(p[4]);

    lon91 = (long)b91(p[5]) * 91 * 91 * 91
          + (long)b91(p[6]) * 91 * 91
          + (long)b91(p[7]) * 91
          + (long)b91(p[8]);

    pos->latitude  = 90.0 - (double)lat91 / 380926.0;
    pos->longitude = -180.0 + (double)lon91 / 190463.0;

    pos->symbol_code = p[9];

    cs = (int)(unsigned char)p[10] - 33;
    se = (int)(unsigned char)p[11] - 33;
    ctype = (int)(unsigned char)p[12] - 33;

    /* decode cs/se based on compression type */
    if (cs >= 0 && cs <= 89) {
        if ((ctype & 0x18) == 0x10) {
            /* altitude */
            pos->altitude_ft = (int)pow(1.002, (double)(cs * 91 + se));
            pos->has_altitude = true;
        } else {
            /* course/speed */
            pos->course = cs * 4;
            pos->speed_knots = (int)(pow(1.08, (double)se) - 1.0);
            pos->has_course_speed = true;
        }
    }

    /* comment after compressed block */
    if (p[13] != '\0')
        copy_str(pos->comment, sizeof(pos->comment), p + 13);

    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* uncompressed position fields                                        */
/* ------------------------------------------------------------------ */

static aprs_err_t parse_position_fields(const char *p, aprs_position_t *pos)
{
    char latbuf[9], lonbuf[10];

    if (strlen(p) < 19) return APRS_ERR_PARSE;

    memcpy(latbuf, p, 8);
    latbuf[8] = '\0';
    if (!parse_lat(latbuf, &pos->latitude)) return APRS_ERR_PARSE;

    pos->symbol_table = p[8];

    memcpy(lonbuf, p + 9, 9);
    lonbuf[9] = '\0';
    if (!parse_lon(lonbuf, &pos->longitude)) return APRS_ERR_PARSE;

    pos->symbol_code = p[18];

    if (p[19] != '\0')
        copy_str(pos->comment, sizeof(pos->comment), p + 19);

    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* position packet (!, =, /, @)                                        */
/* ------------------------------------------------------------------ */

static aprs_err_t parse_position_packet(const char *info, aprs_packet_t *out)
{
    char dti = info[0];
    const char *p = info + 1;
    bool has_ts = (dti == '/' || dti == '@');
    bool messaging = (dti == '=' || dti == '@');

    out->type = messaging ? APRS_PACKET_POSITION_MSGCAP : APRS_PACKET_POSITION;
    out->data.position.messaging = messaging;

    if (has_ts) {
        if (strlen(p) < 7) return APRS_ERR_PARSE;
        if (!parse_timestamp(p, &out->data.position.timestamp))
            return APRS_ERR_PARSE;
        out->data.position.has_timestamp = true;
        p += 7;
    }

    if (is_compressed(p))
        return parse_compressed_position(p, &out->data.position);
    else
        return parse_position_fields(p, &out->data.position);
}

/* ------------------------------------------------------------------ */
/* Mic-E                                                               */
/* ------------------------------------------------------------------ */

/*
 * Mic-E encodes latitude in the destination address (6 chars),
 * and longitude + speed + course in the info field.
 *
 * Destination address digit encoding:
 *   '0'-'9' → digit 0-9 (north, west=0 in that position)
 *   'A'-'J' → digit 0-9 (custom msg bit, north)
 *   'K'-'L' → digit 0-1 (custom msg bit, north)  [space]
 *   'P'-'Y' → digit 0-9 (standard msg bit, north)
 *   'Z'     → digit 0   (standard msg bit, north) [space]
 *
 * Positions 4,5,6 of dst encode N/S, lon offset, E/W.
 */

static int mice_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'J') return c - 'A';
    if (c == 'K' || c == 'L') return c - 'K';
    if (c >= 'P' && c <= 'Y') return c - 'P';
    if (c == 'Z') return 0;
    return -1;
}

static bool mice_is_north(char c)
{
    return (c >= 'P' && c <= 'Z') || (c >= 'A' && c <= 'L');
}

static bool mice_is_lonoff(char c)
{
    return (c >= 'P' && c <= 'Z') || (c >= 'A' && c <= 'L');
}

static bool mice_is_west(char c)
{
    return (c >= 'P' && c <= 'Z') || (c >= 'A' && c <= 'L');
}

static aprs_err_t parse_mice_packet(const char *info, aprs_packet_t *out)
{
    const char *dst = out->dst.callsign;
    const char *p = info + 1; /* skip ` or ' DTI */
    int d[6];
    int lat_deg, lat_min, lat_frac;
    int lon_deg, lon_min, lon_cs;
    double lat, lon;
    int sp, dc, se;
    size_t dlen, i;

    out->type = APRS_PACKET_MIC_E;

    /* need 6 chars in destination for lat encoding */
    dlen = strlen(dst);
    /* strip SSID for destination parsing */
    {
        const char *dash = strchr(dst, '-');
        if (dash) dlen = (size_t)(dash - dst);
    }
    if (dlen < 6) return APRS_ERR_PARSE;

    for (i = 0; i < 6; i++) {
        d[i] = mice_digit(dst[i]);
        if (d[i] < 0) return APRS_ERR_PARSE;
    }

    /* latitude: DD MM.FF */
    lat_deg = d[0] * 10 + d[1];
    lat_min = d[2] * 10 + d[3];
    lat_frac = d[4] * 10 + d[5];

    lat = (double)lat_deg + ((double)lat_min + (double)lat_frac / 100.0) / 60.0;

    /* N/S from dest char index 3 */
    if (!mice_is_north(dst[3]))
        lat = -lat;

    /* info field needs at least 8 bytes: lon_deg lon_min lon_cs sym_code sym_table status */
    if (strlen(p) < 8) return APRS_ERR_PARSE;

    /* longitude degree from info[0] */
    lon_deg = (int)(unsigned char)p[0] - 28;
    /* lon offset from dest char index 4 */
    if (mice_is_lonoff(dst[4]))
        lon_deg += 100;
    if (lon_deg >= 180 && lon_deg <= 189)
        lon_deg -= 80;
    else if (lon_deg >= 190 && lon_deg <= 199)
        lon_deg -= 190;

    /* longitude minutes from info[1] */
    lon_min = (int)(unsigned char)p[1] - 28;
    if (lon_min >= 60) lon_min -= 60;

    /* longitude hundredths from info[2] */
    lon_cs = (int)(unsigned char)p[2] - 28;

    lon = (double)lon_deg + ((double)lon_min + (double)lon_cs / 100.0) / 60.0;

    /* E/W from dest char index 5 */
    if (mice_is_west(dst[5]))
        lon = -lon;

    /* speed and course from info[3..4..5] */
    sp = (int)(unsigned char)p[3] - 28;
    dc = (int)(unsigned char)p[4] - 28;
    se = (int)(unsigned char)p[5] - 28;

    out->data.position.latitude = lat;
    out->data.position.longitude = lon;
    out->data.position.symbol_code = p[6];
    out->data.position.symbol_table = p[7];

    {
        int speed_tens = sp * 10 + dc / 10;
        int course_val = (dc % 10) * 100 + se;
        if (speed_tens >= 800) speed_tens -= 800;
        if (course_val >= 400) course_val -= 400;
        out->data.position.speed_knots = speed_tens;
        out->data.position.course = course_val;
        out->data.position.has_course_speed = true;
    }

    /* status text after the 8 fixed bytes */
    if (p[8] != '\0')
        copy_str(out->data.position.comment, sizeof(out->data.position.comment), p + 8);

    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* object: ;NAME_____*DDHHMMzPOSITION...                               */
/* ------------------------------------------------------------------ */

static aprs_err_t parse_object_packet(const char *info, aprs_packet_t *out)
{
    const char *p;
    char marker;

    out->type = APRS_PACKET_OBJECT;

    /* info[0] = ';', name is next 9 chars, then * or _ */
    if (strlen(info) < 11) return APRS_ERR_PARSE;

    /* copy name (9 chars), trim trailing spaces */
    memcpy(out->data.object.name, info + 1, 9);
    out->data.object.name[9] = '\0';
    {
        int i;
        for (i = 8; i >= 0 && out->data.object.name[i] == ' '; i--)
            out->data.object.name[i] = '\0';
    }

    marker = info[10];
    out->data.object.live = (marker == '*');

    p = info + 11;

    /* timestamp (7 chars) */
    if (strlen(p) < 7) return APRS_ERR_PARSE;
    if (parse_timestamp(p, &out->data.object.timestamp))
        out->data.object.has_timestamp = true;
    p += 7;

    /* position (uncompressed or compressed) */
    if (is_compressed(p))
        return parse_compressed_position(p, &out->data.object.position);
    else
        return parse_position_fields(p, &out->data.object.position);
}

/* ------------------------------------------------------------------ */
/* item: )NAME!POSITION... or )NAME_POSITION...                        */
/* ------------------------------------------------------------------ */

static aprs_err_t parse_item_packet(const char *info, aprs_packet_t *out)
{
    const char *p = info + 1; /* skip ')' */
    const char *marker;
    size_t nlen;

    out->type = APRS_PACKET_ITEM;

    /* find ! or _ marker (end of name, 3-9 chars) */
    marker = NULL;
    {
        size_t i;
        for (i = 0; i < 9 && p[i]; i++) {
            if (p[i] == '!' || p[i] == '_') {
                marker = p + i;
                break;
            }
        }
    }
    if (!marker) return APRS_ERR_PARSE;

    nlen = (size_t)(marker - p);
    if (nlen < 1 || nlen > 9) return APRS_ERR_PARSE;

    memcpy(out->data.item.name, p, nlen);
    out->data.item.name[nlen] = '\0';

    out->data.item.live = (*marker == '!');

    p = marker + 1;

    if (is_compressed(p))
        return parse_compressed_position(p, &out->data.item.position);
    else
        return parse_position_fields(p, &out->data.item.position);
}

/* ------------------------------------------------------------------ */
/* weather: _DDHHMMcSSSsSSStTTTrRRRpPPPPPPhHHbBBBBB                   */
/* ------------------------------------------------------------------ */

static int wx_int(const char *s, int len)
{
    /* parse a fixed-width integer field; spaces or dots mean no data → -1 */
    int i, val = 0;
    for (i = 0; i < len; i++) {
        if (s[i] == ' ' || s[i] == '.') return -1;
        if (!isdigit((unsigned char)s[i]) && s[i] != '-') return -1;
    }
    if (sscanf(s, "%d", &val) != 1) return -1;
    return val;
}

static aprs_err_t parse_weather_packet(const char *info, aprs_packet_t *out)
{
    const char *p = info + 1; /* skip '_' or positionless wx DTI */
    int v;

    out->type = APRS_PACKET_WEATHER;
    memset(&out->data.weather, 0, sizeof(out->data.weather));

    /* skip optional timestamp (7 chars) if present */
    if (strlen(p) >= 7 && (p[6] == 'z' || p[6] == 'h' || p[6] == '/'))
        p += 7;

    /* walk through the weather fields — each prefixed by a letter code */
    while (*p) {
        char code = *p++;
        switch (code) {
        case 'c': /* wind direction */
            if (strlen(p) < 3) return APRS_OK;
            v = wx_int(p, 3);
            if (v >= 0) {
                out->data.weather.wind_dir_deg = v;
                out->data.weather.valid_wind = true;
            }
            p += 3;
            break;
        case 's': /* sustained wind speed mph */
            if (strlen(p) < 3) return APRS_OK;
            v = wx_int(p, 3);
            if (v >= 0) {
                out->data.weather.wind_speed_mph = v;
                out->data.weather.valid_wind = true;
            }
            p += 3;
            break;
        case 'g': /* gust */
            if (strlen(p) < 3) return APRS_OK;
            v = wx_int(p, 3);
            if (v >= 0) out->data.weather.wind_gust_mph = v;
            p += 3;
            break;
        case 't': /* temperature F */
            if (strlen(p) < 3) return APRS_OK;
            v = wx_int(p, 3);
            if (v > -999) {
                out->data.weather.temp_c = ((float)v - 32.0f) * 5.0f / 9.0f;
                out->data.weather.valid_temp = true;
            }
            p += 3;
            break;
        case 'r': /* rain last hour (hundredths of inch) */
            if (strlen(p) < 3) return APRS_OK;
            v = wx_int(p, 3);
            if (v >= 0) {
                out->data.weather.rain_1h_mm = (float)v * 0.254f;
                out->data.weather.valid_rain = true;
            }
            p += 3;
            break;
        case 'p': /* rain last 24h */
            if (strlen(p) < 3) return APRS_OK;
            v = wx_int(p, 3);
            if (v >= 0) {
                out->data.weather.rain_24h_mm = (float)v * 0.254f;
                out->data.weather.valid_rain = true;
            }
            p += 3;
            break;
        case 'P': /* rain since midnight */
            if (strlen(p) < 3) return APRS_OK;
            v = wx_int(p, 3);
            if (v >= 0) {
                out->data.weather.rain_since_midnight_mm = (float)v * 0.254f;
                out->data.weather.valid_rain = true;
            }
            p += 3;
            break;
        case 'h': /* humidity */
            if (strlen(p) < 2) return APRS_OK;
            v = wx_int(p, 2);
            if (v >= 0) {
                out->data.weather.humidity_pct = (v == 0) ? 100 : v;
                out->data.weather.valid_humidity = true;
            }
            p += 2;
            break;
        case 'b': /* barometric pressure (tenths of mbar) */
            if (strlen(p) < 5) return APRS_OK;
            v = wx_int(p, 5);
            if (v >= 0) {
                out->data.weather.pressure_hpa = (float)v / 10.0f;
                out->data.weather.valid_pressure = true;
            }
            p += 5;
            break;
        default:
            /* unknown field — skip to end or next alpha */
            while (*p && !isalpha((unsigned char)*p)) p++;
            break;
        }
    }

    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* telemetry: T#seq,a1,a2,a3,a4,a5,dddddddd                           */
/* ------------------------------------------------------------------ */

static aprs_err_t parse_telemetry_packet(const char *info, aprs_packet_t *out)
{
    const char *p = info;
    int i;
    unsigned int dval;

    out->type = APRS_PACKET_TELEMETRY;
    memset(&out->data.telemetry, 0, sizeof(out->data.telemetry));

    /* expect T#seq, or T# */
    if (p[0] != 'T') return APRS_ERR_PARSE;
    p++;
    if (*p == '#') p++;

    /* sequence number */
    out->data.telemetry.seq = (uint16_t)atoi(p);

    /* skip to first comma */
    p = strchr(p, ',');
    if (!p) return APRS_OK; /* just a sequence number is OK */
    p++;

    /* 5 analog values */
    for (i = 0; i < 5; i++) {
        out->data.telemetry.analog[i] = atoi(p);
        p = strchr(p, ',');
        if (!p) return APRS_OK;
        p++;
    }

    /* 8-bit digital value as binary string like "01100001" */
    dval = 0;
    for (i = 0; i < 8 && *p; i++, p++) {
        dval <<= 1;
        if (*p == '1') dval |= 1;
    }
    out->data.telemetry.digital = (uint8_t)dval;

    /* optional comment */
    if (*p == ',') p++;
    if (*p)
        copy_str(out->data.telemetry.comment,
                 sizeof(out->data.telemetry.comment), p);

    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* public init / reset / validation                                    */
/* ------------------------------------------------------------------ */

void aprs_packet_init(aprs_packet_t *pkt)
{
    if (!pkt) return;
    memset(pkt, 0, sizeof(*pkt));
    pkt->type = APRS_PACKET_UNKNOWN;
}

void aprs_packet_reset(aprs_packet_t *pkt)
{
    aprs_packet_init(pkt);
}

bool aprs_is_valid_callsign(const char *s)
{
    size_t len;
    const char *p;

    if (!s || !*s) return false;
    len = strlen(s);
    if (len >= APRS_CALLSIGN_MAX) return false;

    for (p = s; *p; p++) {
        if (*p == '*' && *(p + 1) == '\0') continue;
        if (!(isalnum((unsigned char)*p) || *p == '-'))
            return false;
    }

    return true;
}

bool aprs_is_valid_path(const char *s)
{
    return aprs_is_valid_callsign(s);
}

bool aprs_is_valid_latlon(double lat, double lon)
{
    return (lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0);
}

const char *aprs_packet_type_name(aprs_packet_type_t type)
{
    switch (type) {
    case APRS_PACKET_UNKNOWN:         return "unknown";
    case APRS_PACKET_POSITION:        return "position";
    case APRS_PACKET_POSITION_MSGCAP: return "position_msgcap";
    case APRS_PACKET_STATUS:          return "status";
    case APRS_PACKET_MESSAGE:         return "message";
    case APRS_PACKET_OBJECT:          return "object";
    case APRS_PACKET_ITEM:            return "item";
    case APRS_PACKET_WEATHER:         return "weather";
    case APRS_PACKET_TELEMETRY:       return "telemetry";
    case APRS_PACKET_QUERY:           return "query";
    case APRS_PACKET_THIRDPARTY:      return "thirdparty";
    case APRS_PACKET_MIC_E:           return "mic_e";
    default: return "invalid";
    }
}

/* ------------------------------------------------------------------ */
/* TNC2 header parsing                                                 */
/* ------------------------------------------------------------------ */

static aprs_err_t parse_header(char *header, aprs_packet_t *out)
{
    char *gt, *saveptr = NULL, *token;

    gt = strchr(header, '>');
    if (!gt) return APRS_ERR_PARSE;
    *gt = '\0';

    if (!aprs_is_valid_callsign(header)) return APRS_ERR_PARSE;
    copy_str(out->src.callsign, sizeof(out->src.callsign), header);

    token = strtok_r(gt + 1, ",", &saveptr);
    if (!token || !aprs_is_valid_callsign(token)) return APRS_ERR_PARSE;
    copy_str(out->dst.callsign, sizeof(out->dst.callsign), token);

    out->path_len = 0;
    while ((token = strtok_r(NULL, ",", &saveptr)) != NULL) {
        if (out->path_len >= APRS_PATH_MAX) return APRS_ERR_OVERFLOW;
        copy_str(out->path[out->path_len].callsign,
                 sizeof(out->path[out->path_len].callsign), token);
        out->path_len++;
    }

    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* message / status parsers                                            */
/* ------------------------------------------------------------------ */

static aprs_err_t parse_message_packet(const char *info, aprs_packet_t *out)
{
    const char *p, *end, *brace;
    size_t alen;

    out->type = APRS_PACKET_MESSAGE;

    p = info + 1;
    end = strchr(p, ':');
    if (!end) return APRS_ERR_PARSE;

    alen = (size_t)(end - p);
    if (alen > 9) alen = 9;

    memset(out->data.message.addressee, 0, sizeof(out->data.message.addressee));
    memcpy(out->data.message.addressee, p, alen);
    {
        int i;
        for (i = (int)alen - 1; i >= 0 && out->data.message.addressee[i] == ' '; i--)
            out->data.message.addressee[i] = '\0';
    }

    p = end + 1;

    if (strncmp(p, "ack", 3) == 0) {
        out->data.message.ack = true;
        copy_str(out->data.message.msgno, sizeof(out->data.message.msgno), p + 3);
        out->data.message.text[0] = '\0';
        return APRS_OK;
    }
    if (strncmp(p, "rej", 3) == 0) {
        out->data.message.rej = true;
        copy_str(out->data.message.msgno, sizeof(out->data.message.msgno), p + 3);
        out->data.message.text[0] = '\0';
        return APRS_OK;
    }

    brace = strchr(p, '{');
    if (brace) {
        size_t tlen = (size_t)(brace - p);
        if (tlen >= sizeof(out->data.message.text))
            tlen = sizeof(out->data.message.text) - 1;
        memcpy(out->data.message.text, p, tlen);
        out->data.message.text[tlen] = '\0';
        copy_str(out->data.message.msgno, sizeof(out->data.message.msgno), brace + 1);
    } else {
        copy_str(out->data.message.text, sizeof(out->data.message.text), p);
        out->data.message.msgno[0] = '\0';
    }

    return APRS_OK;
}

static aprs_err_t parse_status_packet(const char *info, aprs_packet_t *out)
{
    out->type = APRS_PACKET_STATUS;
    copy_str(out->data.status.text, sizeof(out->data.status.text), info + 1);
    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* TNC2 parse                                                          */
/* ------------------------------------------------------------------ */

aprs_err_t aprs_parse_tnc2(const char *line, aprs_packet_t *out)
{
    char buf[768];
    char *colon;
    aprs_err_t rc;
    unsigned char dti;

    if (!line || !out) return APRS_ERR_INVALID_ARG;
    aprs_packet_init(out);

    if (strlen(line) >= sizeof(buf)) return APRS_ERR_OVERFLOW;
    copy_str(buf, sizeof(buf), line);

    colon = strchr(buf, ':');
    if (!colon) return APRS_ERR_PARSE;
    *colon = '\0';

    rc = parse_header(buf, out);
    if (rc != APRS_OK) return rc;

    copy_str(out->raw_info, sizeof(out->raw_info), colon + 1);

    dti = (unsigned char)out->raw_info[0];

    switch (dti) {
    case '!': case '/': case '=': case '@':
        return parse_position_packet(out->raw_info, out);
    case ':':
        return parse_message_packet(out->raw_info, out);
    case '>':
        return parse_status_packet(out->raw_info, out);
    case ';':
        return parse_object_packet(out->raw_info, out);
    case ')':
        return parse_item_packet(out->raw_info, out);
    case '_':
        return parse_weather_packet(out->raw_info, out);
    case 'T':
        return parse_telemetry_packet(out->raw_info, out);
    case '`': case '\'':
        return parse_mice_packet(out->raw_info, out);
    case '?':
        out->type = APRS_PACKET_QUERY;
        return APRS_OK;
    case '}':
        out->type = APRS_PACKET_THIRDPARTY;
        return APRS_OK;
    default:
        out->type = APRS_PACKET_UNKNOWN;
        return APRS_OK;
    }
}

/* ------------------------------------------------------------------ */
/* TNC2 format                                                         */
/* ------------------------------------------------------------------ */

aprs_err_t aprs_format_tnc2(const aprs_packet_t *pkt, char *buf, size_t buflen)
{
    size_t i;
    int n, total = 0;

    if (!pkt || !buf || buflen == 0) return APRS_ERR_INVALID_ARG;

    n = snprintf(buf, buflen, "%s>%s", pkt->src.callsign, pkt->dst.callsign);
    if (n < 0 || (size_t)n >= buflen) return APRS_ERR_OVERFLOW;
    total = n;

    for (i = 0; i < pkt->path_len; i++) {
        n = snprintf(buf + total, buflen - (size_t)total, ",%s",
                     pkt->path[i].callsign);
        if (n < 0 || (size_t)n >= buflen - (size_t)total) return APRS_ERR_OVERFLOW;
        total += n;
    }

    n = snprintf(buf + total, buflen - (size_t)total, ":%s", pkt->raw_info);
    if (n < 0 || (size_t)n >= buflen - (size_t)total) return APRS_ERR_OVERFLOW;

    return APRS_OK;
}

/* ------------------------------------------------------------------ */
/* builders                                                            */
/* ------------------------------------------------------------------ */

static aprs_err_t fill_header(aprs_packet_t *pkt, const char *src,
                              const char *dst, const char *path[],
                              size_t path_len)
{
    size_t i;

    if (!aprs_is_valid_callsign(src)) return APRS_ERR_INVALID_ARG;
    if (!aprs_is_valid_callsign(dst)) return APRS_ERR_INVALID_ARG;
    if (path_len > APRS_PATH_MAX) return APRS_ERR_INVALID_ARG;

    copy_str(pkt->src.callsign, sizeof(pkt->src.callsign), src);
    copy_str(pkt->dst.callsign, sizeof(pkt->dst.callsign), dst);

    for (i = 0; i < path_len; i++) {
        if (path[i] && !aprs_is_valid_path(path[i]))
            return APRS_ERR_INVALID_ARG;
        copy_str(pkt->path[i].callsign,
                 sizeof(pkt->path[i].callsign), path[i]);
    }
    pkt->path_len = path_len;

    return APRS_OK;
}

aprs_err_t aprs_build_position(
    aprs_packet_t *pkt, const char *src, const char *dst,
    const char *path[], size_t path_len,
    double lat, double lon,
    char symbol_table, char symbol_code, const char *comment)
{
    aprs_err_t rc;
    char latbuf[9], lonbuf[10];

    if (!pkt || !src || !dst) return APRS_ERR_INVALID_ARG;
    if (!aprs_is_valid_latlon(lat, lon)) return APRS_ERR_INVALID_ARG;

    aprs_packet_init(pkt);
    pkt->type = APRS_PACKET_POSITION;

    rc = fill_header(pkt, src, dst, path, path_len);
    if (rc != APRS_OK) return rc;

    pkt->data.position.latitude = lat;
    pkt->data.position.longitude = lon;
    pkt->data.position.symbol_table = symbol_table;
    pkt->data.position.symbol_code = symbol_code;
    if (comment)
        copy_str(pkt->data.position.comment,
                 sizeof(pkt->data.position.comment), comment);

    lat_to_aprs(lat, latbuf);
    lon_to_aprs(lon, lonbuf);

    snprintf(pkt->raw_info, sizeof(pkt->raw_info), "!%s%c%s%c%s",
             latbuf, symbol_table, lonbuf, symbol_code,
             comment ? comment : "");

    return APRS_OK;
}

aprs_err_t aprs_build_message(
    aprs_packet_t *pkt, const char *src, const char *dst,
    const char *path[], size_t path_len,
    const char *addressee, const char *text, const char *msgno)
{
    aprs_err_t rc;

    if (!pkt || !src || !dst || !addressee || !text) return APRS_ERR_INVALID_ARG;
    if (strlen(addressee) > 9) return APRS_ERR_INVALID_ARG;

    aprs_packet_init(pkt);
    pkt->type = APRS_PACKET_MESSAGE;

    rc = fill_header(pkt, src, dst, path, path_len);
    if (rc != APRS_OK) return rc;

    copy_str(pkt->data.message.addressee,
             sizeof(pkt->data.message.addressee), addressee);
    copy_str(pkt->data.message.text,
             sizeof(pkt->data.message.text), text);
    if (msgno)
        copy_str(pkt->data.message.msgno,
                 sizeof(pkt->data.message.msgno), msgno);

    snprintf(pkt->raw_info, sizeof(pkt->raw_info), ":%-9s:%s%s%s",
             addressee, text,
             (msgno && *msgno) ? "{" : "",
             (msgno && *msgno) ? msgno : "");

    return APRS_OK;
}

aprs_err_t aprs_build_status(
    aprs_packet_t *pkt, const char *src, const char *dst,
    const char *path[], size_t path_len, const char *text)
{
    aprs_err_t rc;

    if (!pkt || !src || !dst || !text) return APRS_ERR_INVALID_ARG;

    aprs_packet_init(pkt);
    pkt->type = APRS_PACKET_STATUS;

    rc = fill_header(pkt, src, dst, path, path_len);
    if (rc != APRS_OK) return rc;

    copy_str(pkt->data.status.text, sizeof(pkt->data.status.text), text);
    snprintf(pkt->raw_info, sizeof(pkt->raw_info), ">%s", text);

    return APRS_OK;
}
