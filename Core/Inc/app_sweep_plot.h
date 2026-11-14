#ifndef APP_SWEEP_PLOT_H
#define APP_SWEEP_PLOT_H

#include <stdint.h>

typedef enum
{
  SWEEP_IDLE = 0,
  SWEEP_SET_FREQ,
  SWEEP_SETTLE_WAIT,
  SWEEP_SAMPLE_AVG,
  SWEEP_DRAW_POINT,
  SWEEP_DONE
} SweepState_t;

typedef enum
{
  SWEEP_FILTER_UNKNOWN = 0,
  SWEEP_FILTER_LOWPASS,
  SWEEP_FILTER_HIGHPASS,
  SWEEP_FILTER_BANDPASS,
  SWEEP_FILTER_PEAK
} SweepFilterType_t;

typedef struct
{
  uint8_t point_count;
  uint8_t peak_index;
  uint32_t peak_freq_hz;
  int16_t peak_gain_cdb;
  uint8_t min_index;
  uint32_t min_freq_hz;
  int16_t min_gain_cdb;
  SweepFilterType_t filter_type;
  uint8_t cutoff_found;
  uint8_t cutoff_low_index;
  uint8_t cutoff_high_index;
  uint32_t cutoff_low_hz;
  uint32_t cutoff_high_hz;
} SweepSummary_t;

void Sweep_Init(void);
void Sweep_Start(uint8_t calibration);
void Sweep_Abort(void);
void Sweep_Task(void);
void Sweep_RedrawResult(void);
uint8_t Sweep_IsCalibrated(void);
uint8_t Sweep_ToggleCorrection(void);
uint8_t Sweep_IsBusy(void);
uint8_t Sweep_HasResult(void);
uint8_t Sweep_GetSummary(SweepSummary_t *out);
uint8_t Sweep_MoveCursor(int16_t diff);
void Sweep_DrawCursor(void);
void Sweep_ClearCursor(void);

#endif /* APP_SWEEP_PLOT_H */
