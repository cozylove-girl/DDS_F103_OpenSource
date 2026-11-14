#ifndef APP_MEASURE_H
#define APP_MEASURE_H

#include <stdint.h>

typedef enum
{
  WAVEFORM_UNKNOWN = 0,
  WAVEFORM_SQUARE,
  WAVEFORM_SINE,
  WAVEFORM_TRIANGLE
} WaveformId_t;

typedef enum
{
  WAVE_WARN_NONE = 0,
  WAVE_WARN_LOW_SIGNAL,
  WAVE_WARN_CLIP,
  WAVE_WARN_UNSTABLE
} WaveWarn_t;

typedef struct
{
  uint16_t ac_vpp_mv;
  uint32_t freq_hz;
  WaveformId_t waveform;
  WaveWarn_t warn;
} WaveMeasureResult_t;

int16_t Measure_CalcGainCentiDb(uint16_t ref_mv, uint16_t dut_mv);
WaveMeasureResult_t Measure_IdentifyWaveform(const uint16_t *samples, uint16_t count, uint32_t sample_rate_hz);
const char *Measure_WaveformName(WaveformId_t waveform);
const char *Measure_WarnName(WaveWarn_t warn);

#endif /* APP_MEASURE_H */
