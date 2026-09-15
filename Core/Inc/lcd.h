/*
 * lcd.h - LCD 16x2 4-bit mode driver
 * Pins: RS=PA9, EN=PA8, D4=PB12, D5=PB13, D6=PB14, D7=PB15
 */
#ifndef INC_LCD_H_
#define INC_LCD_H_

#include "main.h"
#include <stdint.h>

void LCD_Init(void);
void LCD_Clear(void);
void LCD_SetCursor(uint8_t row, uint8_t col);
void LCD_Print(const char *str);
void LCD_PrintChar(char c);
void LCD_PrintInt(int32_t val);

#endif /* INC_LCD_H_ */
