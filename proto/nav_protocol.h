/*
 * nav_protocol.h - data the phone sends to the display.
 *
 * Shared by the desktop simulator, the ESP32-S3 firmware, and (as a spec)
 * the iPhone app. All multi-byte fields are little-endian.
 *
 * NOTE: this is v1 of the STATUS packet. It differs from the first PDF
 * guide in one way: a speed field was added and street[] shrank from 24 to
 * 22 bytes so the packet stays 48 bytes.
 */
#ifndef NAV_PROTOCOL_H
#define NAV_PROTOCOL_H

#include <stdint.h>

#define NAV_PROTO_VERSION 1

typedef enum {
    NAV_MAN_STRAIGHT     = 0,
    NAV_MAN_LEFT         = 1,
    NAV_MAN_RIGHT        = 2,
    NAV_MAN_SLIGHT_LEFT  = 3,
    NAV_MAN_SLIGHT_RIGHT = 4,
    NAV_MAN_KEEP         = 5,
    NAV_MAN_SHARP_RIGHT  = 6,
    NAV_MAN_SHARP_LEFT   = 7,
    NAV_MAN_UTURN        = 8,
    NAV_MAN_ROUNDABOUT   = 9,
    NAV_MAN_FORK_LEFT    = 10,
    NAV_MAN_FORK_RIGHT   = 11,
    NAV_MAN_ARRIVE       = 12,
    NAV_MAN_EXIT_LEFT    = 13,    /* leave a highway by the ramp on the left */
    NAV_MAN_EXIT_RIGHT   = 14,    /* ... on the right */
    NAV_MAN_MERGE_LEFT   = 15,    /* join the road ahead by moving over to the left (a ramp curving left into it) */
    NAV_MAN_MERGE_RIGHT  = 16,    /* ... to the right */
    NAV_MAN_MERGE        = 17,    /* join the road ahead from a ramp: two lanes become one (no side claimed) */
} nav_maneuver_t;

#define NAV_FLAG_NAVIGATING (1u << 0)
#define NAV_FLAG_REROUTING  (1u << 1)
#define NAV_FLAG_OFF_ROUTE  (1u << 2)
#define NAV_FLAG_NIGHT      (1u << 3)
#define NAV_FLAG_COMPASS_LIVE (1u << 4)   /* heading is the phone's live compass (valid even when standing still) */

#define NAV_STREET_LEN 22

#pragma pack(push, 1)
typedef struct {
    uint8_t  version;                 /*  0 */
    uint8_t  maneuver;                /*  1 nav_maneuver_t */
    uint8_t  next_maneuver;           /*  2 nav_maneuver_t, STRAIGHT = none */
    uint8_t  flags;                   /*  3 NAV_FLAG_* */
    int32_t  lat_e7;                  /*  4 */
    int32_t  lon_e7;                  /*  8 */
    uint16_t heading_x10;             /* 12 degrees x 10, 0..3599 */
    uint16_t dist_turn_m;             /* 14 to current maneuver */
    uint16_t dist_next_m;             /* 16 from current to next maneuver */
    uint32_t remaining_m;             /* 18 */
    uint16_t eta_min;                 /* 22 */
    uint16_t speed_kmh_x10;           /* 24 */
    char     street[NAV_STREET_LEN];  /* 26 UTF-8, NUL padded */
} nav_status_t;
#pragma pack(pop)

_Static_assert(sizeof(nav_status_t) == 48, "nav_status_t must be 48 bytes");

/* ROUTE / ROADS points: whole meters east (dx) and north (dy) of the origin. */
typedef struct {
    int16_t dx;
    int16_t dy;
} nav_pt_t;

/* Background road segment, same coordinate frame as nav_pt_t.
 * major = 1 draws it slightly brighter. */
typedef struct {
    int16_t x1, y1, x2, y2;
    uint8_t major;
} nav_seg_t;

