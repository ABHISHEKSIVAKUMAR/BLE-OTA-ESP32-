#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs_flash.h"

/* NimBLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "host/util/util.h"

/* ============================== Config ============================== */
#define OTA_RINGBUF_SIZE   4096
#define OTA_TASK_STACK     8192
#define OTA_TASK_PRIO      5

#define OTA_SVC_UUID16     0xFFF0
#define OTA_LEN_UUID16     0xFFF1
#define OTA_DATA_UUID16    0xFFF2

/* ============================== Globals ============================= */
static const char *TAG = "NIMBLE_BLE_OTA";

static RingbufHandle_t s_ringbuf = NULL;
static esp_ota_handle_t s_ota_out_handle = 0;

static volatile uint32_t g_expected_fw_len = 0;
static volatile uint32_t g_recv_total = 0;
static uint8_t own_addr_type;

/* ============================== Helpers ============================= */
static inline void print_addr(const uint8_t *addr)
{
    ESP_LOGI(TAG, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static bool ble_ota_ringbuf_init(uint32_t size)
{
    s_ringbuf = xRingbufferCreate(size, RINGBUF_TYPE_BYTEBUF);
    return (s_ringbuf != NULL);
}

static size_t write_to_ringbuf(const uint8_t *data, size_t size)
{
    return xRingbufferSend(s_ringbuf, (void *)data, size, portMAX_DELAY) ? size : 0;
}

/* ============================ OTA Task ============================== */
static void ota_task(void *arg)
{
    ESP_LOGI(TAG, "OTA task started");

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(running);
    if (!update) {
        ESP_LOGE(TAG, "No OTA partition found");
        vTaskDelete(NULL);
    }

    if (esp_ota_begin(update, OTA_SIZE_UNKNOWN, &s_ota_out_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed");
        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG, "Waiting for firmware... expected=%" PRIu32, g_expected_fw_len);

    uint8_t *data = NULL;
    size_t item_size = 0;
    uint32_t recv_len = 0;
    int last_percent = 0;

    for (;;) {
        data = (uint8_t *)xRingbufferReceive(s_ringbuf, &item_size, portMAX_DELAY);
        if (data && item_size > 0) {
            if (esp_ota_write(s_ota_out_handle, data, item_size) != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed");
                vRingbufferReturnItem(s_ringbuf, data);
                goto OTA_ERROR;
            }

            recv_len += item_size;
            g_recv_total = recv_len;

            int percent = (recv_len * 100) / g_expected_fw_len;
            if (percent >= last_percent + 5) {
                ESP_LOGI(TAG, "OTA progress: %d%%", percent);
                last_percent = percent;
            }

            vRingbufferReturnItem(s_ringbuf, data);

            if (g_expected_fw_len > 0 && recv_len >= g_expected_fw_len) break;
        }
    }

    if (esp_ota_end(s_ota_out_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed");
        goto OTA_ERROR;
    }

    if (esp_ota_set_boot_partition(update) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed");
        goto OTA_ERROR;
    }

    ESP_LOGI(TAG, "OTA complete. Rebooting...");
    esp_restart();

OTA_ERROR:
    ESP_LOGE(TAG, "OTA failed");
    vTaskDelete(NULL);
}

/* ========================== GATT Callbacks ========================== */
static int ota_len_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint8_t tmp[4];
    uint16_t pktlen = OS_MBUF_PKTLEN(ctxt->om);
    if (pktlen != 4) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    ble_hs_mbuf_to_flat(ctxt->om, tmp, sizeof(tmp), &pktlen);
    g_expected_fw_len = tmp[0] | (tmp[1] << 8) | (tmp[2] << 16) | (tmp[3] << 24);
    g_recv_total = 0;

    ESP_LOGI(TAG, "Expected FW length = %" PRIu32, g_expected_fw_len);
    return 0;
}

static int ota_data_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    struct os_mbuf *om = ctxt->om;
    while (om) {
        if (om->om_len > 0) {
            if (write_to_ringbuf(om->om_data, om->om_len) != om->om_len) {
                ESP_LOGE(TAG, "Ringbuffer full");
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
        }
        om = SLIST_NEXT(om, om_next);
    }
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(OTA_SVC_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(OTA_LEN_UUID16),
              .access_cb = ota_len_write_cb,
              .flags = BLE_GATT_CHR_F_WRITE },
            { .uuid = BLE_UUID16_DECLARE(OTA_DATA_UUID16),
              .access_cb = ota_data_write_cb,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
            { 0 }
        },
    },
    { 0 }
};

/* ======================== GAP / Advertising ========================= */
static void start_advertising(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) start_advertising();
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        start_advertising();
        return 0;
    default: return 0;
    }
}

static void start_advertising(void)
{
    struct ble_gap_adv_params adv = {0};
    struct ble_hs_adv_fields fields = {0};

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char *name = "nimble-ble-ota";
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event_cb, NULL);

    ESP_LOGI(TAG, "Advertising as \"%s\"", name);
}

/* ============================ Host Hooks ============================ */
static void on_reset(int reason) { ESP_LOGE(TAG, "NimBLE reset %d", reason); }

static void on_sync(void)
{
    ble_att_set_preferred_mtu(512);  // 🚀 Large MTU
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);

    uint8_t addr[6];
    ble_hs_id_copy_addr(own_addr_type, addr, NULL);
    ESP_LOGI(TAG, "Device Address:");
    print_addr(addr);

    start_advertising();
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ============================== Main ================================ */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!ble_ota_ringbuf_init(OTA_RINGBUF_SIZE)) {
        ESP_LOGE(TAG, "Ringbuffer init failed");
        return;
    }

    xTaskCreate(ota_task, "ota_task", OTA_TASK_STACK, NULL, OTA_TASK_PRIO, NULL);

    if (nimble_port_init() != ESP_OK) return;

    ble_svc_gap_init();
    ble_svc_gap_device_name_set("nimble-ble-ota");

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    nimble_port_freertos_init(host_task);
}
//Stable WIP