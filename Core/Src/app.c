/*
 * app.c  –  Oil Machine Application Logic
 *
 * State Machine Overview:
 * ========================
 *  APP_STATE_IDLE
 *    - Manual Pump (held)  → Motor ON  (released → Motor OFF)
 *    - UP / DOWN           → scroll selected program (0..9)
 *    - SET button          → enter APP_STATE_SETTINGS_LIST
 *    - START button (edge) → enter APP_STATE_RUNNING
 *
 *  APP_STATE_RUNNING
 *    - CYCLE_VALVE_ON  : Valve HIGH, wait for Proximity LOW (active-low)
 *    - CYCLE_MOTOR_ON  : Motor ON, run for program time_ms
 *    - CYCLE_MOTOR_OFF : Motor OFF, wait 1000 ms
 *    - CYCLE_VALVE_OFF : Valve LOW → back to IDLE
 *    - START button (edge) → stop immediately → IDLE
 *
 *  APP_STATE_SETTINGS_LIST
 *    - UP / DOWN → scroll program list
 *    - OK        → enter APP_STATE_EDIT_VOLUME for that program
 *    - BACK      → return to IDLE
 *
 *  APP_STATE_EDIT_VOLUME
 *    - UP / DOWN → change volume (step 100 mL, min 100, max 9900)
 *    - OK        → save volume, move to APP_STATE_EDIT_TIME
 *
 *  APP_STATE_EDIT_TIME
 *    - UP / DOWN → change time (step 100 ms, min 100, max 99900)
 *    - OK        → save both to EEPROM, show "Saved!", return to SETTINGS_LIST
 *
 * All inputs are Active-LOW.
 * Button debounce: 30 ms.
 */

#include "app.h"
#include "lcd.h"
#include "eeprom.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Macros                                                              */
/* ------------------------------------------------------------------ */

/* Active-LOW read: returns 1 when pressed */
#define BTN_PRESSED(port, pin)  (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET)

#define MOTOR_ON()    HAL_GPIO_WritePin(MOTOR_GPIO_Port,  MOTOR_Pin,  GPIO_PIN_SET)
#define MOTOR_OFF()   HAL_GPIO_WritePin(MOTOR_GPIO_Port,  MOTOR_Pin,  GPIO_PIN_RESET)
#define VALVE_ON()    HAL_GPIO_WritePin(VALVE_GPIO_Port,  VALVE_Pin,  GPIO_PIN_SET)
#define VALVE_OFF()   HAL_GPIO_WritePin(VALVE_GPIO_Port,  VALVE_Pin,  GPIO_PIN_RESET)
#define BUZZER_ON()   HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET)
#define BUZZER_OFF()  HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET)

#define DEBOUNCE_MS       30u
#define MOTOR_STOP_DELAY  2000u   /* ms between motor off and valve off */

/* ------------------------------------------------------------------ */
/* EEPROM layout: 10 programs × 8 bytes each                          */
/*   [0..3]  = volume_ml  (uint32_t, little-endian)                   */
/*   [4..7]  = time_ms    (uint32_t, little-endian)                   */
/* ------------------------------------------------------------------ */
#define EEPROM_PROG_SIZE  8u

/* ------------------------------------------------------------------ */
/* Static variables                                                    */
/* ------------------------------------------------------------------ */

static Program_t  g_programs[NUM_PROGRAMS];
static AppState_t g_appState   = APP_STATE_IDLE;
static CycleState_t g_cycleState = CYCLE_VALVE_ON;

static uint8_t  g_selectedProg  = 0;   /* 0..9 – main screen and settings */
static uint8_t  g_editProg      = 0;   /* program being edited */
static uint32_t g_editVolume    = 0;
static uint32_t g_editTime      = 0;

/* Timing */
static uint32_t g_cycleTimer    = 0;

/* Display dirty flag – redraw only when needed */
static uint8_t g_displayDirty  = 1;

/* ------------------------------------------------------------------ */
/* Button edge detection helpers                                       */
/* ------------------------------------------------------------------ */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       state;       /* debounced stable state (0 or 1) */
    uint8_t       lastRaw;     /* last sample */
    uint32_t      lastChange;  /* tick when raw changed */
} Button_t;

