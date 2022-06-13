/*
* Copyright (C) 2017 Kubos Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* This program is intended to be compiled and run as a standalone U-Boot
* binary in compliance with their GPL exception.
*/

#include <common.h>
#include <exports.h>
#include <spi.h>
#include "AT91SAM9G20.h"
#include <types.h>

#define BUS 0
#define CS 0
#define MAX_SPEED 40000000
#define MODE SPI_MODE_0

struct spi_slave * fram;

int8_t fram_open(void);
void   fram_close(void);
int8_t fram_readSD(uint8_t * sd_byte);
void fram_writeSD(uint8_t sd_byte);
int8_t fram_protect(void);
void sd_verify(uint8_t * sd_byte);
void clear_gpio(uint32_t mask);
void set_gpio(uint32_t mask);

/* THIS FUNCTION BETTER BE FIRST. DON'T YOU DARE MOVE IT */
int8_t iobc_set_sd(int8_t argc, char * const argv[])
{
    /* Load the U-Boot jump table */
    app_startup(argv);

    /*
     * Check the ABI version. It's unlikely to change, but if it does, we'll
     * need to manually check for incompatibilities.
     */
    if (XF_VERSION != get_version())
    {
        printf("ABI version mismatch: %d vs %lu\n", XF_VERSION, get_version());
        return -1;
    }

    if (fram_open() < 0)
    {
        return -2;
    }

    uint8_t sd_byte = 0;

    if (argc == 1)
    {
        if (fram_readSD(&sd_byte) < 0)
        {
            debug("Error reading SD byte. Using default SD card\n");
            sd_byte = 0;
        }
    }
    else
    {
        sd_byte = (uint8_t) strtoul(argv[1], NULL, 0);

        /* Clear any previously set pins, since it's possible we already did MMC setup */
        clear_gpio(1 << 16);
        clear_gpio(1 << 7);
        clear_gpio(1 << 6);
    }

    sd_verify(&sd_byte);

    if (sd_byte == 1)
    {
        /* Set the SD select pin */
        set_gpio(1 << 16);

        /* Turn on the SD1 power pin */
        set_gpio(1 << 7);
    }
    else
    {
        /* Turn on the SD0 power pin */
        set_gpio(1 << 6);
    }

    fram_protect();

    fram_close();

    return sd_byte;
}

int8_t fram_open(void)
{
    fram = spi_setup_slave(BUS, CS, MAX_SPEED, MODE);
    if (!fram)
    {
        puts("Unable to setup FRAM\n");
        return -1;
    }

    if (spi_claim_bus(fram))
    {
        puts("Unable to claim SPI bus\n");
        spi_free_slave(fram);
        return -1;
    }

    return 0;
}

void fram_close(void)
{
    spi_release_bus(fram);
    spi_free_slave(fram);
    return;
}

/* Read the sd_byte value from FRAM */
int8_t fram_readSD(uint8_t * sd_byte)
{
    /* Read: 0x03, Addr: 0x030000, 1 read byte */
    char buf[5] = { 0 };
    buf[0]      = 0x03;
    buf[1]      = 0x03;

    /* The BEGIN/END flags tell U-Boot to do the CS work for us */
    if (spi_xfer(fram, 8 * sizeof(buf), buf, buf, SPI_XFER_BEGIN | SPI_XFER_END)
        != 0)
    {
        puts("Read: Hit ATMEL_SPI_SR_OVRES error state\n");
        return -1;
    }

    *sd_byte = buf[4];

    return 0;
}

uint8_t count_ones(uint8_t byte)
{
    static const uint8_t NIBBLE_LOOKUP[16]
        = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };

    return NIBBLE_LOOKUP[byte & 0x0F] + NIBBLE_LOOKUP[byte >> 4];
}

/*
 * sd_verify
 *
 * This function will examine the read sd_byte value.
 * Ideally the byte will be either all 1's or 0's, but space is a dangerous
 * place, so some bits might have gotten flipped.
 * If the value is mostly 1's, then we'll write 0xFF back into storage and use
 * SD slot 0. Otherwise, we'll write 0x00 and use SD slot 0.
 */
