#include "bsp_lcd_ili9341.h"
#include "font.h"
#include "project_config.h"

static uint16_t s_lcd_w = LCD_WIDTH;
static uint16_t s_lcd_h = LCD_HEIGHT;

#define LCD_CS_LOW()    (LCD_CS_GPIO_Port->BRR = LCD_CS_Pin)
#define LCD_CS_HIGH()   (LCD_CS_GPIO_Port->BSRR = LCD_CS_Pin)
#define LCD_DC_LOW()    (LCD_DC_GPIO_Port->BRR = LCD_DC_Pin)
#define LCD_DC_HIGH()   (LCD_DC_GPIO_Port->BSRR = LCD_DC_Pin)
#define LCD_RST_LOW()   (LCD_RST_GPIO_Port->BRR = LCD_RST_Pin)
#define LCD_RST_HIGH()  (LCD_RST_GPIO_Port->BSRR = LCD_RST_Pin)
#define LCD_SCK_LOW()   (LCD_SCK_GPIO_Port->BRR = LCD_SCK_Pin)
#define LCD_SCK_HIGH()  (LCD_SCK_GPIO_Port->BSRR = LCD_SCK_Pin)
#define LCD_MOSI_LOW()  (LCD_MOSI_GPIO_Port->BRR = LCD_MOSI_Pin)
#define LCD_MOSI_HIGH() (LCD_MOSI_GPIO_Port->BSRR = LCD_MOSI_Pin)

static void lcd_spi_delay(void)
{
#if LCD_SOFT_SPI_DELAY_CYCLES > 0U
  volatile uint8_t i;
  for (i = 0U; i < LCD_SOFT_SPI_DELAY_CYCLES; ++i)
  {
    __NOP();
  }
#endif
}

static void lcd_write8(uint8_t data)
{
  uint8_t mask;
  for (mask = 0x80U; mask != 0U; mask >>= 1)
  {
    if ((data & mask) != 0U)
    {
      LCD_MOSI_HIGH();
    }
    else
    {
      LCD_MOSI_LOW();
    }
    lcd_spi_delay();
    LCD_SCK_HIGH();
    lcd_spi_delay();
    LCD_SCK_LOW();
    lcd_spi_delay();
  }
}

static void lcd_cmd(uint8_t cmd)
{
  LCD_CS_LOW();
  LCD_DC_LOW();
  lcd_write8(cmd);
  LCD_CS_HIGH();
}

static void lcd_data8(uint8_t data)
{
  LCD_CS_LOW();
  LCD_DC_HIGH();
  lcd_write8(data);
  LCD_CS_HIGH();
}

static void lcd_data16(uint16_t data)
{
  LCD_CS_LOW();
  LCD_DC_HIGH();
  lcd_write8((uint8_t)(data >> 8));
  lcd_write8((uint8_t)data);
  LCD_CS_HIGH();
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  lcd_cmd(0x2A);
  lcd_data16(x0);
  lcd_data16(x1);
  lcd_cmd(0x2B);
  lcd_data16(y0);
  lcd_data16(y1);
  lcd_cmd(0x2C);
}

void LCD_SetRotation(uint8_t rotation)
{
  uint8_t madctl;
  rotation &= 3U;
  if ((rotation & 1U) != 0U)
  {
    s_lcd_w = LCD_HEIGHT;
    s_lcd_h = LCD_WIDTH;
  }
  else
  {
    s_lcd_w = LCD_WIDTH;
    s_lcd_h = LCD_HEIGHT;
  }

  switch (rotation)
  {
  case 1: madctl = LCD_MADCTL_ROT1; break;
  case 2: madctl = LCD_MADCTL_ROT2; break;
  case 3: madctl = LCD_MADCTL_ROT3; break;
  case 0:
  default: madctl = LCD_MADCTL_ROT0; break;
  }
  lcd_cmd(0x36);
  lcd_data8(madctl);
}

uint16_t LCD_GetWidth(void)
{
  return s_lcd_w;
}

uint16_t LCD_GetHeight(void)
{
  return s_lcd_h;
}