static Button_t b_start     = {START_BUTTON_GPIO_Port,    START_BUTTON_Pin,    0, 0, 0};
static Button_t b_manualPump= {MANUAL_PUMP_GPIO_Port,     MANUAL_PUMP_Pin,     0, 0, 0};
static Button_t b_set       = {SETTINGS_SWITCH_GPIO_Port, SETTINGS_SWITCH_Pin, 0, 0, 0};
static Button_t b_back      = {BACK_SWITCH_GPIO_Port,     BACK_SWITCH_Pin,     0, 0, 0};
static Button_t b_ok        = {OK_SWITCH_GPIO_Port,       OK_SWITCH_Pin,       0, 0, 0};
static Button_t b_up        = {UP_SWITCH_GPIO_Port,       UP_SWITCH_Pin,       0, 0, 0};
static Button_t b_down      = {DOWN_SWITCH_GPIO_Port,     DOWN_SWITCH_Pin,     0, 0, 0};

/* 35ms stable debounce */
static uint8_t Btn_Edge(Button_t *b)
{
    uint8_t  raw = BTN_PRESSED(b->port, b->pin);
    uint32_t now = HAL_GetTick();

    if (raw != b->lastRaw)
    {
        b->lastRaw = raw;
        b->lastChange = now;
    }

    if ((now - b->lastChange) >= 35U)
    {
        if (b->state != raw)
        {
            b->state = raw;
            if (b->state == 1)
            {
                return 1; /* Rising edge detected */
            }
        }
    }
    return 0;
}

/* Check if button is currently held (active debounced level) */
static uint8_t Btn_Held(Button_t *b)
{
    uint8_t  raw = BTN_PRESSED(b->port, b->pin);
    uint32_t now = HAL_GetTick();

    if (raw != b->lastRaw)
    {
        b->lastRaw = raw;
        b->lastChange = now;
    }

    if ((now - b->lastChange) >= 20U)
    {
        b->state = raw;
    }
    return b->state;
}

/* ------------------------------------------------------------------ */
/* EEPROM persistence                                                  */
/* ------------------------------------------------------------------ */

static void Programs_Load(void)
{
    for (uint8_t i = 0; i < NUM_PROGRAMS; i++)
    {
        uint16_t addr = EEPROM_PROGRAMS_ADDR + (i * EEPROM_PROG_SIZE);
        uint8_t  buf[EEPROM_PROG_SIZE];
        EEPROM_ReadBuffer(addr, buf, EEPROM_PROG_SIZE);

        uint32_t vol  = (uint32_t)buf[0]
                      | ((uint32_t)buf[1] << 8)
                      | ((uint32_t)buf[2] << 16)
                      | ((uint32_t)buf[3] << 24);
        uint32_t tim  = (uint32_t)buf[4]
                      | ((uint32_t)buf[5] << 8)
                      | ((uint32_t)buf[6] << 16)
                      | ((uint32_t)buf[7] << 24);

        /* Validate – if EEPROM was blank (0xFFFFFFFF), use defaults */
        if (vol == 0xFFFFFFFF || vol < VOL_MIN_ML || vol > VOL_MAX_ML)
            vol = VOL_MIN_ML;
        if (tim == 0xFFFFFFFF || tim < TIME_MIN_MS || tim > TIME_MAX_MS)
            tim = TIME_MIN_MS;

        g_programs[i].volume_ml = vol;
        g_programs[i].time_ms   = tim;
    }
}

static void Programs_Save(uint8_t idx)
{
    uint16_t addr = EEPROM_PROGRAMS_ADDR + (idx * EEPROM_PROG_SIZE);
    uint8_t  buf[EEPROM_PROG_SIZE];
    uint32_t vol = g_programs[idx].volume_ml;
    uint32_t tim = g_programs[idx].time_ms;

    buf[0] = (uint8_t)(vol & 0xFF);
    buf[1] = (uint8_t)((vol >> 8)  & 0xFF);
    buf[2] = (uint8_t)((vol >> 16) & 0xFF);
    buf[3] = (uint8_t)((vol >> 24) & 0xFF);
    buf[4] = (uint8_t)(tim & 0xFF);
    buf[5] = (uint8_t)((tim >> 8)  & 0xFF);
    buf[6] = (uint8_t)((tim >> 16) & 0xFF);
    buf[7] = (uint8_t)((tim >> 24) & 0xFF);

    EEPROM_WriteBuffer(addr, buf, EEPROM_PROG_SIZE);
}

