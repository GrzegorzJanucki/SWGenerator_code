#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/binary_info.h"
#include "pico/util/queue.h"
#include "pico/sem.h"
#include <string.h>
#include <stdlib.h>

#include "font.h"
#include "sh1106.h"
#include "sh1106_setup.h"
#include "bubblesstandard_font.h"
#include "quadrature_encoder.pio.h"
#include "button.pio.h"
#include "AT24C256.h"

#define READY_FLAG 234
#define TARGET_T   101
#define CLICK      102
#define LONG_CLICK 103
#define NUM_DIGITS 9
#define ENCODER_STEP_DIVISOR 4
#define DEBOUNCE_US 10000
#define BUTTON_DEBOUNCE_US 20000
#define DISPLAY_X_OFFSET 5
#define DISPLAY_Y_OFFSET 32
#define UNDERLINE_Y_OFFSET 40
#define BOX_HEIGHT 12

#define ENCODER_A_PIN 3
#define ENCODER_B_PIN 27
#define BUTTON_PIN    2

#define FONT_WIDTH 7     // bazowa szerokość znaku
#define FONT_HEIGHT 8   // bazowa wysokość (musisz znać z fontu)
#define FONT_SCALE 1     // tu zmieniasz rozmiar

uint target = 9;

uint8_t read_data[32] = {0};
uint16_t mem_addr = 0x0100; // Arbitrary address

queue_t core0_to_core1_queue;
queue_t core1_to_core0_queue;

bool oled_timer_callback(repeating_timer_t *rt);
repeating_timer_t oled_timer;

typedef struct
{
    uint8_t msgId; // 234 - READY_FLAG
    uint8_t objId;
    int32_t command;
    void *dataPtr;
    uint16_t dataLen;
} queue_entry_t;

void encoder_button_setup() {
    // --- Encoder ---
    PIO enc_pio = pio0;
    uint enc_offset = pio_add_program(enc_pio, &quadrature_encoder_program);
    quadrature_encoder_program_init(enc_pio, ENCODER_A_PIN, 10); // max_step_rate=0 for max speed

    // --- Button ---
    PIO btn_pio = pio1;
    uint btn_offset = pio_add_program(btn_pio, &button_program);
    button_init(btn_pio, btn_offset, BUTTON_PIN);
}

