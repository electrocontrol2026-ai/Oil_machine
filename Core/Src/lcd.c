/*
 * lcd.c - LCD 16x2 4-bit mode driver
 *
 * Pin mapping (from main.h):
 *   RS  = LCD_RS_Pin  (PA9)
 *   EN  = LCD_EN_Pin  (PA8)
 *   D4  = LD4_Pin     (PB15)  -- mapped to LCD D4
 *   D5  = LD5_Pin     (PB14)  -- mapped to LCD D5
 *   D6  = LD6_Pin     (PB13)  -- mapped to LCD D6
 *   D7  = LD7_Pin     (PB12)  -- mapped to LCD D7
 *
 * Note: LD4..LD7 are PB15..PB12 respectively (decreasing).
 */
#include "lcd.h"
#include <string.h>
#include <stdio.h>

/* ---- Pin helpers ---- */
#define LCD_RS_SET()   HAL_GPIO_WritePin(LCD_RS_GPIO_Port, LCD_RS_Pin, GPIO_PIN_SET)
#define LCD_RS_CLR()   HAL_GPIO_WritePin(LCD_RS_GPIO_Port, LCD_RS_Pin, GPIO_PIN_RESET)
#define LCD_EN_SET()   HAL_GPIO_WritePin(LCD_EN_GPIO_Port, LCD_EN_Pin, GPIO_PIN_SET)
#define LCD_EN_CLR()   HAL_GPIO_WritePin(LCD_EN_GPIO_Port, LCD_EN_Pin, GPIO_PIN_RESET)

/*
 * LCD D4 = LD4 = PB15
 * LCD D5 = LD5 = PB14
 * LCD D6 = LD6 = PB13
 * LCD D7 = LD7 = PB12
 */
static void LCD_SetDataPins(uint8_t nibble)
{
    HAL_GPIO_WritePin(LD4_GPIO_Port, LD4_Pin, (nibble & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD5_GPIO_Port, LD5_Pin, (nibble & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD6_GPIO_Port, LD6_Pin, (nibble & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD7_GPIO_Port, LD7_Pin, (nibble & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void LCD_Pulse(void)
{
    LCD_EN_SET();
    HAL_Delay(1);
    LCD_EN_CLR();
    HAL_Delay(1);
}

static void LCD_SendNibble(uint8_t nibble)
{
    LCD_SetDataPins(nibble & 0x0F);
    LCD_Pulse();
}

static void LCD_SendByte(uint8_t data, uint8_t isData)
{
    if (isData)
        LCD_RS_SET();
    else
        LCD_RS_CLR();

    LCD_SendNibble(data >> 4);   /* High nibble first */
    LCD_SendNibble(data & 0x0F); /* Low nibble */
    HAL_Delay(2);
}

static void LCD_Cmd(uint8_t cmd)
{
    LCD_SendByte(cmd, 0);
}

static void LCD_Data(uint8_t data)
{
    LCD_SendByte(data, 1);
}

void LCD_Init(void)
{
    HAL_Delay(50); /* Power-on delay */

    LCD_RS_CLR();
    LCD_EN_CLR();

    /* 4-bit initialization sequence */
    LCD_SendNibble(0x03);
    HAL_Delay(5);
    LCD_SendNibble(0x03);
    HAL_Delay(1);
    LCD_SendNibble(0x03);
    HAL_Delay(1);
    LCD_SendNibble(0x02); /* Switch to 4-bit mode */
    HAL_Delay(1);

    LCD_Cmd(0x28); /* 4-bit, 2 lines, 5x8 font */
    LCD_Cmd(0x0C); /* Display ON, cursor OFF */
    LCD_Cmd(0x06); /* Increment cursor, no shift */
    LCD_Cmd(0x01); /* Clear display */
    HAL_Delay(2);
}

void LCD_Clear(void)
{
    LCD_Cmd(0x01);
    HAL_Delay(2);
}

void LCD_SetCursor(uint8_t row, uint8_t col)
{
    uint8_t addr = col;
    if (row == 1)
        addr |= 0x40;
    LCD_Cmd(0x80 | addr);
}

void LCD_PrintChar(char c)
{
    LCD_Data((uint8_t)c);
}

void LCD_Print(const char *str)
{
    while (*str)
    {
        LCD_Data((uint8_t)*str++);
    }
}

void LCD_PrintInt(int32_t val)
{
    char buf[12];
    snprintf(buf, sizeof(buf), "%ld", val);
    LCD_Print(buf);
}
