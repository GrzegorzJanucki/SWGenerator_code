#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include <math.h>
#include "pico/binary_info.h"
#include "pico/util/queue.h"
#include "pico/sem.h"
#include <string.h>
#include <stdlib.h>

#include "sh1106.h"
#include "sh1106_setup.h"
#include "main.h"
#include "BMSPA_font.h"
#include "core1_entry.h"
#include "at24c256.h"
#include "core1_entry.h"
#include "Si5351.h"

#define UART_ID uart1
#define BAUD_RATE 115200

#define UART_TX_PIN 4
#define UART_RX_PIN 5

int main()
{ 
    stdio_init_all();

    queue_init(&core0_to_core1_queue, sizeof(queue_entry_t), 10);
    queue_init(&core1_to_core0_queue, sizeof(queue_entry_t), 10);

    // I2C Initialisation. Using it at 400Khz.
    i2c_init(I2C0_PORT, 400*1000);
    gpio_set_function(I2C0_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C0_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C0_SDA);
    gpio_pull_up(I2C0_SCL);

    // Set up our UART
    uart_init(UART_ID, BAUD_RATE);
    // Set the TX and RX pins by using the function select on the GPIO
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    setup_sh1106();
    si5351_init(); 


    queue_entry_t msg = {.msgId = READY_FLAG, .objId = 0, .command = 0, .dataPtr = NULL, .dataLen = 0};
    queue_add_blocking(&core0_to_core1_queue, &msg);

    multicore_launch_core1(core1_entry);

    int digits1[NUM_DIGITS] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        // Load frequency from EEPROM
    const char read_buffer[16] = {0};
    if (at24c256_read(mem_addr, (uint8_t *)read_buffer, 10)) {
        for (int i = 0; i < NUM_DIGITS; i++) {
            if (read_buffer[i] >= '0' && read_buffer[i] <= '9') {
                digits1[i] = read_buffer[i] - '0';
            }else{
                break; // Invalid data, keep default
            }
        }
    }
    
    uint32_t freq_hz = strtoul(read_buffer, NULL, 10);
    si5351_clk0_set(freq_hz);
    
    while (1) {
        queue_entry_t msg;
        if (queue_try_remove(&core1_to_core0_queue, &msg)) {
            if (msg.objId == TARGET_T) {
                uint32_t new_freq = *(uint32_t *)msg.dataPtr;
                si5351_clk0_set(new_freq);
                printf("Nowa częstotliwość CLK0: %u Hz\n", si5351_clk0_get_hz());
            } else if (msg.objId == TARGET_T + 1) {
                uint32_t new_freq1 = *(uint32_t *)msg.dataPtr;
                si5351_clk1_set(new_freq1);
                si5351_clk2_set(new_freq1);
                printf("Nowa częstotliwość CLK1: %u Hz\n", si5351_clk1_get_hz());
                printf("Nowa częstotliwość CLK2: %u Hz\n", si5351_clk2_get_real_hz());
            }
        }
        sleep_ms(100); // Krótkie opóźnienie
    }
    return 0;
}