/* ------------------------------------------------------------------ */
/* Display helpers                                                     */
/* ------------------------------------------------------------------ */

/* Pad a string with spaces to exactly 'width' chars and null-terminate */
static void PadStr(char *dst, const char *src, uint8_t width)
{
    uint8_t i = 0;
    while (src[i] && i < width) { dst[i] = src[i]; i++; }
    while (i < width) { dst[i++] = ' '; }
    dst[i] = '\0';
}

/*
 * Main / Idle screen:
 *   Row 0: "MAIN: Prog  1"
 *   Row 1: " 100mL   100ms"
 */
static void Display_Idle(void)
{
    char buf[17];
    char tmp[17];

    LCD_Clear();

    /* Row 0: Program selector */
    snprintf(tmp, sizeof(tmp), "MAIN: Prog %2d", g_selectedProg + 1);
    PadStr(buf, tmp, 16);
    LCD_SetCursor(0, 0);
    LCD_Print(buf);

    /* Row 1: Volume and Time */
    snprintf(tmp, sizeof(tmp), "%4lumL  %5lums",
             g_programs[g_selectedProg].volume_ml,
             g_programs[g_selectedProg].time_ms);
    PadStr(buf, tmp, 16);
    LCD_SetCursor(1, 0);
    LCD_Print(buf);
}

/* Running screen */
static void Display_Running(void)
{
    char buf[17];
    char tmp[17];

    LCD_Clear();

    PadStr(buf, "** RUNNING **", 16);
    LCD_SetCursor(0, 0);
    LCD_Print(buf);

    snprintf(tmp, sizeof(tmp), "Prog: %d", g_selectedProg + 1);
    PadStr(buf, tmp, 16);
    LCD_SetCursor(1, 0);
    LCD_Print(buf);
}

/*
 * Settings list screen:
 *   Row 0: "SET: Program  1"
 *   Row 1: "OK:Edit BACK:Ret"
 */
static void Display_SettingsList(void)
{
    char buf[17];
    char tmp[17];

    LCD_Clear();

    snprintf(tmp, sizeof(tmp), "SET: Program %2d", g_editProg + 1);
    PadStr(buf, tmp, 16);
    LCD_SetCursor(0, 0);
    LCD_Print(buf);

    snprintf(tmp, sizeof(tmp), "OK:Edit BACK:Ret");
    PadStr(buf, tmp, 16);
    LCD_SetCursor(1, 0);
    LCD_Print(buf);
}

/*
 * Edit Volume screen:
 *   Row 0: "Prog 1: EDIT VOL"
 *   Row 1: "Vol:  100 mL"
 */
static void Display_EditVolume(void)
{
    char buf[17];
    char tmp[17];

    LCD_Clear();

    snprintf(tmp, sizeof(tmp), "P%d EDIT VOLUME", g_editProg + 1);
    PadStr(buf, tmp, 16);
    LCD_SetCursor(0, 0);
    LCD_Print(buf);

    snprintf(tmp, sizeof(tmp), "Vol: %4lu mL", g_editVolume);
    PadStr(buf, tmp, 16);
    LCD_SetCursor(1, 0);
    LCD_Print(buf);
}

/*
 * Edit Time screen:
 *   Row 0: "Prog 1: EDIT TIME"
 *   Row 1: "Time:  100 ms"
 */
static void Display_EditTime(void)
{
    char buf[17];
    char tmp[17];

    LCD_Clear();

    snprintf(tmp, sizeof(tmp), "P%d EDIT TIME", g_editProg + 1);
    PadStr(buf, tmp, 16);
    LCD_SetCursor(0, 0);
    LCD_Print(buf);

    snprintf(tmp, sizeof(tmp), "Time: %5lu ms", g_editTime);
    PadStr(buf, tmp, 16);
    LCD_SetCursor(1, 0);
    LCD_Print(buf);
}

/* Short buzzer beep */
static void Beep(void)
{
    BUZZER_ON();
    HAL_Delay(80);
    BUZZER_OFF();
}

/* ------------------------------------------------------------------ */
/* State transition helper with guard time                             */
/* ------------------------------------------------------------------ */
static uint32_t g_lastSwitchTime = 0;

static void SwitchState(AppState_t nextState)
{
    g_appState = nextState;
    g_displayDirty = 1;
    g_lastSwitchTime = HAL_GetTick();
}

