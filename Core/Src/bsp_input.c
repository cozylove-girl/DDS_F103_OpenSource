#include "bsp_input.h"
#include "project_config.h"
#include "tim.h"

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  uint8_t event;
  uint8_t stable_low;
  uint8_t last_raw_low;
  uint32_t changed_tick;
} KeyState_t;

static uint16_t s_last_cnt;
static int16_t s_encoder_accum;
static int8_t s_last_encoder_dir;
static uint32_t s_last_encoder_tick;

static KeyState_t s_keys[] =
{
  {KEY_ENC_GPIO_Port, KEY_ENC_Pin, KEY_EVENT_ENC, 0U, 0U, 0U},
  {KEY_MODE_GPIO_Port, KEY_MODE_Pin, KEY_EVENT_MODE, 0U, 0U, 0U},
  {KEY_STEP_GPIO_Port, KEY_STEP_Pin, KEY_EVENT_STEP, 0U, 0U, 0U},
  {KEY_SWEEP_GPIO_Port, KEY_SWEEP_Pin, KEY_EVENT_SWEEP, 0U, 0U, 0U},
};

void Input_Init(void)
{
#if INPUT_HARDWARE_ENABLE
  HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL);
  s_last_cnt = (uint16_t)__HAL_TIM_GET_COUNTER(&htim4);
  s_encoder_accum = 0;
  s_last_encoder_dir = 0;
  s_last_encoder_tick = 0U;
#else
  s_last_cnt = 0U;
  s_encoder_accum = 0;
  s_last_encoder_dir = 0;
  s_last_encoder_tick = 0U;
#endif
}

int16_t Encoder_GetDiff(void)
{
#if INPUT_HARDWARE_ENABLE
  uint16_t now = (uint16_t)__HAL_TIM_GET_COUNTER(&htim4);
  int16_t diff = (int16_t)(now - s_last_cnt);
  uint32_t tick = HAL_GetTick();
  int8_t dir;
  int16_t steps = 0;
  s_last_cnt = now;
  if (diff == 0)
  {
    return 0;
  }

  dir = (diff > 0) ? 1 : -1;
  if ((s_last_encoder_dir != 0) &&
      (dir != s_last_encoder_dir) &&
      ((tick - s_last_encoder_tick) < ENCODER_REVERSE_GUARD_MS))
  {
    return 0;
  }

  s_last_encoder_dir = dir;
  s_last_encoder_tick = tick;
  s_encoder_accum = (int16_t)(s_encoder_accum + diff);

  while (s_encoder_accum >= ENCODER_COUNTS_PER_STEP)
  {
    ++steps;
    s_encoder_accum = (int16_t)(s_encoder_accum - ENCODER_COUNTS_PER_STEP);
  }
  while (s_encoder_accum <= -ENCODER_COUNTS_PER_STEP)
  {
    --steps;
    s_encoder_accum = (int16_t)(s_encoder_accum + ENCODER_COUNTS_PER_STEP);
  }

  return steps;
#else
  return 0;
#endif
}

uint8_t Key_Scan(void)
{
#if INPUT_HARDWARE_ENABLE
  uint8_t events = 0U;
  uint32_t now = HAL_GetTick();
  uint32_t i;

  for (i = 0U; i < (sizeof(s_keys) / sizeof(s_keys[0])); ++i)
  {
    uint8_t raw_low = (HAL_GPIO_ReadPin(s_keys[i].port, s_keys[i].pin) == GPIO_PIN_RESET) ? 1U : 0U;
    if (raw_low != s_keys[i].last_raw_low)
    {
      s_keys[i].last_raw_low = raw_low;
      s_keys[i].changed_tick = now;
    }

    if ((now - s_keys[i].changed_tick) >= INPUT_SCAN_MS)
    {
      if ((raw_low != s_keys[i].stable_low) && (raw_low != 0U))
      {
        events |= s_keys[i].event;
      }
      s_keys[i].stable_low = raw_low;
    }
  }

  return events;
#else
  return 0U;
#endif
}
