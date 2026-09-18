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

#define CORNE_LIGHTING_RELAY_PROTOCOL_VERSION 5U

#define CORNE_LIGHTING_RELAY_KIND_CONFIG 0U
#define CORNE_LIGHTING_RELAY_KIND_LAYER 1U
#define CORNE_LIGHTING_RELAY_KIND_BLUETOOTH 2U

enum corne_lighting_config_base_id {
    CORNE_CFG_ENABLED = 0,
    CORNE_CFG_AMBIENT_EFFECT,
    CORNE_CFG_AMBIENT_COLOR,
    CORNE_CFG_AMBIENT_BRIGHTNESS,
    CORNE_CFG_AMBIENT_PERIOD_MS,
    CORNE_CFG_FIREFLY_COUNT,
    CORNE_CFG_FIREFLY_INTERVAL_MS,
    CORNE_CFG_FIREFLY_FADE_MS,
    CORNE_CFG_FIREFLY_VARIATION,
    CORNE_CFG_REACTIVE_BASE_COLOR,
    CORNE_CFG_REACTIVE_RIPPLE_COLOR,
    CORNE_CFG_REACTIVE_TRAVEL_MS,
    CORNE_CFG_REACTIVE_WIDTH,
    CORNE_CFG_REACTIVE_FADE_MS,
    CORNE_CFG_LAYER_ENABLED,
    CORNE_CFG_LAYER_MODE,
    CORNE_CFG_LAYER_DURATION_MS,
    CORNE_CFG_LAYER_BRIGHTNESS,
};

#define CORNE_CFG_LAYER_COLOR_BASE 18U
#define CORNE_CFG_BT_ENABLED (CORNE_CFG_LAYER_COLOR_BASE + CORNE_LIGHTING_LAYER_COUNT)
#define CORNE_CFG_BT_DURATION_MS (CORNE_CFG_BT_ENABLED + 1U)
#define CORNE_CFG_BT_EFFECT (CORNE_CFG_BT_ENABLED + 2U)
#define CORNE_CFG_BT_BRIGHTNESS (CORNE_CFG_BT_ENABLED + 3U)
#define CORNE_CFG_BT_COLOR_BASE (CORNE_CFG_BT_ENABLED + 4U)
#define CORNE_CFG_COUNT (CORNE_CFG_BT_COLOR_BASE + 5U)

struct corne_lighting_relay {
    uint8_t source;
    uint8_t version;
    uint8_t kind;
    uint8_t id;
    uint32_t value;
} __packed;

static inline bool corne_lighting_id_is_layer_color(uint8_t id) {
    return id >= CORNE_CFG_LAYER_COLOR_BASE &&
           id < (CORNE_CFG_LAYER_COLOR_BASE + CORNE_LIGHTING_LAYER_COUNT);
}

static inline bool corne_lighting_id_is_bt_color(uint8_t id) {
    return id >= CORNE_CFG_BT_COLOR_BASE && id < (CORNE_CFG_BT_COLOR_BASE + 5U);
}