void LCD_Init(void)
{
  HAL_GPIO_WritePin(LCD_BLK_GPIO_Port, LCD_BLK_Pin, GPIO_PIN_SET);
  LCD_CS_HIGH();
  LCD_SCK_LOW();
  LCD_MOSI_LOW();

  LCD_RST_LOW();
  HAL_Delay(50);
  LCD_RST_HIGH();
  HAL_Delay(120);

  /* ILI9341/MSP2807 init sequence adapted from the vendor demo, but the bus
   * and pins are rewritten for this project.
   */
  lcd_cmd(0xCF); lcd_data8(0x00); lcd_data8(0xD9); lcd_data8(0x30);
  lcd_cmd(0xED); lcd_data8(0x64); lcd_data8(0x03); lcd_data8(0x12); lcd_data8(0x81);
  lcd_cmd(0xE8); lcd_data8(0x85); lcd_data8(0x10); lcd_data8(0x7A);
  lcd_cmd(0xCB); lcd_data8(0x39); lcd_data8(0x2C); lcd_data8(0x00); lcd_data8(0x34); lcd_data8(0x02);
  lcd_cmd(0xF7); lcd_data8(0x20);
  lcd_cmd(0xEA); lcd_data8(0x00); lcd_data8(0x00);
  lcd_cmd(0xC0); lcd_data8(0x1B);
  lcd_cmd(0xC1); lcd_data8(0x12);
  lcd_cmd(0xC5); lcd_data8(0x08); lcd_data8(0x26);
  lcd_cmd(0xC7); lcd_data8(0xB7);
  lcd_cmd(0x3A); lcd_data8(0x55);
  lcd_cmd(0xB1); lcd_data8(0x00); lcd_data8(0x1A);
  lcd_cmd(0xB6); lcd_data8(0x0A); lcd_data8(0x22);
  lcd_cmd(0xF2); lcd_data8(0x00);
  lcd_cmd(0x26); lcd_data8(0x01);
  lcd_cmd(0xE0);
  lcd_data8(0x0F); lcd_data8(0x1D); lcd_data8(0x1A); lcd_data8(0x0A);
  lcd_data8(0x0D); lcd_data8(0x07); lcd_data8(0x49); lcd_data8(0x66);
  lcd_data8(0x3B); lcd_data8(0x07); lcd_data8(0x11); lcd_data8(0x01);
  lcd_data8(0x09); lcd_data8(0x05); lcd_data8(0x04);
  lcd_cmd(0xE1);
  lcd_data8(0x00); lcd_data8(0x18); lcd_data8(0x1D); lcd_data8(0x02);
  lcd_data8(0x0F); lcd_data8(0x04); lcd_data8(0x36); lcd_data8(0x13);
  lcd_data8(0x4C); lcd_data8(0x07); lcd_data8(0x13); lcd_data8(0x0F);
  lcd_data8(0x2E); lcd_data8(0x2F); lcd_data8(0x05);
  LCD_SetRotation(LCD_DEFAULT_ROTATION);
  lcd_cmd(0x11);
  HAL_Delay(120);
  lcd_cmd(0x29);
  LCD_Fill(LCD_COLOR_WHITE);
}

void LCD_Fill(uint16_t color)
{
  LCD_FillRect(0U, 0U, s_lcd_w, s_lcd_h, color);
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  uint32_t total;
  uint16_t x1;
  uint16_t y1;

  if ((x >= s_lcd_w) || (y >= s_lcd_h) || (w == 0U) || (h == 0U))
  {
    return;
  }
  if ((x + w) > s_lcd_w)
  {
    w = (uint16_t)(s_lcd_w - x);
  }
  if ((y + h) > s_lcd_h)
  {
    h = (uint16_t)(s_lcd_h - y);
  }

  x1 = (uint16_t)(x + w - 1U);
  y1 = (uint16_t)(y + h - 1U);
  total = (uint32_t)w * h;

  lcd_set_window(x, y, x1, y1);
  LCD_CS_LOW();
  LCD_DC_HIGH();
  while (total-- != 0U)
  {
    lcd_write8((uint8_t)(color >> 8));
    lcd_write8((uint8_t)color);
  }
  LCD_CS_HIGH();
}

void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  if ((x >= s_lcd_w) || (y >= s_lcd_h))
  {
    return;
  }
  lcd_set_window(x, y, x, y);
  lcd_data16(color);
}

void LCD_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
  int16_t dx = (x0 < x1) ? (x1 - x0) : (x0 - x1);
  int16_t sx = (x0 < x1) ? 1 : -1;
  int16_t dy = (y0 < y1) ? (y0 - y1) : (y1 - y0);
  int16_t sy = (y0 < y1) ? 1 : -1;
  int16_t err = dx + dy;

  while (1)
  {
    if ((x0 >= 0) && (y0 >= 0))
    {
      LCD_DrawPixel((uint16_t)x0, (uint16_t)y0, color);
    }
    if ((x0 == x1) && (y0 == y1))
    {
      break;
    }
    if ((int16_t)(2 * err) >= dy)
    {
      err += dy;
      x0 += sx;
    }
    if ((int16_t)(2 * err) <= dx)
    {
      err += dx;
      y0 += sy;
    }
  }
}

