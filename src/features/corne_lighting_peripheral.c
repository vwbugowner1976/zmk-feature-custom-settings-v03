/*
 * Corne rear-lighting engine for the split peripheral half.
 *
 * The central owns keymap/Bluetooth state and runtime settings. The peripheral
 * only renders LEDs and consumes compact corne_lighting_relay events.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>

#include <zmk/event_manager.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)
#include <zmk/split/relay/event.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !DT_HAS_CHOSEN(zmk_underglow)
#error "CONFIG_ZMK_CORNE_LIGHTING requires a zmk,underglow chosen LED strip"
#endif

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define LED_COUNT DT_PROP(STRIP_NODE, chain_length)
#define TICK_MS 40
#define RELAY_VERSION 1

struct corne_lighting_config {
    bool enabled;
    uint32_t ambient_color;
    uint8_t ambient_brightness;
    uint8_t firefly_count;
    uint16_t firefly_interval_ms;
    uint16_t firefly_fade_ms;
    uint8_t firefly_variation;

    bool layer_enabled;
    uint8_t layer_mode;
    uint16_t layer_duration_ms;
    uint8_t layer_brightness;
    uint32_t layer_colors[6];

    bool bt_enabled;
    uint16_t bt_duration_ms;
    uint8_t bt_effect;
    uint8_t bt_brightness;
    uint32_t bt_colors[5];
};

struct firefly_state {
    uint8_t level;
    int8_t direction;
    int8_t variation;
};

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[LED_COUNT];
static struct firefly_state fireflies[LED_COUNT];
static struct corne_lighting_config cfg;
static uint8_t active_layer;
static uint8_t active_bt_profile;
static int64_t layer_flash_until;
static int64_t bt_until;
static int64_t bt_started;
static int64_t next_spawn_at;
static bool initialized;

static struct k_work_delayable render_work;
static struct k_work_delayable startup_work;

static uint8_t scale_u8(uint8_t value, uint32_t numerator, uint32_t denominator) {
    if (denominator == 0U) {
        return 0;
    }
    uint32_t scaled = ((uint32_t)value * numerator) / denominator;
    return (uint8_t)MIN(scaled, 255U);
}

static struct led_rgb packed_to_rgb(uint32_t packed, uint8_t brightness, uint8_t level,
                                    int8_t variation) {
    int32_t pct = CLAMP(100 + variation, 0, 200);
    uint32_t factor = (uint32_t)brightness * level * (uint32_t)pct;
    uint32_t denom = 100U * 255U * 100U;
    struct led_rgb out = {
        .r = scale_u8((packed >> 16) & 0xff, factor, denom),
        .g = scale_u8((packed >> 8) & 0xff, factor, denom),
        .b = scale_u8(packed & 0xff, factor, denom),
    };
    return out;
}

static void clear_pixels(void) { memset(pixels, 0, sizeof(pixels)); }

static uint8_t pulse_level(int64_t now, int64_t start, uint16_t duration, uint8_t cycles) {
    if (duration == 0U || now <= start) {
        return 0;
    }
    uint32_t elapsed = (uint32_t)MIN(now - start, (int64_t)duration);
    uint32_t phase = (elapsed * cycles * 510U) / duration;
    uint32_t within = phase % 510U;
    return (uint8_t)(within <= 255U ? within : 510U - within);
}

static void render_solid(uint32_t color, uint8_t brightness, uint8_t level) {
    struct led_rgb rgb = packed_to_rgb(color, brightness, level, 0);
    for (size_t i = 0; i < LED_COUNT; i++) {
        pixels[i] = rgb;
    }
}

static uint8_t count_active_fireflies(void) {
    uint8_t count = 0;
    for (size_t i = 0; i < LED_COUNT; i++) {
        if (fireflies[i].direction != 0) {
            count++;
        }
    }
    return count;
}

static void spawn_firefly(int64_t now) {
    if (count_active_fireflies() >= cfg.firefly_count || LED_COUNT == 0) {
        next_spawn_at = now + MAX(50, cfg.firefly_interval_ms / 3);
        return;
    }

    size_t start = sys_rand32_get() % LED_COUNT;
    for (size_t attempt = 0; attempt < LED_COUNT; attempt++) {
        size_t index = (start + attempt) % LED_COUNT;
        if (fireflies[index].direction == 0) {
            uint8_t variation = cfg.firefly_variation;
            int32_t signed_variation = 0;
            if (variation > 0) {
                signed_variation = (int32_t)(sys_rand32_get() % (variation * 2U + 1U)) - variation;
            }
            fireflies[index].level = 1;
            fireflies[index].direction = 1;
            fireflies[index].variation = (int8_t)signed_variation;
            break;
        }
    }

    uint32_t jitter = MAX(1U, cfg.firefly_interval_ms / 2U);
    next_spawn_at = now + (cfg.firefly_interval_ms / 2U) + (sys_rand32_get() % jitter);
}

static void render_fireflies(int64_t now) {
    clear_pixels();

    if (now >= next_spawn_at) {
        spawn_firefly(now);
    }

    uint32_t half_fade = MAX(50U, cfg.firefly_fade_ms / 2U);
    uint8_t step = (uint8_t)CLAMP((255U * TICK_MS) / half_fade, 1U, 255U);

    for (size_t i = 0; i < LED_COUNT; i++) {
        struct firefly_state *f = &fireflies[i];
        if (f->direction == 0) {
            continue;
        }

        if (f->direction > 0) {
            if ((uint16_t)f->level + step >= 255U) {
                f->level = 255;
                f->direction = -1;
            } else {
                f->level += step;
            }
        } else {
            if (f->level <= step) {
                f->level = 0;
                f->direction = 0;
                f->variation = 0;
                continue;
            }
            f->level -= step;
        }

        pixels[i] = packed_to_rgb(cfg.ambient_color, cfg.ambient_brightness, f->level, f->variation);
    }
}

static void render_current_frame(void) {
    int64_t now = k_uptime_get();

    if (!cfg.enabled || !device_is_ready(strip)) {
        clear_pixels();
    } else if (cfg.bt_enabled && now < bt_until && active_bt_profile < ARRAY_SIZE(cfg.bt_colors)) {
        uint8_t level = 255;
        if (cfg.bt_effect == 1) {
            level = pulse_level(now, bt_started, cfg.bt_duration_ms, 1);
        } else if (cfg.bt_effect == 2) {
            level = pulse_level(now, bt_started, cfg.bt_duration_ms, 2);
        }
        render_solid(cfg.bt_colors[active_bt_profile], cfg.bt_brightness, level);
    } else if (cfg.layer_enabled && cfg.layer_mode == 1 && now < layer_flash_until &&
               active_layer < ARRAY_SIZE(cfg.layer_colors)) {
        render_solid(cfg.layer_colors[active_layer], cfg.layer_brightness, 255);
    } else if (cfg.layer_enabled && cfg.layer_mode == 0 && active_layer > 0 &&
               active_layer < ARRAY_SIZE(cfg.layer_colors)) {
        render_solid(cfg.layer_colors[active_layer], cfg.layer_brightness, 255);
    } else {
        render_fireflies(now);
    }

    int rc = led_strip_update_rgb(strip, pixels, LED_COUNT);
    if (rc < 0) {
        LOG_WRN("Corne peripheral lighting strip update failed: %d", rc);
    }
}

static void render_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    render_current_frame();
    k_work_reschedule(&render_work, K_MSEC(TICK_MS));
}

static void set_defaults(void) {
    cfg = (struct corne_lighting_config){
        .enabled = true,
        .ambient_color = 0xA8FF40,
        .ambient_brightness = 12,
        .firefly_count = 3,
        .firefly_interval_ms = 900,
        .firefly_fade_ms = 1600,
        .firefly_variation = 18,
        .layer_enabled = true,
        .layer_mode = 0,
        .layer_duration_ms = 500,
        .layer_brightness = 35,
        .layer_colors = {0x70FF70, 0x4080FF, 0xA050FF, 0xFF9D30, 0xFF50A8, 0x40DFFF},
        .bt_enabled = true,
        .bt_duration_ms = 800,
        .bt_effect = 1,
        .bt_brightness = 40,
        .bt_colors = {0x3090FF, 0x40E070, 0xFFD040, 0xA060FF, 0xFF5040},
    };
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_RELAY_EVENT)
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
    if (!ev || ev->version != RELAY_VERSION) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    cfg = ev->config;
    active_layer = ev->active_layer;
    active_bt_profile = ev->active_bt_profile;

    int64_t now = k_uptime_get();
    if (ev->kind == 1 && cfg.layer_mode == 1) {
        layer_flash_until = now + cfg.layer_duration_ms;
    } else if (ev->kind == 2) {
        bt_started = now;
        bt_until = now + cfg.bt_duration_ms;
    }

    return ZMK_EV_EVENT_HANDLED;
}
ZMK_LISTENER(corne_lighting_relay_listener, relay_listener);
ZMK_SUBSCRIPTION(corne_lighting_relay_listener, corne_lighting_relay);
#endif

static void startup_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (!device_is_ready(strip)) {
        LOG_ERR("Corne peripheral lighting strip is not ready");
        return;
    }

    next_spawn_at = k_uptime_get();
    initialized = true;
    k_work_reschedule(&render_work, K_NO_WAIT);
}

static int corne_lighting_peripheral_init(void) {
    set_defaults();
    k_work_init_delayable(&render_work, render_work_handler);
    k_work_init_delayable(&startup_work, startup_handler);
    k_work_reschedule(&startup_work, K_MSEC(800));
    return 0;
}
SYS_INIT(corne_lighting_peripheral_init, APPLICATION, 95);
