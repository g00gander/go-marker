#include "ble_server.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "nav_link.h"
#include "nav_protocol.h"

static const char *TAG = "ble";

/* 1: a phone must pair with the code on the display (unpaired links drop after 15 s); 0: open to any phone, bench testing only. */
#ifndef NAV_BLE_SECURE
#define NAV_BLE_SECURE 1
#endif
#if NAV_BLE_SECURE
#define WR_SEC BLE_GATT_CHR_F_WRITE_AUTHEN
#define RD_SEC BLE_GATT_CHR_F_READ_AUTHEN
#else
#define WR_SEC 0
#define RD_SEC 0
#endif
void ble_store_config_init(void);

/* Bump when the GATT table changes: a paired iPhone caches the table, so it is told once after pairing ("service changed"). */
#define GATT_REVISION 3
#define UNPAIRED_TIMEOUT_MS 15000     /* a link that has not started to pair by then is dropped (it would hold the only connection) */
#define PAIRING_TIMEOUT_MS  90000     /* time to type the code once it is on the screen */

/* 2CAF000x-EC1C-467A-A2CC-D460BCF6DA62 as little-endian bytes */
#define UUID128(n) BLE_UUID128_INIT(0x62, 0xDA, 0xF6, 0xBC, 0x60, 0xD4, 0xCC, 0xA2, \
                                    0x7A, 0x46, 0x1C, 0xEC, (n), 0x00, 0xAF, 0x2C)
static const ble_uuid128_t svc_uuid = UUID128(0x01);
static const ble_uuid128_t status_uuid = UUID128(0x02);
static const ble_uuid128_t route_uuid = UUID128(0x03);
static const ble_uuid128_t weather_uuid = UUID128(0x04);
static const ble_uuid128_t info_uuid = UUID128(0x05);
static const ble_uuid128_t roads_uuid = UUID128(0x06);
static const ble_uuid128_t areas_uuid = UUID128(0x07);
static const ble_uuid128_t settings_uuid = UUID128(0x08);
static const ble_uuid128_t time_uuid = UUID128(0x09);
static const ble_uuid128_t telemetry_uuid = UUID128(0x0A);
static const ble_uuid128_t page_uuid = UUID128(0x0B);
static const ble_uuid128_t layout_uuid = UUID128(0x0C);
static const ble_uuid128_t roundabout_uuid = UUID128(0x0D);
static const ble_uuid128_t traffic_uuid = UUID128(0x0E);
static const ble_uuid128_t motion_uuid = UUID128(0x0F);
static const ble_uuid128_t tpms_uuid = UUID128(0x10);
static uint16_t motion_handle;
static volatile int motion_subscribed;
static uint8_t page_value;
static uint16_t page_handle;

#define ITEM_MAX 192   /* the phone caps a write at 180 bytes */
typedef struct { uint8_t ch, len; uint8_t data[ITEM_MAX]; } item_t;
static QueueHandle_t q;
static volatile int connected;
static uint8_t own_addr_type;
static uint32_t dropped;
static volatile uint32_t pairing_code;
static volatile int secure;
static uint16_t cur_conn = BLE_HS_CONN_HANDLE_NONE;
static esp_timer_handle_t guard_timer;

