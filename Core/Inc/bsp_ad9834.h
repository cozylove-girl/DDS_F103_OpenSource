#ifndef BSP_AD9834_H
#define BSP_AD9834_H

#include "main.h"
#include <stdint.h>

typedef enum
{
  AD9834_WAVE_SINE = 0,
  AD9834_WAVE_TRIANGLE,
  AD9834_WAVE_SQUARE
} AD9834_Waveform_t;

typedef enum
{
  AD9834_ROUTE_ANALOG = 0,
  AD9834_ROUTE_SQUARE
} AD9834_Route_t;

void AD9834_Init(void);
void AD9834_SetFrequency(uint32_t freq_hz);
void AD9834_SetWaveform(AD9834_Waveform_t wave);
void AD9834_SetRoute(AD9834_Route_t route);
uint32_t AD9834_GetFrequency(void);
AD9834_Waveform_t AD9834_GetWaveform(void);

#endif /* BSP_AD9834_H */
