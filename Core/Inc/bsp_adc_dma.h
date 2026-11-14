#ifndef BSP_ADC_DMA_H
#define BSP_ADC_DMA_H

#include "main.h"
#include "project_config.h"
#include <stdint.h>

typedef enum
{
  ADC_APP_MODE_IDLE = 0,
  ADC_APP_MODE_BODE,
  ADC_APP_MODE_WAVEFORM
} ADC_AppMode_t;

void ADC_DMA_AppInit(void);
void ADC_StartBodeMode(void);
void ADC_StartWaveformMode(void);
uint32_t ADC_GetWaveSampleRateHz(void);
void ADC_SetWaveSampleRateHz(uint32_t sample_rate_hz);
void ADC_Task(void);
uint16_t ADC_ReadChannelRaw(uint32_t channel, uint32_t sample_time);
void ADC_ReadBodeAverage(uint16_t samples, uint16_t *ref_raw, uint16_t *dut_raw);
uint16_t ADC_RawToMv(uint16_t raw);
uint8_t ADC_GetWaveSnapshot(uint16_t *out, uint16_t max_count);
ADC_AppMode_t ADC_GetAppMode(void);

#endif /* BSP_ADC_DMA_H */
