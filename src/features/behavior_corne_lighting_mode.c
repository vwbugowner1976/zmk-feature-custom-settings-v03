/*
 * Key behaviors for cycling Corne Lighting ambient modes.
 *
 * Mode changes are staged in RAM (Custom Settings MEMORY mode) to avoid a
 * flash write every time the user taps the cycle key. The existing lighting
 * custom-setting listener applies the change immediately and mirrors it to
 * the split peripheral. MyKeebStudio can persist the selected mode later.
 */

#define DT_DRV_COMPAT cormoran_behavior_corne_lighting_mode

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include "corne_lighting_modes.h"

#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
#include <cormoran/zmk/custom_settings.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define LIGHTING_SUBSYSTEM "corne_lighting"
#define LIGHTING_EFFECT_KEY "ambient_effect"

struct behavior_corne_lighting_mode_config {
    int8_t delta;
};

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);

#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    if (!dev) {
        return -ENODEV;
    }

    const struct behavior_corne_lighting_mode_config *cfg = dev->config;
    struct zmk_custom_setting_value current;
    int rc = zmk_custom_setting_read_by_key(LIGHTING_SUBSYSTEM, LIGHTING_EFFECT_KEY, &current);
    if (rc < 0) {
        LOG_WRN("Failed to read Corne Lighting ambient mode: %d", rc);
        return rc;
    }
    if (current.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32) {
        return -EINVAL;
    }

    int32_t next = current.int32_value + cfg->delta;
    if (next < 0) {
        next = CORNE_AMBIENT_MODE_COUNT - 1;
    } else if (next >= CORNE_AMBIENT_MODE_COUNT) {
        next = 0;
    }

    struct zmk_custom_setting_value value = ZMK_CUSTOM_SETTING_VALUE_INT32(next);
    rc = zmk_custom_setting_write_by_key(LIGHTING_SUBSYSTEM, LIGHTING_EFFECT_KEY, &value,
                                         ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (rc < 0) {
        LOG_WRN("Failed to change Corne Lighting ambient mode: %d", rc);
        return rc;
    }

    LOG_DBG("Corne Lighting ambient mode %d -> %d", current.int32_value, next);
#else
    ARG_UNUSED(binding);
#endif

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_corne_lighting_mode_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define CORNE_LIGHT_MODE_INST(n)                                                                   \
    static const struct behavior_corne_lighting_mode_config behavior_corne_light_mode_config_##n = { \
        .delta = DT_ENUM_IDX(DT_DRV_INST(n), direction) == 0 ? 1 : -1,                            \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_corne_light_mode_config_##n,            \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                       \
                            &behavior_corne_lighting_mode_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CORNE_LIGHT_MODE_INST)

#endif
