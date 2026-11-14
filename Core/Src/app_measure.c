#include "app_measure.h"
#include "project_config.h"
#include <math.h>

int16_t Measure_CalcGainCentiDb(uint16_t ref_mv, uint16_t dut_mv)
{
  int32_t delta_mv = (int32_t)dut_mv - (int32_t)ref_mv;
  int32_t centi_db = ((delta_mv * 100L) / AD8307_MV_PER_DB) + BODE_GAIN_OFFSET_CDB;
  if (centi_db > 32767L)
  {
    centi_db = 32767L;
  }
  if (centi_db < -32768L)
  {
    centi_db = -32768L;
  }
  return (int16_t)centi_db;
}

static uint32_t estimate_frequency_hz(const uint16_t *samples, uint16_t count, uint16_t dc_raw,
                                      uint32_t sample_rate_hz, uint8_t *cycle_count)
{
  uint16_t i;
  uint16_t crossings = 0U;
  uint32_t first_cross_x16 = 0U;
  uint32_t last_cross_x16 = 0U;
  uint8_t was_high;
  const uint16_t hyst_raw = (uint16_t)(((uint32_t)20U * 4095U + (ADC_REF_MV / 2U)) / ADC_REF_MV);
  uint16_t low_th = (dc_raw > hyst_raw) ? (uint16_t)(dc_raw - hyst_raw) : 0U;
  uint16_t high_th = (uint16_t)(dc_raw + hyst_raw);

  if (high_th > 4095U)
  {
    high_th = 4095U;
  }

  if ((samples == 0) || (count < 4U) || (sample_rate_hz == 0U))
  {
    if (cycle_count != 0)
    {
      *cycle_count = 0U;
    }
    return 0U;
  }

  was_high = (samples[0] >= dc_raw) ? 1U : 0U;
  for (i = 1U; i < count; ++i)
  {
    if (was_high == 0U)
    {
      if (samples[i] >= high_th)
      {
        uint16_t prev = samples[i - 1U];
        uint16_t curr = samples[i];
        uint32_t frac_x16 = 0U;
        if (curr > prev)
        {
          uint32_t num = ((uint32_t)high_th > prev) ? ((uint32_t)high_th - prev) : 0U;
          frac_x16 = (num * 16U) / ((uint32_t)curr - prev);
          if (frac_x16 > 16U)
          {
            frac_x16 = 16U;
          }
        }
        last_cross_x16 = (((uint32_t)i - 1U) * 16U) + frac_x16;
        if (crossings == 0U)
        {
          first_cross_x16 = last_cross_x16;
        }
        ++crossings;
        was_high = 1U;
      }
    }
    else if (samples[i] <= low_th)
    {
      was_high = 0U;
    }
  }

  if (cycle_count != 0)
  {
    *cycle_count = (crossings > 255U) ? 255U : (uint8_t)crossings;
  }

  if (crossings < 2U)
  {
    return 0U;
  }

  if (last_cross_x16 <= first_cross_x16)
  {
    return 0U;
  }

  return (uint32_t)((((uint64_t)(crossings - 1U) * (uint64_t)sample_rate_hz * 16ULL) +
                    ((uint64_t)(last_cross_x16 - first_cross_x16) / 2ULL)) /
                   (uint64_t)(last_cross_x16 - first_cross_x16));
}

