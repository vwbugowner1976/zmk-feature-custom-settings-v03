/*
 * Corne lighting central/controller for ZMK v0.3 + DYA Studio extensions.
 *
 * Runtime settings live on the split central. Rendering is shared with the
 * peripheral through corne_lighting_common.h. Split synchronization uses a
 * compact 8-byte relay event so it remains reliable on the BLE split link.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include "corne_lighting_common.h"
#include "corne_lighting_relay.h"

#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
#include <cormoran/zmk/custom_settings.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)
#include <zmk/split/central.h>
#include <zmk/split/relay/event.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_STUDIO_RPC) && IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC)
#include <zmk/studio/custom.h>
#endif

#define LIGHTING_SUBSYSTEM "corne_lighting"
#define CONFIG_SYNC_STEP_MS 40
#define PERIODIC_SYNC_SEC 10

#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
#define PUBLIC ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC
#define RW ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE
#define INT_SETTING(name, key, def, min, max)                                                       \
    ZMK_CUSTOM_SETTING_DEFINE(name, LIGHTING_SUBSYSTEM, key, ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,  \
                              ZMK_CUSTOM_SETTING_VALUE_INT32(def), PUBLIC, RW, RW,                  \
                              ZMK_CUSTOM_SETTING_RANGE_INT32(min, max))
#define BOOL_SETTING(name, key, def)                                                                \
    ZMK_CUSTOM_SETTING_DEFINE(name, LIGHTING_SUBSYSTEM, key, ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL,   \
                              ZMK_CUSTOM_SETTING_VALUE_BOOL(def), PUBLIC, RW, RW,                   \
                              ZMK_CUSTOM_SETTING_NO_CONSTRAINT)

BOOL_SETTING(corne_led_enabled, "enabled", true);
INT_SETTING(corne_led_ambient_effect, "ambient_effect", CORNE_AMBIENT_FIREFLY, 0, 4);
INT_SETTING(corne_led_ambient_color, "ambient_color", 0xA8FF40, 0, 0xFFFFFF);
INT_SETTING(corne_led_ambient_brightness, "ambient_brightness", 12, 0, 100);
INT_SETTING(corne_led_ambient_period, "ambient_period_ms", 2400, 400, 10000);
INT_SETTING(corne_led_firefly_count, "firefly_count", 3, 1, 8);
INT_SETTING(corne_led_firefly_interval, "firefly_interval_ms", 900, 100, 5000);
INT_SETTING(corne_led_firefly_fade, "firefly_fade_ms", 1600, 100, 5000);
INT_SETTING(corne_led_firefly_variation, "firefly_variation", 18, 0, 50);

BOOL_SETTING(corne_led_layer_enabled, "layer_enabled", true);
INT_SETTING(corne_led_layer_mode, "layer_mode", 0, 0, 1);
INT_SETTING(corne_led_layer_duration, "layer_duration_ms", 500, 100, 3000);
INT_SETTING(corne_led_layer_brightness, "layer_brightness", 35, 1, 100);
INT_SETTING(corne_led_layer_color_0, "layer_color_0", 0x70FF70, 0, 0xFFFFFF);
INT_SETTING(corne_led_layer_color_1, "layer_color_1", 0x4080FF, 0, 0xFFFFFF);
INT_SETTING(corne_led_layer_color_2, "layer_color_2", 0xA050FF, 0, 0xFFFFFF);
INT_SETTING(corne_led_layer_color_3, "layer_color_3", 0xFF9D30, 0, 0xFFFFFF);
INT_SETTING(corne_led_layer_color_4, "layer_color_4", 0xFF50A8, 0, 0xFFFFFF);
INT_SETTING(corne_led_layer_color_5, "layer_color_5", 0x40DFFF, 0, 0xFFFFFF);

BOOL_SETTING(corne_led_bt_enabled, "bt_enabled", true);
INT_SETTING(corne_led_bt_duration, "bt_duration_ms", 800, 100, 3000);
INT_SETTING(corne_led_bt_effect, "bt_effect", 1, 0, 2);
INT_SETTING(corne_led_bt_brightness, "bt_brightness", 40, 1, 100);
INT_SETTING(corne_led_bt_color_0, "bt_color_0", 0x3090FF, 0, 0xFFFFFF);
INT_SETTING(corne_led_bt_color_1, "bt_color_1", 0x40E070, 0, 0xFFFFFF);
INT_SETTING(corne_led_bt_color_2, "bt_color_2", 0xFFD040, 0, 0xFFFFFF);
INT_SETTING(corne_led_bt_color_3, "bt_color_3", 0xA060FF, 0, 0xFFFFFF);
INT_SETTING(corne_led_bt_color_4, "bt_color_4", 0xFF5040, 0, 0xFFFFFF);

static int read_int(const char *key, int32_t *out) {
    struct zmk_custom_setting_value value;
    int rc = zmk_custom_setting_read_by_key(LIGHTING_SUBSYSTEM, key, &value);
    if (rc == 0 && value.type == ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32) {
        *out = value.int32_value;
        return 0;
    }
    return rc == 0 ? -EINVAL : rc;
}

static int read_bool(const char *key, bool *out) {
    struct zmk_custom_setting_value value;
    int rc = zmk_custom_setting_read_by_key(LIGHTING_SUBSYSTEM, key, &value);
    if (rc == 0 && value.type == ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL) {
        *out = value.bool_value;
        return 0;
    }
    return rc == 0 ? -EINVAL : rc;
}

static void load_settings(void) {
    int32_t v;
    (void)read_bool("enabled", &corne_lighting_cfg.enabled);
    if (read_int("ambient_effect", &v) == 0) corne_lighting_cfg.ambient_effect = (uint8_t)v;
    if (read_int("ambient_color", &v) == 0) corne_lighting_cfg.ambient_color = (uint32_t)v;
    if (read_int("ambient_brightness", &v) == 0) corne_lighting_cfg.ambient_brightness = (uint8_t)v;
    if (read_int("ambient_period_ms", &v) == 0) corne_lighting_cfg.ambient_period_ms = (uint16_t)v;
    if (read_int("firefly_count", &v) == 0) corne_lighting_cfg.firefly_count = (uint8_t)v;
    if (read_int("firefly_interval_ms", &v) == 0) corne_lighting_cfg.firefly_interval_ms = (uint16_t)v;
    if (read_int("firefly_fade_ms", &v) == 0) corne_lighting_cfg.firefly_fade_ms = (uint16_t)v;
    if (read_int("firefly_variation", &v) == 0) corne_lighting_cfg.firefly_variation = (uint8_t)v;

    (void)read_bool("layer_enabled", &corne_lighting_cfg.layer_enabled);
    if (read_int("layer_mode", &v) == 0) corne_lighting_cfg.layer_mode = (uint8_t)v;
    if (read_int("layer_duration_ms", &v) == 0) corne_lighting_cfg.layer_duration_ms = (uint16_t)v;
    if (read_int("layer_brightness", &v) == 0) corne_lighting_cfg.layer_brightness = (uint8_t)v;
    for (int i = 0; i < 6; i++) {
        char key[20];
        snprintk(key, sizeof(key), "layer_color_%d", i);
        if (read_int(key, &v) == 0) corne_lighting_cfg.layer_colors[i] = (uint32_t)v;
    }

    (void)read_bool("bt_enabled", &corne_lighting_cfg.bt_enabled);
    if (read_int("bt_duration_ms", &v) == 0) corne_lighting_cfg.bt_duration_ms = (uint16_t)v;
    if (read_int("bt_effect", &v) == 0) corne_lighting_cfg.bt_effect = (uint8_t)v;
    if (read_int("bt_brightness", &v) == 0) corne_lighting_cfg.bt_brightness = (uint8_t)v;
    for (int i = 0; i < 5; i++) {
        char key[16];
        snprintk(key, sizeof(key), "bt_color_%d", i);
        if (read_int(key, &v) == 0) corne_lighting_cfg.bt_colors[i] = (uint32_t)v;
    }
}
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)
ZMK_EVENT_DECLARE(corne_lighting_relay);
ZMK_EVENT_IMPL(corne_lighting_relay);
ZMK_RELAY_EVENT_CENTRAL_TO_PERIPHERAL(corne_lighting_relay, clr, source)
ZMK_RELAY_EVENT_HANDLE(corne_lighting_relay, clr, source)

static void send_relay(uint8_t kind, uint8_t id, uint32_t value) {
    struct corne_lighting_relay ev = {
        .source = ZMK_RELAY_EVENT_SOURCE_SELF,
        .version = CORNE_LIGHTING_RELAY_PROTOCOL_VERSION,
        .kind = kind,
        .id = id,
        .value = value,
    };
    raise_corne_lighting_relay(ev);
}

static struct k_work_delayable config_sync_work;
static struct k_work_delayable periodic_sync_work;
static uint8_t config_sync_id;

static void start_config_sync(void) {
    config_sync_id = 0U;
    k_work_reschedule(&config_sync_work, K_NO_WAIT);
}

static void config_sync_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (config_sync_id < CORNE_CFG_COUNT) {
        uint8_t id = config_sync_id++;
        send_relay(CORNE_LIGHTING_RELAY_KIND_CONFIG, id, corne_lighting_config_value(id));
        k_work_reschedule(&config_sync_work, K_MSEC(CONFIG_SYNC_STEP_MS));
        return;
    }

    /* End every snapshot with the current layer so the peripheral converges
     * even if it connected after the original layer event was emitted. */
    send_relay(CORNE_LIGHTING_RELAY_KIND_LAYER, corne_active_layer, 0U);
}

