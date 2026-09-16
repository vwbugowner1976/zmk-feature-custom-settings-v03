/*
 * Corne v2 front-LED diagnostic for ZMK v0.3 + DYA.
 *
 * Corne v2 LED order:
 *   1..6   = rear underglow
 *   7..27  = front/per-key backlight
 *
 * This diagnostic keeps LEDs 1..6 off and walks LEDs 7..27 one at a time.
 * Each LED is shown red, then green, then blue so all three channels can be
 * checked as well as daisy-chain continuity.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
#define FRONT_FIRST_INDEX 6U /* zero-based index: LED 7 */
#define TEST_STEP_MS 350
#define START_DELAY_MS 800

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[LED_COUNT];
static struct k_work_delayable test_work;
static struct k_work_delayable startup_work;
static size_t current_index = FRONT_FIRST_INDEX;
static uint8_t channel;

static void clear_pixels(void) { memset(pixels, 0, sizeof(pixels)); }

static void set_test_color(struct led_rgb *pixel, uint8_t ch) {
    *pixel = (struct led_rgb){0};
    switch (ch) {
    case 0:
        pixel->r = 255;
        break;
    case 1:
        pixel->g = 255;
        break;
    default:
        pixel->b = 255;
        break;
    }
}

static void test_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    clear_pixels();

    if (LED_COUNT > FRONT_FIRST_INDEX) {
        set_test_color(&pixels[current_index], channel);
    }

    int rc = led_strip_update_rgb(strip, pixels, LED_COUNT);
    if (rc < 0) {
        LOG_WRN("Corne LED test strip update failed: %d", rc);
    }

    channel++;
    if (channel >= 3U) {
        channel = 0U;
        current_index++;
        if (current_index >= LED_COUNT) {
            current_index = FRONT_FIRST_INDEX;
        }
    }

    k_work_reschedule(&test_work, K_MSEC(TEST_STEP_MS));
}

static void startup_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (!device_is_ready(strip)) {
        LOG_ERR("Corne LED test strip is not ready");
        return;
    }

    clear_pixels();
    (void)led_strip_update_rgb(strip, pixels, LED_COUNT);
    k_work_reschedule(&test_work, K_NO_WAIT);
}

static int corne_lighting_led_test_init(void) {
    k_work_init_delayable(&test_work, test_work_handler);
    k_work_init_delayable(&startup_work, startup_handler);
    k_work_reschedule(&startup_work, K_MSEC(START_DELAY_MS));
    return 0;
}

SYS_INIT(corne_lighting_led_test_init, APPLICATION, 95);
