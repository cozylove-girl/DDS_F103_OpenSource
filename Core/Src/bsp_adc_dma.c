#include "bsp_adc_dma.h"
#include "adc.h"

static ADC_AppMode_t s_mode = ADC_APP_MODE_IDLE;
static uint16_t s_wave_dma[WAVE_DMA_BUFFER_COUNT];
static uint16_t s_wave_snapshot[WAVE_SAMPLE_COUNT];
static volatile uint8_t s_snapshot_ready;
static TIM_HandleTypeDef s_wave_tim3;
static uint32_t s_wave_sample_rate_hz = WAVE_SAMPLE_RATE_LOW_HZ;

static void adc_stop_all(void)
{
  if (s_wave_tim3.Instance != 0)
  {
    (void)HAL_TIM_Base_Stop(&s_wave_tim3);
  }
  (void)HAL_ADC_Stop_DMA(&hadc1);
  (void)HAL_ADC_Stop(&hadc1);
}

static uint32_t tim3_clock_hz(void)
{
  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
  if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1)
  {
    pclk1 *= 2U;
  }
  return pclk1;
}

static void wave_tim3_config(uint32_t sample_rate_hz)
{
  TIM_MasterConfigTypeDef master;
  uint32_t timer_clk;
  uint32_t period;

  if (sample_rate_hz == 0U)
  {
    sample_rate_hz = WAVE_SAMPLE_RATE_MID_HZ;
  }

  __HAL_RCC_TIM3_CLK_ENABLE();
  timer_clk = tim3_clock_hz();
  period = (timer_clk + (sample_rate_hz / 2U)) / sample_rate_hz;
  if (period < 2U)
  {
    period = 2U;
  }
  if (period > 65536U)
  {
    period = 65536U;
  }

  s_wave_tim3.Instance = TIM3;
  s_wave_tim3.Init.Prescaler = 0U;
  s_wave_tim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  s_wave_tim3.Init.Period = period - 1U;
  s_wave_tim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  s_wave_tim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&s_wave_tim3) != HAL_OK)
  {
    Error_Handler();
  }

  master.MasterOutputTrigger = TIM_TRGO_UPDATE;
  master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&s_wave_tim3, &master) != HAL_OK)
  {
    Error_Handler();
  }

  s_wave_sample_rate_hz = timer_clk / period;
}

static void adc_config_single(uint32_t channel, uint32_t sample_time, FunctionalState continuous,
                              uint32_t external_trigger)
{
  ADC_ChannelConfTypeDef cfg = {0};
  adc_stop_all();

  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = continuous;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = external_trigger;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1U;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  cfg.Channel = channel;
  cfg.Rank = ADC_REGULAR_RANK_1;
  cfg.SamplingTime = sample_time;
  if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK)
  {
    Error_Handler();
  }
}

void ADC_DMA_AppInit(void)
{
  HAL_ADCEx_Calibration_Start(&hadc1);
  ADC_StartBodeMode();
}

void ADC_StartBodeMode(void)
{
  adc_stop_all();
  s_mode = ADC_APP_MODE_BODE;
}

void ADC_StartWaveformMode(void)
{
  s_snapshot_ready = 0U;
  wave_tim3_config(s_wave_sample_rate_hz);
  adc_config_single(ADC_CHANNEL_5, ADC_SAMPLETIME_1CYCLE_5, DISABLE, ADC_EXTERNALTRIGCONV_T3_TRGO);
  s_mode = ADC_APP_MODE_WAVEFORM;
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_wave_dma, WAVE_DMA_BUFFER_COUNT) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_Base_Start(&s_wave_tim3) != HAL_OK)
  {
    Error_Handler();
  }
}

uint32_t ADC_GetWaveSampleRateHz(void)
{
  return s_wave_sample_rate_hz;
}

void ADC_SetWaveSampleRateHz(uint32_t sample_rate_hz)
{
  if (sample_rate_hz == 0U)
  {
    sample_rate_hz = WAVE_SAMPLE_RATE_MID_HZ;
  }

  if (s_wave_sample_rate_hz == sample_rate_hz)
  {
    return;
  }

  s_wave_sample_rate_hz = sample_rate_hz;
  if (s_mode == ADC_APP_MODE_WAVEFORM)
  {
    ADC_StartWaveformMode();
  }
}