/* Background area (park / water) as an axis-aligned rectangle in meters. */
typedef enum { NAV_AREA_PARK = 0, NAV_AREA_WATER = 1 } nav_area_kind_t;
typedef struct {
    int16_t x1, y1, x2, y2;
    uint8_t kind;
} nav_area_t;

/* ---- WEATHER (phone -> display, every ~10 minutes) ------------------------
 * Temperatures are Celsius; the UI converts to F unless metric is selected. */
typedef enum {
    NAV_WX_CLEAR    = 0,
    NAV_WX_PARTLY   = 1,
    NAV_WX_CLOUDY   = 2,
    NAV_WX_FOG      = 3,
    NAV_WX_DRIZZLE  = 4,
    NAV_WX_RAIN     = 5,
    NAV_WX_THUNDER  = 6,
    NAV_WX_SNOW     = 7,
} nav_wx_cond_t;

#define NAV_WX_FLAG_NIGHT (1u << 0)

#pragma pack(push, 1)
typedef struct {
    uint8_t  version;         /*  0 */
    uint8_t  condition;       /*  1 nav_wx_cond_t */
    uint8_t  flags;           /*  2 NAV_WX_FLAG_* */
    uint8_t  hour0;           /*  3 local hour (0-23) of hourly[0] */
    int16_t  temp_x10;        /*  4 current, C x 10 */
    int16_t  feels_x10;       /*  6 feels-like, C x 10 */
    int8_t   hi_c;            /*  8 today's high */
    int8_t   lo_c;            /*  9 today's low */
    uint8_t  wind_kmh;        /* 10 */
    uint8_t  gust_kmh;        /* 11 */
    uint16_t wind_from_deg;   /* 12 direction the wind comes FROM, 0-359 */
    uint8_t  precip_pct;      /* 14 chance of rain in the next hour */
    uint8_t  uv;              /* 15 */
    struct {
        int8_t  temp_c;
        uint8_t condition;
        uint8_t precip_pct;
    } hourly[4];              /* 16 next four hours (not drawn yet) */
} nav_weather_t;
#pragma pack(pop)

_Static_assert(sizeof(nav_weather_t) == 28, "nav_weather_t must be 28 bytes");

/* ---- BLE GATT layout -------------------------------------------------------
 * The display is the peripheral (GATT server); the phone is the central.
 * 128-bit UUIDs, written here in the usual big-endian text form. NimBLE wants
 * them as little-endian byte arrays.
 *
 *   service  2CAF0001-EC1C-467A-A2CC-D460BCF6DA62
 *   STATUS   2CAF0002-...   write without response   nav_status_t     (~1 Hz)
 *   ROUTE    2CAF0003-...   write with response      route chunks
 *   WEATHER  2CAF0004-...   write with response      nav_weather_t    (~10 min)
 *   INFO     2CAF0005-...   read                     nav_info_t
 *   ROADS    2CAF0006-...   write with response      background road segments
 *   AREAS    2CAF0007-...   write with response      park / water polygons
 *   SETTINGS 2CAF0008-...   write with response      nav_settings_t (backlight levels, voice on/off, units, clock format)
 *   TIME     2CAF0009-...   write with response      nav_time_t (clock + time zone)
 *   TELEMETRY 2CAF000A-...  write without response   nav_telemetry_t (altitude, climb rate; ~1 Hz)
 *   PAGE     2CAF000B-...   read + notify            u8: the screen id the display is showing (NAV_PAGE_*)
 *   LAYOUT   2CAF000C-...   write with response      nav_layout_t (the order of the screens)
 *   ROUNDABOUT 2CAF000D-... write with response      nav_roundabout_t (the exits of the next roundabout)
 *   TRAFFIC  2CAF000E-...   write with response      slow stretches of the route, and an alert
 *   MOTION   2CAF000F-...   write + notify           the display's motion sensor (1.85B): start/stop, then a stream of readings
 *   TPMS     2CAF0010-...   write (with or without)  nav_tpms_t (tire pressures and the low-pressure alert flags; ~every 2 s)
 *
 * The display advertises the service UUID and the name "NavDisplay".
 *
 * SECURITY: every write, and the INFO and PAGE reads, need a paired link. The first time a phone connects, the display shows a 6-digit code
 * on its screen and the phone asks the person to type it (BLE passkey entry, LE Secure Connections, with bonding). After that the phone is
 * remembered and reconnects without a code. A link that has not paired within 15 seconds is dropped. Build with -DNAV_BLE_SECURE=0 to turn
 * this off for bench testing.
 */