WaveMeasureResult_t Measure_IdentifyWaveform(const uint16_t *samples, uint16_t count, uint32_t sample_rate_hz)
{
  WaveMeasureResult_t r = {0U, 0UL, WAVEFORM_UNKNOWN, WAVE_WARN_NONE};
  uint32_t sum = 0U;
  int32_t min_ac = 0L;
  int32_t max_ac = 0L;
  uint64_t sum_sq = 0U;
  uint16_t i;
  uint16_t dc_raw;
  uint16_t vpp_raw;
  uint16_t rms_raw;
  uint16_t ac_rms_mv;
  uint16_t crest_x1000;
  uint16_t clip_count = 0U;
  uint8_t cycle_count = 0U;
  uint32_t slope_sum = 0U;
  uint32_t curve_sum = 0U;
  uint32_t abs_sum = 0U;
  int32_t last_slope = 0L;

  if ((samples == 0) || (count == 0U))
  {
    return r;
  }

  for (i = 0U; i < count; ++i)
  {
    if ((samples[i] <= WAVE_CLIP_LOW_RAW) || (samples[i] >= WAVE_CLIP_HIGH_RAW))
    {
      ++clip_count;
    }
    sum += samples[i];
  }
  dc_raw = (uint16_t)(sum / count);
  r.freq_hz = estimate_frequency_hz(samples, count, dc_raw, sample_rate_hz, &cycle_count);

  /* OPA2354 shifts the external waveform into a positive ADC range.
   * Remove this DC offset first; all RMS/Vpp/crest calculations use AC only.
   */
  for (i = 0U; i < count; ++i)
  {
    int32_t ac = (int32_t)samples[i] - (int32_t)dc_raw;
    if ((i == 0U) || (ac < min_ac))
    {
      min_ac = ac;
    }
    if ((i == 0U) || (ac > max_ac))
    {
      max_ac = ac;
    }
    sum_sq += (uint64_t)((uint32_t)(ac * ac));
    abs_sum += (uint32_t)((ac >= 0L) ? ac : -ac);
    if (i != 0U)
    {
      int32_t slope = (int32_t)samples[i] - (int32_t)samples[i - 1U];
      slope_sum += (uint32_t)((slope >= 0L) ? slope : -slope);
      if (i > 1U)
      {
        int32_t bend = slope - last_slope;
        curve_sum += (uint32_t)((bend >= 0L) ? bend : -bend);
      }
      last_slope = slope;
    }
  }

  rms_raw = (uint16_t)sqrtf((float)sum_sq / (float)count);
  vpp_raw = (uint16_t)(max_ac - min_ac);
  ac_rms_mv = (uint16_t)(((uint32_t)rms_raw * ADC_REF_MV + 2047U) / 4095U);
  r.ac_vpp_mv = (uint16_t)(((uint32_t)vpp_raw * ADC_REF_MV + 2047U) / 4095U);

  if ((ac_rms_mv < WAVE_MIN_RMS_MV) || (r.ac_vpp_mv < WAVE_MIN_VPP_MV))
  {
    r.warn = WAVE_WARN_LOW_SIGNAL;
    return r;
  }
  if (r.ac_vpp_mv < WAVE_LOW_WARN_VPP_MV)
  {
    r.warn = WAVE_WARN_LOW_SIGNAL;
  }

  /* Do not classify rail-clipped input: the crest factor is no longer reliable. */
  if (clip_count >= WAVE_CLIP_COUNT_LIMIT)
  {
    r.warn = WAVE_WARN_CLIP;
    return r;
  }

  crest_x1000 = (uint16_t)(((uint32_t)r.ac_vpp_mv * 500U) / ac_rms_mv);

  if ((crest_x1000 >= CREST_SQUARE_MIN_X1000) && (crest_x1000 <= CREST_SQUARE_MAX_X1000))
  {
    r.waveform = WAVEFORM_SQUARE;
  }
  else if ((cycle_count >= 3U) && (slope_sum != 0U) &&
           (crest_x1000 >= 1250U) && (crest_x1000 <= 1900U))
  {
    uint16_t samples_per_cycle = (uint16_t)(count / (uint16_t)(cycle_count - 1U));
    uint16_t mean_abs_shape_x1000;
    uint32_t shape_x100;
    if (samples_per_cycle < 6U)
    {
      samples_per_cycle = 6U;
    }
    if (samples_per_cycle > 80U)
    {
      samples_per_cycle = 80U;
    }
    /* The curvature result is only a dead-band fallback. Quantize it to
     * 100-point steps so the calculation stays in 32 bits and does not add
     * another 64-bit multiply/divide sequence.
     */
    shape_x100 = ((curve_sum * samples_per_cycle) / slope_sum) * 100U;
    mean_abs_shape_x1000 = (uint16_t)((((abs_sum + (count / 2U)) / count) * 2000U) /
                                      vpp_raw);

    /* Robust primary discriminator: ideal triangle ~= 0.500, sine ~= 0.637.
     * Curvature is noise-sensitive, so only use it inside the dead band.
     */
    if (mean_abs_shape_x1000 <= 555U)
    {
      r.waveform = WAVEFORM_TRIANGLE;
    }
    else if (mean_abs_shape_x1000 >= 590U)
    {
      r.waveform = WAVEFORM_SINE;
    }
    else if ((shape_x100 > 380U) &&
             (crest_x1000 >= CREST_SINE_MIN_X1000) &&
             (crest_x1000 <= CREST_SINE_MAX_X1000))
    {
      r.waveform = WAVEFORM_SINE;
    }
    else if ((shape_x100 < 260U) &&
             (crest_x1000 >= CREST_TRIANGLE_MIN_X1000) &&
             (crest_x1000 <= CREST_TRIANGLE_MAX_X1000))
    {
      r.waveform = WAVEFORM_TRIANGLE;
    }
    else
    {
      r.waveform = WAVEFORM_UNKNOWN;
    }
  }
  else if ((crest_x1000 >= CREST_SINE_MIN_X1000) && (crest_x1000 <= CREST_SINE_MAX_X1000))
  {
    r.waveform = WAVEFORM_SINE;
  }
  else if ((crest_x1000 >= CREST_TRIANGLE_MIN_X1000) && (crest_x1000 <= CREST_TRIANGLE_MAX_X1000))
  {
    r.waveform = WAVEFORM_TRIANGLE;
  }
  else
  {
    r.waveform = WAVEFORM_UNKNOWN;
  }
  return r;
}

const char *Measure_WaveformName(WaveformId_t waveform)
{
  switch (waveform)
  {
  case WAVEFORM_SQUARE: return "SQ";
  case WAVEFORM_SINE: return "SINE";
  case WAVEFORM_TRIANGLE: return "TRI";
  case WAVEFORM_UNKNOWN:
  default: return "UNK";
  }
}

const char *Measure_WarnName(WaveWarn_t warn)
{
  switch (warn)
  {
  case WAVE_WARN_LOW_SIGNAL: return "LOW";
  case WAVE_WARN_CLIP: return "CLIP";
  case WAVE_WARN_UNSTABLE: return "UNS";
  case WAVE_WARN_NONE:
  default: return "NONE";
  }
}
