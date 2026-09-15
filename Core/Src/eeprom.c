/*
 * eeprom.c - AT25320 / CAT25320 SPI EEPROM driver (32Kbit = 4096 bytes)
 *
 * AT25320 SPI commands:
 *   WREN  = 0x06  (Set Write Enable Latch)
 *   WRDI  = 0x04  (Reset Write Enable Latch)
 *   RDSR  = 0x05  (Read Status Register)
 *   WRITE = 0x02  (Write Memory)
 *   READ  = 0x03  (Read Memory)
 *
 * CS pin : CS_ROM_Pin / CS_ROM_GPIO_Port  (PB0, from main.h)
 * SPI    : hspi1  (extern from main.c, bound in EEPROM_Init)
 *
 * Notes:
 *  - All READ operations use HAL_SPI_TransmitReceive to keep the
 *    SPI clock continuous across the command+address+data phases.
 *  - WRITE operations are limited to a single page (32 bytes) per
 *    SPI transaction.  EEPROM_WriteBuffer() splits longer writes
 *    automatically at page boundaries.
 *  - tWC (write cycle time) for AT25320 is max 5 ms; we wait 6 ms
 *    then poll the WIP bit.
 */

#include "eeprom.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* AT25320 command bytes                                               */
/* ------------------------------------------------------------------ */
#define CMD_WREN   0x06U   /* Write Enable        */
#define CMD_WRITE  0x02U   /* Write               */
#define CMD_READ   0x03U   /* Read                */
#define CMD_RDSR   0x05U   /* Read Status Register */

#define SR_WIP     0x01U   /* Write-In-Progress bit in status register */
#define SR_WEL     0x02U   /* Write Enable Latch   bit in status register */

/* Maximum data bytes in one SPI packet (3-byte header + up to PAGE bytes) */
#define BUF_SIZE   (3U + EEPROM_PAGE_SIZE)   /* = 35 bytes */

/* SPI timeout in ms */
#define SPI_TIMEOUT_MS   200U

/* ------------------------------------------------------------------ */
/* Private: pointer to the SPI handle (set in EEPROM_Init)            */
/* ------------------------------------------------------------------ */
extern SPI_HandleTypeDef hspi1;   /* defined by CubeMX in main.c */

static SPI_HandleTypeDef *s_hspi = NULL;

/* ------------------------------------------------------------------ */
/* Private: CS helpers                                                 */
/* ------------------------------------------------------------------ */
static inline void CS_Assert(void)
{
    HAL_GPIO_WritePin(CS_ROM_GPIO_Port, CS_ROM_Pin, GPIO_PIN_RESET);
}

static inline void CS_Deassert(void)
{
    HAL_GPIO_WritePin(CS_ROM_GPIO_Port, CS_ROM_Pin, GPIO_PIN_SET);
}

/* ------------------------------------------------------------------ */
/* Public: EEPROM_ReadStatus                                           */
/* ------------------------------------------------------------------ */
/*
 * Send [RDSR, dummy] and capture [don't-care, STATUS].
 * Uses TransmitReceive so the clock is continuous.
 */
uint8_t EEPROM_ReadStatus(void)
{
    uint8_t tx[2] = { CMD_RDSR, 0xFFU };
    uint8_t rx[2] = { 0x00U, 0x00U };

    CS_Assert();
    HAL_SPI_TransmitReceive(s_hspi, tx, rx, 2, SPI_TIMEOUT_MS);
    CS_Deassert();

    return rx[1];   /* byte[0] = don't-care echo, byte[1] = status */
}

/* ------------------------------------------------------------------ */
/* Private helpers                                                     */
/* ------------------------------------------------------------------ */

/* Poll WIP bit until cleared or 50 ms timeout */
static void WaitReady(void)
{
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 50U)
    {
        if ((EEPROM_ReadStatus() & SR_WIP) == 0U)
            break;
        HAL_Delay(1);
    }
}

/*
 * Send WREN command, then verify that WEL was set.
 * Returns 1 on success, 0 if WEL did not latch (hardware fault).
 */
static uint8_t WriteEnable(void)
{
    uint8_t cmd = CMD_WREN;

    CS_Assert();
    HAL_SPI_Transmit(s_hspi, &cmd, 1, SPI_TIMEOUT_MS);
    CS_Deassert();

    HAL_Delay(1);   /* tCSH: CS must be deasserted briefly before RDSR */

    return ((EEPROM_ReadStatus() & SR_WEL) != 0U) ? 1U : 0U;
}

/*
 * Write up to EEPROM_PAGE_SIZE bytes in a single SPI transaction.
 * The caller guarantees that (addr + len) does not cross a page boundary.
 * Returns HAL_OK or HAL_ERROR.
 */