#define NAV_BLE_SERVICE_UUID "2CAF0001-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_STATUS_UUID  "2CAF0002-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_ROUTE_UUID   "2CAF0003-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_WEATHER_UUID "2CAF0004-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_INFO_UUID    "2CAF0005-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_ROADS_UUID   "2CAF0006-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_AREAS_UUID   "2CAF0007-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_SETTINGS_UUID "2CAF0008-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_TIME_UUID    "2CAF0009-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_TELEMETRY_UUID "2CAF000A-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_PAGE_UUID    "2CAF000B-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_LAYOUT_UUID  "2CAF000C-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_ROUNDABOUT_UUID "2CAF000D-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_TRAFFIC_UUID "2CAF000E-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_MOTION_UUID  "2CAF000F-EC1C-467A-A2CC-D460BCF6DA62"
#define NAV_BLE_TPMS_UUID    "2CAF0010-EC1C-467A-A2CC-D460BCF6DA62"

/* PAGE characteristic values: which page the display is showing. The phone sends the fast heading and altitude
 * updates only while it is the compass page (index 3), to save the phone's battery. */
#define NAV_PAGE_MAP 0
#define NAV_PAGE_WEATHER 1
#define NAV_PAGE_TRIP 2
#define NAV_PAGE_COMPASS 3
#define NAV_PAGE_CLOCK 4
#define NAV_PAGE_TPMS 5
#define NAV_PAGE_COUNT 6

/* LAYOUT characteristic: the order of the screens in the display's swipe strip. order[i] is the id (NAV_PAGE_*) of the
 * i-th screen. It must be a permutation of 0..NAV_PAGE_COUNT-1; anything else is ignored. The display remembers it
 * across power cycles. Screen ids never change, only their order. A list from before the tire page existed (count 5, 7 bytes) is still
 * accepted: the tire page goes last. */
#pragma pack(push, 1)
typedef struct {
    uint8_t proto_version;    /* NAV_PROTO_VERSION */
    uint8_t count;            /* NAV_PAGE_COUNT */
    uint8_t order[NAV_PAGE_COUNT];
} nav_layout_t;
#pragma pack(pop)
_Static_assert(sizeof(nav_layout_t) == 8, "nav_layout_t must be 8 bytes");
#define NAV_LAYOUT_V1_BYTES 7     /* the five-screen list that older phone apps send */
/* ROUNDABOUT characteristic: the exits of the first roundabout still ahead on the route, so the turn arrow can draw the real ones.
 * Directions are in 2-degree units, clockwise from straight ahead of the road you enter by: 0 = straight on, +45 = right (90 deg),
 * -45 = left, +-90 = back the way you came. Listed in the order you pass them (counter-clockwise). count 0 = no data: draw a plain
 * roundabout. flags bit 0 = the exits are the real ones from the map; clear = only the exit the route takes is listed (draw just that). */
#define NAV_RB_MAX_EXITS 8
#pragma pack(push, 1)
typedef struct {
    uint8_t proto_version;     /* NAV_PROTO_VERSION */
    uint8_t count;             /* exits listed, 0..NAV_RB_MAX_EXITS */
    uint8_t taken;             /* index of the exit to take, 0xFF = unknown */
    uint8_t flags;             /* bit 0: exits are the real ones */
    int8_t  dir2[NAV_RB_MAX_EXITS];
} nav_roundabout_t;
#pragma pack(pop)
_Static_assert(sizeof(nav_roundabout_t) == 12, "nav_roundabout_t must be 12 bytes");
#define NAV_BLE_DEVICE_NAME  "NavDisplay"