void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  LCD_DrawLine((int16_t)x, (int16_t)y, (int16_t)(x + w - 1U), (int16_t)y, color);
  LCD_DrawLine((int16_t)x, (int16_t)(y + h - 1U), (int16_t)(x + w - 1U), (int16_t)(y + h - 1U), color);
  LCD_DrawLine((int16_t)x, (int16_t)y, (int16_t)x, (int16_t)(y + h - 1U), color);
  LCD_DrawLine((int16_t)(x + w - 1U), (int16_t)y, (int16_t)(x + w - 1U), (int16_t)(y + h - 1U), color);
}

static void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg)
{
  uint8_t cols[5];
  uint8_t col;
  uint8_t row;
  Font5x7_Get(c, cols);

  /* One window for the whole 6×8 glyph, then burst-write 48 pixels.
   * ILI9341 GRAM auto-increments column-first, so output row-major. */
  lcd_set_window(x, y, (uint16_t)(x + 5U), (uint16_t)(y + 7U));
  LCD_CS_LOW();
  LCD_DC_HIGH();
  for (row = 0U; row < 8U; ++row)
  {
    for (col = 0U; col < 6U; ++col)
    {
      uint8_t bits = (col < 5U) ? cols[col] : 0U;
      uint16_t color = ((bits & (1U << row)) != 0U) ? fg : bg;
      lcd_write8((uint8_t)(color >> 8));
      lcd_write8((uint8_t)color);
    }
  }
  LCD_CS_HIGH();
}

void LCD_ShowString(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg)
{
  while ((s != 0) && (*s != '\0'))
  {
    if ((x + 6U) >= s_lcd_w)
    {
      x = 0U;
      y = (uint16_t)(y + 9U);
    }
    if ((y + 8U) >= s_lcd_h)
    {
      return;
    }
    lcd_draw_char(x, y, *s, fg, bg);
    x = (uint16_t)(x + 6U);
    ++s;
  }
}

static char *lcd_append_uint(char *out, uint32_t value)
{
  char rev[10];
  uint8_t n = 0U;

  if (value == 0U)
  {
    *out++ = '0';
    return out;
  }
  while ((value != 0U) && (n < sizeof(rev)))
  {
    rev[n++] = (char)('0' + (value % 10U));
    value /= 10U;
  }
  while (n != 0U)
  {
    *out++ = rev[--n];
  }
  return out;
}

void LCD_FormatSignedFixed(char *out, int32_t value, uint8_t decimals, const char *unit)
{
  uint32_t scale = 1U;
  uint8_t i;
  char *p = out;
  if (value < 0)
  {
    *p++ = '-';
    value = -value;
  }
  for (i = 0U; i < decimals; ++i)
  {
    scale *= 10U;
  }
  p = lcd_append_uint(p, (uint32_t)value / scale);
  if (decimals != 0U)
  {
    uint32_t frac = (uint32_t)value % scale;
    *p++ = '.';
    if (decimals == 2U)
    {
      *p++ = (char)('0' + ((frac / 10U) % 10U));
      *p++ = (char)('0' + (frac % 10U));
    }
    else
    {
      *p++ = (char)('0' + (frac % 10U));
    }
  }
  while ((unit != 0) && (*unit != '\0'))
  {
    *p++ = *unit++;
  }
  *p = '\0';
}

void LCD_FormatFrequency(char *out, uint32_t hz)
{
  if (hz >= 1000000UL)
  {
    uint32_t centi = (hz + 5000UL) / 10000UL;
    LCD_FormatSignedFixed(out, (int32_t)centi, 2U, "MHZ");
  }
  else if (hz >= 1000UL)
  {
    uint32_t centi = (hz + 5UL) / 10UL;
    LCD_FormatSignedFixed(out, (int32_t)centi, 2U, "KHZ");
  }
  else
  {
    char *p = lcd_append_uint(out, hz);
    *p++ = 'H';
    *p++ = 'Z';
    *p = '\0';
  }
}

void LCD_FormatFrequencyFixed(char *out, uint32_t hz, uint8_t width)
{
  char temp[20];
  const char *src;
  uint8_t len = 0U;
  uint8_t pad;
  LCD_FormatFrequency(temp, hz);
  while (temp[len] != '\0')
  {
    ++len;
  }

  pad = (width > len) ? (uint8_t)(width - len) : 0U;
  while (pad-- != 0U)
  {
    *out++ = ' ';
  }
  src = temp;
  while (*src != '\0')
  {
    *out++ = *src++;
  }
  *out = '\0';
}

void LCD_FormatFrequencyFixedLeft(char *out, uint32_t hz, uint8_t width)
{
  char temp[20];
  uint8_t i = 0U;

  LCD_FormatFrequency(temp, hz);
  while ((temp[i] != '\0') && (i < width))
  {
    out[i] = temp[i];
    ++i;
  }
  while (i < width) 
  {
     out[i++] = ' ';
  }
  out[width] = '\0';
}
