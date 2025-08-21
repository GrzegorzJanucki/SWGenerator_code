#ifndef Si5351_H
#define Si5351_H

void si5351_init(void);
bool si5351_clk0_set(uint32_t fout_hz);
uint32_t si5351_clk0_get_hz(void);

#endif