uint16_t ADC_ReadChannelRaw(uint32_t channel, uint32_t sample_time)
{
  uint16_t value = 0U;
  uint8_t i;
  adc_config_single(channel, sample_time, DISABLE, ADC_SOFTWARE_START);

  /* STM32F1 ADC keeps charge from the previous channel. Discard several
   * conversions so PA4/DUT cannot inherit PA3/REF when the DUT path is open
   * or high impedance.
   */
  for (i = 0U; i < 4U; ++i)
  {
    if (HAL_ADC_Start(&hadc1) == HAL_OK)
    {
      if (HAL_ADC_PollForConversion(&hadc1, 5U) == HAL_OK)
      {
        value = (uint16_t)HAL_ADC_GetValue(&hadc1);
      }
      (void)HAL_ADC_Stop(&hadc1);
    }
  }
  s_mode = ADC_APP_MODE_BODE;
  return value;
}

void ADC_ReadBodeAverage(uint16_t samples, uint16_t *ref_raw, uint16_t *dut_raw)
{
  uint32_t ch3_sum = 0U;
  uint32_t ch4_sum = 0U;
  uint16_t ch3_avg;
  uint16_t ch4_avg;
  uint16_t i;
  if (samples == 0U)
  {
    samples = 1U;
  }
  if (s_mode != ADC_APP_MODE_BODE)
  {
    ADC_StartBodeMode();
  }

  for (i = 0U; i < samples; ++i)
  {
    ch3_sum += ADC_ReadChannelRaw(ADC_CHANNEL_3, ADC_SAMPLETIME_71CYCLES_5);
    ch4_sum += ADC_ReadChannelRaw(ADC_CHANNEL_4, ADC_SAMPLETIME_71CYCLES_5);
  }

  ch3_avg = (uint16_t)(ch3_sum / samples);
  ch4_avg = (uint16_t)(ch4_sum / samples);

  if (ref_raw != 0)
  {
#if BODE_SWAP_REF_DUT
    *ref_raw = ch4_avg;
#else
    *ref_raw = ch3_avg;
#endif
  }
  if (dut_raw != 0)
  {
#if BODE_SWAP_REF_DUT
    *dut_raw = ch3_avg;
#else
    *dut_raw = ch4_avg;
#endif
  }
}

uint16_t ADC_RawToMv(uint16_t raw)
{
  return (uint16_t)(((uint32_t)raw * ADC_REF_MV + 2047U) / 4095U);
}

uint8_t ADC_GetWaveSnapshot(uint16_t *out, uint16_t max_count)
{
  uint16_t i;
  if ((s_snapshot_ready == 0U) || (out == 0) || (max_count < WAVE_SAMPLE_COUNT))
  {
    return 0U;
  }
  __disable_irq();
  s_snapshot_ready = 0U;
  for (i = 0U; i < WAVE_SAMPLE_COUNT; ++i)
  {
    out[i] = s_wave_snapshot[i];
  }
  __enable_irq();
  return 1U;
}

ADC_AppMode_t ADC_GetAppMode(void)
{
  return s_mode;
}

void ADC_Task(void)
{
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  uint16_t i;
  if ((hadc->Instance == ADC1) && (s_mode == ADC_APP_MODE_WAVEFORM))
  {
    for (i = 0U; i < WAVE_SAMPLE_COUNT; ++i)
    {
      s_wave_snapshot[i] = s_wave_dma[i];
    }
    s_snapshot_ready = 1U;
  }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  uint16_t i;
  if ((hadc->Instance == ADC1) && (s_mode == ADC_APP_MODE_WAVEFORM))
  {
    for (i = 0U; i < WAVE_SAMPLE_COUNT; ++i)
    {
      s_wave_snapshot[i] = s_wave_dma[WAVE_SAMPLE_COUNT + i];
    }
    s_snapshot_ready = 1U;
  }
}
