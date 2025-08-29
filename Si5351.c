#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#ifndef SI5351_I2C_ADDR
#define SI5351_I2C_ADDR 0x60
#endif

/* Rejestry SI5351 (z AN619) */
#define REG_OE_CTRL            3
#define REG_CLK0_CTRL          16
#define REG_CLK1_CTRL          17
#define REG_CLK2_CTRL          18
#define REG_MSNA_P3_15_8       26
#define REG_MSNA_P3_7_0        27
#define REG_MSNA_P1_17_16      28
#define REG_MSNA_P1_15_8       29
#define REG_MSNA_P1_7_0        30
#define REG_MSNA_P3_19_16_P2_19_16 31
#define REG_MSNA_P2_15_8       32
#define REG_MSNA_P2_7_0        33
#define REG_MS0_P3_15_8        42
#define REG_MS0_P3_7_0         43
#define REG_MS0_P1_MISC        44
#define REG_MS0_P1_15_8        45
#define REG_MS0_P1_7_0         46
#define REG_MS0_P3_19_16_P2_19_16 47
#define REG_MS0_P2_15_8        48
#define REG_MS0_P2_7_0         49
#define REG_MS1_P3_15_8        50
#define REG_MS1_P3_7_0         51
#define REG_MS1_P1_MISC        52
#define REG_MS1_P1_15_8        53
#define REG_MS1_P1_7_0         54
#define REG_MS1_P3_19_16_P2_19_16 55
#define REG_MS1_P2_15_8        56
#define REG_MS1_P2_7_0         57
#define REG_MS2_P3_15_8        58
#define REG_MS2_P3_7_0         59
#define REG_MS2_P1_MISC        60
#define REG_MS2_P1_15_8        61
#define REG_MS2_P1_7_0         62
#define REG_MS2_P3_19_16_P2_19_16 63
#define REG_MS2_P2_15_8        64
#define REG_MS2_P2_7_0         65
#define REG_MSNB_P3_15_8       90
#define REG_MSNB_P3_7_0        91
#define REG_MSNB_P1_17_16      92
#define REG_MSNB_P1_15_8       93
#define REG_MSNB_P1_7_0        94
#define REG_MSNB_P3_19_16_P2_19_16 95
#define REG_MSNB_P2_15_8       96
#define REG_MSNB_P2_7_0        97
#define REG_PLL_RESET          177

/* Bity w CLKx_CONTROL */
#define CLKx_PDN               (1u << 7)
#define CLKx_INT               (1u << 6)
#define CLKx_SRC_PLLB          (1u << 5)
#define CLKx_INV               (1u << 4)
#define CLKx_SRC_MASK          (3u << 2)
#define CLKx_SRC_XO            (0u << 2)
#define CLKx_SRC_CLKIN         (1u << 2)
#define CLKx_SRC_MS            (3u << 2)
#define CLKx_DRIVE_2MA         0u
#define CLKx_DRIVE_4MA         1u
#define CLKx_DRIVE_6MA         2u
#define CLKx_DRIVE_8MA         3u

/* Bity w MSx_P1_MISC */
#define MSx_DIVBY4_MASK        0x60
#define MSx_DIVBY4_OFF         0x00
#define MSx_DIVBY4_ON          0x60
#define MSx_P1_17_16_MASK      0x03

/* Zakresy częstotliwości */
#define SI5351_MIN_HZ          8000u
#define SI5351_MAX_HZ          160000000u
#define SI5351_VCO_MIN_HZ      600000000u
#define SI5351_VCO_MAX_HZ      900000000u

/* Częstotliwość XTAL */
#ifndef SI5351_XTAL_HZ
#define SI5351_XTAL_HZ         25000000u
#endif

/* Zmienne stanu dla każdego kanału */
static uint32_t g_clk0_hz = 0;
static uint32_t g_clk0_real_hz = 0;
static uint32_t g_clk1_hz = 0;
static uint32_t g_clk1_real_hz = 0;
static uint32_t g_clk2_hz = 0;
static uint32_t g_clk2_real_hz = 0;

