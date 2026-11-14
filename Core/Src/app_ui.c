#include "app_ui.h"
#include "app_measure.h"
#include "app_sweep_plot.h"
#include "bsp_adc_dma.h"
#include "bsp_ad9834.h"
#include "bsp_input.h"
#include "bsp_lcd_ili9341.h"
#include "project_config.h"
#include <string.h>

typedef enum
{
  PAGE_MAIN = 0,
  PAGE_BODE,
  PAGE_WAVE_IDENTIFY
} UiPage_t;

static UiPage_t s_page = PAGE_MAIN;
static uint32_t s_freq = DDS_DEFAULT_FREQ_HZ;
static uint32_t s_step = 100U;
static AD9834_Waveform_t s_wave = AD9834_WAVE_SINE;
static uint32_t s_last_refresh;
static uint32_t s_last_wave_result_tick;
static uint16_t s_wave_buf[WAVE_SAMPLE_COUNT];
static WaveMeasureResult_t s_wave_result;
static char s_last_freq_text[16];
static char s_last_step_text[16];
static char s_last_wave_text[12];
static char s_last_gain_text[20];
static char s_last_input_text[20];
static char s_last_bode_peak_text[20];
static char s_last_bode_stat_text[20];
static char s_last_ident_text[16];
static char s_last_vpp_text[16];
static char s_last_wave_freq_text[16];
static char s_last_warn_text[16];
static WaveformId_t s_prev_ident_wave = WAVEFORM_UNKNOWN;
static uint8_t s_ident_change_count;
static uint8_t s_wave_no_freq_count;
static uint8_t s_bode_result_visible;
static uint8_t s_bode_cal_mode;

static const uint32_t s_steps[] = {1U, 10U, 100U, 1000U, 10000U, 100000U, 1000000U};
static uint8_t s_step_index = 2U;

static void show_main_page(void);
static void show_wave_identify_page(uint8_t start_adc);
static void draw_bode_wait_page(void);
static void enter_page(UiPage_t page);

static const char *wave_name(AD9834_Waveform_t wave)
{
  switch (wave)
  {
  case AD9834_WAVE_TRIANGLE: return "TRI";
  case AD9834_WAVE_SQUARE: return "SQ";
  case AD9834_WAVE_SINE:
  default: return "SINE";
  }
}

static const char *sweep_type_name(SweepFilterType_t type)
{
  switch (type)
  {
  case SWEEP_FILTER_LOWPASS: return "LPF";
  case SWEEP_FILTER_HIGHPASS: return "HPF";
  case SWEEP_FILTER_BANDPASS: return "BPF";
  case SWEEP_FILTER_PEAK: return "PK";
  case SWEEP_FILTER_UNKNOWN:
  default: return "UNK";
  }
}

static void apply_generator(void)
{
  AD9834_SetFrequency(s_freq);
  AD9834_SetWaveform(s_wave);
}

static uint8_t page_allows_generator_control(void)
{
  return (uint8_t)((s_page == PAGE_MAIN) || (s_page == PAGE_WAVE_IDENTIFY));
}

static void cycle_generator_waveform(void)
{
  if (s_wave == AD9834_WAVE_SINE)
  {
    s_wave = AD9834_WAVE_TRIANGLE;
  }
  else if (s_wave == AD9834_WAVE_TRIANGLE)
  {
    s_wave = AD9834_WAVE_SQUARE;
  }
  else
  {
    s_wave = AD9834_WAVE_SINE;
  }
  apply_generator();
}