#pragma pack(push, 1)
typedef struct {
    uint8_t proto_version;    /* NAV_PROTO_VERSION */
    uint8_t fw_major, fw_minor, fw_patch;
} nav_info_t;
#pragma pack(pop)

/* ---- MOTION characteristic: the display's motion sensor ----------------------------------------
 * The 1.85B has a 6-axis sensor (accelerometer and gyroscope) that sits on the bike with the display. While the phone records a ride it turns
 * the stream on, and works out lean angle and g-forces from it (the display only reports what the sensor reads).
 *   write  : u8 command: 0 = stop, 1 = start. Streaming also stops by itself when the link drops, so send 1 again after a reconnect.
 *   notify : u8 type=0 | u8 count | u16 seq | u32 t0_ms | u16 dt_us | count x { i16 ax, ay, az, gx, gy, gz }
 * t0_ms is the display's uptime when the first sample was read, and the samples are dt_us apart. The axes are the sensor's own, not the
 * bike's: the phone finds which way is up and which way is forward. Accelerometer: 4096 counts per g (range +-8 g). Gyroscope: 64 counts
 * per degree per second (range +-512). About 60 samples a second, in packets of up to NAV_MOTION_MAX_SAMPLES. */
#define NAV_MOTION_STOP  0
#define NAV_MOTION_START 1
#define NAV_MOTION_DATA  0
#define NAV_MOTION_ACC_LSB_PER_G    4096
#define NAV_MOTION_GYR_LSB_PER_DPS  64
#define NAV_MOTION_MAX_SAMPLES      6
#define NAV_MOTION_HEADER_BYTES     10
#define NAV_MOTION_SAMPLE_BYTES     12

/* ---- TPMS characteristic: tire pressures -----------------------------------------------------
 * The phone reads the tire sensors (or, while testing, makes the numbers up) and writes this packet about every 2 seconds and whenever something
 * changes. When monitoring is turned off in the app it writes one last packet without NAV_TPMS_ENABLED, and the display takes the tire screen out of
 * its swipe strip (and remembers that across power cycles). The phone decides when a tire is low (it knows the limit and keeps the alert from flickering) and speaks the warning itself; the display
 * shows the numbers on its tire page and pulses a thick red ring around whatever screen is up while an alert flag is set. If no packet arrives
 * for NAV_TPMS_STALE_S seconds the numbers are dropped and the ring stops (the display cannot tell a flat tire from a lost link).
 * Pressures are psi x 10 (0xFFFF = no reading); the alert limits (the pressure at which a tire alerts: a reading at or under it) and the pressures each tire
 * should have are psi x 10 (shown on the page); temperatures are degrees F (0xFF = no reading). */
#define NAV_TPMS_FRONT_OK   0x01
#define NAV_TPMS_REAR_OK    0x02
#define NAV_TPMS_FRONT_LOW  0x04   /* alert: pulse the ring */
#define NAV_TPMS_REAR_LOW   0x08
#define NAV_TPMS_FRONT_BATT 0x10   /* the sensor's own battery is low */
#define NAV_TPMS_REAR_BATT  0x20
#define NAV_TPMS_ENABLED    0x40   /* monitoring is on in the app: the tire screen is in the swipe strip. Without it the screen is not shown at all. */
#define NAV_TPMS_NONE       0xFFFF
#define NAV_TPMS_STALE_S    15
#pragma pack(push, 1)
typedef struct {
    uint8_t  proto_version;    /* NAV_PROTO_VERSION */
    uint8_t  flags;            /* NAV_TPMS_* bits */
    uint16_t front_x10, rear_x10;         /* psi x 10 */
    uint16_t front_low_x10, rear_low_x10; /* the alert limits, psi x 10 */
    uint8_t  front_temp_f, rear_temp_f;
    uint16_t front_set_x10, rear_set_x10; /* the pressure each tire should have (shown beside the alert limit); 0 = not set */
} nav_tpms_t;
#pragma pack(pop)
_Static_assert(sizeof(nav_tpms_t) == 16, "nav_tpms_t must be 16 bytes");

