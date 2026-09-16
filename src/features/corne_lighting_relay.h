/*
 * Compact Corne lighting split-relay protocol.
 *
 * Keep every relay event small enough for the stock DYA split transport.
 * Runtime configuration is sent one setting at a time instead of copying the
 * whole lighting configuration into a single BLE relay packet.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define CORNE_LIGHTING_RELAY_PROTOCOL_VERSION 3U

#define CORNE_LIGHTING_RELAY_KIND_CONFIG 0U
#define CORNE_LIGHTING_RELAY_KIND_LAYER 1U
#define CORNE_LIGHTING_RELAY_KIND_BLUETOOTH 2U

enum corne_lighting_config_id {
    CORNE_CFG_ENABLED = 0,
    CORNE_CFG_AMBIENT_EFFECT,
    CORNE_CFG_AMBIENT_COLOR,
    CORNE_CFG_AMBIENT_BRIGHTNESS,
    CORNE_CFG_AMBIENT_PERIOD_MS,
    CORNE_CFG_FIREFLY_COUNT,
    CORNE_CFG_FIREFLY_INTERVAL_MS,
    CORNE_CFG_FIREFLY_FADE_MS,
    CORNE_CFG_FIREFLY_VARIATION,
    CORNE_CFG_LAYER_ENABLED,
    CORNE_CFG_LAYER_MODE,
    CORNE_CFG_LAYER_DURATION_MS,
    CORNE_CFG_LAYER_BRIGHTNESS,
    CORNE_CFG_LAYER_COLOR_0,
    CORNE_CFG_LAYER_COLOR_1,
    CORNE_CFG_LAYER_COLOR_2,
    CORNE_CFG_LAYER_COLOR_3,
    CORNE_CFG_LAYER_COLOR_4,
    CORNE_CFG_LAYER_COLOR_5,
    CORNE_CFG_BT_ENABLED,
    CORNE_CFG_BT_DURATION_MS,
    CORNE_CFG_BT_EFFECT,
    CORNE_CFG_BT_BRIGHTNESS,
    CORNE_CFG_BT_COLOR_0,
    CORNE_CFG_BT_COLOR_1,
    CORNE_CFG_BT_COLOR_2,
    CORNE_CFG_BT_COLOR_3,
    CORNE_CFG_BT_COLOR_4,
    CORNE_CFG_COUNT,
};

struct corne_lighting_relay {
    uint8_t source;
    uint8_t version;
    uint8_t kind;
    uint8_t id;
    uint32_t value;
} __packed;

static inline uint32_t corne_lighting_config_value(uint8_t id) {
    if (id >= CORNE_CFG_LAYER_COLOR_0 && id <= CORNE_CFG_LAYER_COLOR_5) {
        return corne_lighting_cfg.layer_colors[id - CORNE_CFG_LAYER_COLOR_0];
    }
    if (id >= CORNE_CFG_BT_COLOR_0 && id <= CORNE_CFG_BT_COLOR_4) {
        return corne_lighting_cfg.bt_colors[id - CORNE_CFG_BT_COLOR_0];
    }

    switch (id) {
    case CORNE_CFG_ENABLED:
        return corne_lighting_cfg.enabled;
    case CORNE_CFG_AMBIENT_EFFECT:
        return corne_lighting_cfg.ambient_effect;
    case CORNE_CFG_AMBIENT_COLOR:
        return corne_lighting_cfg.ambient_color;
    case CORNE_CFG_AMBIENT_BRIGHTNESS:
        return corne_lighting_cfg.ambient_brightness;
    case CORNE_CFG_AMBIENT_PERIOD_MS:
        return corne_lighting_cfg.ambient_period_ms;
    case CORNE_CFG_FIREFLY_COUNT:
        return corne_lighting_cfg.firefly_count;
    case CORNE_CFG_FIREFLY_INTERVAL_MS:
        return corne_lighting_cfg.firefly_interval_ms;
    case CORNE_CFG_FIREFLY_FADE_MS:
        return corne_lighting_cfg.firefly_fade_ms;
    case CORNE_CFG_FIREFLY_VARIATION:
        return corne_lighting_cfg.firefly_variation;
    case CORNE_CFG_LAYER_ENABLED:
        return corne_lighting_cfg.layer_enabled;
    case CORNE_CFG_LAYER_MODE:
        return corne_lighting_cfg.layer_mode;
    case CORNE_CFG_LAYER_DURATION_MS:
        return corne_lighting_cfg.layer_duration_ms;
    case CORNE_CFG_LAYER_BRIGHTNESS:
        return corne_lighting_cfg.layer_brightness;
    case CORNE_CFG_BT_ENABLED:
        return corne_lighting_cfg.bt_enabled;
    case CORNE_CFG_BT_DURATION_MS:
        return corne_lighting_cfg.bt_duration_ms;
    case CORNE_CFG_BT_EFFECT:
        return corne_lighting_cfg.bt_effect;
    case CORNE_CFG_BT_BRIGHTNESS:
        return corne_lighting_cfg.bt_brightness;
    default:
        return 0U;
    }
}

static inline bool corne_lighting_apply_config_value(uint8_t id, uint32_t value) {
    bool effect_changed = false;

    if (id >= CORNE_CFG_LAYER_COLOR_0 && id <= CORNE_CFG_LAYER_COLOR_5) {
        corne_lighting_cfg.layer_colors[id - CORNE_CFG_LAYER_COLOR_0] = value;
        return false;
    }
    if (id >= CORNE_CFG_BT_COLOR_0 && id <= CORNE_CFG_BT_COLOR_4) {
        corne_lighting_cfg.bt_colors[id - CORNE_CFG_BT_COLOR_0] = value;
        return false;
    }

    switch (id) {
    case CORNE_CFG_ENABLED:
        corne_lighting_cfg.enabled = value != 0U;
        break;
    case CORNE_CFG_AMBIENT_EFFECT:
        effect_changed = corne_lighting_cfg.ambient_effect != (uint8_t)value;
        corne_lighting_cfg.ambient_effect = (uint8_t)value;
        break;
    case CORNE_CFG_AMBIENT_COLOR:
        corne_lighting_cfg.ambient_color = value;
        break;
    case CORNE_CFG_AMBIENT_BRIGHTNESS:
        corne_lighting_cfg.ambient_brightness = (uint8_t)value;
        break;
    case CORNE_CFG_AMBIENT_PERIOD_MS:
        corne_lighting_cfg.ambient_period_ms = (uint16_t)value;
        break;
    case CORNE_CFG_FIREFLY_COUNT:
        corne_lighting_cfg.firefly_count = (uint8_t)value;
        break;
    case CORNE_CFG_FIREFLY_INTERVAL_MS:
        corne_lighting_cfg.firefly_interval_ms = (uint16_t)value;
        break;
    case CORNE_CFG_FIREFLY_FADE_MS:
        corne_lighting_cfg.firefly_fade_ms = (uint16_t)value;
        break;
    case CORNE_CFG_FIREFLY_VARIATION:
        corne_lighting_cfg.firefly_variation = (uint8_t)value;
        break;
    case CORNE_CFG_LAYER_ENABLED:
        corne_lighting_cfg.layer_enabled = value != 0U;
        break;
    case CORNE_CFG_LAYER_MODE:
        corne_lighting_cfg.layer_mode = (uint8_t)value;
        break;
    case CORNE_CFG_LAYER_DURATION_MS:
        corne_lighting_cfg.layer_duration_ms = (uint16_t)value;
        break;
    case CORNE_CFG_LAYER_BRIGHTNESS:
        corne_lighting_cfg.layer_brightness = (uint8_t)value;
        break;
    case CORNE_CFG_BT_ENABLED:
        corne_lighting_cfg.bt_enabled = value != 0U;
        break;
    case CORNE_CFG_BT_DURATION_MS:
        corne_lighting_cfg.bt_duration_ms = (uint16_t)value;
        break;
    case CORNE_CFG_BT_EFFECT:
        corne_lighting_cfg.bt_effect = (uint8_t)value;
        break;
    case CORNE_CFG_BT_BRIGHTNESS:
        corne_lighting_cfg.bt_brightness = (uint8_t)value;
        break;
    default:
        break;
    }

    return effect_changed;
}

static inline uint8_t corne_lighting_config_id_from_key(const char *key) {
    static const char *const keys[CORNE_CFG_COUNT] = {
        [CORNE_CFG_ENABLED] = "enabled",
        [CORNE_CFG_AMBIENT_EFFECT] = "ambient_effect",
        [CORNE_CFG_AMBIENT_COLOR] = "ambient_color",
        [CORNE_CFG_AMBIENT_BRIGHTNESS] = "ambient_brightness",
        [CORNE_CFG_AMBIENT_PERIOD_MS] = "ambient_period_ms",
        [CORNE_CFG_FIREFLY_COUNT] = "firefly_count",
        [CORNE_CFG_FIREFLY_INTERVAL_MS] = "firefly_interval_ms",
        [CORNE_CFG_FIREFLY_FADE_MS] = "firefly_fade_ms",
        [CORNE_CFG_FIREFLY_VARIATION] = "firefly_variation",
        [CORNE_CFG_LAYER_ENABLED] = "layer_enabled",
        [CORNE_CFG_LAYER_MODE] = "layer_mode",
        [CORNE_CFG_LAYER_DURATION_MS] = "layer_duration_ms",
        [CORNE_CFG_LAYER_BRIGHTNESS] = "layer_brightness",
        [CORNE_CFG_LAYER_COLOR_0] = "layer_color_0",
        [CORNE_CFG_LAYER_COLOR_1] = "layer_color_1",
        [CORNE_CFG_LAYER_COLOR_2] = "layer_color_2",
        [CORNE_CFG_LAYER_COLOR_3] = "layer_color_3",
        [CORNE_CFG_LAYER_COLOR_4] = "layer_color_4",
        [CORNE_CFG_LAYER_COLOR_5] = "layer_color_5",
        [CORNE_CFG_BT_ENABLED] = "bt_enabled",
        [CORNE_CFG_BT_DURATION_MS] = "bt_duration_ms",
        [CORNE_CFG_BT_EFFECT] = "bt_effect",
        [CORNE_CFG_BT_BRIGHTNESS] = "bt_brightness",
        [CORNE_CFG_BT_COLOR_0] = "bt_color_0",
        [CORNE_CFG_BT_COLOR_1] = "bt_color_1",
        [CORNE_CFG_BT_COLOR_2] = "bt_color_2",
        [CORNE_CFG_BT_COLOR_3] = "bt_color_3",
        [CORNE_CFG_BT_COLOR_4] = "bt_color_4",
    };

    if (!key) {
        return CORNE_CFG_COUNT;
    }
    for (uint8_t i = 0; i < CORNE_CFG_COUNT; i++) {
        if (strcmp(key, keys[i]) == 0) {
            return i;
        }
    }
    return CORNE_CFG_COUNT;
}
