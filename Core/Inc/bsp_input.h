#ifndef BSP_INPUT_H
#define BSP_INPUT_H

#include "main.h"
#include <stdint.h>

#define KEY_EVENT_ENC    (1U << 0)
#define KEY_EVENT_MODE   (1U << 1)
#define KEY_EVENT_STEP   (1U << 2)
#define KEY_EVENT_SWEEP  (1U << 3)

void Input_Init(void);
int16_t Encoder_GetDiff(void);
uint8_t Key_Scan(void);

#endif /* BSP_INPUT_H */
