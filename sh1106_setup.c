#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "sh1106.h"
#include "image.h"
#include "main.h"
#include "sh1106_setup.h"

sh1106_t disp;

void setup_sh1106(void) {
    i2c_init(I2C1_PORT, 400*1000);
    gpio_set_function(I2C1_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C1_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C1_SDA);
    gpio_pull_up(I2C1_SCL);
    disp.external_vcc=false;
    sh1106_init(&disp, 128, 64, 0x3C, I2C1_PORT);
    sh1106_clear(&disp);
    sh1106_show(&disp);

}