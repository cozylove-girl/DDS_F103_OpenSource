/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LOG_REF_Pin GPIO_PIN_3
#define LOG_REF_GPIO_Port GPIOA
#define LOG_DUT_Pin GPIO_PIN_4
#define LOG_DUT_GPIO_Port GPIOA
#define WAVE_ADC_Pin GPIO_PIN_5
#define WAVE_ADC_GPIO_Port GPIOA
#define WAVE_SEL_Pin GPIO_PIN_6
#define WAVE_SEL_GPIO_Port GPIOA
#define DDS_PSEL_Pin GPIO_PIN_7
#define DDS_PSEL_GPIO_Port GPIOA
#define DDS_FSEL_Pin GPIO_PIN_0
#define DDS_FSEL_GPIO_Port GPIOB
#define KEY_MODE_Pin GPIO_PIN_1
#define KEY_MODE_GPIO_Port GPIOB
#define DDS_FSYNC_Pin GPIO_PIN_12
#define DDS_FSYNC_GPIO_Port GPIOB
#define DDS_RST_Pin GPIO_PIN_14
#define DDS_RST_GPIO_Port GPIOB
#define LCD_BLK_Pin GPIO_PIN_8
#define LCD_BLK_GPIO_Port GPIOA
#define LCD_DC_Pin GPIO_PIN_11
#define LCD_DC_GPIO_Port GPIOA
#define LCD_RST_Pin GPIO_PIN_12
#define LCD_RST_GPIO_Port GPIOA
#define LCD_CS_Pin GPIO_PIN_15
#define LCD_CS_GPIO_Port GPIOA
#define LCD_SCK_Pin GPIO_PIN_3
#define LCD_SCK_GPIO_Port GPIOB
#define KEY_STEP_Pin GPIO_PIN_4
#define KEY_STEP_GPIO_Port GPIOB
#define LCD_MOSI_Pin GPIO_PIN_5
#define LCD_MOSI_GPIO_Port GPIOB
#define KEY_ENC_Pin GPIO_PIN_8
#define KEY_ENC_GPIO_Port GPIOB
#define KEY_SWEEP_Pin GPIO_PIN_9
#define KEY_SWEEP_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
