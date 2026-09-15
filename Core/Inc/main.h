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
#define METAL_SENSOR_Pin GPIO_PIN_0
#define METAL_SENSOR_GPIO_Port GPIOA
#define START_BUTTON_Pin GPIO_PIN_1
#define START_BUTTON_GPIO_Port GPIOA
#define MANUAL_PUMP_Pin GPIO_PIN_2
#define MANUAL_PUMP_GPIO_Port GPIOA
#define SETTINGS_SWITCH_Pin GPIO_PIN_3
#define SETTINGS_SWITCH_GPIO_Port GPIOA
#define BACK_SWITCH_Pin GPIO_PIN_4
#define BACK_SWITCH_GPIO_Port GPIOA
#define CS_ROM_Pin GPIO_PIN_0
#define CS_ROM_GPIO_Port GPIOB
#define OK_SWITCH_Pin GPIO_PIN_1
#define OK_SWITCH_GPIO_Port GPIOB
#define DOWN_SWITCH_Pin GPIO_PIN_10
#define DOWN_SWITCH_GPIO_Port GPIOB
#define UP_SWITCH_Pin GPIO_PIN_11
#define UP_SWITCH_GPIO_Port GPIOB
#define LD7_Pin GPIO_PIN_12
#define LD7_GPIO_Port GPIOB
#define LD6_Pin GPIO_PIN_13
#define LD6_GPIO_Port GPIOB
#define LD5_Pin GPIO_PIN_14
#define LD5_GPIO_Port GPIOB
#define LD4_Pin GPIO_PIN_15
#define LD4_GPIO_Port GPIOB
#define LCD_EN_Pin GPIO_PIN_8
#define LCD_EN_GPIO_Port GPIOA
#define LCD_RS_Pin GPIO_PIN_9
#define LCD_RS_GPIO_Port GPIOA
#define BUZZER_Pin GPIO_PIN_10
#define BUZZER_GPIO_Port GPIOA
#define VALVE_Pin GPIO_PIN_11
#define VALVE_GPIO_Port GPIOA
#define MOTOR_Pin GPIO_PIN_12
#define MOTOR_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