static inline bool wr8(uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    int result;
    for (int retry = 0; retry < 3; retry++) {
        result = i2c_write_blocking(i2c0, SI5351_I2C_ADDR, b, 2, false);
        if (result == 2) return true;
        printf("I2C write error (retry %d): reg=0x%02X, val=0x%02X, result=%d\n", retry, reg, val, result);
        sleep_us(100);
    }
    return false;
}

static inline bool rd8(uint8_t reg, uint8_t *val) {
    int result = i2c_write_blocking(i2c0, SI5351_I2C_ADDR, &reg, 1, true);
    if (result != 1) {
        printf("I2C write address error: reg=0x%02X, result=%d\n", reg, result);
        return false;
    }
    result = i2c_read_blocking(i2c0, SI5351_I2C_ADDR, val, 1, false);
    if (result != 1) {
        printf("I2C read error: reg=0x%02X, result=%d\n", reg, result);
        return false;
    }
    return true;
}

static inline bool wrm(uint8_t reg, const uint8_t *data, uint8_t n) {
    if (n > 9) {
        printf("I2C multi-write error: too many bytes (%u)\n", n);
        return false;
    }
    uint8_t buf[10];
    buf[0] = reg;
    for (uint8_t i = 0; i < n; ++i) buf[1 + i] = data[i];
    int result;
    for (int retry = 0; retry < 3; retry++) {
        result = i2c_write_blocking(i2c0, SI5351_I2C_ADDR, buf, (size_t)n + 1, false);
        if (result == (int)(n + 1)) return true;
        printf("I2C multi-write error (retry %d): reg=0x%02X, n=%u, result=%d\n", retry, reg, n, result);
        sleep_us(100);
    }
    return false;
}

typedef struct {
    uint32_t P1, P2, P3;
    bool integer_mode;
    bool divby4;
    uint8_t rdiv;
} ms_params_t;

static uint8_t choose_rdiv(uint32_t fvco_hz, uint32_t fout_hz) {
    uint8_t r = 0;
    uint64_t ms = (uint64_t)fvco_hz / fout_hz;
    while (r < 7 && ms > 2048) {
        ms >>= 1;
        r++;
    }
    if (ms < 8 && r > 0) r--; // Ensure ms >= 8
    return (r > 7) ? 7 : r;
}

static void calc_pll_params(uint32_t fvco_hz, uint32_t fxtal_hz, ms_params_t *o) {
    uint32_t a = fvco_hz / fxtal_hz;
    uint32_t rem = fvco_hz % fxtal_hz;
    if (rem == 0) {
        // Tryb integer
        o->P1 = 128u * a - 512u;
        o->P2 = 0;
        o->P3 = 1;
        o->integer_mode = true;
    } else {
        // Tryb ułamkowy
        const uint32_t C = 1048576u;
        uint32_t b = (uint32_t)((((uint64_t)rem) * C) / fxtal_hz);
        uint32_t floor_term = (128u * b) / C;
        o->P1 = 128u * a + floor_term - 512u;
        o->P2 = 128u * b - C * floor_term;
        o->P3 = C;
        o->integer_mode = false;
    }
    o->divby4 = false;
    printf("PLL params: P1=%u, P2=%u, P3=%u, integer_mode=%d\n", o->P1, o->P2, o->P3, o->integer_mode);
}

