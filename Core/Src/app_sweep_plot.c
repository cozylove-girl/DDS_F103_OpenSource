#include "app_sweep_plot.h"
#include "app_measure.h"
#include "bsp_adc_dma.h"
#include "bsp_ad9834.h"
#include "bsp_lcd_ili9341.h"
#include "project_config.h"

typedef struct
{
  uint32_t freq_hz;
  int16_t gain_cdb;
  uint16_t ref_mv;
  uint16_t dut_mv;
} SweepPoint_t;

static SweepState_t s_state = SWEEP_IDLE;
static uint8_t s_index;
static uint8_t s_point_count;
static uint32_t s_freq;
static uint32_t s_settle_tick;
static int16_t s_last_gain_cdb;
static uint32_t s_last_freq;
static uint16_t s_last_ref_mv;
static uint16_t s_last_dut_mv;
static uint8_t s_done_reason;
static uint8_t s_result_valid;
static uint8_t s_cursor_index;
static uint8_t s_cursor_visible;
static uint16_t s_cursor_pos_q2;
static uint16_t s_cursor_draw_pos_q2;
static int16_t s_cursor_draw_x;
static int16_t s_cursor_draw_y;
static uint8_t s_run_calibration;
static uint8_t s_cal_valid;
static uint8_t s_cal_enabled;
static SweepSummary_t s_summary;
static SweepPoint_t s_points[SWEEP_MAX_POINTS];
static int16_t s_cal_error_cdb[SWEEP_MAX_POINTS];

static void analyze_cutoff(void);
static void draw_interpolated_segment(int16_t x0, int16_t y0, int16_t g0,
                                      int16_t x1, int16_t y1, int16_t g1);

enum { CURSOR_SUBSTEPS = 4U };

static const uint16_t s_log16_ratio_x1000[16] =
{
  1000U, 1155U, 1334U, 1540U,
  1778U, 2054U, 2371U, 2738U,
  3162U, 3652U, 4217U, 4870U,
  5623U, 6494U, 7499U, 8660U
};

static const uint32_t s_decade_base_hz[6] =
{
  100UL, 1000UL, 10000UL, 100000UL, 1000000UL, 10000000UL
};

static const char *filter_type_name(SweepFilterType_t type)
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

static int16_t gain_to_y(int16_t gain_cdb)
{
  int32_t min_cdb = -8000L;
  int32_t max_cdb = 2000L;
  int32_t plot_top = 36L;
  int32_t plot_h = 170L;
  int32_t y;
  if (gain_cdb < min_cdb) gain_cdb = (int16_t)min_cdb;
  if (gain_cdb > max_cdb) gain_cdb = (int16_t)max_cdb;
  y = plot_top + ((max_cdb - gain_cdb) * plot_h) / (max_cdb - min_cdb);
  return (int16_t)y;
}

static int16_t index_to_x(uint8_t index)
{
  return (int16_t)(28U + (((uint32_t)index * 280U) / (SWEEP_MAX_POINTS - 1U)));
}

static uint32_t sweep_freq_for_index(uint8_t index)
{
  uint8_t decade = (uint8_t)(index / 16U);
  uint8_t step = (uint8_t)(index % 16U);

  if ((index >= (SWEEP_MAX_POINTS - 1U)) || (decade >= 5U))
  {
    return SWEEP_STOP_FREQ_HZ;
  }
  return (uint32_t)(((uint64_t)s_decade_base_hz[decade] *
                     (uint64_t)s_log16_ratio_x1000[step] + 500ULL) / 1000ULL);
}

static void draw_point_marker(int16_t x, int16_t y, uint16_t color)
{
  if ((x <= 24) || (x >= 315) || (y <= 32) || (y >= 212))
  {
    return;
  }
  LCD_DrawPixel((uint16_t)x, (uint16_t)y, color);
  LCD_DrawPixel((uint16_t)(x - 1), (uint16_t)y, color);
  LCD_DrawPixel((uint16_t)(x + 1), (uint16_t)y, color);
  LCD_DrawPixel((uint16_t)x, (uint16_t)(y - 1), color);
  LCD_DrawPixel((uint16_t)x, (uint16_t)(y + 1), color);
}

static uint8_t point_is_valid(uint8_t index)
{
  return (uint8_t)((index < s_point_count) && (s_points[index].freq_hz != 0U));
}

static uint16_t cursor_max_pos(void)
{
  if (s_point_count == 0U)
  {
    return 0U;
  }
  return (uint16_t)((uint16_t)(s_point_count - 1U) * CURSOR_SUBSTEPS);
}

static void cursor_set_index(uint8_t index)
{
  s_cursor_index = index;
  s_cursor_pos_q2 = (uint16_t)((uint16_t)index * CURSOR_SUBSTEPS);
}

