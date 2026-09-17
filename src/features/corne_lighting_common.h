/*
 * Shared Corne lighting renderer for ZMK v0.3 + DYA.
 *
 * Priority: Bluetooth profile > layer indicator > ambient effect.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>

#include <zmk/keymap.h>

#include <zephyr/logging/log.h>

#if !DT_HAS_CHOSEN(zmk_underglow)
#error "CONFIG_ZMK_CORNE_LIGHTING requires a zmk,underglow chosen LED strip"
#endif

#define CORNE_LIGHTING_STRIP_NODE DT_CHOSEN(zmk_underglow)
#define CORNE_LIGHTING_LED_COUNT DT_PROP(CORNE_LIGHTING_STRIP_NODE, chain_length)
#define CORNE_LIGHTING_TICK_MS 40
#define CORNE_LIGHTING_RELAY_VERSION 2
#define CORNE_LIGHTING_LAYER_COUNT ZMK_KEYMAP_LAYERS_LEN

#define CORNE_AMBIENT_FIREFLY 0
#define CORNE_AMBIENT_BREATHING 1
#define CORNE_AMBIENT_COMET 2
#define CORNE_AMBIENT_SPARKLE 3
#define CORNE_AMBIENT_RAINBOW 4

struct corne_lighting_config {
    bool enabled;
    uint8_t ambient_effect;
    uint32_t ambient_color;
    uint8_t ambient_brightness;
    uint16_t ambient_period_ms;
    uint8_t firefly_count;
    uint16_t firefly_interval_ms;
    uint16_t firefly_fade_ms;
    uint8_t firefly_variation;

    bool layer_enabled;
    uint8_t layer_mode; /* 0 = while active, 1 = flash on change */
    uint16_t layer_duration_ms;
    uint8_t layer_brightness;
    uint32_t layer_colors[CORNE_LIGHTING_LAYER_COUNT];

    bool bt_enabled;
    uint16_t bt_duration_ms;
    uint8_t bt_effect; /* 0 = solid, 1 = single pulse, 2 = double pulse */
    uint8_t bt_brightness;
    uint32_t bt_colors[5];
};

struct corne_firefly_state {
    uint8_t level;
    int8_t direction;
    int8_t variation;
};

static const struct device *const corne_lighting_strip = DEVICE_DT_GET(CORNE_LIGHTING_STRIP_NODE);
static struct led_rgb corne_lighting_pixels[CORNE_LIGHTING_LED_COUNT];
static struct corne_firefly_state corne_fireflies[CORNE_LIGHTING_LED_COUNT];
static bool corne_sparkles[CORNE_LIGHTING_LED_COUNT];
static struct corne_lighting_config corne_lighting_cfg;
static uint8_t corne_active_layer;
static uint8_t corne_active_bt_profile;
static int64_t corne_layer_flash_until;
static int64_t corne_bt_until;
static int64_t corne_bt_started;
static int64_t corne_next_spawn_at;
static uint32_t corne_last_sparkle_slot = UINT32_MAX;
static bool corne_lighting_initialized;

static struct k_work_delayable corne_render_work;
static struct k_work_delayable corne_startup_work;

static uint8_t corne_scale_u8(uint8_t value, uint32_t numerator, uint32_t denominator) {
    if (denominator == 0U) {
        return 0;
    }
    uint32_t scaled = ((uint32_t)value * numerator) / denominator;
    return (uint8_t)MIN(scaled, 255U);
}

static struct led_rgb corne_packed_to_rgb(uint32_t packed, uint8_t brightness, uint8_t level,
                                          int8_t variation) {
    int32_t pct = CLAMP(100 + variation, 0, 200);
    uint32_t factor = (uint32_t)brightness * level * (uint32_t)pct;
    uint32_t denom = 100U * 255U * 100U;
    return (struct led_rgb){
        .r = corne_scale_u8((packed >> 16) & 0xff, factor, denom),
        .g = corne_scale_u8((packed >> 8) & 0xff, factor, denom),
        .b = corne_scale_u8(packed & 0xff, factor, denom),
    };
}

static void corne_clear_pixels(void) { memset(corne_lighting_pixels, 0, sizeof(corne_lighting_pixels)); }

