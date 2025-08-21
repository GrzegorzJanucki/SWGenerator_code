#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#ifndef SI5351_I2C_ADDR
#define SI5351_I2C_ADDR 0x60
#endif

/* Rejestry (AN619) */
#define REG_CLK0_CTRL          16
#define REG_MS0_P3_15_8        42
#define REG_MS0_P3_7_0         43
#define REG_MS0_P1_MISC        44
#define REG_MS0_P1_15_8        45
#define REG_MS0_P1_7_0         46
#define REG_MS0_P3_19_16_P2_19_16 47
#define REG_MS0_P2_15_8        48
#define REG_MS0_P2_7_0         49

#define REG_MSNA_P3_15_8       26
#define REG_MSNA_P3_7_0        27
#define REG_MSNA_P1_17_16      28
#define REG_MSNA_P1_15_8       29
#define REG_MSNA_P1_7_0        30
#define REG_MSNA_P3_19_16_P2_19_16 31
#define REG_MSNA_P2_15_8       32
#define REG_MSNA_P2_7_0        33

#define REG_PLL_RESET          177
#define REG_OE_CTRL            3

/* Bity w CLKx_CONTROL */
#define CLKx_PDN               (1u << 7)
#define CLKx_INT               (1u << 6)   /* MS integer mode */
#define CLKx_SRC_PLLB          (1u << 5)   /* 0 = PLLA, 1 = PLLB */
#define CLKx_INV               (1u << 4)
#define CLKx_SRC_MASK          (3u << 2)
#define CLKx_SRC_XO            (0u << 2)
#define CLKx_SRC_CLKIN         (1u << 2)
#define CLKx_SRC_MS            (3u << 2)   /* własny Multisynth (CLK0->MS0) */
#define CLKx_DRIVE_2MA         0u
#define CLKx_DRIVE_4MA         1u
#define CLKx_DRIVE_6MA         2u
#define CLKx_DRIVE_8MA         3u

/* Bity w MS0_P1_MISC (reg 44) */
#define MSx_DIVBY4_MASK        0x60        /* [7:6] */
#define MSx_DIVBY4_OFF         0x00
#define MSx_DIVBY4_ON          0x60        /* 11b => /4 */
#define MSx_P1_17_16_MASK      0x03

/* Zakresy */
#define SI5351_MIN_HZ          8000u
#define SI5351_MAX_HZ          160000000u

/* XTAL */
#ifndef SI5351_XTAL_HZ
#define SI5351_XTAL_HZ         25000000u
#endif

/* Prywatny stan */
static uint32_t g_clk0_hz = 0;

/* I2C helpers */
static inline bool wr8(uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    return i2c_write_blocking(i2c0, SI5351_I2C_ADDR, b, 2, false) == 2;
}
static inline bool wrm(uint8_t reg, const uint8_t *data, uint8_t n) {
    uint8_t buf[10];
    if (n > 9) return false;
    buf[0] = reg;
    for (uint8_t i = 0; i < n; ++i) buf[1 + i] = data[i];
    return i2c_write_blocking(i2c0, SI5351_I2C_ADDR, buf, (size_t)n + 1, false) == (int)(n + 1);
}

/* Wzory AN619 */
typedef struct {
    uint32_t P1, P2, P3;
    bool integer_mode;
    bool divby4;
    uint8_t rdiv;   /* 0..7: 0=>/1, 1=>/2, ..., 7=>/128 */
} ms_params_t;

/* wybór R tak, by MS w 8..2048 */
static uint8_t choose_rdiv(uint32_t fvco_hz, uint32_t fout_hz) {
    uint8_t r = 0; // /1
    while (r < 7) { // do /128
        uint32_t R = 1u << r;
        uint64_t ms = (uint64_t)fvco_hz / (fout_hz * R);
        if (ms >= 8 && ms <= 2048) break;
        if (ms < 8) break; // już za mało – większe R tylko zmniejszy ms
        r++;
    }
    return r;
}

static void calc_pll_params(uint32_t fvco_hz, uint32_t fxtal_hz, ms_params_t *o) {
    /* fvco = a + b/c, gdzie a = floor(fvco/fxtal) */
    const uint32_t C = 1048576u; /* 2^20 limit to 1,048,575; używamy 2^20 na błąd <1ppm */
    uint32_t a = fvco_hz / fxtal_hz;
    uint32_t rem = fvco_hz % fxtal_hz;
    uint32_t b = (uint32_t)((((uint64_t)rem) * C) / fxtal_hz);
    uint32_t floor_term = (128u * b) / C;
    o->P1 = 128u * a + floor_term - 512u;
    o->P2 = 128u * b - C * floor_term;
    o->P3 = C;
    o->integer_mode = (o->P2 == 0);
    o->divby4 = false;
}

static void calc_ms_params(uint32_t fvco_hz, uint32_t fout_hz, ms_params_t *o) {
    /* Dla fout > 150 MHz użyj /4 */
    if (fout_hz > 150000000u) {
        o->P1 = 0; o->P2 = 0; o->P3 = 1;
        o->integer_mode = true;
        o->divby4 = true;
        return;
    }
    const uint32_t C = 1048576u;
    /* fvco / fout = a + b/c */
    uint32_t a = fvco_hz / fout_hz;
    uint32_t rem = fvco_hz % fout_hz;
    uint32_t b = (uint32_t)((((uint64_t)rem) * C) / fout_hz);
    uint32_t floor_term = (128u * b) / C;
    o->P1 = 128u * a + floor_term - 512u;
    o->P2 = 128u * b - C * floor_term;
    o->P3 = C;
    o->integer_mode = (o->P2 == 0);
    o->divby4 = false;
}