static void calc_ms_params(uint32_t fvco_hz, uint32_t fout_hz, ms_params_t *o) {
    if (fout_hz > 150000000u) {
        o->P1 = 0;
        o->P2 = 0;
        o->P3 = 1;
        o->integer_mode = true;
        o->divby4 = true;
        o->rdiv = 0;
    } else {
        uint32_t a = fvco_hz / fout_hz;
        uint32_t rem = fvco_hz % fout_hz;
        if (rem == 0) {
            // Tryb integer
            o->P1 = 128u * a - 512u;
            o->P2 = 0;
            o->P3 = 1;
            o->integer_mode = true;
        } else {
            // Tryb ułamkowy
            const uint32_t C = 1048576u;
            uint32_t b = (uint32_t)((((uint64_t)rem) * C) / fout_hz);
            uint32_t floor_term = (128u * b) / C;
            o->P1 = 128u * a + floor_term - 512u;
            o->P2 = 128u * b - C * floor_term;
            o->P3 = C;
            o->integer_mode = false;
        }
        o->divby4 = false;
        o->rdiv = choose_rdiv(fvco_hz, fout_hz);
    }
    printf("MS params: P1=%u, P2=%u, P3=%u, integer_mode=%d, divby4=%d, rdiv=%u\n", 
           o->P1, o->P2, o->P3, o->integer_mode, o->divby4, o->rdiv);
}

static bool program_pll(uint8_t reg_base, const ms_params_t *p) {
    uint8_t r[8];
    r[0] = (uint8_t)((p->P3 >> 8) & 0xFF);
    r[1] = (uint8_t)(p->P3 & 0xFF);
    r[2] = (uint8_t)((p->P1 >> 16) & 0x03);
    r[3] = (uint8_t)((p->P1 >> 8) & 0xFF);
    r[4] = (uint8_t)(p->P1 & 0xFF);
    r[5] = (uint8_t)(((p->P3 >> 16) & 0x0F) << 4 | ((p->P2 >> 16) & 0x0F));
    r[6] = (uint8_t)((p->P2 >> 8) & 0xFF);
    r[7] = (uint8_t)(p->P2 & 0xFF);
    printf("Program PLL (reg_base=0x%02X): P3=0x%04X, P1=0x%06X, P2=0x%06X\n", 
           reg_base, p->P3, p->P1, p->P2);
    return wrm(reg_base, r, 8);
}

static bool program_ms(uint8_t reg_base, const ms_params_t *p) {
    if (p->divby4) {
        if (!wr8(reg_base, 0x00)) return false;
        if (!wr8(reg_base + 1, 0x01)) return false;
        uint8_t p1misc = MSx_DIVBY4_ON | ((p->rdiv & 0x07) << 4);
        if (!wr8(reg_base + 2, p1misc)) return false;
        if (!wr8(reg_base + 3, 0x00)) return false;
        if (!wr8(reg_base + 4, 0x00)) return false;
        if (!wr8(reg_base + 5, 0x10)) return false;
        if (!wr8(reg_base + 6, 0x00)) return false;
        if (!wr8(reg_base + 7, 0x00)) return false;
        printf("Program MS (reg_base=0x%02X): divby4, rdiv=%u\n", reg_base, p->rdiv);
        return true;
    } else {
        uint8_t r[8];
        r[0] = (uint8_t)((p->P3 >> 8) & 0xFF);
        r[1] = (uint8_t)(p->P3 & 0xFF);
        r[2] = (uint8_t)((p->P1 >> 16) & 0x03) | ((p->rdiv & 0x07) << 4);
        r[3] = (uint8_t)((p->P1 >> 8) & 0xFF);
        r[4] = (uint8_t)(p->P1 & 0xFF);
        r[5] = (uint8_t)(((p->P3 >> 16) & 0x0F) << 4 | ((p->P2 >> 16) & 0x0F));
        r[6] = (uint8_t)((p->P2 >> 8) & 0xFF);
        r[7] = (uint8_t)(p->P2 & 0xFF);
        printf("Program MS (reg_base=0x%02X): P3=0x%04X, P1=0x%06X, P2=0x%06X, rdiv=%u\n", 
               reg_base, p->P3, p->P1, p->P2, p->rdiv);
        return wrm(reg_base, r, 8);
    }
}

