#include <pico/stdlib.h>
#include <hardware/i2c.h>
#include <pico/binary_info.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "sh1106.h"
#include "font.h"

inline static void swap(int32_t *a, int32_t *b) {
    int32_t *t=a;
    *a=*b;
    *b=*t;
}

inline static void fancy_write(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, char *name) {
    switch(i2c_write_blocking(i2c, addr, src, len, false)) {
    case PICO_ERROR_GENERIC:
        //printf("[%s] addr not acknowledged!\n", name);
        break;
    case PICO_ERROR_TIMEOUT:
        //printf("[%s] timeout!\n", name);
        break;
    default:
        //printf("[%s] wrote successfully %lu bytes!\n", name, len);
        break;
    }
}

inline static void sh1106_write(sh1106_t *p, uint8_t val) {
    uint8_t d[2]= {0x00, val};
    fancy_write(p->i2c_i, p->address, d, 2, "sh1106_write");
}

bool sh1106_init(sh1106_t *p, uint16_t width, uint16_t height, uint8_t address, i2c_inst_t *i2c_instance) {
    p->width=width;
    p->height=height;
    p->pages=height/8;
    p->address=address;

    p->i2c_i=i2c_instance;

    p->bufsize = p->pages * p->width;
    p->buffer = malloc(p->bufsize);
    if (!p->buffer) {
        p->bufsize = 0;
        return false;
    }
    memset(p->buffer, 0, p->bufsize); // Wyczyść cały bufor

    uint8_t cmds[] = {
        0xAE,             // DISPLAY OFF
        0xD5, 0x80,       // Set display clock divide ratio/oscillator frequency
        0xA8, 0x3F,       // Multiplex ratio (1/64)
        0xD3, 0x00,       // Display offset = 0
        0x40,             // Display start line = 0
        0xAD, 0x8B,       // DC-DC control (internal, enable)
        0xA0,             // Segment remap (domyślna orientacja)
        0xC0,             // COM scan direction (domyślna)
        0xDA, 0x12,       // COM pins hardware configuration
        0x81, 0x7F,       // Contrast control (oryginalna wartość)
        0xD9, 0x22,       // Pre-charge period
        0xDB, 0x35,       // VCOMH deselect level
        0xA4,             // Output follows RAM
        0xA6,             // Normal display
        0xAF              // DISPLAY ON
    };

    for(size_t i=0; i<sizeof(cmds); ++i) {
        sh1106_write(p, cmds[i]);
        sleep_ms(10); // Dodaj opóźnienie
    }

    return true;
}

inline void sh1106_deinit(sh1106_t *p) {
    free(p->buffer-1);
}

inline void sh1106_poweroff(sh1106_t *p) {
    sh1106_write(p, SET_DISP|0x00);
}

inline void sh1106_poweron(sh1106_t *p) {
    sh1106_write(p, SET_DISP|0x01);
}

inline void sh1106_contrast(sh1106_t *p, uint8_t val) {
    sh1106_write(p, SET_CONTRAST);
    sh1106_write(p, val);
}

inline void sh1106_invert(sh1106_t *p, uint8_t inv) {
    sh1106_write(p, SET_NORM_INV | (inv & 1));
}

inline void sh1106_clear(sh1106_t *p) {
    memset(p->buffer, 0, p->bufsize);
}

void sh1106_clear_pixel(sh1106_t *p, uint32_t x, uint32_t y) {
    if(x>=p->width || y>=p->height) return;

    p->buffer[x+p->width*(y>>3)]&=~(0x1<<(y&0x07));
}

void sh1106_draw_pixel(sh1106_t *p, uint32_t x, uint32_t y) {
    if(x>=p->width || y>=p->height) return;

    p->buffer[x+p->width*(y>>3)]|=0x1<<(y&0x07);
}

void sh1106_draw_line(sh1106_t *p, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    if(x1>x2) {
        swap(&x1, &x2);
        swap(&y1, &y2);
    }

    if(x1==x2) {
        if(y1>y2)
            swap(&y1, &y2);
        for(int32_t i=y1; i<=y2; ++i)
            sh1106_draw_pixel(p, x1, i);
        return;
    }

    float m=(float) (y2-y1) / (float) (x2-x1);

    for(int32_t i=x1; i<=x2; ++i) {
        float y=m*(float) (i-x1)+(float) y1;
        sh1106_draw_pixel(p, i, (uint32_t) y);
    }
}

void sh1106_clear_square(sh1106_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    for(uint32_t i=0; i<width; ++i)
        for(uint32_t j=0; j<height; ++j)
            sh1106_clear_pixel(p, x+i, y+j);
}

void sh1106_draw_square(sh1106_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    for(uint32_t i=0; i<width; ++i)
        for(uint32_t j=0; j<height; ++j)
            sh1106_draw_pixel(p, x+i, y+j);
}