static void guard_expired(void *arg)
{
    (void)arg;
    if (!secure && cur_conn != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "link did not pair in time: dropping it");
        ble_gap_terminate(cur_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void arm_guard(uint32_t ms)
{
    if (!guard_timer) return;
    esp_timer_stop(guard_timer);
    esp_timer_start_once(guard_timer, (uint64_t)ms * 1000);
}

static void start_advertising(void);

static int write_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len > ITEM_MAX) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    item_t it = {.ch = (uint8_t)(uintptr_t)arg, .len = (uint8_t)len};
    if (ble_hs_mbuf_to_flat(ctxt->om, it.data, ITEM_MAX, NULL) != 0) return BLE_ATT_ERR_UNLIKELY;
    if (xQueueSend(q, &it, 0) != pdTRUE) {
        dropped++;
        /* a route/roads chunk lost would corrupt the set: make the phone see the failure */
        if (it.ch == NAV_CH_ROUTE || it.ch == NAV_CH_ROADS || it.ch == NAV_CH_AREAS) return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

static int info_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    nav_info_t info = {NAV_PROTO_VERSION, 0, 1, 0};
    return os_mbuf_append(ctxt->om, &info, sizeof info) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int page_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, &page_value, 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {.uuid = &status_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_STATUS,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | WR_SEC},
            {.uuid = &route_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_ROUTE,
             .flags = BLE_GATT_CHR_F_WRITE | WR_SEC},
            {.uuid = &weather_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_WEATHER,
             .flags = BLE_GATT_CHR_F_WRITE | WR_SEC},
            {.uuid = &roads_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_ROADS,
             .flags = BLE_GATT_CHR_F_WRITE | WR_SEC},
            {.uuid = &areas_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_AREAS,
             .flags = BLE_GATT_CHR_F_WRITE | WR_SEC},
            {.uuid = &settings_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_SETTINGS,
             .flags = BLE_GATT_CHR_F_WRITE | WR_SEC},
            {.uuid = &time_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_TIME,
             .flags = BLE_GATT_CHR_F_WRITE | WR_SEC},
            {.uuid = &telemetry_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_TELEMETRY,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | WR_SEC},
            {.uuid = &page_uuid.u, .access_cb = page_cb, .val_handle = &page_handle,
             .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | RD_SEC},
            {.uuid = &layout_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_LAYOUT,
             .flags = BLE_GATT_CHR_F_WRITE | WR_SEC},
            {.uuid = &roundabout_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_ROUNDABOUT,
             .flags = BLE_GATT_CHR_F_WRITE | WR_SEC},
            {.uuid = &traffic_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_TRAFFIC,
             .flags = BLE_GATT_CHR_F_WRITE | WR_SEC},
            {.uuid = &tpms_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_TPMS,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | WR_SEC},
            {.uuid = &motion_uuid.u, .access_cb = write_cb, .arg = (void *)(uintptr_t)NAV_CH_MOTION, .val_handle = &motion_handle,
             .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY | WR_SEC},
            {.uuid = &info_uuid.u, .access_cb = info_cb, .flags = BLE_GATT_CHR_F_READ | RD_SEC},      /* the first thing the app reads: it is what starts the pairing */
            {0},
        },
    },
    {0},
};