static bool si5351_set_clock(uint8_t clk_ctrl_reg, uint8_t pll_reg_base, uint8_t ms_reg_base, uint32_t fout_hz, bool use_pllb, bool invert, uint32_t *clk_hz, uint32_t *clk_real_hz, uint32_t phase_offset) {
    if (fout_hz < SI5351_MIN_HZ || fout_hz > SI5351_MAX_HZ) {
        printf("CLK: Nieprawidłowa częstotliwość %u Hz (musi być %u–%u Hz)\n", fout_hz, SI5351_MIN_HZ, SI5351_MAX_HZ);
        return false;
    }
    printf("CLK: Ustawianie częstotliwości %u Hz (PLL%s, invert=%d, phase_offset=%u)\n", fout_hz, use_pllb ? "B" : "A", invert, phase_offset);

    ms_params_t pll, ms;
    uint32_t fvco, div;
    uint8_t rdiv;

    if (fout_hz > 150000000u) {
        fvco = fout_hz * 4;
        if (fvco < SI5351_VCO_MIN_HZ || fvco > SI5351_VCO_MAX_HZ) {
            printf("CLK: Nieprawidłowa częstotliwość VCO %u Hz (musi być %u–%u Hz)\n", fvco, SI5351_VCO_MIN_HZ, SI5351_VCO_MAX_HZ);
            return false;
        }
        calc_pll_params(fvco, SI5351_XTAL_HZ, &pll);
        calc_ms_params(fvco, fout_hz, &ms);
        ms.divby4 = true;
        ms.rdiv = 0;
        div = 4;
        rdiv = 0;
    } else {
        uint32_t best_fvco = 0;
        uint32_t best_div = 0;
        uint8_t best_rdiv = 0;
        uint64_t best_err = UINT64_MAX;
        bool best_integer = false;
        for (uint8_t r = 0; r <= 7; r++) {
            uint32_t R = 1u << r;
            uint64_t target_fout = (uint64_t)fout_hz * R;
            for (uint32_t n = 6; n <= 1800; n++) {
                uint64_t cand = target_fout * n;
                if (cand < SI5351_VCO_MIN_HZ || cand > SI5351_VCO_MAX_HZ) continue;
                uint64_t fout_calc = cand / n / R;
                uint64_t err = (fout_calc > fout_hz) ? (fout_calc - fout_hz) : (fout_hz - fout_calc);
                bool is_integer = (cand % target_fout == 0);
                if (err < best_err || (err == best_err && is_integer && !best_integer)) {
                    best_err = err;
                    best_fvco = (uint32_t)cand;
                    best_div = n;
                    best_rdiv = r;
                    best_integer = is_integer;
                }
            }
        }
        if (best_div == 0) {
            printf("CLK: Nie znaleziono prawidłowego dzielnika\n");
            return false;
        }
        calc_pll_params(best_fvco, SI5351_XTAL_HZ, &pll);
        calc_ms_params(best_fvco, (best_fvco / best_div), &ms);
        ms.rdiv = best_rdiv;
        if (best_integer) {
            ms.P2 = 0;
            ms.P3 = 1;
            ms.integer_mode = true;
        }
        fvco = best_fvco;
        div = best_div;
        rdiv = best_rdiv;
    }

    // Dodaj przesunięcie fazowe do P1, jeśli podano
    if (phase_offset > 0) {
        if (ms.divby4) {
            if (phase_offset > 1) phase_offset = 1; // Dla divby4 max offset to 1
        } else {
            double ms_divider = (double)fvco / (fout_hz * (1u << ms.rdiv));
            uint32_t calc_phase = (uint32_t)(ms_divider / 4.0); // 90 stopni to 1/4 dzielnika
            phase_offset = (phase_offset < calc_phase) ? calc_phase : phase_offset;
            if (phase_offset > 127) phase_offset = 127; // Max 127
        }
        ms.P1 += phase_offset * 128;
        printf("CLK: Zastosowano przesunięcie fazowe %u jednostek\n", phase_offset);
    }

    if (!program_pll(pll_reg_base, &pll)) {
        printf("CLK: Błąd programowania PLL\n");
        return false;
    }
    if (!program_ms(ms_reg_base, &ms)) {
        printf("CLK: Błąd programowania MS\n");
        return false;
    }

    uint8_t clk_ctrl = CLKx_SRC_MS | CLKx_DRIVE_8MA;
    if (use_pllb) clk_ctrl |= CLKx_SRC_PLLB;
    if (ms.integer_mode && !ms.divby4) clk_ctrl |= CLKx_INT;
    else                               clk_ctrl &= ~CLKx_INT;
    if (invert) clk_ctrl |= CLKx_INV;
    if (!wr8(clk_ctrl_reg, clk_ctrl)) {
        printf("CLK: Błąd zapisu rejestru sterującego (0x%02X)\n", clk_ctrl_reg);
        return false;
    }
    uint8_t read_back;
    if (rd8(clk_ctrl_reg, &read_back)) {
        if (read_back != clk_ctrl) {
            printf("CLK: Mismatch in CLK_CTRL: wrote 0x%02X, read 0x%02X\n", clk_ctrl, read_back);
        }
    }
    for (uint8_t i = 0; i < 8; i++) {
        if (rd8(ms_reg_base + i, &read_back)) {
            printf("CLK: Read MS[%u]=0x%02X\n", i, read_back);
        }
    }

    // Włącz wszystkie wyjścia
    if (!wr8(REG_OE_CTRL, 0xF8)) {
        printf("CLK: Błąd zapisu rejestru OE\n");
        return false;
    }

    // Reset PLL (PLLA=0xA0, PLLB=0x20)
    uint8_t pll_reset_val = use_pllb ? 0x20 : 0xA0;
    if (!wr8(REG_PLL_RESET, pll_reset_val)) {
        printf("CLK: Błąd resetu PLL\n");
        return false;
    }

    // Weryfikacja rejestrów dla CLK1
    if (clk_ctrl_reg == REG_CLK1_CTRL) {
        uint8_t val;
        if (rd8(REG_OE_CTRL, &val)) {
            printf("CLK1: REG_OE_CTRL=0x%02X (oczekiwano 0xF8)\n", val);
        }
        if (rd8(REG_CLK1_CTRL, &val)) {
            printf("CLK1: REG_CLK1_CTRL=0x%02X\n", val);
        }
        for (uint8_t i = 0; i < 8; i++) {
            if (rd8(REG_MS1_P3_15_8 + i, &val)) {
                printf("CLK1: REG_MS1[%u]=0x%02X\n", i, val);
            }
        }
        for (uint8_t i = 0; i < 8; i++) {
            if (rd8(REG_MSNB_P3_15_8 + i, &val)) {
                printf("CLK1: REG_MSNB[%u]=0x%02X\n", i, val);
            }
        }
    }
    // Weryfikacja rejestrów dla CLK2
    if (clk_ctrl_reg == REG_CLK2_CTRL) {
        uint8_t val;
        if (rd8(REG_OE_CTRL, &val)) {
            printf("CLK2: REG_OE_CTRL=0x%02X (oczekiwano 0xF8)\n", val);
        }
        if (rd8(REG_CLK2_CTRL, &val)) {
            printf("CLK2: REG_CLK2_CTRL=0x%02X\n", val);
        }
        for (uint8_t i = 0; i < 8; i++) {
            if (rd8(REG_MS2_P3_15_8 + i, &val)) {
                printf("CLK2: REG_MS2[%u]=0x%02X\n", i, val);
            }
        }
        for (uint8_t i = 0; i < 8; i++) {
            if (rd8(REG_MSNA_P3_15_8 + i, &val)) {
                printf("CLK2: REG_MSNA[%u]=0x%02X\n", i, val);
            }
        }
    }

    *clk_hz = fout_hz;
    *clk_real_hz = fvco / div / (1u << rdiv);
    printf("CLK: Ustawiono VCO=%u Hz, div=%u, rdiv=%u, real_freq=%u Hz\n", fvco, div, rdiv, *clk_real_hz);
    sleep_us(500);
    return true;
}