static void cursor_update_nearest_index(void)
{
  uint16_t index;

  if (s_point_count == 0U)
  {
    s_cursor_index = 0U;
    return;
  }
  index = (uint16_t)((s_cursor_pos_q2 + (CURSOR_SUBSTEPS / 2U)) / CURSOR_SUBSTEPS);
  if (index >= s_point_count)
  {
    index = (uint16_t)(s_point_count - 1U);
  }
  s_cursor_index = (uint8_t)index;
}

static int32_t lerp_i32(int32_t a, int32_t b, uint8_t frac)
{
  return (int32_t)(a + (((b - a) * (int32_t)frac) / (int32_t)CURSOR_SUBSTEPS));
}

static uint8_t cursor_get_sample(uint32_t *freq_hz, int16_t *gain_cdb,
                                 uint16_t *ref_mv, uint16_t *dut_mv,
                                 int16_t *x, int16_t *y)
{
  uint8_t a;
  uint8_t b;
  uint8_t frac;
  int16_t gain;

  if ((s_result_valid == 0U) || (s_point_count == 0U))
  {
    return 0U;
  }

  a = (uint8_t)(s_cursor_pos_q2 / CURSOR_SUBSTEPS);
  frac = (uint8_t)(s_cursor_pos_q2 % CURSOR_SUBSTEPS);
  if (a >= s_point_count)
  {
    a = (uint8_t)(s_point_count - 1U);
    frac = 0U;
  }
  b = (uint8_t)(a + 1U);

  if ((frac == 0U) || (b >= s_point_count) ||
      (point_is_valid(a) == 0U) || (point_is_valid(b) == 0U))
  {
    if (point_is_valid(a) == 0U)
    {
      return 0U;
    }
    gain = s_points[a].gain_cdb;
    if (freq_hz != 0) *freq_hz = s_points[a].freq_hz;
    if (gain_cdb != 0) *gain_cdb = gain;
    if (ref_mv != 0) *ref_mv = s_points[a].ref_mv;
    if (dut_mv != 0) *dut_mv = s_points[a].dut_mv;
    if (x != 0) *x = index_to_x(a);
    if (y != 0) *y = gain_to_y(gain);
    return 1U;
  }

  gain = (int16_t)lerp_i32(s_points[a].gain_cdb, s_points[b].gain_cdb, frac);
  if (freq_hz != 0) *freq_hz = (uint32_t)lerp_i32((int32_t)s_points[a].freq_hz, (int32_t)s_points[b].freq_hz, frac);
  if (gain_cdb != 0) *gain_cdb = gain;
  if (ref_mv != 0) *ref_mv = (uint16_t)lerp_i32((int32_t)s_points[a].ref_mv, (int32_t)s_points[b].ref_mv, frac);
  if (dut_mv != 0) *dut_mv = (uint16_t)lerp_i32((int32_t)s_points[a].dut_mv, (int32_t)s_points[b].dut_mv, frac);
  if (x != 0) *x = (int16_t)lerp_i32(index_to_x(a), index_to_x(b), frac);
  if (y != 0) *y = gain_to_y(gain);
  return 1U;
}

static void analyze_summary(void)
{
  uint8_t i;
  uint8_t peak_index = 0U;
  uint8_t min_index = 0U;
  int16_t peak_gain = 0;
  int16_t min_gain = 0;

  s_summary.point_count = s_point_count;
  s_summary.peak_index = 0U;
  s_summary.peak_freq_hz = 0U;
  s_summary.peak_gain_cdb = 0;
  s_summary.min_index = 0U;
  s_summary.min_freq_hz = 0U;
  s_summary.min_gain_cdb = 0;
  s_summary.filter_type = SWEEP_FILTER_UNKNOWN;
  s_summary.cutoff_found = 0U;
  s_summary.cutoff_low_index = 0U;
  s_summary.cutoff_high_index = 0U;
  s_summary.cutoff_low_hz = 0U;
  s_summary.cutoff_high_hz = 0U;

  if (s_point_count == 0U)
  {
    return;
  }

  peak_gain = s_points[0].gain_cdb;
  min_gain = s_points[0].gain_cdb;

  for (i = 0U; i < s_point_count; ++i)
  {
    if (s_points[i].freq_hz == 0U)
    {
      continue;
    }
    if (s_points[i].gain_cdb > peak_gain)
    {
      peak_gain = s_points[i].gain_cdb;
      peak_index = i;
    }
    if (s_points[i].gain_cdb < min_gain)
    {
      min_gain = s_points[i].gain_cdb;
      min_index = i;
    }
  }

  s_summary.peak_index = peak_index;
  s_summary.peak_freq_hz = s_points[peak_index].freq_hz;
  s_summary.peak_gain_cdb = peak_gain;
  s_summary.min_index = min_index;
  s_summary.min_freq_hz = s_points[min_index].freq_hz;
  s_summary.min_gain_cdb = min_gain;

  analyze_cutoff();
}