static void periodic_sync_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (corne_lighting_initialized) {
        start_config_sync();
    }
    k_work_reschedule(&periodic_sync_work, K_SECONDS(PERIODIC_SYNC_SEC));
}
#endif

#if IS_ENABLED(CONFIG_ZMK_STUDIO_RPC) && IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC)
static bool corne_lighting_rpc_handle_request(const zmk_custom_CallRequest *request,
                                               pb_callback_t *encode_response);
static struct zmk_rpc_custom_subsystem_meta corne_lighting_meta = {
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};
ZMK_RPC_CUSTOM_SUBSYSTEM(corne_lighting, &corne_lighting_meta, corne_lighting_rpc_handle_request);

static bool corne_lighting_rpc_handle_request(const zmk_custom_CallRequest *request,
                                               pb_callback_t *encode_response) {
    ARG_UNUSED(request);
    ARG_UNUSED(encode_response);
    return false;
}
#endif

static int layer_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    corne_active_layer = zmk_keymap_highest_layer_active();
    if (corne_lighting_cfg.layer_mode == 1) {
        corne_layer_flash_until = k_uptime_get() + corne_lighting_cfg.layer_duration_ms;
    }
#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)
    send_relay(CORNE_LIGHTING_RELAY_KIND_LAYER, corne_active_layer, 0U);