bool si5351_init(void) {
    // Włącz wszystkie wyjścia (CLK0, CLK1, CLK2)
    if (!wr8(REG_OE_CTRL, 0xF8)) {
        printf("INIT: Błąd zapisu rejestru OE\n");
        return false;
    }
    // Ustaw domyślny stan rejestrów sterujących
    uint8_t clk_ctrl = CLKx_SRC_MS | CLKx_DRIVE_8MA;
    if (!wr8(REG_CLK0_CTRL, clk_ctrl)) {
        printf("INIT: Błąd zapisu CLK0_CTRL\n");
        return false;
    }
    if (!wr8(REG_CLK1_CTRL, clk_ctrl | CLKx_SRC_PLLB)) {
        printf("INIT: Błąd zapisu CLK1_CTRL\n");
        return false;
    }
    if (!wr8(REG_CLK2_CTRL, clk_ctrl | CLKx_SRC_PLLB)) {
        printf("INIT: Błąd zapisu CLK2_CTRL\n");
        return false;
    }
    printf("INIT: SI5351 zainicjalizowany, wszystkie kanały włączone\n");
    return true;
}

bool si5351_clk0_set(uint32_t fout_hz) {
    return si5351_set_clock(REG_CLK0_CTRL, REG_MSNA_P3_15_8, REG_MS0_P3_15_8, fout_hz, false, false, &g_clk0_hz, &g_clk0_real_hz, 0);
}

