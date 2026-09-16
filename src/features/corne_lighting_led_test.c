/*
 * Corne v2 all-LED diagnostic for ZMK v0.3 + DYA.
 *
 * Corne v2 LED order:
 *   1..6   = rear underglow
 *   7..27  = front/per-key backlight
 *
 * This diagnostic lights the entire configured LED chain at once with a
 * low-brightness white. Using a conservative level keeps USB current modest
 * while making daisy-chain continuity easy to inspect: every LED that can
 * receive data should remain visibly lit.
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !DT_HAS_CHOSEN(zmk_underglow)
#error "CONFIG_ZMK_CORNE_LIGHTING_LED_TEST requires a zmk,underglow chosen LED strip"
#endif

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define LED_COUNT DT_PROP(STRIP_NODE, chain_length)
#define TEST_LEVEL 32U
#define START_DELAY_MS 800

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[LED_COUNT];
static struct k_work_delayable startup_work;

static void fill_all_leds(void) {
    const struct led_rgb white = {
        .r = TEST_LEVEL,
        .g = TEST_LEVEL,
        .b = TEST_LEVEL,
    };

    for (size_t i = 0; i < LED_COUNT; i++) {
        pixels[i] = white;
    }
}

static void startup_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (!device_is_ready(strip)) {
        LOG_ERR("Corne LED test strip is not ready");
        return;
    }

    fill_all_leds();
    int rc = led_strip_update_rgb(strip, pixels, LED_COUNT);
    if (rc < 0) {
        LOG_ERR("Corne LED test strip update failed: %d", rc);
    } else {
        LOG_INF("Corne LED test: %u LEDs lit at level %u", (unsigned int)LED_COUNT,
                (unsigned int)TEST_LEVEL);
    }
}

static int corne_lighting_led_test_init(void) {
    k_work_init_delayable(&startup_work, startup_handler);
    k_work_reschedule(&startup_work, K_MSEC(START_DELAY_MS));
    return 0;
}

SYS_INIT(corne_lighting_led_test_init, APPLICATION, 95);