static HAL_StatusTypeDef WritePage(uint16_t addr,
                                   const uint8_t *data,
                                   uint16_t len)
{
    /* 1. Wait for any previous write to finish */
    WaitReady();

    /* 2. Send WREN and verify WEL */
    if (!WriteEnable())
        return HAL_ERROR;   /* WEL not set – hardware problem */

    /* 3. Build packet: [WRITE, ADDR_HI, ADDR_LO, data...] */
    uint8_t pkt[BUF_SIZE];
    pkt[0] = CMD_WRITE;
    pkt[1] = (uint8_t)(addr >> 8);
    pkt[2] = (uint8_t)(addr & 0xFFU);
    memcpy(&pkt[3], data, len);

    CS_Assert();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(s_hspi, pkt, 3U + len, SPI_TIMEOUT_MS);
    CS_Deassert();

    if (st != HAL_OK)
        return st;

    /* 4. tWC: give EEPROM time to complete the internal write cycle */
    HAL_Delay(6);       /* AT25320 max tWC = 5 ms, we add 1 ms margin */
    WaitReady();        /* then poll WIP just in case */

    return HAL_OK;
}

/* ------------------------------------------------------------------ */
/* Public: EEPROM_Init                                                 */
/* ------------------------------------------------------------------ */
void EEPROM_Init(void)
{
    s_hspi = &hspi1;
    CS_Deassert();   /* ensure CS is inactive at startup */
}

/* ------------------------------------------------------------------ */
/* Public: EEPROM_ReadBuffer                                           */
/* ------------------------------------------------------------------ */
/*
 * Read 'len' bytes starting at 'addr'.
 * Uses a single continuous SPI transaction:
 *   TX: [READ, ADDR_HI, ADDR_LO, 0xFF × len]
 *   RX: [x,    x,       x,       d0, d1, ...]
 *
 * For reads larger than EEPROM_PAGE_SIZE we use a small local stack
 * buffer and call in chunks (avoids large VLA on stack).
 */
HAL_StatusTypeDef EEPROM_ReadBuffer(uint16_t addr,
                                    uint8_t *buf,
                                    uint16_t len)
{
    if (buf == NULL || len == 0U)
        return HAL_ERROR;
    if ((uint32_t)addr + len > EEPROM_SIZE_BYTES)
        return HAL_ERROR;

    WaitReady();

    uint16_t remaining = len;
    uint16_t offset    = 0U;

    while (remaining > 0U)
    {
        uint16_t chunk = (remaining > EEPROM_PAGE_SIZE) ? EEPROM_PAGE_SIZE : remaining;

        uint8_t tx[BUF_SIZE];
        uint8_t rx[BUF_SIZE];

        tx[0] = CMD_READ;
        tx[1] = (uint8_t)((addr + offset) >> 8);
        tx[2] = (uint8_t)((addr + offset) & 0xFFU);
        memset(&tx[3], 0xFFU, chunk);   /* dummy TX bytes during data phase */

        CS_Assert();
        HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(s_hspi,
                                                        tx, rx,
                                                        3U + chunk,
                                                        SPI_TIMEOUT_MS);
        CS_Deassert();

        if (st != HAL_OK)
            return st;

        memcpy(&buf[offset], &rx[3], chunk);
        offset    += chunk;
        remaining -= chunk;
    }

    return HAL_OK;
}

/* ------------------------------------------------------------------ */
/* Public: EEPROM_WriteBuffer                                          */
/* ------------------------------------------------------------------ */
/*
 * Write 'len' bytes from 'buf' to EEPROM starting at 'addr'.
 * Splits the write at page boundaries so that no single SPI
 * transaction crosses a 32-byte page.
 */
HAL_StatusTypeDef EEPROM_WriteBuffer(uint16_t addr,
                                     const uint8_t *buf,
                                     uint16_t len)
{
    if (buf == NULL || len == 0U)
        return HAL_ERROR;
    if ((uint32_t)addr + len > EEPROM_SIZE_BYTES)
        return HAL_ERROR;

    uint16_t remaining = len;
    uint16_t offset    = 0U;

    while (remaining > 0U)
    {
        uint16_t curAddr   = addr + offset;
        /* How many bytes remain in the current page? */
        uint16_t pageSpace = EEPROM_PAGE_SIZE - (curAddr % EEPROM_PAGE_SIZE);
        uint16_t chunk     = (remaining < pageSpace) ? remaining : pageSpace;

        HAL_StatusTypeDef st = WritePage(curAddr, &buf[offset], chunk);
        if (st != HAL_OK)
            return st;

        offset    += chunk;
        remaining -= chunk;
    }

    return HAL_OK;
}

/* ------------------------------------------------------------------ */
/* Public: 16-bit helpers                                              */
/* ------------------------------------------------------------------ */

HAL_StatusTypeDef EEPROM_WriteU16(uint16_t addr, uint16_t value)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(value >> 8);
    buf[1] = (uint8_t)(value & 0xFFU);
    return EEPROM_WriteBuffer(addr, buf, 2);
}

uint16_t EEPROM_ReadU16(uint16_t addr)
{
    uint8_t buf[2] = { 0x00U, 0x00U };
    if (EEPROM_ReadBuffer(addr, buf, 2) != HAL_OK)
        return 0xFFFFU;
    return ((uint16_t)buf[0] << 8) | buf[1];
}
