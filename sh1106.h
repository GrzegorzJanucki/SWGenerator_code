#ifndef _inc_sh1106
#define _inc_sh1106
#include <pico/stdlib.h>
#include <hardware/i2c.h>

/**
*	@brief defines commands used in sh1106
*/
typedef enum {
    SET_CONTRAST = 0x81,
    SET_ENTIRE_ON = 0xA4,
    SET_NORM_INV = 0xA6,
    SET_DISP = 0xAE,
    SET_MEM_ADDR = 0x20,
    SET_DISP_START_LINE = 0x40,
    SET_SEG_REMAP = 0xA0,
    SET_MUX_RATIO = 0xA8,
    SET_COM_OUT_DIR = 0xC0,
    SET_DISP_OFFSET = 0xD3,
    SET_COM_PIN_CFG = 0xDA,
    SET_DISP_CLK_DIV = 0xD5,
    SET_PRECHARGE = 0xD9,
    SET_VCOM_DESEL = 0xDB,
    SET_CHARGE_PUMP = 0x8D
} sh11066_command_t;

/**
*	@brief holds the configuration
*/
typedef struct {
    uint8_t width; 		/**< width of display */
    uint8_t height; 	/**< height of display */
    uint8_t pages;		/**< stores pages of display (calculated on initialization*/
    uint8_t address; 	/**< i2c address of display*/
    i2c_inst_t *i2c_i; 	/**< i2c connection instance */
    bool external_vcc; 	/**< whether display uses external vcc */ 
    uint8_t *buffer;	/**< display buffer */
    size_t bufsize;		/**< buffer size */
} sh1106_t;

/**
*	@brief initialize display
*
*	@param[in] p : pointer to instance of sh1106_t
*	@param[in] width : width of display
*	@param[in] height : heigth of display
*	@param[in] address : i2c address of display
*	@param[in] i2c_instance : instance of i2c connection
*	
* 	@return bool.
*	@retval true for Success
*	@retval false if initialization failed
*/
bool sh1106_init(sh1106_t *p, uint16_t width, uint16_t height, uint8_t address, i2c_inst_t *i2c_instance);

/**
*	@brief deinitialize display
*
*	@param[in] p : instance of display
*
*/
void sh1106_deinit(sh1106_t *p);

/**
*	@brief turn off display
*
*	@param[in] p : instance of display
*
*/
void sh1106_poweroff(sh1106_t *p);

/**
	@brief turn on display

	@param[in] p : instance of display

*/
void sh1106_poweron(sh1106_t *p);

/**
	@brief set contrast of display

	@param[in] p : instance of display
	@param[in] val : contrast

*/
void sh1106_contrast(sh1106_t *p, uint8_t val);

/**
	@brief set invert display

	@param[in] p : instance of display
	@param[in] inv : inv==0: disable inverting, inv!=0: invert

*/
void sh1106_invert(sh1106_t *p, uint8_t inv);

/**
	@brief display buffer, should be called on change

	@param[in] p : instance of display

*/
void sh1106_show(sh1106_t *p);

/**
	@brief clear display buffer

	@param[in] p : instance of display

*/
void sh1106_clear(sh1106_t *p);

/**
	@brief clear pixel on buffer

	@param[in] p : instance of display
	@param[in] x : x position
	@param[in] y : y position
*/
void sh1106_clear_pixel(sh1106_t *p, uint32_t x, uint32_t y);

/**
	@brief draw pixel on buffer

	@param[in] p : instance of display
	@param[in] x : x position
	@param[in] y : y position
*/
void sh1106_draw_pixel(sh1106_t *p, uint32_t x, uint32_t y);

/**
	@brief draw line on buffer

	@param[in] p : instance of display
	@param[in] x1 : x position of starting point
	@param[in] y1 : y position of starting point
	@param[in] x2 : x position of end point
	@param[in] y2 : y position of end point
*/
void sh1106_draw_line(sh1106_t *p, int32_t x1, int32_t y1, int32_t x2, int32_t y2);

/**
	@brief clear square at given position with given size

	@param[in] p : instance of display
	@param[in] x : x position of starting point
	@param[in] y : y position of starting point
	@param[in] width : width of square
	@param[in] height : height of square
*/
void sh1106_clear_square(sh1106_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

/**
	@brief draw filled square at given position with given size

	@param[in] p : instance of display
	@param[in] x : x position of starting point
	@param[in] y : y position of starting point
	@param[in] width : width of square
	@param[in] height : height of square
*/
void sh1106_draw_square(sh1106_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

/**
	@brief draw empty square at given position with given size

	@param[in] p : instance of display
	@param[in] x : x position of starting point
	@param[in] y : y position of starting point
	@param[in] width : width of square
	@param[in] height : height of square
*/
void sh1106_draw_empty_square(sh1106_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

/**
	@brief draw monochrome bitmap with offset

	@param[in] p : instance of display
	@param[in] data : image data (whole file)
	@param[in] size : size of image data in bytes
	@param[in] x_offset : offset of horizontal coordinate
	@param[in] y_offset : offset of vertical coordinate
*/
void sh1106_bmp_show_image_with_offset(sh1106_t *p, const uint8_t *data, const long size, uint32_t x_offset, uint32_t y_offset);

/**
	@brief draw monochrome bitmap

	@param[in] p : instance of display
	@param[in] data : image data (whole file)
	@param[in] size : size of image data in bytes
*/
void sh1106_bmp_show_image(sh1106_t *p, const uint8_t *data, const long size);

/**
	@brief draw char with given font

	@param[in] p : instance of display
	@param[in] x : x starting position of char
	@param[in] y : y starting position of char
	@param[in] scale : scale font to n times of original size (default should be 1)
	@param[in] font : pointer to font
	@param[in] c : character to draw
*/
void sh1106_draw_char_with_font(sh1106_t *p, uint32_t x, uint32_t y, uint32_t scale, const uint8_t *font, char c);

/**
	@brief draw char with builtin font

	@param[in] p : instance of display
	@param[in] x : x starting position of char
	@param[in] y : y starting position of char
	@param[in] scale : scale font to n times of original size (default should be 1)
	@param[in] c : character to draw
*/
void sh1106_draw_char(sh1106_t *p, uint32_t x, uint32_t y, uint32_t scale, char c);

/**
	@brief draw string with given font

	@param[in] p : instance of display
	@param[in] x : x starting position of text
	@param[in] y : y starting position of text
	@param[in] scale : scale font to n times of original size (default should be 1)
	@param[in] font : pointer to font
	@param[in] s : text to draw	
*/
void sh1106_draw_string_with_font(sh1106_t *p, uint32_t x, uint32_t y, uint32_t scale, const uint8_t *font, const char *s );

/**
	@brief draw string with builtin font

	@param[in] p : instance of display
	@param[in] x : x starting position of text
	@param[in] y : y starting position of text
	@param[in] scale : scale font to n times of original size (default should be 1)
	@param[in] s : text to draw
*/
void sh1106_draw_string(sh1106_t *p, uint32_t x, uint32_t y, uint32_t scale, const char *s);


#endif