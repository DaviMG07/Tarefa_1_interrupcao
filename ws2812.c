#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

#define IS_RGBW   false
#define PIXELS    25
#define MATRIX    7
#define RED       13
#define BUTTON_A  6
#define BUTTON_B  5

typedef uint32_t Color;

bool LED_BUFFER[PIXELS] = {0};

uint8_t LED_REFERENCE[5][5] = {
    {24, 23, 22, 21, 20},
    {15, 16, 17, 18, 19},
    {14, 13, 12, 11, 10},
    {5,  6,  7,  8,  9},
    {4,  3,  2,  1,  0}
};

const uint8_t full     = 0b11111;
const uint8_t right    = 0b00001;
const uint8_t left     = 0b10000;
const uint8_t border   = 0b10001;
const uint8_t center_1 = 0b01110;
const uint8_t center_2 = 0b00100;

const uint8_t numbers[10][5] = {
    {full,    border, border, border, full},
    {center_2, 3 << 2, center_2, center_2, center_1},
    {center_1, (right << 1) | left, center_2, right << 3, full},
    {full,    right,  0b00111, right,  full},
    {border,  border, full,    right,  right},
    {full,    left,   full,    right,  full},
    {full,    left,   full,    border, full},
    {full,    right,  right,   right,  right},
    {full,    border, full,    border, full},
    {full,    border, full,    right,  right}
};

int count = 0;
uint32_t last_time = 0;

static inline void put_pixel(Color color) {
    pio_sm_put_blocking(pio0, 0, color << 8u);
}

void frame_to_led_buffer(const uint8_t frame[5]) {
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 5; col++) {
            LED_BUFFER[LED_REFERENCE[row][col]] = (frame[row] & (1 << (4 - col))) != 0;
        }
    }
}

static void set_led(Color color) {
    for (int i = 0; i < PIXELS; ++i) {
        put_pixel(LED_BUFFER[i] ? color : 0);
    }
}

static void show_frame(const uint8_t* frame, Color color) {
    frame_to_led_buffer(frame);
    set_led(color);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 8) | ((uint32_t)g << 16) | (uint32_t)b;
}

static void clear(void) {
    uint8_t frame[5] = {0};
    frame_to_led_buffer(frame);
    set_led(urgb_u32(0, 0, 0));
}

static void setup(void) {
    uint offset = pio_add_program(pio0, &ws2812_program);
    ws2812_program_init(pio0, 0, offset, MATRIX, 800000, IS_RGBW);
    uint inputs[] = {BUTTON_A, BUTTON_B};
    uint outputs[] = {RED};
    for (int i = 0; i < 2; ++i) {
        gpio_init(inputs[i]);
        gpio_set_dir(inputs[i], GPIO_IN);
        gpio_pull_up(inputs[i]);
    }
    for (int i = 0; i < 1; ++i) {
        gpio_init(outputs[i]);
        gpio_set_dir(outputs[i], GPIO_OUT);
        gpio_put(outputs[i], false);
    }
}

static void blink(uint pin, uint32_t period) {
    gpio_put(pin, true);
    sleep_ms(period);
    gpio_put(pin, false);
    sleep_ms(period);
}

static void buttons_handler(uint gpio, uint32_t events) {
    uint32_t now = to_us_since_boot(get_absolute_time());
    if (now - last_time > 200000) {
        last_time = now;
        if (gpio_get(BUTTON_A)) {
            count = (count == 9) ? 0 : count + 1;
        } else if (gpio_get(BUTTON_B)) {
            count = (count == 0) ? 9 : count - 1;
        }
        clear();
    }
}

int main(void) {
    setup();
    const Color white = urgb_u32(10, 10, 10);
    while (1) {
        show_frame(numbers[count], white);
        blink(RED, 200);
        gpio_set_irq_enabled_with_callback(BUTTON_A, GPIO_IRQ_EDGE_FALL, true, &buttons_handler);
        gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &buttons_handler);
    }
    return 0;
}
