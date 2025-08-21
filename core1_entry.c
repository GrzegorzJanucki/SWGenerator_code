#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/binary_info.h"
#include "pico/util/queue.h"
#include "pico/sem.h"
#include <string.h>
#include <stdlib.h>

#include "ssd1306_setup.h"
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
#define FONT_SCALE 2
#define FONT_WIDTH 7
#define DISPLAY_X_OFFSET 5
#define DISPLAY_Y_OFFSET 32
#define UNDERLINE_Y_OFFSET 40
#define BOX_HEIGHT 12

#define ENCODER_A_PIN 3
#define ENCODER_B_PIN 27
#define BUTTON_PIN    2

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
    int digits[NUM_DIGITS] = {0, 0, 0, 0, 0, 0, 0, 0, 0}; // 9 digits int tabela[]={0,0,0,0,0,0,0,0,0};
    int selected_digit = 0;       // Which digit is selected (0-8)
    bool editing = false;         // Are we editing the digit value?
    int old_encoder = 0;
    int new_encoder, delta;
    uint32_t last_button_state = 0;

    // Helper buffer for display
    char digits_str[NUM_DIGITS + 1];

    // Initial display
    for (int i = 0; i < NUM_DIGITS; ++i)
    digits_str[i] = digits[i] + '0';
    digits_str[NUM_DIGITS] = '\0';
    ssd1306_clear(&disp);
    ssd1306_draw_string_with_font(&disp, 5, 35, 2, bubblesstandard_font, digits_str);
    ssd1306_show(&disp);

    int idx =0;
    while (1){
        new_encoder = quadrature_encoder_get_count();
        delta = new_encoder - old_encoder;
        old_encoder = new_encoder;

            if(delta !=0){
            int steps = delta / ENCODER_STEP_DIVISOR; 
                if (editing) {
                // Zapętlanie wartości cyfry z ograniczeniami zakresu
                    if (selected_digit == 0) {
                    // Pierwsza pozycja: zakres 0-1
                    digits[selected_digit] = (digits[selected_digit] + steps) % 2;
                        if (digits[selected_digit] < 0) digits[selected_digit] += 2;
                    }else {
                    // Pozostałe pozycje: zakres 0-9
                    digits[selected_digit] = (digits[selected_digit] + steps) % 10;
                        if (digits[selected_digit] < 0) digits[selected_digit] += 10;
                    }
            }else{
                selected_digit += steps;
                if (selected_digit < 0) selected_digit = 0;
                if (selected_digit > NUM_DIGITS - 1) selected_digit = NUM_DIGITS - 1;
            }
        }
        // Handle button
        uint32_t button_state = 0;
        bool result = button_get_state(&button_state);
        if (result) {
            if (!last_button_state && button_state) {
                editing = !editing;
                if (!editing) { // Save to EEPROM when exiting edit mode
                    for (int i = 0; i < NUM_DIGITS; ++i){
                        digits_str[i] = digits[i] + '0';
                    }
                        digits_str[NUM_DIGITS] = '\0';

                        char write_buffer[16];
                        strncpy(write_buffer, digits_str, sizeof(write_buffer) - 1);
                        write_buffer[sizeof(write_buffer) - 1] = '\0';
                        at24c256_write(mem_addr, (uint8_t *)write_buffer, strlen(write_buffer) + 1);

                        uint32_t new_freq = strtoul(digits_str, NULL, 10);
                        if (new_freq < 8000u) new_freq = 8000u;
                        if (new_freq > 160000000u) new_freq = 160000000u;
                        queue_entry_t msg = {
                        .msgId = 0,
                        .objId = TARGET_T,
                        .command = new_freq,
                        .dataPtr = &new_freq,
                        .dataLen = sizeof(new_freq)
                         };
                queue_add_blocking(&core1_to_core0_queue, &msg);
                printf("Sent frequency to core0: %lu Hz\n", new_freq);
                }
            }
            last_button_state = button_state;
        }
        
        // Prepare display string
        for (int i = 0; i < NUM_DIGITS; ++i)
            digits_str[i] = digits[i] + '0';
            digits_str[NUM_DIGITS] = '\0';

        //OLED update
        ssd1306_clear(&disp);
        ssd1306_draw_string_with_font(&disp, 5, 35, 2, bubblesstandard_font, digits_str);

        // Draw underline or box for selected digit
        int char_width = 7 * 2; // font width * scale (adjust if needed)
        int x = 5 + selected_digit * char_width;
        int y = 55;
        if (editing){
            // Draw a box around the digit
            ssd1306_draw_empty_square(&disp, x - 2, y - 20, char_width, 20);
        }else{
            // Draw underline
            ssd1306_draw_line(&disp, x, y, x + char_width - 5, y);
        }

        ssd1306_show(&disp);
        
        if(queue_try_remove(&core0_to_core1_queue, &msg)) {
            // handle messages if needed
        } 
        sleep_ms(50); // Add a small delay to avoid flicker 

    }
}