static void update_wave_sample_rate(uint32_t measured_freq_hz)
{
  uint32_t current_rate;

  current_rate = ADC_GetWaveSampleRateHz();
  if (measured_freq_hz == 0UL)
  {
    if ((current_rate != WAVE_SAMPLE_RATE_LOW_HZ) && (s_wave_no_freq_count < 255U))
    {
      ++s_wave_no_freq_count;
    }
    if ((current_rate != WAVE_SAMPLE_RATE_LOW_HZ) && (s_wave_no_freq_count >= 4U))
    {
      s_wave_no_freq_count = 0U;
      ADC_SetWaveSampleRateHz(WAVE_SAMPLE_RATE_LOW_HZ);
    }
    return;
  }

  s_wave_no_freq_count = 0U;
  if ((current_rate != WAVE_SAMPLE_RATE_LOW_HZ) && (measured_freq_hz < WAVE_FREQ_LOW_BAND_HZ))
  {
    ADC_SetWaveSampleRateHz(WAVE_SAMPLE_RATE_LOW_HZ);
  }
  else if ((current_rate == WAVE_SAMPLE_RATE_LOW_HZ) && (measured_freq_hz > WAVE_FREQ_LOW_EXIT_HZ))
  {
    ADC_SetWaveSampleRateHz(WAVE_SAMPLE_RATE_MID_HZ);
  }
  else if ((current_rate == WAVE_SAMPLE_RATE_MID_HZ) && (measured_freq_hz > WAVE_FREQ_HIGH_ENTER_HZ))
  {
    ADC_SetWaveSampleRateHz(WAVE_SAMPLE_RATE_HIGH_HZ);
  }
  else if ((current_rate == WAVE_SAMPLE_RATE_HIGH_HZ) && (measured_freq_hz < WAVE_FREQ_HIGH_EXIT_HZ))
  {
    ADC_SetWaveSampleRateHz(WAVE_SAMPLE_RATE_MID_HZ);
  }
}

static void clear_field_cache(void)
{
  s_last_freq_text[0] = '\0';
  s_last_step_text[0] = '\0';
  s_last_wave_text[0] = '\0';
  s_last_gain_text[0] = '\0';
  s_last_input_text[0] = '\0';
  s_last_bode_peak_text[0] = '\0';
  s_last_bode_stat_text[0] = '\0';
  s_last_ident_text[0] = '\0';
  s_last_vpp_text[0] = '\0';
  s_last_wave_freq_text[0] = '\0';
  s_last_warn_text[0] = '\0';
}

static void format_fixed_text(char *out, const char *src, uint8_t width)
{
  uint8_t i = 0U;
  while ((src[i] != '\0') && (i < width))
  {
    out[i] = src[i];
    ++i;
  }
  while (i < width)
  {
    out[i++] = ' ';
  }
  out[width] = '\0';
}

static void update_text_field(uint16_t x, uint16_t y, char *last, const char *now, uint16_t color)
{
  if (strcmp(last, now) != 0)
  {
    LCD_ShowString(x, y, now, color, LCD_COLOR_BLACK);
    strcpy(last, now);
  }
}

static char *append_uint(char *out, uint32_t value)
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

static void format_uint_unit_fixed(char *out, uint32_t value, const char *unit, uint8_t width)
{
  char temp[20];
  char *p = temp;

  p = append_uint(p, value);
  while ((unit != 0) && (*unit != '\0'))
  {
    *p++ = *unit++;
  }
  *p = '\0';
  format_fixed_text(out, temp, width);
}

static void handle_keys(uint8_t events)
{
  uint8_t run_cal;

  if ((events & KEY_EVENT_MODE) != 0U)
  {
    if (s_page == PAGE_MAIN)
    {
      enter_page(PAGE_BODE);
    }
    else if (s_page == PAGE_BODE)
    {
      enter_page(PAGE_WAVE_IDENTIFY);
    }
    else
    {
      enter_page(PAGE_MAIN);
    }
  }

  if ((page_allows_generator_control() != 0U) && ((events & KEY_EVENT_ENC) != 0U))
  {
    s_step_index = (uint8_t)((s_step_index + 1U) % (sizeof(s_steps) / sizeof(s_steps[0])));
    s_step = s_steps[s_step_index];
  }
  else if ((s_page == PAGE_BODE) && ((events & KEY_EVENT_ENC) != 0U))
  {
    (void)Sweep_ToggleCorrection();
    enter_page(PAGE_BODE);
  }

  if ((page_allows_generator_control() != 0U) && ((events & KEY_EVENT_STEP) != 0U))
  {
    cycle_generator_waveform();
  }
  else if ((s_page == PAGE_BODE) && ((events & KEY_EVENT_STEP) != 0U))
  {
    s_bode_cal_mode = (s_bode_cal_mode == 0U) ? 1U : 0U;
    enter_page(PAGE_BODE);
  }

  if ((events & KEY_EVENT_SWEEP) != 0U)
  {
    run_cal = (s_page == PAGE_BODE) ? s_bode_cal_mode : 0U;
    s_bode_result_visible = 0U;
    Sweep_Start(run_cal);
    s_bode_cal_mode = 0U;
    s_page = PAGE_BODE;
  }
}