#endif
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(corne_lighting_layer_listener, layer_listener);
ZMK_SUBSCRIPTION(corne_lighting_layer_listener, zmk_layer_state_changed);

static int bt_listener(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *ev = as_zmk_ble_active_profile_changed(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    corne_active_bt_profile = ev->index;
    corne_bt_started = k_uptime_get();
    corne_bt_until = corne_bt_started + corne_lighting_cfg.bt_duration_ms;
#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)
    send_relay(CORNE_LIGHTING_RELAY_KIND_BLUETOOTH, corne_active_bt_profile, 0U);
#endif
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(corne_lighting_bt_listener, bt_listener);
ZMK_SUBSCRIPTION(corne_lighting_bt_listener, zmk_ble_active_profile_changed);

#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
static int setting_listener(const zmk_event_t *eh) {
    const struct zmk_custom_setting_changed *ev = as_zmk_custom_setting_changed(eh);
    if (!ev || !ev->setting || strcmp(ev->setting->custom_subsystem_id, LIGHTING_SUBSYSTEM) != 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint8_t previous_effect = corne_lighting_cfg.ambient_effect;
    load_settings();
    if (previous_effect != corne_lighting_cfg.ambient_effect) {
        corne_reset_ambient_state();
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)
    uint8_t id = corne_lighting_config_id_from_key(ev->setting->key);
    if (id < CORNE_CFG_COUNT) {
        send_relay(CORNE_LIGHTING_RELAY_KIND_CONFIG, id, corne_lighting_config_value(id));
    } else {
        start_config_sync();
    }
#endif
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(corne_lighting_setting_listener, setting_listener);
ZMK_SUBSCRIPTION(corne_lighting_setting_listener, zmk_custom_setting_changed);
#endif

static void startup_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (!device_is_ready(corne_lighting_strip)) {
        LOG_ERR("Corne lighting strip is not ready");
        return;
    }

#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    load_settings();
#endif
    corne_active_layer = zmk_keymap_highest_layer_active();
    corne_reset_ambient_state();
    corne_lighting_initialized = true;

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)
    start_config_sync();
    k_work_reschedule(&periodic_sync_work, K_SECONDS(PERIODIC_SYNC_SEC));
#endif
    k_work_reschedule(&corne_render_work, K_NO_WAIT);
}

static int corne_lighting_init(void) {
    corne_lighting_set_defaults();
    k_work_init_delayable(&corne_render_work, corne_render_work_handler);
    k_work_init_delayable(&corne_startup_work, startup_handler);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)
    k_work_init_delayable(&config_sync_work, config_sync_handler);
    k_work_init_delayable(&periodic_sync_work, periodic_sync_handler);
#endif
    k_work_reschedule(&corne_startup_work, K_MSEC(800));
    return 0;
}
SYS_INIT(corne_lighting_init, APPLICATION, 95);