/* ------------------------------------------------------------------ */
/* App_Init                                                            */
/* ------------------------------------------------------------------ */

void App_Init(void)
{
    EEPROM_Init();
    LCD_Init();

    VALVE_OFF();
    MOTOR_OFF();
    BUZZER_OFF();

    Programs_Load();

    g_appState     = APP_STATE_IDLE;
    g_selectedProg = 0;
    g_editProg     = 0;
    g_displayDirty = 1;

    /* Splash */
    LCD_Clear();
    LCD_SetCursor(0, 2);
    LCD_Print("Oil Machine");
    LCD_SetCursor(1, 3);
    LCD_Print("Starting...");
    HAL_Delay(1500);
}

/* ------------------------------------------------------------------ */
/* App_Run  (call in while(1))                                         */
/* ------------------------------------------------------------------ */

void App_Run(void)
{
    uint32_t now = HAL_GetTick();

    /* Read button edges */
    uint8_t edge_start  = Btn_Edge(&b_start);
    uint8_t edge_set    = Btn_Edge(&b_set);
    uint8_t edge_back   = Btn_Edge(&b_back);
    uint8_t edge_ok     = Btn_Edge(&b_ok);
    uint8_t edge_up     = Btn_Edge(&b_up);
    uint8_t edge_down   = Btn_Edge(&b_down);
    uint8_t held_manual = Btn_Held(&b_manualPump);

    /* Guard time: ignore transition buttons (OK, BACK, SET) for 300ms
       after state changes to prevent bleed-through */
    uint8_t canConfirm = ((now - g_lastSwitchTime) >= 300U);

    /* ================================================================ */
    switch (g_appState)
    {
    /* ============================================================== */
    case APP_STATE_IDLE:
    {
        /* Manual pump – motor follows button */
        if (held_manual)
            MOTOR_ON();
        else
            MOTOR_OFF();

        /* UP / DOWN – switch program */
        if (edge_up)
        {
            if (g_selectedProg < NUM_PROGRAMS - 1) g_selectedProg++;
            else                                    g_selectedProg = 0;
            g_displayDirty = 1;
        }
        if (edge_down)
        {
            if (g_selectedProg > 0)        g_selectedProg--;
            else                           g_selectedProg = NUM_PROGRAMS - 1;
            g_displayDirty = 1;
        }

        /* SET → Enter settings mode immediately */
        if (edge_set && canConfirm)
        {
            g_editProg = g_selectedProg;
            SwitchState(APP_STATE_SETTINGS_LIST);
            Beep();
            break;
        }

        /* START → Begin auto cycle */
        if (edge_start)
        {
            MOTOR_OFF();
            VALVE_ON();
            g_cycleState   = CYCLE_VALVE_ON;
            SwitchState(APP_STATE_RUNNING);
            Beep();
            break;
        }

        if (g_displayDirty)
        {
            Display_Idle();
            g_displayDirty = 0;
        }
        break;
    }

    /* ============================================================== */
    case APP_STATE_RUNNING:
    {
        /* START again → stop cycle immediately */
        if (edge_start)
        {
            MOTOR_OFF();
            VALVE_OFF();
            SwitchState(APP_STATE_IDLE);
            Beep();
            break;
        }

        if (g_displayDirty)
        {
            Display_Running();
            g_displayDirty = 0;
        }

        /* Cycle state machine */
        switch (g_cycleState)
        {
        case CYCLE_VALVE_ON:
        {
            if (BTN_PRESSED(METAL_SENSOR_GPIO_Port, METAL_SENSOR_Pin))
            {
                MOTOR_ON();
                g_cycleTimer = HAL_GetTick();
                g_cycleState = CYCLE_MOTOR_ON;
            }
            break;
        }

        case CYCLE_MOTOR_ON:
        {
            uint32_t elapsed = HAL_GetTick() - g_cycleTimer;
            if (elapsed >= g_programs[g_selectedProg].time_ms)
            {
                MOTOR_OFF();
                g_cycleTimer = HAL_GetTick();
                g_cycleState = CYCLE_MOTOR_OFF;
            }
            break;
        }

        case CYCLE_MOTOR_OFF:
        {
            if ((HAL_GetTick() - g_cycleTimer) >= MOTOR_STOP_DELAY)
            {
                VALVE_OFF();
                g_cycleTimer = HAL_GetTick();
                g_cycleState = CYCLE_VALVE_OFF;
            }
            break;
        }

        case CYCLE_VALVE_OFF:
        {
            Beep();
            SwitchState(APP_STATE_IDLE);
            break;
        }
        }
        break;
    }

    /* ============================================================== */
    case APP_STATE_SETTINGS_LIST:
    {
        /* UP / DOWN – select program to configure */
        if (edge_up)
        {
            if (g_editProg < NUM_PROGRAMS - 1) g_editProg++;
            else                               g_editProg = 0;
            g_displayDirty = 1;
        }
        if (edge_down)
        {
            if (g_editProg > 0) g_editProg--;
            else                g_editProg = NUM_PROGRAMS - 1;
            g_displayDirty = 1;
        }

        /* OK → Select program and enter volume editing */
        if (edge_ok && canConfirm)
        {
            g_editVolume = g_programs[g_editProg].volume_ml;
            g_editTime   = g_programs[g_editProg].time_ms;
            SwitchState(APP_STATE_EDIT_VOLUME);
            Beep();
            break;
        }

        /* BACK → Exit settings mode and return to Main Page */
        if (edge_back && canConfirm)
        {
            g_selectedProg = g_editProg;
            SwitchState(APP_STATE_IDLE);
            Beep();
            break;
        }

        if (g_displayDirty)
        {
            Display_SettingsList();
            g_displayDirty = 0;
        }
        break;
    }

    /* ============================================================== */
    case APP_STATE_EDIT_VOLUME:
    {
        /* UP / DOWN – increase or decrease volume */
        if (edge_up)
        {
            if (g_editVolume < VOL_MAX_ML) g_editVolume += VOL_STEP_ML;
            else                           g_editVolume  = VOL_MIN_ML;
            g_displayDirty = 1;
        }
        if (edge_down)
        {
            if (g_editVolume > VOL_MIN_ML) g_editVolume -= VOL_STEP_ML;
            else                           g_editVolume  = VOL_MAX_ML;
            g_displayDirty = 1;
        }

        /* OK → Confirm volume and proceed to edit time */
        if (edge_ok && canConfirm)
        {
            g_programs[g_editProg].volume_ml = g_editVolume;
            SwitchState(APP_STATE_EDIT_TIME);
            Beep();
            break;
        }

        /* BACK → Cancel volume edit, return to Settings list */
        if (edge_back && canConfirm)
        {
            SwitchState(APP_STATE_SETTINGS_LIST);
            Beep();
            break;
        }

        if (g_displayDirty)
        {
            Display_EditVolume();
            g_displayDirty = 0;
        }
        break;
    }

    /* ============================================================== */
    case APP_STATE_EDIT_TIME:
    {
        /* UP / DOWN – increase or decrease time */
        if (edge_up)
        {
            if (g_editTime < TIME_MAX_MS) g_editTime += TIME_STEP_MS;
            else                          g_editTime  = TIME_MIN_MS;
            g_displayDirty = 1;
        }
        if (edge_down)
        {
            if (g_editTime > TIME_MIN_MS) g_editTime -= TIME_STEP_MS;
            else                          g_editTime  = TIME_MAX_MS;
            g_displayDirty = 1;
        }

        /* OK → Confirm time, save to EEPROM, and return to Settings list */
        if (edge_ok && canConfirm)
        {
            g_programs[g_editProg].time_ms = g_editTime;

            /* Save to EEPROM */
            Programs_Save(g_editProg);

            /* Show confirmation */
            LCD_Clear();
            LCD_SetCursor(0, 1);
            LCD_Print("Saved Prog ");
            LCD_PrintInt(g_editProg + 1);
            LCD_SetCursor(1, 2);
            LCD_Print("Successfully!");
            Beep();
            HAL_Delay(100);
            Beep();
            HAL_Delay(1200);

            /* Return to settings list */
            SwitchState(APP_STATE_SETTINGS_LIST);
            break;
        }

        /* BACK → Go back to edit volume */
        if (edge_back && canConfirm)
        {
            SwitchState(APP_STATE_EDIT_VOLUME);
            Beep();
            break;
        }

        if (g_displayDirty)
        {
            Display_EditTime();
            g_displayDirty = 0;
        }
        break;
    }

    } /* end main switch */
}