void core1_entry() {
    encoder_button_setup();

    queue_entry_t response;
    queue_remove_blocking(&core0_to_core1_queue, &response);
    if (response.msgId != READY_FLAG) {
        printf("Core1: Unexpected initial message ID %d\n", response.msgId);
    }

    queue_entry_t msg;

    // --- dwa zestawy cyfr ---
    int digits1[NUM_DIGITS] = {0};
    int digits2[NUM_DIGITS] = {0};

    int selected_digit = 0;  
    bool editing = false;      
    int active_field = 0; // 0 = górny (digits1), 1 = dolny (digits2)

    int old_encoder = 0;
    int new_encoder, delta;

    uint32_t last_button_state = 0;
    absolute_time_t button_press_time = nil_time;

    // buffery stringów
    char digits_str1[NUM_DIGITS + 1];
    char digits_str2[NUM_DIGITS + 1];

    // --- initial display ---
    for (int i = 0; i < NUM_DIGITS; ++i) {
        digits_str1[i] = digits1[i] + '0';
        digits_str2[i] = digits2[i] + '0';
    }
    digits_str1[NUM_DIGITS] = '\0';
    digits_str2[NUM_DIGITS] = '\0';

    sh1106_clear(&disp);
    sh1106_draw_string_with_font(&disp, DISPLAY_X_OFFSET, 10, FONT_SCALE, bubblesstandard_font, digits_str1);
    sh1106_draw_string_with_font(&disp, DISPLAY_X_OFFSET, 40, FONT_SCALE, bubblesstandard_font, digits_str2);
    sh1106_show(&disp);

    while (1) {
        // --- ENKODER ---
        new_encoder = quadrature_encoder_get_count();
        delta = new_encoder - old_encoder;
        old_encoder = new_encoder;

        int *digits = (active_field == 0) ? digits1 : digits2;

        if (delta != 0) {
            int steps = delta / ENCODER_STEP_DIVISOR; 
            if (editing) {
                if (selected_digit == 0) {
                    digits[selected_digit] = (digits[selected_digit] + steps) % 2;
                    if (digits[selected_digit] < 0) digits[selected_digit] += 2;
                } else {
                    digits[selected_digit] = (digits[selected_digit] + steps) % 10;
                    if (digits[selected_digit] < 0) digits[selected_digit] += 10;
                }
            } else {
                selected_digit += steps;
                if (selected_digit < 0) selected_digit = 0;
                if (selected_digit > NUM_DIGITS - 1) selected_digit = NUM_DIGITS - 1;
            }
        }

        // --- PRZYCISK ---
    uint32_t button_state = 0;
    bool result = button_get_state(&button_state);
    if (result) {
        if (!last_button_state && button_state) {
            // wciśnięty
            button_press_time = get_absolute_time();
        }
        if (last_button_state && !button_state) {
            // puszczony
            int64_t held_us = absolute_time_diff_us(button_press_time, get_absolute_time());
            if (held_us > 1000000) {
                // długi klik → zmiana pola
                active_field = !active_field;
                printf("Switched to field %d\n", active_field);
            } else {
                // krótki klik → edycja on/off
                editing = !editing;
                if (!editing) {
                    // zapis częstotliwości z aktywnego pola po wyjściu z edycji
                    char digits_str[NUM_DIGITS + 1];
                    for (int i = 0; i < NUM_DIGITS; ++i) {
                        digits_str[i] = digits[i] + '0';
                    }
                    digits_str[NUM_DIGITS] = '\0';
                    at24c256_write(mem_addr, (uint8_t *)digits_str, NUM_DIGITS);

                    uint32_t new_freq = strtoul(digits_str, NULL, 10);
                    if (new_freq < 8000u) new_freq = 8000u;
                    if (new_freq > 160000000u) new_freq = 160000000u;

                    queue_entry_t msg = {
                        .msgId = 0,
                        .objId = (active_field == 0) ? TARGET_T : (TARGET_T + 1), // rozróżnij pole
                        .command = new_freq,
                        .dataPtr = &new_freq,
                        .dataLen = sizeof(new_freq)
                    };
                    queue_add_blocking(&core1_to_core0_queue, &msg);
                    printf("Sent Freq (field %d): %lu Hz\n", active_field, new_freq);
                }
            }
        }
        last_button_state = button_state;
    }
        // --- OLED update ---
        for (int i = 0; i < NUM_DIGITS; ++i) {
            digits_str1[i] = digits1[i] + '0';
            digits_str2[i] = digits2[i] + '0';
        }
        digits_str1[NUM_DIGITS] = '\0';
        digits_str2[NUM_DIGITS] = '\0';

        sh1106_clear(&disp);
        sh1106_draw_string_with_font(&disp, DISPLAY_X_OFFSET, 10, FONT_SCALE, bubblesstandard_font, digits_str1);
        sh1106_draw_string_with_font(&disp, DISPLAY_X_OFFSET, 40, FONT_SCALE, bubblesstandard_font, digits_str2);

        // obliczenia do highlightu
        int char_width  = FONT_WIDTH * FONT_SCALE;
        int char_height = FONT_HEIGHT * FONT_SCALE;

        int x = DISPLAY_X_OFFSET + selected_digit * char_width;
        int y_text = (active_field == 0) ? 10 : 40;   // Y początek tekstu
        int y_underline = y_text + char_height;       // pod linią tekstu

        if (editing) {
            sh1106_draw_empty_square(&disp, x - 2, y_text - 2, char_width + 4, char_height + 4);
        } else {
            sh1106_draw_line(&disp, x, y_underline, x + char_width - 2, y_underline);
        }

        sh1106_show(&disp);

        // odbiór wiadomości (opcjonalnie)
        if (queue_try_remove(&core0_to_core1_queue, &msg)) {
            // obsługa wiadomości z core0 → core1 jeśli potrzebne
        }

        sleep_ms(50);
    }
}