static inline uint32_t corne_lighting_config_value(uint8_t id) {
    if (corne_lighting_id_is_layer_color(id)) {
        return corne_lighting_cfg.layer_colors[id - CORNE_CFG_LAYER_COLOR_BASE];
    }
    if (corne_lighting_id_is_bt_color(id)) {
        return corne_lighting_cfg.bt_colors[id - CORNE_CFG_BT_COLOR_BASE];
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
    case CORNE_CFG_REACTIVE_BASE_COLOR:
        return corne_lighting_cfg.reactive_base_color;
    case CORNE_CFG_REACTIVE_RIPPLE_COLOR:
        return corne_lighting_cfg.reactive_ripple_color;
    case CORNE_CFG_REACTIVE_TRAVEL_MS:
        return corne_lighting_cfg.reactive_travel_ms;
    case CORNE_CFG_REACTIVE_WIDTH:
        return corne_lighting_cfg.reactive_width;
    case CORNE_CFG_REACTIVE_FADE_MS:
        return corne_lighting_cfg.reactive_fade_ms;
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

    if (corne_lighting_id_is_layer_color(id)) {
        corne_lighting_cfg.layer_colors[id - CORNE_CFG_LAYER_COLOR_BASE] = value;
        return false;
    }
    if (corne_lighting_id_is_bt_color(id)) {
        corne_lighting_cfg.bt_colors[id - CORNE_CFG_BT_COLOR_BASE] = value;
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
    case CORNE_CFG_REACTIVE_BASE_COLOR:
        corne_lighting_cfg.reactive_base_color = value;
        break;
    case CORNE_CFG_REACTIVE_RIPPLE_COLOR:
        corne_lighting_cfg.reactive_ripple_color = value;
        break;
    case CORNE_CFG_REACTIVE_TRAVEL_MS:
        corne_lighting_cfg.reactive_travel_ms = (uint16_t)value;
        break;
    case CORNE_CFG_REACTIVE_WIDTH:
        corne_lighting_cfg.reactive_width = (uint8_t)value;
        break;
    case CORNE_CFG_REACTIVE_FADE_MS:
        corne_lighting_cfg.reactive_fade_ms = (uint16_t)value;
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

static inline bool corne_lighting_parse_index(const char *key, const char *prefix,
                                               uint8_t limit, uint8_t *out) {
    size_t prefix_len = strlen(prefix);
    if (strncmp(key, prefix, prefix_len) != 0) {
        return false;
    }

    const char *p = key + prefix_len;
    if (*p == '\0') {
        return false;
    }

    uint16_t value = 0U;
    while (*p != '\0') {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = (uint16_t)(value * 10U + (uint16_t)(*p - '0'));
        if (value >= limit) {
            return false;
        }
        p++;
    }

    *out = (uint8_t)value;
    return true;
}

static inline uint8_t corne_lighting_config_id_from_key(const char *key) {
    if (!key) {
        return CORNE_CFG_COUNT;
    }

    uint8_t index;
    if (corne_lighting_parse_index(key, "layer_color_", CORNE_LIGHTING_LAYER_COUNT, &index)) {
        return (uint8_t)(CORNE_CFG_LAYER_COLOR_BASE + index);
    }
    if (corne_lighting_parse_index(key, "bt_color_", 5U, &index)) {
        return (uint8_t)(CORNE_CFG_BT_COLOR_BASE + index);
    }

    static const struct {
        const char *key;
        uint8_t id;
    } fixed_keys[] = {
        {"enabled", CORNE_CFG_ENABLED},
        {"ambient_effect", CORNE_CFG_AMBIENT_EFFECT},
        {"ambient_color", CORNE_CFG_AMBIENT_COLOR},
        {"ambient_brightness", CORNE_CFG_AMBIENT_BRIGHTNESS},
        {"ambient_period_ms", CORNE_CFG_AMBIENT_PERIOD_MS},
        {"firefly_count", CORNE_CFG_FIREFLY_COUNT},
        {"firefly_interval_ms", CORNE_CFG_FIREFLY_INTERVAL_MS},
        {"firefly_fade_ms", CORNE_CFG_FIREFLY_FADE_MS},
        {"firefly_variation", CORNE_CFG_FIREFLY_VARIATION},
        {"reactive_base_color", CORNE_CFG_REACTIVE_BASE_COLOR},
        {"reactive_ripple_color", CORNE_CFG_REACTIVE_RIPPLE_COLOR},
        {"reactive_travel_ms", CORNE_CFG_REACTIVE_TRAVEL_MS},
        {"reactive_width", CORNE_CFG_REACTIVE_WIDTH},
        {"reactive_fade_ms", CORNE_CFG_REACTIVE_FADE_MS},
        {"layer_enabled", CORNE_CFG_LAYER_ENABLED},
        {"layer_mode", CORNE_CFG_LAYER_MODE},
        {"layer_duration_ms", CORNE_CFG_LAYER_DURATION_MS},
        {"layer_brightness", CORNE_CFG_LAYER_BRIGHTNESS},
        {"bt_enabled", CORNE_CFG_BT_ENABLED},
        {"bt_duration_ms", CORNE_CFG_BT_DURATION_MS},
        {"bt_effect", CORNE_CFG_BT_EFFECT},
        {"bt_brightness", CORNE_CFG_BT_BRIGHTNESS},
    };

    for (size_t i = 0; i < ARRAY_SIZE(fixed_keys); i++) {
        if (strcmp(key, fixed_keys[i].key) == 0) {
            return fixed_keys[i].id;
        }
    }
    return CORNE_CFG_COUNT;
}
