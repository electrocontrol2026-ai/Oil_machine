/*
 * app.h - Oil Machine Application Logic Header
 *
 * Hardware:
 *   Inputs  (all Active-LOW with internal/external pull-ups):
 *     - METAL_SENSOR  : PA0  (Proximity sensor)
 *     - START_BUTTON  : PA1  (Start/Stop toggle)
 *     - MANUAL_PUMP   : PA2  (Manual pump – momentary)
 *     - SETTINGS_SWITCH: PA3 (SET button – enter settings)
 *     - BACK_SWITCH   : PA4  (BACK button)
 *     - OK_SWITCH     : PB1  (OK/Confirm)
 *     - DOWN_SWITCH   : PB10 (Scroll down / decrement)
 *     - UP_SWITCH     : PB11 (Scroll up / increment)
 *
 *   Outputs:
 *     - LCD_EN        : PA8
 *     - LCD_RS        : PA9
 *     - BUZZER        : PA10
 *     - VALVE         : PA11  (relay)
 *     - MOTOR         : PA12  (relay)
 *     - LCD_D4..D7    : PB12..PB15
 *     - CS_ROM        : PB0   (SPI EEPROM CS)
 *
 *  10 Programs, each with:
 *     - volume_ml  : uint32_t (mL, step 100)
 *     - time_ms    : uint32_t (ms, step 100)
 */
#ifndef INC_APP_H_
#define INC_APP_H_

#include "main.h"
#include <stdint.h>

/* -------- Program Data -------- */
#define NUM_PROGRAMS     10
#define VOL_STEP_ML      100u
#define TIME_STEP_MS     100u
#define VOL_MIN_ML       100u
#define VOL_MAX_ML       9900u
#define TIME_MIN_MS      100u
#define TIME_MAX_MS      99900u

/* EEPROM base address for program storage */
#define EEPROM_PROGRAMS_ADDR  0x0000u

typedef struct {
    uint32_t volume_ml;   /* mL */
    uint32_t time_ms;     /* ms */
} Program_t;

/* -------- Application States -------- */
typedef enum {
    APP_STATE_IDLE,           /* Machine stopped – main screen */
    APP_STATE_RUNNING,        /* Auto cycle running */
    APP_STATE_SETTINGS_LIST,  /* Showing program list in settings */
    APP_STATE_EDIT_VOLUME,    /* Editing volume of selected program */
    APP_STATE_EDIT_TIME,      /* Editing time of selected program */
} AppState_t;

/* -------- Cycle sub-states -------- */
typedef enum {
    CYCLE_VALVE_ON,     /* Valve opened, waiting for proximity */
    CYCLE_MOTOR_ON,     /* Motor running, timing */
    CYCLE_MOTOR_OFF,    /* Motor stopped, 1 s delay */
    CYCLE_VALVE_OFF,    /* Valve closed, cycle done */
} CycleState_t;

void App_Init(void);
void App_Run(void);   /* Call in main while(1) */

#endif /* INC_APP_H_ */