void sd_verify(uint8_t * sd_byte)
{
    uint8_t ones = count_ones(*sd_byte);

    *sd_byte = 0;

    if (ones > 4)
    {
        *sd_byte = 1;
        if (ones != 8)
        {
            fram_writeSD(0xFF);
        }
    }
    else
    {
        if (ones == 4)
        {
            puts("Unable to determine SD slot value. Using default SD card\n");
        }
        if (ones != 0)
        {
            fram_writeSD(0);
        }
    }
}

/* Write the sd_byte value to FRAM */
void fram_writeSD(uint8_t sd_byte)
{
    /* Enable writes (this includes the status register */
    char wren = 0x06;
    if (spi_xfer(fram, 8, &wren, &wren, SPI_XFER_BEGIN | SPI_XFER_END) != 0)
    {
        puts("WREN: Hit ATMEL_SPI_SR_OVRES error state\n");
        return;
    }

    /* WRSR: 0x01, Value: Turn off write protection */
    char wrsr_buf[2];
    wrsr_buf[0] = 0x01;
    wrsr_buf[1] = 0x00;

    if (spi_xfer(fram, 8 * sizeof(wrsr_buf), wrsr_buf, wrsr_buf, SPI_XFER_BEGIN | SPI_XFER_END)
        != 0)
    {
        puts("WRSR: Hit ATMEL_SPI_SR_OVRES error state\n");
        return;
    }

    /* Enable writes (this includes the status register */
    wren = 0x06;
    if (spi_xfer(fram, 8, &wren, &wren, SPI_XFER_BEGIN | SPI_XFER_END) != 0)
    {
        puts("WREN: Hit ATMEL_SPI_SR_OVRES error state\n");
        return;
    }

    /* Write: 0x02, Addr: 0x030000, 1 data byte */
    char write_buf[5] = { 0 };
    write_buf[0]      = 0x02;
    write_buf[1]      = 0x03;
    write_buf[4]      = sd_byte;

    /* The BEGIN/END flags tell U-Boot to do the CS work for us */
    if (spi_xfer(fram, 8 * sizeof(write_buf), write_buf, write_buf, SPI_XFER_BEGIN | SPI_XFER_END)
        != 0)
    {
        puts("Read: Hit ATMEL_SPI_SR_OVRES error state\n");
        return;
    }

    return;
}

/* Make sure our data area is protected from users */
int8_t fram_protect(void)
{
    /* Enable writes (this includes the status register */
    char wren = 0x06;
    if (spi_xfer(fram, 8, &wren, &wren, SPI_XFER_BEGIN | SPI_XFER_END) != 0)
    {
        puts("WREN: Hit ATMEL_SPI_SR_OVRES error state\n");
        return -1;
    }

    /* WRSR: 0x01, Value: Protect upper 1/4 */
    char buf[2];
    buf[0] = 0x01;
    buf[1] = 0x04;

    if (spi_xfer(fram, 8 * sizeof(buf), buf, buf, SPI_XFER_BEGIN | SPI_XFER_END)
        != 0)
    {
        puts("WRSR: Hit ATMEL_SPI_SR_OVRES error state\n");
        return -1;
    }

    return 0;
}

/* Turn off the requested GPIO */
void clear_gpio(uint32_t mask)
{

    AT91PS_PIO pio = AT91C_BASE_PIOB;

    pio->PIO_IDR   = mask;
    pio->PIO_PPUDR = mask;
    pio->PIO_SODR  = mask;
    pio->PIO_OER   = mask;
    pio->PIO_PER   = mask;
}


/* Turn on the requested GPIO */
void set_gpio(uint32_t mask)
{

    AT91PS_PIO pio = AT91C_BASE_PIOB;

    pio->PIO_IDR   = mask;
    pio->PIO_PPUDR = mask;
    pio->PIO_CODR  = mask;
    pio->PIO_OER   = mask;
    pio->PIO_PER   = mask;
}
