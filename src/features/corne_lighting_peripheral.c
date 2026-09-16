/*
 * Corne lighting renderer for the split peripheral half.
 *
 * The central owns keymap/Bluetooth state and runtime settings. The peripheral
 * only renders LEDs and consumes compact corne_lighting_relay events.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include "corne_lighting_common.h"

#include <zmk/event_manager.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)
#include <zmk/split/relay/event.h>

struct corne_lighting_relay {
    uint8_t source;
    uint8_t version;
    uint8_t kind;
    uint8_t active_layer;
    uint8_t active_bt_profile;
    struct corne_lighting_config config;
} __packed;

ZMK_EVENT_DECLARE(corne_lighting_relay);
ZMK_EVENT_IMPL(corne_lighting_relay);
ZMK_RELAY_EVENT_HANDLE(corne_lighting_relay, clr, source)

static int relay_listener(const zmk_event_t *eh) {
    const struct corne_lighting_relay *ev = as_corne_lighting_relay(eh);
    if (!ev || ev->version != CORNE_LIGHTING_RELAY_VERSION) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint8_t previous_effect = corne_lighting_cfg.ambient_effect;
    corne_lighting_cfg = ev->config;
    corne_active_layer = ev->active_layer;
    corne_active_bt_profile = ev->active_bt_profile;

    if (previous_effect != corne_lighting_cfg.ambient_effect) {
        corne_reset_ambient_state();
    }

    int64_t now = k_uptime_get();
    if (ev->kind == 1 && corne_lighting_cfg.layer_mode == 1) {
        corne_layer_flash_until = now + corne_lighting_cfg.layer_duration_ms;
    } else if (ev->kind == 2) {
        corne_bt_started = now;
        corne_bt_until = now + corne_lighting_cfg.bt_duration_ms;
    }

    return ZMK_EV_EVENT_HANDLED;
}
ZMK_LISTENER(corne_lighting_relay_listener, relay_listener);
ZMK_SUBSCRIPTION(corne_lighting_relay_listener, corne_lighting_relay);
#endif

static void startup_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (!device_is_ready(corne_lighting_strip)) {
        LOG_ERR("Corne peripheral lighting strip is not ready");
        return;
    }

    corne_reset_ambient_state();
    corne_lighting_initialized = true;
    k_work_reschedule(&corne_render_work, K_NO_WAIT);
}

static int corne_lighting_peripheral_init(void) {
    corne_lighting_set_defaults();
    k_work_init_delayable(&corne_render_work, corne_render_work_handler);
    k_work_init_delayable(&corne_startup_work, startup_handler);
    k_work_reschedule(&corne_startup_work, K_MSEC(800));
    return 0;
}
SYS_INIT(corne_lighting_peripheral_init, APPLICATION, 95);