static void tell_phone_table_changed(void)
{
    nvs_handle_t h;
    uint8_t seen = 0;
    if (nvs_open("ble_nav", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_get_u8(h, "gatt_rev", &seen);
    nvs_close(h);
    if (seen == GATT_REVISION) return;
    ESP_LOGI(TAG, "feature table changed (revision %u, phone last saw %u): telling the phone", GATT_REVISION, seen);
    ble_svc_gatt_changed(0x0001, 0xFFFF);                 /* an indication a bonded phone has asked for; it then looks at the table again */
    if (nvs_open("ble_nav", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "gatt_rev", GATT_REVISION);             /* once is enough: if the phone missed it, turning its Bluetooth off and on does the same */
    nvs_commit(h);
    nvs_close(h);
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect %s (status %d)", ev->connect.status == 0 ? "ok" : "failed", ev->connect.status);
        if (ev->connect.status == 0) {
            connected = 1;
            cur_conn = ev->connect.conn_handle;
            pairing_code = 0;
            secure = !NAV_BLE_SECURE;
#if NAV_BLE_SECURE
            arm_guard(UNPAIRED_TIMEOUT_MS);
#endif
        } else {
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect (reason %d)", ev->disconnect.reason);
        connected = 0;
        secure = 0;
        motion_subscribed = 0;
        pairing_code = 0;
        cur_conn = BLE_HS_CONN_HANDLE_NONE;
        if (guard_timer) esp_timer_stop(guard_timer);
        start_advertising();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.attr_handle == page_handle && ev->subscribe.cur_notify) ble_gatts_chr_updated(page_handle);
        if (ev->subscribe.attr_handle == motion_handle) motion_subscribed = ev->subscribe.cur_notify;
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %d", ev->mtu.value);
        break;
#if NAV_BLE_SECURE
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        /* The phone asks for a code; the person types in what the display shows. */
        if (ev->passkey.params.action == BLE_SM_IOACT_DISP) {
            struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = esp_random() % 1000000u};
            if (io.passkey == 0) io.passkey = 1;               /* 0 means "no code" to the screen code */
            pairing_code = io.passkey;
            arm_guard(PAIRING_TIMEOUT_MS);
            int rc = ble_sm_inject_io(ev->passkey.conn_handle, &io);
            ESP_LOGI(TAG, "pairing: code is on the screen (rc %d)", rc);
        }
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc d;
        int ok = ev->enc_change.status == 0 && ble_gap_conn_find(ev->enc_change.conn_handle, &d) == 0 &&
                 d.sec_state.encrypted && d.sec_state.authenticated;
        struct ble_gap_conn_desc dd = {0};
        const int found = ble_gap_conn_find(ev->enc_change.conn_handle, &dd) == 0;
        ESP_LOGI(TAG, "encryption %s (status %d; link found %d, encrypted %d, authenticated %d, bonded %d, key size %d)", ok ? "on" : "FAILED",
                 ev->enc_change.status, found, dd.sec_state.encrypted, dd.sec_state.authenticated, dd.sec_state.bonded, dd.sec_state.key_size);
        pairing_code = 0;
        if (ok) {
            secure = 1;
            if (guard_timer) esp_timer_stop(guard_timer);
            tell_phone_table_changed();
        } else {
            /* Not paired: keep the link open for another try (the app asks again); the guard drops it if nothing comes of it. */
            secure = 0;
            arm_guard(UNPAIRED_TIMEOUT_MS);
        }
        break;
    }
    case BLE_GAP_EVENT_PARING_COMPLETE:
        ESP_LOGI(TAG, "pairing complete (status %d)", ev->pairing_complete.status);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* The phone is pairing again: drop the old pairing and require the code on the screen again. */
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) ble_store_util_delete_peer(&d.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
#endif
    default: break;
    }
    return 0;
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = (ble_uuid128_t *)&svc_uuid;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc) { ESP_LOGE(TAG, "adv fields rc=%d", rc); return; }
    struct ble_hs_adv_fields r = {0};      /* name goes in the scan response (UUID fills the main packet) */
    r.name = (uint8_t *)NAV_BLE_DEVICE_NAME;
    r.name_len = strlen(NAV_BLE_DEVICE_NAME);
    r.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&r);
    struct ble_gap_adv_params p = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN};
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc) ESP_LOGE(TAG, "adv start rc=%d", rc); else ESP_LOGI(TAG, "advertising as %s", NAV_BLE_DEVICE_NAME);
}

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &own_addr_type);
    start_advertising();
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_server_start(void)
{
    q = xQueueCreate(24, sizeof(item_t));
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
#if NAV_BLE_SECURE
    /* Passkey pairing (LE Secure Connections), bonds kept across restarts. */
    const esp_timer_create_args_t ta = {.callback = guard_expired, .name = "ble_guard"};
    ESP_ERROR_CHECK(esp_timer_create(&ta, &guard_timer));
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;       /* a full list of remembered phones makes room by forgetting the oldest */
    ble_store_config_init();
#endif
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(gatt_svcs));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(gatt_svcs));
    ble_svc_gap_device_name_set(NAV_BLE_DEVICE_NAME);
    nimble_port_freertos_init(host_task);
}

uint32_t nav_write_worst_us, nav_write_worst_ch, nav_write_worst_len;      /* the slowest nav_link_write since the last report (see lvgl_port.c) */

int ble_server_poll(void)
{
    item_t it;
    int n = 0;
    while (n < 8 && xQueueReceive(q, &it, 0) == pdTRUE) {
        const int64_t t0 = esp_timer_get_time();
        nav_link_write((nav_channel_t)it.ch, it.data, it.len);
        const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
        if (us > nav_write_worst_us) { nav_write_worst_us = us; nav_write_worst_ch = it.ch; nav_write_worst_len = it.len; }
        n++;
    }
    return n;
}

int ble_server_connected(void) { return connected; }
uint32_t ble_server_pairing_code(void) { return pairing_code; }
int ble_server_secure(void) { return secure; }

int ble_server_notify_motion(const uint8_t *data, unsigned len)
{
    if (!secure || !motion_subscribed || cur_conn == BLE_HS_CONN_HANDLE_NONE) return -1;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (!om) return -2;
    return ble_gatts_notify_custom(cur_conn, motion_handle, om);
}

/* Tell the phone which page is showing (it sends the fast compass data only for the compass page). */
void ble_server_set_page(uint8_t page)
{
    if (page == page_value) return;
    page_value = page;
    if (connected) ble_gatts_chr_updated(page_handle);      /* notifies the phone if it has subscribed */
}
