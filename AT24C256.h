#ifndef AT24C256_H
#define AT24C256_H


bool at24c256_read(uint16_t mem_addr, uint8_t *data, uint8_t len);
bool at24c256_write(uint16_t mem_addr, uint8_t *data, uint8_t len);

#endif