/* Zapis parametrów do PLLA (MSNA, reg 26..33) */
static bool program_plla(const ms_params_t *p) {
    uint8_t r[8];
    r[0] = (uint8_t)((p->P3 >> 8) & 0xFF);
    r[1] = (uint8_t)( p->P3       & 0xFF);
    r[2] = (uint8_t)((p->P1 >> 16) & 0x03);
    r[3] = (uint8_t)((p->P1 >> 8)  & 0xFF);
    r[4] = (uint8_t)( p->P1        & 0xFF);
    r[5] = (uint8_t)(((p->P3 >> 16) & 0x0F) << 4 | ((p->P2 >> 16) & 0x0F));
    r[6] = (uint8_t)((p->P2 >> 8)  & 0xFF);
    r[7] = (uint8_t)( p->P2        & 0xFF);
    return wrm(REG_MSNA_P3_15_8, r, 8);
}

/* Zapis parametrów do MS0 (reg 42..49) */
static bool program_ms0(const ms_params_t *p) {
    if (p->divby4) {
        if (!wr8(REG_MS0_P3_15_8, 0x00)) return false;
        if (!wr8(REG_MS0_P3_7_0, 0x01)) return false;
        uint8_t p1misc = MSx_DIVBY4_ON | ((p->rdiv & 0x07) << 4); // R w bitach 4-6
        if (!wr8(REG_MS0_P1_MISC, p1misc)) return false;
        if (!wr8(REG_MS0_P1_15_8, 0x00)) return false;
        if (!wr8(REG_MS0_P1_7_0, 0x00)) return false;
        if (!wr8(REG_MS0_P3_19_16_P2_19_16, 0x10)) return false;
        if (!wr8(REG_MS0_P2_15_8, 0x00)) return false;
        if (!wr8(REG_MS0_P2_7_0, 0x00)) return false;
        return true;
    } else {
        uint8_t r[8];
        r[0] = (uint8_t)((p->P3 >> 8) & 0xFF);
        r[1] = (uint8_t)(p->P3 & 0xFF);
        r[2] = (uint8_t)((p->P1 >> 16) & 0x03) | ((p->rdiv & 0x07) << 4); // R w bitach 4-6
        r[3] = (uint8_t)((p->P1 >> 8) & 0xFF);
        r[4] = (uint8_t)(p->P1 & 0xFF);
        r[5] = (uint8_t)(((p->P3 >> 16) & 0x0F) << 4 | ((p->P2 >> 16) & 0x0F));
        r[6] = (uint8_t)((p->P2 >> 8) & 0xFF);
        r[7] = (uint8_t)(p->P2 & 0xFF);
        return wrm(REG_MS0_P3_15_8, r, 8);
    }
}


bool si5351_clk0_set(uint32_t fout_hz) {
    if (fout_hz < SI5351_MIN_HZ || fout_hz > SI5351_MAX_HZ) return false;

    uint32_t fvco_hz = 0;
    uint32_t div = 0;
    bool force_integer = false;
    uint8_t rdiv = 0;

    /* Wybór R dzielnika dla niskich częstotliwości */
    if (fout_hz < 500000) {
        rdiv = 7; // Start z /128
        uint32_t R = 1u << rdiv;
        uint64_t ms_numerator = (uint64_t)900000000 / fout_hz; // Maksymalne VCO / fout
        while (rdiv > 0 && (ms_numerator / R < 8 || ms_numerator / R > 2048)) {
            rdiv--;
            R = 1u << rdiv;
        }
    }

    /* Szukanie VCO z uwzględnieniem R dzielnika */
    uint32_t R = 1u << rdiv;
    uint32_t target_fout = fout_hz * R; // Skorygowana częstotliwość wejściowa
    for (uint32_t n = 6; n <= 1800; n++) {
        uint64_t cand = (uint64_t)target_fout * n;
        if (cand >= 600000000ULL && cand <= 900000000ULL) {
            fvco_hz = (uint32_t)cand;
            div = n;
            break;
        }
    }

    if (div == 0) return false;

    ms_params_t pll, ms;
    calc_pll_params(fvco_hz, SI5351_XTAL_HZ, &pll);
    calc_ms_params(fvco_hz, target_fout, &ms); // Użyj skorygowanej częstotliwości
    ms.rdiv = rdiv; // Przekaż R dzielnik

    if (fout_hz < 500000) {
        ms.integer_mode = true;
        ms.P2 = 0;
        ms.P3 = 1;
    }

    if (!program_plla(&pll)) return false;
    if (!program_ms0(&ms)) return false;

    uint8_t clk0 = CLKx_SRC_MS | CLKx_DRIVE_8MA;
    if (ms.integer_mode) clk0 |= CLKx_INT;
    if (!wr8(REG_CLK0_CTRL, clk0)) return false;

    if (!wr8(REG_OE_CTRL, 0x00)) return false;
    if (!wr8(REG_PLL_RESET, 0xA0)) return false;

    g_clk0_hz = fout_hz;
    sleep_us(100);
    return true;
}

uint32_t si5351_clk0_get_hz(void) {
    return g_clk0_hz;
}

/* Prosta inicjalizacja: wyłącz wszystko na starcie */
bool si5351_init(void) {
    /* Domyślnie wyłącz wyjścia (OE high) i potem włączamy przy ustawianiu częstotliwości */
    if (!wr8(REG_OE_CTRL, 0xFF)) return false; /* wszystkie disabled */
    return true;
}