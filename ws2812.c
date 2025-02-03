/*
 * Autor: Davi
 * Projeto baseado no código do Professor Wilton Lacerdo, apresentado na aula do dia 27.
 * Original: https://github.com/wiltonlacerda/EmbarcaTechU4C4/tree/main/06_ws2812_Escolha
 */

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

// Define configurações e constantes gerais utilizadas no projeto.
// IS_RGBW: Define se os LEDs possuem canal branco; PIXELS: quantidade de LEDs; MATRIX: pino de saída do PIO;
// RED: pino de LED indicador; BUTTON_A e BUTTON_B: pinos dos botões.
#define IS_RGBW   false
#define PIXELS    25
#define MATRIX    7
#define RED       13
#define BUTTON_A  6
#define BUTTON_B  5

typedef uint32_t Color;

// Buffer que representa o estado de cada LED
bool LED_BUFFER[PIXELS] = {0};

// Constantes utilizadas para definir os padrões dos dígitos a serem exibidos
const uint8_t full     = 0b11111;
const uint8_t right    = 0b00001;
const uint8_t left     = 0b10000;
const uint8_t border   = 0b10001;
const uint8_t center_1 = 0b01110;
const uint8_t center_2 = 0b00100;

// Padrões de exibição dos dígitos de 0 a 9 em uma matriz 5x5 (as colunas são representadas pelos primeiros 5 bits dos numeros)
const uint8_t numbers[10][5] = {
    {full,    border, border, border, full}, // 0
    {center_2, 3 << 2, center_2, center_2, center_1}, // 1
    {center_1, (right << 1) | left, center_2, right << 3, full}, // 2 
    {full,    right,  0b00111, right,  full}, // 3
    {border,  border, full,    right,  right}, // 4
    {full,    left,   full,    right,  full}, // 5
    {full,    left,   full,    border, full}, // 6
    {full,    right,  right,   right,  right}, // 7
    {full,    border, full,    border, full}, // 8
    {full,    border, full,    right,  right} // 9
};

// Variáveis globais para controle da lógica dos botões e tempo
int count = 0;
uint32_t last_time = 0;

/* 
 * Envia a cor de um pixel para o PIO que controla os LEDs.
 * Realiza o deslocamento necessário para adequar o formato do dado.
 */
static inline void put_pixel(Color color) {
    pio_sm_put_blocking(pio0, 0, color << 8u);
}

/* 
 * Converte um frame (definido por uma matriz de 5 bytes) para o buffer dos LEDs.
 * Utiliza o mapeamento definido em LED_REFERENCE para posicionar os bits corretamente.
 */
void frame_to_led_buffer(const uint8_t frame[5]) {
    // Mapeamento dos LEDs para formar uma matriz 5x5, definindo a ordem física dos LEDs
    const uint8_t LED_REFERENCE[5][5] = {
        {24, 23, 22, 21, 20},
        {15, 16, 17, 18, 19},
        {14, 13, 12, 11, 10},
        {5,  6,  7,  8,  9},
        {4,  3,  2,  1,  0}
    };

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 5; col++) {
            LED_BUFFER[LED_REFERENCE[row][col]] = (frame[row] & (1 << (4 - col))) != 0;
        }
    }
}

/* 
 * Atualiza todos os LEDs conforme o estado do LED_BUFFER.
 * Acende o LED com a cor especificada se o bit correspondente estiver ativo.
 */
static void set_led(Color color) {
    for (int i = 0; i < PIXELS; ++i) {
        put_pixel(LED_BUFFER[i] ? color : 0);
    }
}

/* 
 * Exibe um frame na matriz de LEDs.
 * Converte o frame para o buffer e em seguida atualiza os LEDs com a cor informada.
 */
static void show_frame(const uint8_t* frame, Color color) {
    frame_to_led_buffer(frame);
    set_led(color);
}

/* 
 * Constrói um valor de 32 bits representando a cor a partir dos componentes R, G e B.
 */
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 8) | ((uint32_t)g << 16) | (uint32_t)b;
}

/* 
 * Limpa a matriz de LEDs.
 * Cria um frame vazio e atualiza o buffer, apagando todos os LEDs.
 */
static void clear(void) {
    uint8_t frame[5] = {0};
    frame_to_led_buffer(frame);
    set_led(urgb_u32(0, 0, 0));
}

/* 
 * Inicializa o sistema:
 * - Configura o PIO com o programa ws2812 para controle dos LEDs.
 * - Inicializa os pinos dos botões como entrada com pull-up.
 * - Configura o pino do LED indicador como saída.
 */
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

/* 
 * Gera um efeito de piscar no pino indicado.
 * Acende e apaga o pino com intervalo definido pelo parâmetro period.
 */
static void blink(uint pin, uint32_t period) {
    gpio_put(pin, true);
    sleep_ms(period);
    gpio_put(pin, false);
    sleep_ms(period);
}

/* 
 * Função de callback para o tratamento dos botões.
 * Debounce de 200ms é aplicado. Ao detectar um evento, atualiza a variável 'count'
 * para alternar entre os dígitos (incrementa se BUTTON_A for pressionado, decrementa se BUTTON_B).
 * Em seguida, limpa a matriz de LEDs.
 */
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

/* 
 * Função principal:
 * - Inicializa o sistema.
 * - Entra em loop infinito exibindo o dígito atual (definido pela variável 'count')
 *   na matriz de LEDs com cor branca.
 * - Pisca o LED indicador e habilita as interrupções dos botões a cada iteração.
 */
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