static void corne_reset_ambient_state(void) {
    memset(corne_fireflies, 0, sizeof(corne_fireflies));
    memset(corne_sparkles, 0, sizeof(corne_sparkles));
    corne_next_spawn_at = k_uptime_get();
    corne_last_sparkle_slot = UINT32_MAX;
}

static uint8_t corne_one_shot_pulse_level(int64_t now, int64_t start, uint16_t duration,
                                          uint8_t cycles) {
    if (duration == 0U || now <= start) {
        return 0;
    }
    uint32_t elapsed = (uint32_t)MIN(now - start, (int64_t)duration);
    uint32_t phase = (elapsed * cycles * 510U) / duration;
    uint32_t within = phase % 510U;
    return (uint8_t)(within <= 255U ? within : 510U - within);
}

static uint8_t corne_periodic_triangle(int64_t now, uint32_t period_ms) {
    period_ms = MAX(period_ms, 80U);
    uint32_t phase_ms = (uint32_t)(now % period_ms);
    uint32_t phase = (phase_ms * 510U) / period_ms;
    return (uint8_t)(phase <= 255U ? phase : 510U - phase);
}

static void corne_render_solid(uint32_t color, uint8_t brightness, uint8_t level) {
    struct led_rgb rgb = corne_packed_to_rgb(color, brightness, level, 0);
    for (size_t i = 0; i < CORNE_LIGHTING_LED_COUNT; i++) {
        corne_lighting_pixels[i] = rgb;
    }
}

static uint8_t corne_count_active_fireflies(void) {
    uint8_t count = 0;
    for (size_t i = 0; i < CORNE_LIGHTING_LED_COUNT; i++) {
        if (corne_fireflies[i].direction != 0) {
            count++;
        }
    }
    return count;
}

static void corne_spawn_firefly(int64_t now) {
    if (CORNE_LIGHTING_LED_COUNT == 0 ||
        corne_count_active_fireflies() >= corne_lighting_cfg.firefly_count) {
        corne_next_spawn_at = now + MAX(50, corne_lighting_cfg.firefly_interval_ms / 3);
        return;
    }

    size_t start = sys_rand32_get() % CORNE_LIGHTING_LED_COUNT;
    for (size_t attempt = 0; attempt < CORNE_LIGHTING_LED_COUNT; attempt++) {
        size_t index = (start + attempt) % CORNE_LIGHTING_LED_COUNT;
        if (corne_fireflies[index].direction != 0) {
            continue;
        }

        uint8_t variation = corne_lighting_cfg.firefly_variation;
        int32_t signed_variation = 0;
        if (variation > 0) {
            signed_variation = (int32_t)(sys_rand32_get() % (variation * 2U + 1U)) - variation;
        }
        corne_fireflies[index].level = 1;
        corne_fireflies[index].direction = 1;
        corne_fireflies[index].variation = (int8_t)signed_variation;
        break;
    }

    uint32_t jitter = MAX(1U, corne_lighting_cfg.firefly_interval_ms / 2U);
    corne_next_spawn_at = now + (corne_lighting_cfg.firefly_interval_ms / 2U) +
                          (sys_rand32_get() % jitter);
}

static void corne_render_fireflies(int64_t now) {
    corne_clear_pixels();

    if (now >= corne_next_spawn_at) {
        corne_spawn_firefly(now);
    }

    uint32_t half_fade = MAX(50U, corne_lighting_cfg.firefly_fade_ms / 2U);
    uint8_t step = (uint8_t)CLAMP((255U * CORNE_LIGHTING_TICK_MS) / half_fade, 1U, 255U);

    for (size_t i = 0; i < CORNE_LIGHTING_LED_COUNT; i++) {
        struct corne_firefly_state *f = &corne_fireflies[i];
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

        corne_lighting_pixels[i] =
            corne_packed_to_rgb(corne_lighting_cfg.ambient_color,
                                corne_lighting_cfg.ambient_brightness, f->level, f->variation);
    }
}

static void corne_render_breathing(int64_t now) {
    uint8_t wave = corne_periodic_triangle(now, corne_lighting_cfg.ambient_period_ms);
    uint8_t level = (uint8_t)(28U + ((uint32_t)wave * 227U) / 255U);
    corne_render_solid(corne_lighting_cfg.ambient_color,
                       corne_lighting_cfg.ambient_brightness, level);
}