/* ---- TRAFFIC characteristic: where the route is slow ----------------------------
 * The phone looks up live traffic along the route and sends two kinds of write (the first byte says which):
 *   spans : u8 type=0 | u8 count | count x { u16 first_point, u16 last_point, u8 level }   (5 bytes each, count <= NAV_MAX_TRAFFIC_SPANS)
 *   alert : u8 type=1 | u8 level | u16 dist_m | u16 length_m
 * A span colors the route line from point first_point through point last_point (indexes into the route the display holds now; the phone adds a
 * point where a stretch starts or ends, so the color changes exactly there). level is NAV_TRAFFIC_SLOW or NAV_TRAFFIC_STOPPED. A span list replaces
 * the previous one, an empty list clears it, and a new route (header) clears it too: send the spans after the route.
 * The alert is about the nearest slow stretch still ahead, even beyond the part of the route the display holds: level 0 = none; dist_m is how far
 * ahead it starts (0xFFFF = unknown) and length_m how long it is. The display flashes a message when a stretch appears or gets worse. */
#define NAV_TRAFFIC_SPANS 0
#define NAV_TRAFFIC_ALERT 1
#define NAV_TRAFFIC_SLOW    1      /* under 75% of the free-flow speed: amber */
#define NAV_TRAFFIC_STOPPED 2      /* under 40%: red */
#define NAV_MAX_TRAFFIC_SPANS 16
#pragma pack(push, 1)
typedef struct {
    uint16_t first, last;
    uint8_t  level;
} nav_traffic_span_t;
#pragma pack(pop)
_Static_assert(sizeof(nav_traffic_span_t) == 5, "nav_traffic_span_t must be 5 bytes");

/* ---- ROUTE characteristic: a route is sent as a series of writes ----------
 *   header : u8 type=0 | u16 total_points | i32 origin_lat_e7 | i32 origin_lon_e7
 *   points : u8 type=1 | u16 start_index  | N x { i16 dx_m, i16 dy_m }
 *   end    : u8 type=2 | u16 crc16 of every point byte (CRC-16/CCITT-FALSE:
 *            poly 0x1021, init 0xFFFF, no reflection, no final xor)
 * A new header discards the previous route. dx = meters east of the origin,
 * dy = meters north. Write a route with a window of about 4 km ahead. */
#define NAV_ROUTE_HEADER 0
#define NAV_ROUTE_POINTS 1
#define NAV_ROUTE_END    2

/* ---- ROADS characteristic: background street segments, sent like a route ------
 *   header  : u8 type=0 | u16 total_segments | i32 origin_lat_e7 | i32 origin_lon_e7
 *   records : u8 type=1 | u16 start_index    | N x { i16 x1, y1, x2, y2 ; u8 major }   (9 bytes each)
 *   end     : u8 type=2 | u16 crc16 of every record byte (same CRC as the route)
 * Coordinates are meters east/north of the origin given in THIS header, which normally is the
 * route's origin. If a later route has a different origin the display shifts the roads to match, so
 * roads can be sent independently of (and less often than) the route. A new header discards a
 * partly received set; the previous complete set stays on screen until the new one is complete. */
/* ---- SETTINGS characteristic: display options chosen in the phone app -----------
 * Day/night itself is not a setting here: the phone decides (automatically from sunrise/sunset, or the
 * user's choice) and sends it in the STATUS night flag. The display picks the backlight level that goes with it.
 * The display remembers the levels across power cycles. */