static void draw_main(void)
{
  char num[20];
  char field[20];
  char temp[20];
  SweepSummary_t summary;

  LCD_FormatFrequencyFixedLeft(field, s_freq, 12U);
  update_text_field(64U, 24U, s_last_freq_text, field, LCD_COLOR_WHITE);

  format_fixed_text(field, wave_name(s_wave), 10U);
  update_text_field(64U, 44U, s_last_wave_text, field, LCD_COLOR_WHITE);

  LCD_FormatFrequencyFixedLeft(field, s_step, 12U);
  update_text_field(64U, 64U, s_last_step_text, field, LCD_COLOR_WHITE);

  if (Sweep_GetSummary(&summary) != 0U)
  {
    const char *type = sweep_type_name(summary.filter_type);
    char *p;
    uint8_t i;

    format_fixed_text(field, type, 18U);
    update_text_field(64U, 104U, s_last_gain_text, field, LCD_COLOR_CYAN);

    p = temp;
    i = 0U;
    LCD_FormatFrequency(num, (summary.cutoff_found != 0U) ? summary.cutoff_low_hz : summary.peak_freq_hz);
    while ((num[i] != '\0') && (i < 11U))
    {
      *p++ = num[i++];
    }
    if ((summary.filter_type == SWEEP_FILTER_BANDPASS) && (summary.cutoff_found != 0U))
    {
      i = 0U;
      if ((uint8_t)(p - temp) < 18U)
      {
        *p++ = '-';
      }
      LCD_FormatFrequency(num, summary.cutoff_high_hz);
      while ((num[i] != '\0') && ((uint8_t)(p - temp) < 18U))
      {
        *p++ = num[i++];
      }
    }
    *p = '\0';
    format_fixed_text(field, temp, 18U);
    update_text_field(64U, 124U, s_last_input_text, field, LCD_COLOR_GREEN);

    LCD_FormatSignedFixed(num, summary.peak_gain_cdb, 2U, "DB");
    format_fixed_text(field, num, 18U);
    update_text_field(64U, 144U, s_last_bode_peak_text, field, LCD_COLOR_WHITE);
  }
  else
  {
    format_fixed_text(field, "N/A", 18U);
    update_text_field(64U, 104U, s_last_gain_text, field, LCD_COLOR_CYAN);
    update_text_field(64U, 124U, s_last_input_text, field, LCD_COLOR_GREEN);
    update_text_field(64U, 144U, s_last_bode_peak_text, field, LCD_COLOR_WHITE);
  }

  format_fixed_text(field, (Sweep_IsCalibrated() != 0U) ? "ON" : "OFF", 18U);
  update_text_field(64U, 164U, s_last_bode_stat_text, field, LCD_COLOR_YELLOW);
}

static void draw_wave_identify(void)
{
  char field[20];
  uint16_t warn_color = (s_wave_result.warn == WAVE_WARN_NONE) ? LCD_COLOR_GREEN : LCD_COLOR_YELLOW;

  LCD_FormatFrequencyFixedLeft(field, s_freq, 12U);
  update_text_field(52U, 48U, s_last_freq_text, field, LCD_COLOR_WHITE);

  format_fixed_text(field, wave_name(s_wave), 10U);
  update_text_field(52U, 68U, s_last_wave_text, field, LCD_COLOR_WHITE);

  LCD_FormatFrequencyFixedLeft(field, s_step, 12U);
  update_text_field(52U, 88U, s_last_step_text, field, LCD_COLOR_WHITE);

  format_fixed_text(field, Measure_WaveformName(s_wave_result.waveform), 12U);
  update_text_field(196U, 48U, s_last_ident_text, field, LCD_COLOR_WHITE);

  format_uint_unit_fixed(field, s_wave_result.ac_vpp_mv, "MV", 12U);
  update_text_field(196U, 88U, s_last_vpp_text, field, LCD_COLOR_WHITE);

  if (s_wave_result.freq_hz != 0UL)
  {
    LCD_FormatFrequencyFixedLeft(field, s_wave_result.freq_hz, 12U);
  }
  else
  {
    format_fixed_text(field, "N/A", 12U);
  }
  update_text_field(196U, 68U, s_last_wave_freq_text, field, LCD_COLOR_WHITE);

  format_fixed_text(field, Measure_WarnName(s_wave_result.warn), 12U);
  update_text_field(196U, 128U, s_last_warn_text, field, warn_color);
}