static void corne_render_comet(int64_t now) {
    corne_clear_pixels();
    if (CORNE_LIGHTING_LED_COUNT == 0) {
        return;
    }

    uint32_t period = MAX((uint32_t)corne_lighting_cfg.ambient_period_ms, 400U);
    uint32_t phase = (uint32_t)(now % period);
    size_t head = ((uint64_t)phase * CORNE_LIGHTING_LED_COUNT) / period;
    uint8_t trail = CLAMP(corne_lighting_cfg.firefly_count, 1U,
                          (uint8_t)MIN(CORNE_LIGHTING_LED_COUNT, 8U));

    for (uint8_t offset = 0; offset < trail; offset++) {
        size_t index = (head + CORNE_LIGHTING_LED_COUNT - offset) % CORNE_LIGHTING_LED_COUNT;
        uint8_t level = (uint8_t)(255U - ((uint32_t)offset * 220U) / trail);
        corne_lighting_pixels[index] =
            corne_packed_to_rgb(corne_lighting_cfg.ambient_color,
                                corne_lighting_cfg.ambient_brightness, level, 0);
    }
}

static void corne_refresh_sparkles(uint32_t slot) {
    memset(corne_sparkles, 0, sizeof(corne_sparkles));
    if (CORNE_LIGHTING_LED_COUNT == 0) {
        return;
    }

    uint8_t target = CLAMP(corne_lighting_cfg.firefly_count, 1U,
                           (uint8_t)MIN(CORNE_LIGHTING_LED_COUNT, 8U));
    uint8_t placed = 0;
    uint16_t attempts = 0;
    while (placed < target && attempts++ < 64U) {
        size_t index = sys_rand32_get() % CORNE_LIGHTING_LED_COUNT;
        if (!corne_sparkles[index]) {
            corne_sparkles[index] = true;
            placed++;
        }
    }
    corne_last_sparkle_slot = slot;
}

static void corne_render_sparkle(int64_t now) {
    corne_clear_pixels();
    uint32_t slot_ms = MAX(60U, (uint32_t)corne_lighting_cfg.ambient_period_ms / 12U);
    uint32_t slot = (uint32_t)(now / slot_ms);
    if (slot != corne_last_sparkle_slot) {
        corne_refresh_sparkles(slot);
    }

    for (size_t i = 0; i < CORNE_LIGHTING_LED_COUNT; i++) {
        if (!corne_sparkles[i]) {
            continue;
        }
        int8_t variation = 0;
        uint8_t range = corne_lighting_cfg.firefly_variation;
        if (range > 0U) {
            uint32_t hash = (uint32_t)(slot * 1103515245U + (uint32_t)i * 12345U);
            variation = (int8_t)((hash % (range * 2U + 1U)) - range);
        }
        corne_lighting_pixels[i] =
            corne_packed_to_rgb(corne_lighting_cfg.ambient_color,
                                corne_lighting_cfg.ambient_brightness, 255, variation);
    }
}

static uint32_t corne_wheel_color(uint8_t pos) {
    if (pos < 85U) {
        return ((uint32_t)(255U - pos * 3U) << 16) | ((uint32_t)(pos * 3U) << 8);
    }
    if (pos < 170U) {
        pos -= 85U;
        return ((uint32_t)(255U - pos * 3U) << 8) | (uint32_t)(pos * 3U);
    }
    pos -= 170U;
    return ((uint32_t)(pos * 3U) << 16) | (uint32_t)(255U - pos * 3U);
}

static void corne_render_rainbow(int64_t now) {
    corne_clear_pixels();
    if (CORNE_LIGHTING_LED_COUNT == 0) {
        return;
    }

    uint32_t period = MAX((uint32_t)corne_lighting_cfg.ambient_period_ms, 400U);
    uint8_t base = (uint8_t)(((uint64_t)(now % period) * 256U) / period);
    for (size_t i = 0; i < CORNE_LIGHTING_LED_COUNT; i++) {
        uint8_t pos = (uint8_t)(base + ((uint32_t)i * 256U) / CORNE_LIGHTING_LED_COUNT);
        corne_lighting_pixels[i] =
            corne_packed_to_rgb(corne_wheel_color(pos), corne_lighting_cfg.ambient_brightness,
                                255, 0);
    }
}