void sh1106_draw_empty_square(sh1106_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    sh1106_draw_line(p, x, y, x+width, y);
    sh1106_draw_line(p, x, y+height, x+width, y+height);
    sh1106_draw_line(p, x, y, x, y+height);
    sh1106_draw_line(p, x+width, y, x+width, y+height);
}

void sh1106_draw_char_with_font(sh1106_t *p, uint32_t x, uint32_t y, uint32_t scale, const uint8_t *font, char c) {
    if(c<font[3]||c>font[4])
        return;

    uint32_t parts_per_line=(font[0]>>3)+((font[0]&7)>0);
    for(uint8_t w=0; w<font[1]; ++w) {
        uint32_t pp=(c-font[3])*font[1]*parts_per_line+w*parts_per_line+5;
        for(uint32_t lp=0; lp<parts_per_line; ++lp) {
            uint8_t line=font[pp];

            for(int8_t j=0; j<8; ++j, line>>=1) {
                if(line & 1)
                    sh1106_draw_square(p, x+w*scale, y+((lp<<3)+j)*scale, scale, scale);
            }

            ++pp;
        }
    }
}

void sh1106_draw_string_with_font(sh1106_t *p, uint32_t x, uint32_t y, uint32_t scale, const uint8_t *font, const char *s) {
    for(int32_t x_n=x; *s; x_n+=(font[1]+font[2])*scale) {
        sh1106_draw_char_with_font(p, x_n, y, scale, font, *(s++));
    }
}

void sh1106_draw_char(sh1106_t *p, uint32_t x, uint32_t y, uint32_t scale, char c) {
    sh1106_draw_char_with_font(p, x, y, scale, font_8x5, c);
}

void sh1106_draw_string(sh1106_t *p, uint32_t x, uint32_t y, uint32_t scale, const char *s) {
    sh1106_draw_string_with_font(p, x, y, scale, font_8x5, s);
}

static inline uint32_t sh1106_bmp_get_val(const uint8_t *data, const size_t offset, uint8_t size) {
    switch(size) {
    case 1:
        return data[offset];
    case 2:
        return data[offset]|(data[offset+1]<<8);
    case 4:
        return data[offset]|(data[offset+1]<<8)|(data[offset+2]<<16)|(data[offset+3]<<24);
    default:
        __builtin_unreachable();
    }
    __builtin_unreachable();
}

void sh1106_bmp_show_image_with_offset(sh1106_t *p, const uint8_t *data, const long size, uint32_t x_offset, uint32_t y_offset) {
    if(size<54) // data smaller than header
        return;

    const uint32_t bfOffBits=sh1106_bmp_get_val(data, 10, 4);
    const uint32_t biSize=sh1106_bmp_get_val(data, 14, 4);
    const uint32_t biWidth=sh1106_bmp_get_val(data, 18, 4);
    const int32_t biHeight=(int32_t) sh1106_bmp_get_val(data, 22, 4);
    const uint16_t biBitCount=(uint16_t) sh1106_bmp_get_val(data, 28, 2);
    const uint32_t biCompression=sh1106_bmp_get_val(data, 30, 4);

    if(biBitCount!=1) // image not monochrome
        return;

    if(biCompression!=0) // image compressed
        return;

    const int table_start=14+biSize;
    uint8_t color_val=0;

    for(uint8_t i=0; i<2; ++i) {
        if(!((data[table_start+i*4]<<16)|(data[table_start+i*4+1]<<8)|data[table_start+i*4+2])) {
            color_val=i;
            break;
        }
    }

    uint32_t bytes_per_line=(biWidth/8)+(biWidth&7?1:0);
    if(bytes_per_line&3)
        bytes_per_line=(bytes_per_line^(bytes_per_line&3))+4;

    const uint8_t *img_data=data+bfOffBits;

    int32_t step=biHeight>0?-1:1;
    int32_t border=biHeight>0?-1:-biHeight;

    for(uint32_t y=biHeight>0?biHeight-1:0; y!=(uint32_t)border; y+=step) {
        for(uint32_t x=0; x<biWidth; ++x) {
            if(((img_data[x>>3]>>(7-(x&7)))&1)==color_val)
                sh1106_draw_pixel(p, x_offset+x, y_offset+y);
        }
        img_data+=bytes_per_line;
    }
}

inline void sh1106_bmp_show_image(sh1106_t *p, const uint8_t *data, const long size) {
    sh1106_bmp_show_image_with_offset(p, data, size, 0, 0);
}

void sh1106_show(sh1106_t *p) {
    for (uint8_t page = 0; page < p->pages; page++) {
        sh1106_write(p, 0xB0 + page);         // wybór strony
        sh1106_write(p, 0x00);                // ustawienie kolumny startowej, low nibble
        sh1106_write(p, 0x10);                // ustawienie kolumny startowej, high nibble
        uint8_t data[p->width + 1];
        data[0] = 0x40;                       // kontrolny bajt
        memcpy(&data[1], &p->buffer[page * p->width], p->width);
        fancy_write(p->i2c_i, p->address, data, p->width + 1, "sh1106_show");
    }
}