static void draw_wave_identify_static_layout(void)
{
  LCD_Fill(LCD_COLOR_BLACK);
  LCD_ShowString(4U, 4U, "WAVE", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
  LCD_ShowString(288U, 4U, "PA5", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
  LCD_DrawLine(126, 24, 126, 214, LCD_COLOR_GRAY);

  LCD_ShowString(4U, 24U, "DDS", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
  LCD_ShowString(136U, 24U, "IN", LCD_COLOR_CYAN, LCD_COLOR_BLACK);

  LCD_ShowString(4U, 48U, "FREQ:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(4U, 68U, "WAVE:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(4U, 88U, "STEP:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);

  LCD_ShowString(136U, 48U, "WAVE:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(136U, 68U, "FIN :", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(136U, 88U, "VPP :", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(136U, 128U, "WARN:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);

}

static void draw_static_layout(void)
{
  LCD_Fill(LCD_COLOR_BLACK);
  LCD_ShowString(4U, 4U, "STM32 DDS", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
#if INPUT_HARDWARE_ENABLE
  LCD_ShowString(250U, 4U, "IN", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
#else
  LCD_ShowString(220U, 4U, "LCD", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
#endif
  LCD_ShowString(4U, 24U, "FREQ :", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(4U, 44U, "WAVE :", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(4U, 64U, "STEP :", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(4U, 92U, "LAST BODE", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
  LCD_ShowString(4U, 104U, "TYPE :", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(4U, 124U, "FREQ :", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(4U, 144U, "PEAK :", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(4U, 164U, "CORR :", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
}

static void draw_bode_wait_page(void)
{
  LCD_Fill(LCD_COLOR_BLACK);
  LCD_ShowString(4U, 4U, "BODE", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
  LCD_ShowString(72U, 4U, "NEXT:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(116U, 4U, (s_bode_cal_mode == 0U) ? "MEAS" : "CAL", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
  LCD_ShowString(172U, 4U, "CORR:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(216U, 4U, (Sweep_IsCalibrated() != 0U) ? "ON" : "OFF", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
  LCD_DrawRect(24U, 32U, 292U, 180U, LCD_COLOR_GRAY);
  LCD_ShowString(44U, 92U, (s_bode_cal_mode == 0U) ? "SWEEP:MEAS" : "SWEEP:CAL", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
  LCD_ShowString(44U, 116U, "STEP:NEXT", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(44U, 140U, "ENC:CORR", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
}

static void show_main_page(void)
{
  s_page = PAGE_MAIN;
  s_bode_result_visible = 0U;
  ADC_StartBodeMode();
  clear_field_cache();
  draw_static_layout();
  draw_main();
}

static void reset_wave_identify_state(void)
{
  s_prev_ident_wave = WAVEFORM_UNKNOWN;
  s_ident_change_count = 0U;
  s_wave_no_freq_count = 0U;
  s_last_wave_result_tick = 0U;
  s_wave_result.ac_vpp_mv = 0U;
  s_wave_result.freq_hz = 0UL;
  s_wave_result.waveform = WAVEFORM_UNKNOWN;
  s_wave_result.warn = WAVE_WARN_NONE;
}

static void show_wave_identify_page(uint8_t start_adc)
{
  s_page = PAGE_WAVE_IDENTIFY;
  s_bode_result_visible = 0U;
  clear_field_cache();
  reset_wave_identify_state();
  ADC_SetWaveSampleRateHz(WAVE_SAMPLE_RATE_LOW_HZ);
  draw_wave_identify_static_layout();
  draw_wave_identify();
  if (start_adc != 0U)
  {
    /* Page switching only changes the ADC path; DDS keeps its last user output. */
    ADC_StartWaveformMode();
  }
}

static void enter_page(UiPage_t page)
{
  if (page == PAGE_WAVE_IDENTIFY)
  {
    show_wave_identify_page(1U);
  }
  else if (page == PAGE_BODE)
  {
    ADC_StartBodeMode();
    s_page = PAGE_BODE;
    s_bode_result_visible = Sweep_HasResult();
    clear_field_cache();
    if (s_bode_result_visible != 0U)
    {
      Sweep_RedrawResult();
      LCD_ShowString(220U, 228U, (s_bode_cal_mode == 0U) ? "N:MEAS" : "N:CAL", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
      LCD_ShowString(272U, 228U, (Sweep_IsCalibrated() != 0U) ? "C:ON" : "C:OFF", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
    }
    else
    {
      draw_bode_wait_page();
    }
  }
  else
  {
    show_main_page();
  }
}

void UI_Init(void)
{
  s_freq = DDS_DEFAULT_FREQ_HZ;
  s_step = s_steps[s_step_index];
  s_wave = AD9834_WAVE_SINE;
  apply_generator();
  show_main_page();
}

void UI_Task(void)
{
  int16_t diff = Encoder_GetDiff();
  uint8_t events = Key_Scan();
  uint32_t now = HAL_GetTick();

  if (Sweep_IsBusy() != 0U)
  {
    if ((events & KEY_EVENT_SWEEP) != 0U)
    {
      Sweep_Abort();
      s_bode_result_visible = 0U;
      apply_generator();
      ADC_StartBodeMode();
      enter_page(PAGE_BODE);
      return;
    }
    if ((events & KEY_EVENT_MODE) != 0U)
    {
      Sweep_Abort();
      s_bode_result_visible = 0U;
      apply_generator();
      enter_page(PAGE_WAVE_IDENTIFY);
      return;
    }
    Sweep_Task();
    if (Sweep_IsBusy() == 0U)
    {
      apply_generator();
      ADC_StartBodeMode();
      s_bode_result_visible = Sweep_HasResult();
      s_bode_cal_mode = 0U;
      enter_page(PAGE_BODE);
    }
    return;
  }

  if ((diff != 0) && (s_page == PAGE_BODE) && (s_bode_result_visible != 0U))
  {
    (void)Sweep_MoveCursor(diff);
    diff = 0;
  }

  if ((diff != 0) && (page_allows_generator_control() != 0U))
  {
    int32_t next = (int32_t)s_freq + ((int32_t)diff * (int32_t)s_step);
    if (next < (int32_t)DDS_FREQ_MIN_HZ)
    {
      next = DDS_FREQ_MIN_HZ;
    }
    if (next > (int32_t)DDS_FREQ_MAX_HZ)
    {
      next = DDS_FREQ_MAX_HZ;
    }
    s_freq = (uint32_t)next;
    apply_generator();
  }

  handle_keys(events);

  if (s_page == PAGE_WAVE_IDENTIFY)
  {
    if (((now - s_last_wave_result_tick) >= UI_FIELD_REFRESH_MS) &&
        (ADC_GetWaveSnapshot(s_wave_buf, WAVE_SAMPLE_COUNT) != 0U))
    {
      s_last_wave_result_tick = now;
      s_wave_result = Measure_IdentifyWaveform(s_wave_buf, WAVE_SAMPLE_COUNT, ADC_GetWaveSampleRateHz());
      update_wave_sample_rate(s_wave_result.freq_hz);
      if (s_wave_result.warn == WAVE_WARN_NONE)
      {
        if ((s_prev_ident_wave != WAVEFORM_UNKNOWN) &&
            (s_wave_result.waveform != WAVEFORM_UNKNOWN) &&
            (s_wave_result.waveform != s_prev_ident_wave))
        {
          if (s_ident_change_count < 255U)
          {
            ++s_ident_change_count;
          }
        }
        else if (s_ident_change_count != 0U)
        {
          --s_ident_change_count;
        }

        if (s_ident_change_count >= 3U)
        {
          s_wave_result.waveform = WAVEFORM_UNKNOWN;
          s_wave_result.warn = WAVE_WARN_UNSTABLE;
        }
      }
      s_prev_ident_wave = s_wave_result.waveform;
    }
  }

  if (((now - s_last_refresh) >= UI_REFRESH_MS) || (diff != 0) || (events != 0U))
  {
    s_last_refresh = now;
    if (s_page == PAGE_MAIN)
    {
      draw_main();
    }
    else if (s_page == PAGE_WAVE_IDENTIFY)
    {
      draw_wave_identify();
    }
  }
}
