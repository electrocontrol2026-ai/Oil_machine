/*
 * eeprom.h - AT25320 / CAT25320 SPI EEPROM driver (32Kbit = 4096 bytes)
 *
 * Hardware:
 *   SPI  : SPI1 (hspi1 – defined in main.c)
 *   CS   : CS_ROM_Pin / CS_ROM_GPIO_Port  (PB0, defined in main.h)
 *
 * Public API (no hspi argument – handle is bound once in EEPROM_Init):
 *   EEPROM_Init()
 *   EEPROM_ReadBuffer (addr, buf, len)
 *   EEPROM_WriteBuffer(addr, buf, len)   – handles page-boundary splits
 *   EEPROM_WriteU16  (addr, value)
 *   EEPROM_ReadU16   (addr)             -> uint16_t
 *   EEPROM_ReadStatus()                 -> uint8_t
 */

#ifndef INC_EEPROM_H_
#define INC_EEPROM_H_

#include "main.h"
#include <stdint.h>

/* Total capacity and page size */
#define EEPROM_SIZE_BYTES  4096U
#define EEPROM_PAGE_SIZE     32U

/* ------------------------------------------------------------------ */
/* Initialisation                                                      */
/* ------------------------------------------------------------------ */
/**
 * @brief Bind the driver to hspi1 and de-assert CS.
 *        Must be called after MX_SPI1_Init() and before any read/write.
 */
void EEPROM_Init(void);

/* ------------------------------------------------------------------ */
/* Core read / write                                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Read 'len' bytes from EEPROM address 'addr' into 'buf'.
 * @return HAL_OK on success, HAL_ERROR on bad args, HAL_TIMEOUT on timeout.
 */
HAL_StatusTypeDef EEPROM_ReadBuffer(uint16_t addr,
                                    uint8_t *buf,
                                    uint16_t len);

/**
 * @brief Write 'len' bytes from 'buf' to EEPROM starting at 'addr'.
 *        Automatically splits writes across page boundaries.
 * @return HAL_OK on success, HAL_ERROR on bad args or WEL failure.
 */
HAL_StatusTypeDef EEPROM_WriteBuffer(uint16_t addr,
                                     const uint8_t *buf,
                                     uint16_t len);

/* ------------------------------------------------------------------ */
/* 16-bit convenience helpers                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Write a uint16_t (big-endian) to EEPROM address.
 */
HAL_StatusTypeDef EEPROM_WriteU16(uint16_t addr, uint16_t value);

/**
 * @brief Read a uint16_t (big-endian) from EEPROM address.
 * @return The value read, or 0xFFFF on error.
 */
uint16_t EEPROM_ReadU16(uint16_t addr);

/* ------------------------------------------------------------------ */
/* Diagnostic                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Read the AT25320 Status Register (RDSR).
 *        Bit 0 = WIP (Write-In-Progress), Bit 1 = WEL.
 */
uint8_t EEPROM_ReadStatus(void);

#endif /* INC_EEPROM_H_ */