#pragma pack(push, 1)
typedef struct {
    uint8_t proto_version;    /* NAV_PROTO_VERSION */
    uint8_t day_pct;          /* backlight in day mode, 5-100 */
    uint8_t night_pct;        /* backlight in night mode, 5-100 */
    uint8_t voice_on;         /* 1 = the display speaks ("Power on", "Connected", "Let's go!"), 0 = silent. Older 3-byte packets mean on. */
    uint8_t metric;           /* 0 = miles, feet, mph, F (default); 1 = km, m, km/h, C */
    uint8_t clock24;          /* 0 = 12-hour clock with AM/PM (default); 1 = 24-hour clock */
    uint8_t style;            /* 0 = standard theme, light by day and dark by night (default); 1 = vintage (green gauge colors); 2 = tripper day (pale grey); 3 = tripper night (black and gold); 4 = tripper auto (day or night as the STATUS night flag says); 5 = standard night (always dark); 6 = standard day (always light); 7 = vintage day (cream); 8 = vintage auto; 1 is vintage night */
    uint8_t volume;           /* volume of the display's speaker, 0-100 (default 75) */
    uint8_t touch;            /* touch screen: 0 = off (on the 1.85B the BOOT button still works), 1 = on, 2 = leave it as it is (default). The display also keeps its own state, which a long press of the 1.85B BOOT button toggles. */
} nav_settings_t;
#pragma pack(pop)
_Static_assert(sizeof(nav_settings_t) == 9, "nav_settings_t must be 9 bytes");
/* Packets of 3, 4, 6, 7, 8 or 9 bytes are accepted: the fields an older phone app does not send keep their defaults (voice on, imperial, 12-hour, standard theme, volume 75). */

/* ---- TIME characteristic: the phone sets the display's clock (on connection, then every ~10 minutes) ---
 * unix_utc is seconds since 1970 in UTC; tz_offset_s is the phone's current offset east of UTC (so daylight
 * saving is right). The display keeps UTC and applies the offset when it shows the time. */
#pragma pack(push, 1)
typedef struct {
    uint8_t  proto_version;   /* NAV_PROTO_VERSION */
    uint32_t unix_utc;
    int32_t  tz_offset_s;
} nav_time_t;
#pragma pack(pop)
_Static_assert(sizeof(nav_time_t) == 9, "nav_time_t must be 9 bytes");

/* ---- TELEMETRY characteristic: extra live data that does not fit in STATUS (about once a second) --------- */
#pragma pack(push, 1)
typedef struct {
    uint8_t  proto_version;   /* NAV_PROTO_VERSION */
    int16_t  altitude_m;      /* metres above sea level (the phone's smoothed GPS altitude) */
    int16_t  vspeed_dm_s;     /* climb rate in tenths of a metre per second, + = climbing */
    uint8_t  valid;           /* 1 = the altitude is trustworthy (good vertical accuracy) */
} nav_telemetry_t;
#pragma pack(pop)
_Static_assert(sizeof(nav_telemetry_t) == 6, "nav_telemetry_t must be 6 bytes");

#define NAV_ROADS_HEADER 0
#define NAV_ROADS_SEGS   1
#define NAV_ROADS_END    2
#define NAV_ROADS_REC    9
#define NAV_MAX_ROADS    800

/* ---- AREAS characteristic: park / water polygons ------------------------------
 *   header  : u8 type=0 | u16 total_polygons | i32 origin_lat_e7 | i32 origin_lon_e7
 *   polygons: u8 type=1 | u16 start_index    | whole polygons, each  u8 kind | u8 nverts | nverts x { i16 x, i16 y }
 *   end     : u8 type=2 | u16 crc16 of every polygon byte (all type-1 payloads after the 3-byte prefix)
 * A polygon is a simple outline (even-odd fill), meters east/north of the header's origin, at most
 * NAV_POLY_MAXV vertices; kind is nav_area_kind_t. Chunks never split a polygon. Same origin/shift
 * rules as ROADS. */
#define NAV_AREAS_HEADER 0
#define NAV_AREAS_POLYS  1
#define NAV_AREAS_END    2
#define NAV_POLY_MAXV    24
#define NAV_MAX_POLYS    48
typedef struct {
    uint8_t kind;
    uint8_t n;
    int16_t x[NAV_POLY_MAXV], y[NAV_POLY_MAXV];
} nav_poly_t;

#endif
