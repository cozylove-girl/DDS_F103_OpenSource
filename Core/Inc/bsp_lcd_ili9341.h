#ifndef BSP_LCD_ILI9341_H
#define BSP_LCD_ILI9341_H

#include "main.h"
#include <stdint.h>

#define LCD_COLOR_BLACK    0x0000U
#define LCD_COLOR_WHITE    0xFFFFU
#define LCD_COLOR_RED      0xF800U
#define LCD_COLOR_GREEN    0x07E0U
#define LCD_COLOR_BLUE     0x001FU
#define LCD_COLOR_YELLOW   0xFFE0U
#define LCD_COLOR_CYAN     0x07FFU
#define LCD_COLOR_GRAY     0x8410U
#define LCD_COLOR_DARK     0x2104U

void LCD_Init(void);
void LCD_SetRotation(uint8_t rotation);
uint16_t LCD_GetWidth(void);
uint16_t LCD_GetHeight(void);
void LCD_Fill(uint16_t color);
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void LCD_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void LCD_ShowString(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg);
void LCD_FormatSignedFixed(char *out, int32_t value, uint8_t decimals, const char *unit);
void LCD_FormatFrequency(char *out, uint32_t hz);
void LCD_FormatFrequencyFixed(char *out, uint32_t hz, uint8_t width);
void LCD_FormatFrequencyFixedLeft(char *out, uint32_t hz, uint8_t width);

#endif /* BSP_LCD_ILI9341_H */
