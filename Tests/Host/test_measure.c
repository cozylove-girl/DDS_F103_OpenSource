#include "../../Core/Inc/app_measure.h"
#include "../../Core/Inc/project_config.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static uint16_t mv_to_raw(uint16_t mv)
{
  return (uint16_t)(((uint32_t)mv * 4095U) / ADC_REF_MV);
}

static int test_sine_with_dc(void)
{
  uint16_t samples[WAVE_SAMPLE_COUNT];
  uint16_t i;
  WaveMeasureResult_t r;
  for (i = 0U; i < WAVE_SAMPLE_COUNT; ++i)
  {
    float a = sinf((float)i * 6.2831853f / 64.0f);
    int32_t mv = 1650 + (int32_t)(500.0f * a);
    samples[i] = mv_to_raw((uint16_t)mv);
  }
  r = Measure_IdentifyWaveform(samples, WAVE_SAMPLE_COUNT);
  return (r.waveform == WAVEFORM_SINE) ? 0 : 1;
}

static int test_zero_is_unknown(void)
{
  uint16_t samples[WAVE_SAMPLE_COUNT];
  uint16_t i;
  WaveMeasureResult_t r;
  for (i = 0U; i < WAVE_SAMPLE_COUNT; ++i)
  {
    samples[i] = mv_to_raw(1650U);
  }
  r = Measure_IdentifyWaveform(samples, WAVE_SAMPLE_COUNT);
  return (r.waveform == WAVEFORM_UNKNOWN) ? 0 : 1;
}

int main(void)
{
  int fail = 0;
  fail += test_sine_with_dc();
  fail += test_zero_is_unknown();
  fail += (Measure_CalcGainCentiDb(1000U, 1000U) == 0) ? 0 : 1;
  printf("%s\n", (fail == 0) ? "PASS" : "FAIL");
  return fail;
}