static void draw_summary(void)
{
  char text[24];

  LCD_FillRect(24U, 228U, 292U, 12U, LCD_COLOR_BLACK);
  LCD_ShowString(24U, 228U, filter_type_name(s_summary.filter_type), LCD_COLOR_CYAN, LCD_COLOR_BLACK);
  if (s_summary.filter_type == SWEEP_FILTER_PEAK)
  {
    LCD_ShowString(54U, 228U, "F:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
    LCD_FormatFrequency(text, s_summary.peak_freq_hz);
    LCD_ShowString(72U, 228U, text, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
  }
  else if ((s_summary.cutoff_found != 0U) && (s_summary.filter_type == SWEEP_FILTER_BANDPASS))
  {
    LCD_FormatFrequency(text, s_summary.cutoff_low_hz);
    LCD_ShowString(54U, 228U, text, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    LCD_ShowString(112U, 228U, "-", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
    LCD_FormatFrequency(text, s_summary.cutoff_high_hz);
    LCD_ShowString(122U, 228U, text, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
  }
  else if (s_summary.cutoff_found != 0U)
  {
    LCD_ShowString(54U, 228U, "FC:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
    LCD_FormatFrequency(text, s_summary.cutoff_low_hz);
    LCD_ShowString(80U, 228U, text, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
  }
  else
  {
    LCD_ShowString(54U, 228U, "FC:N/A", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
  }
}

static void draw_cursor_text(void)
{
  char text[24];
  uint32_t freq_hz;
  int16_t gain_cdb;
  uint16_t ref_mv;
  uint16_t dut_mv;

  if (cursor_get_sample(&freq_hz, &gain_cdb, &ref_mv, &dut_mv, 0, 0) == 0U)
  {
    return;
  }

  LCD_FillRect(4U, 4U, 66U, 28U, LCD_COLOR_BLACK);
  LCD_ShowString(4U, 4U, "CUR", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
  LCD_FillRect(72U, 4U, 244U, 28U, LCD_COLOR_BLACK);
  LCD_ShowString(72U, 4U, "F:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_FormatFrequency(text, freq_hz);
  LCD_ShowString(86U, 4U, text, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
  LCD_ShowString(72U, 16U, "G:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_FormatSignedFixed(text, gain_cdb, 2U, "DB");
  LCD_ShowString(86U, 16U, text, LCD_COLOR_GREEN, LCD_COLOR_BLACK);
  LCD_ShowString(144U, 4U, "R:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_FormatSignedFixed(text, ref_mv, 0U, "MV");
  LCD_ShowString(158U, 4U, text, LCD_COLOR_CYAN, LCD_COLOR_BLACK);
  LCD_ShowString(144U, 16U, "D:", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_FormatSignedFixed(text, dut_mv, 0U, "MV");
  LCD_ShowString(158U, 16U, text, LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
}

static void draw_cursor_cross(int16_t x, int16_t y, uint16_t color)
{
  LCD_DrawLine((int16_t)(x - 4), y, (int16_t)(x + 4), y, color);
  LCD_DrawLine(x, (int16_t)(y - 4), x, (int16_t)(y + 4), color);
}

static void redraw_curve_near(uint16_t pos_q2)
{
  int16_t start;
  int16_t end;
  int16_t i;

  if (s_point_count == 0U)
  {
    return;
  }
  start = (int16_t)(pos_q2 / CURSOR_SUBSTEPS) - 1;
  end = start + 3;
  if (start < 0)
  {
    start = 0;
  }
  if (end >= (int16_t)s_point_count)
  {
    end = (int16_t)s_point_count - 1;
  }
  for (i = start; i < end; ++i)
  {
    if ((point_is_valid((uint8_t)i) != 0U) && (point_is_valid((uint8_t)(i + 1)) != 0U))
    {
      draw_interpolated_segment(index_to_x((uint8_t)i), gain_to_y(s_points[i].gain_cdb), s_points[i].gain_cdb,
                                index_to_x((uint8_t)(i + 1)), gain_to_y(s_points[i + 1].gain_cdb), s_points[i + 1].gain_cdb);
    }
  }
  for (i = start; i <= end; ++i)
  {
    if (point_is_valid((uint8_t)i) != 0U)
    {
      draw_point_marker(index_to_x((uint8_t)i), gain_to_y(s_points[i].gain_cdb), LCD_COLOR_YELLOW);
    }
  }
}

static int16_t median3_i16(int16_t a, int16_t b, int16_t c)
{
  if (a > b)
  {
    int16_t t = a;
    a = b;
    b = t;
  }
  if (b > c)
  {
    int16_t t = b;
    b = c;
    c = t;
  }
  if (a > b)
  {
    b = a;
  }
  return b;
}

static uint16_t median3_u16(uint16_t a, uint16_t b, uint16_t c)
{
  if (a > b)
  {
    uint16_t t = a;
    a = b;
    b = t;
  }
  if (b > c)
  {
    uint16_t t = b;
    b = c;
    c = t;
  }
  if (a > b)
  {
    b = a;
  }
  return b;
}

static int16_t smooth_gain(uint8_t index)
{
  if (index == 0U)
  {
    return s_points[0].gain_cdb;
  }
  if (index >= (uint8_t)(s_point_count - 1U))
  {
    return s_points[s_point_count - 1U].gain_cdb;
  }
  return median3_i16(s_points[index - 1U].gain_cdb,
                     s_points[index].gain_cdb,
                     s_points[index + 1U].gain_cdb);
}

static uint8_t nearer_threshold_index(uint8_t a, uint8_t b, int16_t threshold)
{
  int16_t ga = smooth_gain(a);
  int16_t gb = smooth_gain(b);
  int16_t da = (ga >= threshold) ? (int16_t)(ga - threshold) : (int16_t)(threshold - ga);
  int16_t db = (gb >= threshold) ? (int16_t)(gb - threshold) : (int16_t)(threshold - gb);

  return (db < da) ? b : a;
}

static uint8_t stable_window_level(uint8_t start, uint8_t *first, int16_t *level)
{
  enum { WIN = 5U, MAX_SPAN_CDB = 200 };
  uint8_t i;
  int16_t g;
  int16_t min_g;
  int16_t max_g;
  int32_t sum;

  if ((uint16_t)start + WIN > s_point_count)
  {
    return 0U;
  }
  min_g = smooth_gain(start);
  max_g = min_g;
  sum = 0L;
  for (i = 0U; i < WIN; ++i)
  {
    g = smooth_gain((uint8_t)(start + i));
    if (g < min_g) min_g = g;
    if (g > max_g) max_g = g;
    sum += g;
  }
  if ((int16_t)(max_g - min_g) > MAX_SPAN_CDB)
  {
    return 0U;
  }
  *first = start;
  *level = (int16_t)(sum / WIN);
  return 1U;
}

static uint8_t find_platform_forward(uint8_t *first, int16_t *level)
{
  uint8_t i;

  for (i = 0U; i < s_point_count; ++i)
  {
    if (stable_window_level(i, first, level) != 0U)
    {
      return 1U;
    }
  }
  return 0U;
}

static uint8_t find_platform_reverse(uint8_t *first, int16_t *level)
{
  uint8_t i;

  if (s_point_count < 5U)
  {
    return 0U;
  }
  i = (uint8_t)(s_point_count - 5U);
  while (1)
  {
    if (stable_window_level(i, first, level) != 0U)
    {
      return 1U;
    }
    if (i == 0U) break;
    --i;
  }
  return 0U;
}

static uint8_t threshold_side_ok(uint8_t index, int16_t threshold, uint8_t high_side)
{
  int16_t g = smooth_gain(index);
  return (high_side != 0U) ? (uint8_t)(g >= threshold) : (uint8_t)(g < threshold);
}

static uint8_t find_confirmed_crossing(uint8_t start, uint8_t end,
                                       int16_t threshold, uint8_t rising,
                                       uint8_t confirm_high)
{
  uint8_t i;
  uint8_t k;
  int16_t prev;
  int16_t curr;

  if (end >= s_point_count)
  {
    end = (uint8_t)(s_point_count - 1U);
  }
  for (i = (uint8_t)(start + 1U); i <= end; ++i)
  {
    prev = smooth_gain((uint8_t)(i - 1U));
    curr = smooth_gain(i);
    if (((rising != 0U) && (prev < threshold) && (curr >= threshold)) ||
        ((rising == 0U) && (prev >= threshold) && (curr < threshold)))
    {
      uint8_t ok = 1U;
      for (k = 0U; k < 2U; ++k)
      {
        if (((uint16_t)i + k > end) ||
            (threshold_side_ok((uint8_t)(i + k), threshold, confirm_high) == 0U))
        {
          ok = 0U;
          break;
        }
      }
      if (ok != 0U)
      {
        return nearer_threshold_index((uint8_t)(i - 1U), i, threshold);
      }
    }
  }
  return 255U;
}

static uint8_t raw_above(uint8_t index, int16_t threshold)
{
  return (uint8_t)((index < s_point_count) && (s_points[index].gain_cdb >= threshold));
}

static void analyze_cutoff(void)
{
  uint8_t n = s_point_count;
  uint8_t low_first;
  uint8_t high_first;
  uint8_t left;
  uint8_t right;
  uint8_t run_count;
  int16_t threshold;
  uint8_t idx;
  int16_t low_level;
  int16_t high_level;

  if (n < 12U)
  {
    return;
  }
  if ((int16_t)(s_summary.peak_gain_cdb - s_summary.min_gain_cdb) < 600)
  {
    return;
  }

  if (find_platform_forward(&low_first, &low_level) != 0U)
  {
    if ((int16_t)(s_summary.peak_gain_cdb - low_level) <= 400)
    {
      threshold = (int16_t)(low_level - 300);
      idx = find_confirmed_crossing(low_first, (uint8_t)(n - 1U), threshold, 0U, 0U);
      if (idx != 255U)
      {
        s_summary.filter_type = SWEEP_FILTER_LOWPASS;
        s_summary.cutoff_found = 1U;
        s_summary.cutoff_low_index = idx;
        s_summary.cutoff_low_hz = s_points[idx].freq_hz;
        return;
      }
    }
  }

  if (find_platform_reverse(&high_first, &high_level) != 0U)
  {
    if ((int16_t)(s_summary.peak_gain_cdb - high_level) <= 400)
    {
      threshold = (int16_t)(high_level - 300);
      idx = find_confirmed_crossing(0U, (uint8_t)(high_first + 4U), threshold, 1U, 1U);
      if (idx != 255U)
      {
        s_summary.filter_type = SWEEP_FILTER_HIGHPASS;
        s_summary.cutoff_found = 1U;
        s_summary.cutoff_low_index = idx;
        s_summary.cutoff_low_hz = s_points[idx].freq_hz;
        return;
      }
    }
  }

  threshold = (int16_t)(s_summary.peak_gain_cdb - 300);
  left = s_summary.peak_index;
  while ((left > 0U) && (raw_above((uint8_t)(left - 1U), threshold) != 0U))
  {
    --left;
  }
  right = s_summary.peak_index;
  while (((uint16_t)right + 1U < n) && (raw_above((uint8_t)(right + 1U), threshold) != 0U))
  {
    ++right;
  }
  run_count = (uint8_t)(right - left + 1U);
  if (run_count >= 3U)
  {
    s_summary.filter_type = SWEEP_FILTER_BANDPASS;
    s_summary.cutoff_found = 1U;
    s_summary.cutoff_low_index = left;
    s_summary.cutoff_high_index = right;
    s_summary.cutoff_low_hz = s_points[left].freq_hz;
    s_summary.cutoff_high_hz = s_points[right].freq_hz;
  }
  else if ((s_summary.peak_index > 0U) && (s_summary.peak_index < (uint8_t)(n - 1U)))
  {
    s_summary.filter_type = SWEEP_FILTER_PEAK;
  }
}

static void draw_curve_segment(int16_t prev_x, int16_t prev_y, int16_t x, int16_t y)
{
  int16_t dx = x - prev_x;
  int16_t dy = y - prev_y;
  uint8_t i;

  if (dx <= 0)
  {
    return;
  }

  /* Keep the display responsive on the GPIO-bitbanged LCD. A dotted segment
   * reads as a curve but is much cheaper than Bresenham lines through every
   * pixel at each sweep point.
   */
  for (i = 1U; i < 4U; ++i)
  {
    int16_t px = (int16_t)(prev_x + ((int32_t)dx * i) / 4L);
    int16_t py = (int16_t)(prev_y + ((int32_t)dy * i) / 4L);
    LCD_DrawPixel((uint16_t)px, (uint16_t)py, LCD_COLOR_YELLOW);
  }
}

static void draw_interpolated_segment(int16_t x0, int16_t y0, int16_t g0,
                                      int16_t x1, int16_t y1, int16_t g1)
{
  int16_t dg = (g1 >= g0) ? (int16_t)(g1 - g0) : (int16_t)(g0 - g1);
  uint8_t steps;
  uint8_t i;
  int16_t last_x = x0;
  int16_t last_y = y0;

  if (dg < 300)
  {
    draw_curve_segment(x0, y0, x1, y1);
    return;
  }
  if (dg < 1200)
  {
    steps = 4U;
  }
  else if (dg < 2500)
  {
    steps = 6U;
  }
  else
  {
    return;
  }

  for (i = 1U; i < steps; ++i)
  {
    int16_t ix = (int16_t)(x0 + ((int32_t)(x1 - x0) * i) / (int32_t)steps);
    int16_t iy = (int16_t)(y0 + ((int32_t)(y1 - y0) * i) / (int32_t)steps);
    draw_curve_segment(last_x, last_y, ix, iy);
    last_x = ix;
    last_y = iy;
  }
  draw_curve_segment(last_x, last_y, x1, y1);
}

static void draw_stored_curve(void)
{
  uint8_t i;
  int16_t prev_x = -1;
  int16_t prev_y = -1;
  int16_t prev_gain = 0;

  for (i = 0U; i < SWEEP_MAX_POINTS; ++i)
  {
    int16_t x;
    int16_t y;
    int16_t gain;

    if (s_points[i].freq_hz == 0U)
    {
      continue;
    }

    gain = s_points[i].gain_cdb;
    x = index_to_x(i);
    y = gain_to_y(gain);

    if (prev_x >= 0)
    {
      draw_interpolated_segment(prev_x, prev_y, prev_gain, x, y, gain);
    }
    draw_point_marker(x, y, LCD_COLOR_YELLOW);

    prev_x = x;
    prev_y = y;
    prev_gain = gain;
  }
}

static void read_bode_median(uint16_t *ref_mv, uint16_t *dut_mv, int16_t *gain_cdb)
{
  uint16_t ref_raw;
  uint16_t dut_raw;
  uint16_t ref_mv_0;
  uint16_t ref_mv_1;
  uint16_t ref_mv_2;
  uint16_t dut_mv_0;
  uint16_t dut_mv_1;
  uint16_t dut_mv_2;
  int16_t gain_0;
  int16_t gain_1;
  int16_t gain_2;

  ADC_ReadBodeAverage(BODE_AVG_SAMPLES, &ref_raw, &dut_raw);
  ref_mv_0 = ADC_RawToMv(ref_raw);
  dut_mv_0 = ADC_RawToMv(dut_raw);
  gain_0 = Measure_CalcGainCentiDb(ref_mv_0, dut_mv_0);

  ADC_ReadBodeAverage(BODE_AVG_SAMPLES, &ref_raw, &dut_raw);
  ref_mv_1 = ADC_RawToMv(ref_raw);
  dut_mv_1 = ADC_RawToMv(dut_raw);
  gain_1 = Measure_CalcGainCentiDb(ref_mv_1, dut_mv_1);

  ADC_ReadBodeAverage(BODE_AVG_SAMPLES, &ref_raw, &dut_raw);
  ref_mv_2 = ADC_RawToMv(ref_raw);
  dut_mv_2 = ADC_RawToMv(dut_raw);
  gain_2 = Measure_CalcGainCentiDb(ref_mv_2, dut_mv_2);

  *ref_mv = median3_u16(ref_mv_0, ref_mv_1, ref_mv_2);
  *dut_mv = median3_u16(dut_mv_0, dut_mv_1, dut_mv_2);
  *gain_cdb = median3_i16(gain_0, gain_1, gain_2);
}

static void draw_axis(void)
{
  int16_t y_p20 = gain_to_y(2000);
  int16_t y_0 = gain_to_y(0);
  int16_t y_n20 = gain_to_y(-2000);
  int16_t y_n40 = gain_to_y(-4000);
  int16_t y_n60 = gain_to_y(-6000);
  int16_t y_n80 = gain_to_y(-8000);
  int16_t x_100 = index_to_x(0U);
  int16_t x_1k = index_to_x(16U);
  int16_t x_10k = index_to_x(32U);
  int16_t x_100k = index_to_x(48U);
  int16_t x_1m = index_to_x(64U);
  int16_t x_10m = index_to_x(80U);

  LCD_Fill(LCD_COLOR_BLACK);
  LCD_ShowString(4U, 4U, "BODE", LCD_COLOR_CYAN, LCD_COLOR_BLACK);
  LCD_DrawRect(24U, 32U, 292U, 180U, LCD_COLOR_GRAY);
  LCD_DrawLine(24, y_p20, 315, y_p20, LCD_COLOR_DARK);
  LCD_DrawLine(24, y_0, 315, y_0, LCD_COLOR_DARK);
  LCD_DrawLine(24, y_n20, 315, y_n20, LCD_COLOR_DARK);
  LCD_DrawLine(24, y_n40, 315, y_n40, LCD_COLOR_DARK);
  LCD_DrawLine(24, y_n60, 315, y_n60, LCD_COLOR_DARK);
  LCD_DrawLine(24, y_n80, 315, y_n80, LCD_COLOR_DARK);
  LCD_DrawLine(x_100, 32, x_100, 211, LCD_COLOR_DARK);
  LCD_DrawLine(x_1k, 32, x_1k, 211, LCD_COLOR_DARK);
  LCD_DrawLine(x_10k, 32, x_10k, 211, LCD_COLOR_DARK);
  LCD_DrawLine(x_100k, 32, x_100k, 211, LCD_COLOR_DARK);
  LCD_DrawLine(x_1m, 32, x_1m, 211, LCD_COLOR_DARK);
  LCD_DrawLine(x_10m, 32, x_10m, 211, LCD_COLOR_DARK);
  LCD_ShowString(2U, (uint16_t)(y_p20 - 4), "+20", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(4U, (uint16_t)(y_0 - 4), "0dB", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(2U, (uint16_t)(y_n20 - 4), "-20", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(2U, (uint16_t)(y_n40 - 4), "-40", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(2U, (uint16_t)(y_n60 - 4), "-60", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(2U, (uint16_t)(y_n80 - 4), "-80", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  /* X-axis decade labels: log10(100)=2, log10(10M)=7, span=5 decades, 280/5=56px/dec */
  LCD_ShowString(24U, 216U, "100", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(78U, 216U, "1k", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(130U, 216U, "10k", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(184U, 216U, "100k", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(246U, 216U, "1M", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
  LCD_ShowString(298U, 216U, "10M", LCD_COLOR_GRAY, LCD_COLOR_BLACK);
}

void Sweep_Init(void)
{
  s_state = SWEEP_IDLE;
}

void Sweep_Start(uint8_t calibration)
{
  uint8_t i;
  s_cursor_visible = 0U;
  s_run_calibration = calibration;
  if (calibration == 0U)
  {
    s_cursor_index = 0U;
    s_cursor_pos_q2 = 0U;
    s_cursor_draw_pos_q2 = 0U;
    s_cursor_draw_x = 0;
    s_cursor_draw_y = 0;
    s_result_valid = 0U;
    s_point_count = 0U;
    for (i = 0U; i < SWEEP_MAX_POINTS; ++i)
    {
      s_points[i].freq_hz = 0U;
      s_points[i].gain_cdb = 0;
      s_points[i].ref_mv = 0U;
      s_points[i].dut_mv = 0U;
    }
  }
  s_done_reason = 0U;
  draw_axis();
  if (calibration != 0U)
  {
    LCD_ShowString(220U, 228U, "CAL", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
  }
  s_index = 0U;
  s_freq = SWEEP_START_FREQ_HZ;
  AD9834_SetWaveform(AD9834_WAVE_SINE);
  ADC_StartBodeMode();
  s_state = SWEEP_SET_FREQ;
}

void Sweep_Abort(void)
{
  s_state = SWEEP_IDLE;
}

void Sweep_Task(void)
{
  uint16_t ref_mv;
  uint16_t dut_mv;
  int16_t x;
  int16_t y;
  char text[24];

  switch (s_state)
  {
  case SWEEP_SET_FREQ:
    AD9834_SetFrequency(s_freq);
    s_settle_tick = HAL_GetTick();
    s_state = SWEEP_SETTLE_WAIT;
    break;

  case SWEEP_SETTLE_WAIT:
    if ((HAL_GetTick() - s_settle_tick) >=
        ((s_index == 0U) ? SWEEP_INITIAL_SETTLE_MS : SWEEP_SETTLE_MS))
    {
      s_state = SWEEP_SAMPLE_AVG;
    }
    break;

  case SWEEP_SAMPLE_AVG:
    read_bode_median(&ref_mv, &dut_mv, &s_last_gain_cdb);
    if ((s_run_calibration == 0U) && (s_cal_valid != 0U) && (s_cal_enabled != 0U))
    {
      s_last_gain_cdb = (int16_t)(s_last_gain_cdb - s_cal_error_cdb[s_index]);
    }
    s_last_ref_mv = ref_mv;
    s_last_dut_mv = dut_mv;
    s_last_freq = s_freq;
    s_state = SWEEP_DRAW_POINT;
    break;

  case SWEEP_DRAW_POINT:
    /* Store data point for optional future export/debug. */
    if ((s_run_calibration != 0U) && (s_index < SWEEP_MAX_POINTS))
    {
      s_cal_error_cdb[s_index] = s_last_gain_cdb;
    }
    else if (s_index < SWEEP_MAX_POINTS)
    {
      s_points[s_index].freq_hz = s_freq;
      s_points[s_index].gain_cdb = s_last_gain_cdb;
      s_points[s_index].ref_mv = s_last_ref_mv;
      s_points[s_index].dut_mv = s_last_dut_mv;
      s_point_count = (uint8_t)(s_index + 1U);
    }
    x = index_to_x(s_index);
    y = gain_to_y(s_last_gain_cdb);
    draw_point_marker(x, y, LCD_COLOR_YELLOW);
    LCD_FillRect(126U, 4U, 190U, 28U, LCD_COLOR_BLACK);
    LCD_FormatFrequency(text, s_last_freq);
    LCD_ShowString(126U, 4U, text, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
    LCD_FormatSignedFixed(text, s_last_gain_cdb, 2U, "DB");
    LCD_ShowString(126U, 16U, text, LCD_COLOR_GREEN, LCD_COLOR_BLACK);
    LCD_FormatSignedFixed(text, s_last_ref_mv, 0U, "MV");
    LCD_ShowString(188U, 4U, text, LCD_COLOR_CYAN, LCD_COLOR_BLACK);
    LCD_FormatSignedFixed(text, s_last_dut_mv, 0U, "MV");
    LCD_ShowString(188U, 16U, text, LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
    ++s_index;
    if (s_index >= SWEEP_MAX_POINTS)
    {
      s_done_reason = 1U;
      s_state = SWEEP_DONE;
    }
    else
    {
      s_freq = sweep_freq_for_index(s_index);
      s_state = SWEEP_SET_FREQ;
    }
    break;

  case SWEEP_DONE:
    if (s_run_calibration != 0U)
    {
      if (s_done_reason == 1U)
      {
        s_cal_valid = 1U;
        s_cal_enabled = 1U;
      }
      draw_axis();
      if (s_result_valid != 0U)
      {
        draw_stored_curve();
        draw_summary();
        Sweep_DrawCursor();
      }
      LCD_ShowString(220U, 228U, (s_cal_valid != 0U) ? "CORR:ON" : "CAL:ERR",
                     (s_cal_valid != 0U) ? LCD_COLOR_GREEN : LCD_COLOR_YELLOW,
                     LCD_COLOR_BLACK);
      s_run_calibration = 0U;
      s_state = SWEEP_IDLE;
      break;
    }
    /* Generic sweep mode: keep the measured curve as the result. Do not infer
     * cutoff frequency here; low-pass/high-pass/band-pass interpretation is a
     * separate analysis step.
     */
    if (s_done_reason != 1U)
    {
      LCD_ShowString(24U, 230U, "UNK", LCD_COLOR_YELLOW, LCD_COLOR_BLACK);
    }
    draw_stored_curve();
    if (s_point_count != 0U)
    {
      analyze_summary();
      s_result_valid = 1U;
      cursor_set_index(s_summary.peak_index);
      draw_summary();
      Sweep_DrawCursor();
    }
    if (s_result_valid == 0U)
    {
      LCD_ShowString(220U, 4U, "DONE", LCD_COLOR_GREEN, LCD_COLOR_BLACK);
    }
    s_state = SWEEP_IDLE;
    break;

  case SWEEP_IDLE:
  default:
    break;
  }
}

uint8_t Sweep_IsBusy(void)
{
  return (s_state != SWEEP_IDLE) ? 1U : 0U;
}

uint8_t Sweep_HasResult(void)
{
  return s_result_valid;
}

uint8_t Sweep_IsCalibrated(void)
{
  return (uint8_t)((s_cal_valid != 0U) && (s_cal_enabled != 0U));
}

uint8_t Sweep_ToggleCorrection(void)
{
  if (s_cal_valid == 0U)
  {
    s_cal_enabled = 0U;
    return 0U;
  }
  s_cal_enabled = (s_cal_enabled == 0U) ? 1U : 0U;
  return s_cal_enabled;
}

void Sweep_RedrawResult(void)
{
  draw_axis();
  if (s_result_valid != 0U)
  {
    draw_stored_curve();
    draw_summary();
    Sweep_DrawCursor();
  }
}

uint8_t Sweep_GetSummary(SweepSummary_t *out)
{
  if ((out == 0) || (s_result_valid == 0U))
  {
    return 0U;
  }
  *out = s_summary;
  return 1U;
}

uint8_t Sweep_MoveCursor(int16_t diff)
{
  int32_t next;

  if ((s_result_valid == 0U) || (s_point_count == 0U) || (diff == 0))
  {
    return 0U;
  }

  Sweep_ClearCursor();
  next = (int32_t)s_cursor_pos_q2 + (int32_t)diff;
  if (next < 0)
  {
    next = 0;
  }
  if (next > (int32_t)cursor_max_pos())
  {
    next = (int32_t)cursor_max_pos();
  }
  s_cursor_pos_q2 = (uint16_t)next;
  cursor_update_nearest_index();
  Sweep_DrawCursor();
  return 1U;
}

void Sweep_DrawCursor(void)
{
  int16_t x;
  int16_t y;

  if ((s_result_valid == 0U) || (cursor_get_sample(0, 0, 0, 0, &x, &y) == 0U))
  {
    return;
  }
  draw_cursor_cross(x, y, LCD_COLOR_RED);
  s_cursor_draw_pos_q2 = s_cursor_pos_q2;
  s_cursor_draw_x = x;
  s_cursor_draw_y = y;
  draw_cursor_text();
  s_cursor_visible = 1U;
}

void Sweep_ClearCursor(void)
{
  if (s_cursor_visible == 0U)
  {
    return;
  }
  draw_cursor_cross(s_cursor_draw_x, s_cursor_draw_y, LCD_COLOR_BLACK);
  redraw_curve_near(s_cursor_draw_pos_q2);
  s_cursor_visible = 0U;
}