/* Modyfikujemy si5351_clk1_set, żeby używało PLLA */
bool si5351_clk1_set(uint32_t fout_hz) {
    // Wyłącz CLK2, żeby uniknąć konfliktu na PLLA
    if (!wr8(REG_CLK2_CTRL, CLKx_PDN)) {
        printf("CLK1: Błąd wyłączania CLK2\n");
        return false;
    }
    bool result = si5351_set_clock(REG_CLK1_CTRL, REG_MSNA_P3_15_8, REG_MS1_P3_15_8, fout_hz, false, false, &g_clk1_hz, &g_clk1_real_hz, 0);
    if (result) {
        printf("CLK1: Nowa częstotliwość %u Hz\n", g_clk1_real_hz);
    }
    return result;
}

bool si5351_clk2_set(uint32_t fout_hz, bool invert) {
    // Sprawdź, czy częstotliwość CLK2 zgadza się z CLK1
    if (g_clk1_hz != 0 && fout_hz != g_clk1_hz) {
        printf("CLK2: Częstotliwość CLK2 (%u Hz) musi być taka sama jak CLK1 (%u Hz) dla przesunięcia fazowego\n", fout_hz, g_clk1_hz);
        return false;
    }
    // Użyj si5351_set_clock z przesunięciem fazowym 90 stopni
    return si5351_set_clock(REG_CLK2_CTRL, REG_MSNA_P3_15_8, REG_MS2_P3_15_8, fout_hz, false, invert, &g_clk2_hz, &g_clk2_real_hz, 1); // 1 jako placeholder, offset obliczany w set_clock
}

uint32_t si5351_clk0_get_hz(void) {
    return g_clk0_hz;
}

uint32_t si5351_clk0_get_real_hz(void) {
    return g_clk0_real_hz;
}

uint32_t si5351_clk1_get_hz(void) {
    return g_clk1_hz;
}

uint32_t si5351_clk1_get_real_hz(void) {
    return g_clk1_real_hz;
}

uint32_t si5351_clk2_get_hz(void) {
    return g_clk2_hz;
}

uint32_t si5351_clk2_get_real_hz(void) {
    return g_clk2_real_hz;
}