static void corne_render_ambient(int64_t now) {
    switch (corne_lighting_cfg.ambient_effect) {
    case CORNE_AMBIENT_BREATHING:
        corne_render_breathing(now);
        break;
    case CORNE_AMBIENT_COMET:
        corne_render_comet(now);
        break;
    case CORNE_AMBIENT_SPARKLE:
        corne_render_sparkle(now);
        break;
    case CORNE_AMBIENT_RAINBOW:
        corne_render_rainbow(now);
        break;
    case CORNE_AMBIENT_FIREFLY:
    default:
        corne_render_fireflies(now);
        break;
    }
}

static void corne_render_current_frame(void) {
    int64_t now = k_uptime_get();

    if (!corne_lighting_cfg.enabled || !device_is_ready(corne_lighting_strip)) {
        corne_clear_pixels();
    } else if (corne_lighting_cfg.bt_enabled && now < corne_bt_until &&
               corne_active_bt_profile < ARRAY_SIZE(corne_lighting_cfg.bt_colors)) {
        uint8_t level = 255;
        if (corne_lighting_cfg.bt_effect == 1) {
            level = corne_one_shot_pulse_level(now, corne_bt_started,
                                               corne_lighting_cfg.bt_duration_ms, 1);
        } else if (corne_lighting_cfg.bt_effect == 2) {
            level = corne_one_shot_pulse_level(now, corne_bt_started,
                                               corne_lighting_cfg.bt_duration_ms, 2);
        }
        corne_render_solid(corne_lighting_cfg.bt_colors[corne_active_bt_profile],
                           corne_lighting_cfg.bt_brightness, level);
    } else if (corne_lighting_cfg.layer_enabled && corne_lighting_cfg.layer_mode == 1 &&
               now < corne_layer_flash_until &&
               corne_active_layer < ARRAY_SIZE(corne_lighting_cfg.layer_colors)) {
        corne_render_solid(corne_lighting_cfg.layer_colors[corne_active_layer],
                           corne_lighting_cfg.layer_brightness, 255);
    } else if (corne_lighting_cfg.layer_enabled && corne_lighting_cfg.layer_mode == 0 &&
               corne_active_layer > 0 &&
               corne_active_layer < ARRAY_SIZE(corne_lighting_cfg.layer_colors)) {
        corne_render_solid(corne_lighting_cfg.layer_colors[corne_active_layer],
                           corne_lighting_cfg.layer_brightness, 255);
    } else {
        corne_render_ambient(now);
    }

    int rc = led_strip_update_rgb(corne_lighting_strip, corne_lighting_pixels,
                                  CORNE_LIGHTING_LED_COUNT);
    if (rc < 0) {
        LOG_WRN("Corne lighting strip update failed: %d", rc);
    }
}

static void corne_render_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    corne_render_current_frame();
    k_work_reschedule(&corne_render_work, K_MSEC(CORNE_LIGHTING_TICK_MS));
}

static void corne_lighting_set_defaults(void) {
    static const uint32_t layer_palette[] = {
        0x70FF70, 0x4080FF, 0xA050FF, 0xFF9D30, 0xFF50A8, 0x40DFFF,
    };

    corne_lighting_cfg = (struct corne_lighting_config){
        .enabled = true,
        .ambient_effect = CORNE_AMBIENT_FIREFLY,
        .ambient_color = 0xA8FF40,
        .ambient_brightness = 12,
        .ambient_period_ms = 2400,
        .firefly_count = 3,
        .firefly_interval_ms = 900,
        .firefly_fade_ms = 1600,
        .firefly_variation = 18,
        .layer_enabled = true,
        .layer_mode = 0,
        .layer_duration_ms = 500,
        .layer_brightness = 35,
        .bt_enabled = true,
        .bt_duration_ms = 800,
        .bt_effect = 1,
        .bt_brightness = 40,
        .bt_colors = {0x3090FF, 0x40E070, 0xFFD040, 0xA060FF, 0xFF5040},
    };

    for (size_t i = 0; i < ARRAY_SIZE(corne_lighting_cfg.layer_colors); i++) {
        corne_lighting_cfg.layer_colors[i] = layer_palette[i % ARRAY_SIZE(layer_palette)];
    }
}
