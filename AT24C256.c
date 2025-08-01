#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "main.h"

// Write to AT24C256
bool at24c256_write(uint16_t mem_addr, uint8_t *data, uint8_t len) {
    // Prepare buffer: 2-byte memory address + data
    uint8_t buffer[2 + len];
    buffer[0] = (mem_addr >> 8) & 0xFF; // High byte
    buffer[1] = mem_addr & 0xFF;        // Low byte
    memcpy(&buffer[2], data, len);

    // Write to EEPROM
    int result = i2c_write_blocking(I2C0_PORT, AT24C256_ADDR, buffer, 2 + len, false);
    if (result != (2 + len)) {
        printf("Write failed: %d\n", result);
        return false;
    }

    // Poll for write completion (max 5 ms)
    sleep_ms(1);
    for (int i = 0; i < 50; i++) {
        uint8_t dummy = 0;
        result = i2c_write_blocking(I2C0_PORT, AT24C256_ADDR, &dummy, 1, true);
        if (result == 1) {
            return true;
        }
        sleep_ms(1);
    }
    printf("Write timeout\n");
    return false;
}

// Read from AT24C256
bool at24c256_read(uint16_t mem_addr, uint8_t *data, uint8_t len) {
    // Write memory address
    uint8_t addr_buf[2] = {(mem_addr >> 8) & 0xFF, mem_addr & 0xFF};
    int result = i2c_write_blocking(I2C0_PORT, AT24C256_ADDR, addr_buf, 2, true);
    if (result != 2) {
        printf("Address write failed: %d\n", result);
        return false;
    }

    // Read data
    result = i2c_read_blocking(I2C0_PORT, AT24C256_ADDR, data, len, false);
    if (result != len) {
        printf("Read failed: %d\n", result);
        return false;
    }
    return true;
}