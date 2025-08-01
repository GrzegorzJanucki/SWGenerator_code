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


#include "quadrature_encoder.pio.h"
#include "button.pio.h"
#include "ssd1306.h"
#include "ssd1306_setup.h"
#include "main.h"
#include "BMSPA_font.h"
#include "core1_entry.h"
#include "at24c256.h"



// UART defines
// By default the stdout UART is `uart0`, so we will use the second one
#define UART_ID uart1
#define BAUD_RATE 115200

// Use pins 4 and 5 for UART1
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define UART_TX_PIN 4
#define UART_RX_PIN 5

int main()
{ 
    stdio_init_all();

    queue_init(&core0_to_core1_queue, sizeof(queue_entry_t), 10);
    queue_init(&core1_to_core0_queue, sizeof(queue_entry_t), 10);

    // I2C Initialisation. Using it at 400Khz.
    i2c_init(I2C0_PORT, 100*1000);
    gpio_set_function(I2C0_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C0_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C0_SDA);
    gpio_pull_up(I2C0_SCL);

    // Set up our UART
    uart_init(UART_ID, BAUD_RATE);
    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    // Use some the various UART functions to send out data
    // In a default system, printf will also output via the default UART
    
    // Send out a string, with CR/LF conversions
    uart_puts(UART_ID, " Hello, UART!\n");

    setup();

    queue_entry_t msg = {.msgId = READY_FLAG, .objId = 0, .command = 0, .dataPtr = NULL, .dataLen = 0};
    queue_add_blocking(&core0_to_core1_queue, &msg);
    
    multicore_launch_core1(core1_entry);

    

    while (1) {
        tight_loop_contents(); // Keep Core 0 running
    }


    return 0;
}
