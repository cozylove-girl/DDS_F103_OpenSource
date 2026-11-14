#include "bsp_ad9834.h"
#include "project_config.h"
#include "spi.h"

#define AD9834_CTRL_B28       0x2000U
#define AD9834_CTRL_RESET     0x0100U
#define AD9834_CTRL_OPBITEN   0x0020U
#define AD9834_CTRL_DIV2      0x0008U
#define AD9834_CTRL_MODE      0x0002U
#define AD9834_FREQ0_WRITE    0x4000U
#define AD9834_PHASE0_WRITE   0xC000U

/* SN74LVC1G3157: S=0 selects B1. The board uses B1 for sine/triangle. */
#define DDS_ROUTE_ANALOG_LEVEL GPIO_PIN_RESET
#define DDS_ROUTE_SQUARE_LEVEL GPIO_PIN_SET

static uint32_t s_freq_hz = DDS_DEFAULT_FREQ_HZ;
static AD9834_Waveform_t s_wave = AD9834_WAVE_SINE;

static void dds_fsync_low(void)
{
  DDS_FSYNC_GPIO_Port->BRR = DDS_FSYNC_Pin;
}

static void dds_fsync_high(void)
{
  DDS_FSYNC_GPIO_Port->BSRR = DDS_FSYNC_Pin;
}

static void dds_write16(uint16_t word)
{
  dds_fsync_low();
  (void)HAL_SPI_Transmit(&hspi2, (uint8_t *)&word, 1U, 10U);
  while (__HAL_SPI_GET_FLAG(&hspi2, SPI_FLAG_BSY) != RESET)
  {
  }
  dds_fsync_high();
}

static uint32_t dds_calc_freq_word(uint32_t freq_hz)
{
  if (freq_hz < DDS_FREQ_MIN_HZ)
  {
    freq_hz = DDS_FREQ_MIN_HZ;
  }
  if (freq_hz > DDS_FREQ_MAX_HZ)
  {
    freq_hz = DDS_FREQ_MAX_HZ;
  }

  /* Force 64-bit math. 10 MHz * 2^28 overflows uint32_t. */
  return (uint32_t)(((uint64_t)freq_hz * (1ULL << 28)) / AD9834_MCLK_HZ);
}

void AD9834_SetRoute(AD9834_Route_t route)
{
  GPIO_PinState level = (route == AD9834_ROUTE_SQUARE) ?
                        DDS_ROUTE_SQUARE_LEVEL : DDS_ROUTE_ANALOG_LEVEL;
  HAL_GPIO_WritePin(WAVE_SEL_GPIO_Port, WAVE_SEL_Pin, level);
}

void AD9834_Init(void)
{
  dds_fsync_high();
  HAL_GPIO_WritePin(DDS_FSEL_GPIO_Port, DDS_FSEL_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(DDS_PSEL_GPIO_Port, DDS_PSEL_Pin, GPIO_PIN_RESET);

  HAL_GPIO_WritePin(DDS_RST_GPIO_Port, DDS_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(2);
  HAL_GPIO_WritePin(DDS_RST_GPIO_Port, DDS_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(2);

  dds_write16(AD9834_CTRL_B28 | AD9834_CTRL_RESET);
  AD9834_SetFrequency(s_freq_hz);
  dds_write16(AD9834_PHASE0_WRITE);
  AD9834_SetWaveform(s_wave);
}

void AD9834_SetFrequency(uint32_t freq_hz)
{
  uint32_t word = dds_calc_freq_word(freq_hz);
  s_freq_hz = freq_hz;

  dds_write16(AD9834_CTRL_B28 | AD9834_CTRL_RESET);
  dds_write16((uint16_t)(AD9834_FREQ0_WRITE | (word & 0x3FFFU)));
  dds_write16((uint16_t)(AD9834_FREQ0_WRITE | ((word >> 14) & 0x3FFFU)));
  AD9834_SetWaveform(s_wave);
}

void AD9834_SetWaveform(AD9834_Waveform_t wave)
{
  uint16_t ctrl = AD9834_CTRL_B28;
  s_wave = wave;

  if (wave == AD9834_WAVE_TRIANGLE)
  {
    ctrl |= AD9834_CTRL_MODE;
    AD9834_SetRoute(AD9834_ROUTE_ANALOG);
  }
  else if (wave == AD9834_WAVE_SQUARE)
  {
    ctrl |= AD9834_CTRL_OPBITEN | AD9834_CTRL_DIV2;
    AD9834_SetRoute(AD9834_ROUTE_SQUARE);
  }
  else
  {
    AD9834_SetRoute(AD9834_ROUTE_ANALOG);
  }

  dds_write16(ctrl);
}

uint32_t AD9834_GetFrequency(void)
{
  return s_freq_hz;
}

AD9834_Waveform_t AD9834_GetWaveform(void)
{
  return s_wave;
}
