/*
 * (C) Copyright 2007-2008
 * Stelian Pop <stelian@popies.net>
 * Lead Tech Design <www.leadtechdesign.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 *
 * Modified for Kubos Linux:
 *   This file was originally based on at91sam920ek.c and
 *   has been modified for the at91sam9g20isis board.
 *   Extraneous options have been removed and some code
 *   to initialize the SD card port has been added.
 *   Added logic to control external watchdog.
 * Author: Catherine Freed <catherine@kubos.co>
 */

#include <common.h>
#include <asm/io.h>
#include <asm/arch/at91sam9_matrix.h>
#include <asm/arch/at91sam9_smc.h>
#include <asm/arch/at91_common.h>
#include <asm/arch/clk.h>
#include <asm/arch/gpio.h>
#include <atmel_mci.h>


#include <common.h>
#include <exports.h>
#include <spi.h>
#include "AT91SAM9G20.h"


#include <netdev.h>

#define DEFAULT_WATCHDOG_COUNT 100000

DECLARE_GLOBAL_DATA_PTR;

//these decls were in klb

#define BUS 0
#define CS 0
#define MAX_SPEED 40000000
#define MODE SPI_MODE_0
#define GET false
#define SET true

struct spi_slave * fram;

int8_t fram_open(void);
void   fram_close(void);
int8_t fram_readSD(uint8_t * sd_byte);
void fram_writeSD(uint8_t sd_byte);
int8_t fram_protect(void);
void sd_verify(uint8_t * sd_byte);
void clear_gpio(uint32_t mask);
void set_gpio(uint32_t mask);
int8_t iobc_set_sd(uint8_t sd_byte_in, bool get_set);


/* ------------------------------------------------------------------------- */
/*
 * Miscellaneous platform dependent initializations
 */


#ifdef CONFIG_HW_WATCHDOG

//static int wdc;

void hw_watchdog_init(void)
{
	/* Mark watchdog pin as output */
	gd->wdc = 0;
	at91_set_pio_output(AT91_PIO_PORTA, 30, 1);
}

void hw_watchdog_reset_count(int val)
{
	int i = 0;

	if (gd->wdc > val)
	{

		for (i = 0; i < 10; i++)
		{
			at91_set_pio_value(AT91_PIO_PORTA, 30, 0);
			at91_set_pio_value(AT91_PIO_PORTA, 30, 1);
		}

		gd->wdc = 0;
	}

	gd->wdc = gd->wdc + 1;

	return;
}

void hw_watchdog_reset(void)
{
	hw_watchdog_reset_count(DEFAULT_WATCHDOG_COUNT);
}


void hw_watchdog_force(void)
{
	gd->wdc = DEFAULT_WATCHDOG_COUNT + 1;

	hw_watchdog_reset();

	return;
}
#endif /* CONFIG_HW_WATCHDOG */

#ifdef CONFIG_SD_SWITCH
int set_mmc_slot(uint8_t slot)
{
    /*
     * Verify and then run the external binary which will detect and power
     * the appropriate SD card slot.
     */
    void * src = (void *)STANDALONE_SOURCE;
    int status = 0;
    const void * data;

    printf("In side set MMC slot \r\n");

    /* Verify that the binary is a) present and b) uncorrupted */
    if(fit_check_format(src))
    {
        int depth = 0;
        int offset = fdt_next_node(src, fdt_path_offset(src, FIT_IMAGES_PATH), &depth);

        printf("\n"); /* Fix the console output formatting */

        if(fit_image_verify(src, offset))
        {
            size_t * len = 0;
            fit_image_get_data(src, offset, &data, len);
        }
        else
        {       	
            status = -1;
        }

        printf("\n"); /* Fix the console output formatting */
    }
    else
    {
        status = -1;
    }

    if(status == 0)
    {
        /* Copy binary from flash to SDRAM */
        int size = 4;
        int count = 1000;
        void *buf = (void *)CONFIG_STANDALONE_LOAD_ADDR;
        void *from = (void *)data;

        while (count-- > 0) {
            *((u32 *)buf) = *((u32  *)from);
            from += size;
            buf += size;
        }

        char sd_byte[] = "0xnn";
        if(slot == '0')
        {
            strcpy(sd_byte, "0x01");
        }
        else if(slot == '1')
        {
            strcpy(sd_byte, "0x9F");
        }

        /* Go run it */
        char go_cmd[] = "go 0xnnnnnnnn 0xnn\n";

        if(!slot)
        {
            sprintf(go_cmd, "go 0x%p\n", (void *)CONFIG_STANDALONE_LOAD_ADDR);
        }
        else
        {
            sprintf(go_cmd, "go 0x%p %s\n", (void *)CONFIG_STANDALONE_LOAD_ADDR, sd_byte);
        }

        run_command_list(go_cmd, -1, 0);
    }

    return status;
}
#endif

#ifdef CONFIG_GENERIC_ATMEL_MCI
/* this is a weak define that we are overriding */
int board_mmc_init(bd_t *bd)
{
	at91_mci_hw_init();

#ifdef CONFIG_SD_SWITCH
    if(set_mmc_slot(0) !=0)
#endif
	{
		debug("Using default SD card\n");
		/* Turn on the SD0 power pin - value must be LOW */
		at91_set_pio_output(AT91_PIO_PORTB, 6, 0);
	}

	debug("board_mmc_init turn on power pin\r\n");

	return atmel_mci_init((void *)ATMEL_BASE_MCI);
}
#endif

int board_early_init_f(void)
{
	at91_periph_clk_enable(ATMEL_ID_PIOA);
	at91_periph_clk_enable(ATMEL_ID_PIOB);
	at91_periph_clk_enable(ATMEL_ID_PIOC);

	return 0;
}

int board_init(void)
{
	/* adress of boot parameters */
	gd->bd->bi_boot_params = CONFIG_SYS_SDRAM_BASE + 0x100;

	at91_seriald_hw_init();

#ifdef CONFIG_HW_WATCHDOG
	hw_watchdog_init();
#endif

#if defined(CONFIG_CMD_SPI) && defined(CONFIG_SD_SWITCH)
	/* Enable SPI bus 0 and CS 0 */
	at91_spi0_hw_init(1 << 0);
#endif

	return 0;
}

int dram_init(void)
{
    /*
     * This used to be = get_ram_size((void *)CONFIG_SYS_SDRAM_BASE, CONFIG_SYS_SDRAM_SIZE);
     * I ran into a bizarre heisenbug during init while adding a printf in cmd/mmc.c which
     * caused get_ram_size() to hang. No idea what the root cause is, but simply not using
     * the function doesn't appear to impact the system in any other way.
     */
	gd->ram_size = CONFIG_SYS_SDRAM_SIZE;
	return 0;
}

#ifdef CONFIG_RESET_PHY_R
void reset_phy(void)
{
}
#endif

int board_eth_init(bd_t *bis)
{
	int rc = 0;
#ifdef CONFIG_MACB
	rc = macb_eth_initialize(0, (void *)ATMEL_BASE_EMAC0, 0x00);
#endif
	return rc;
}


/* SPI chip select control */
#ifdef CONFIG_ATMEL_SPI
#include <spi.h>
int spi_cs_is_valid(unsigned int bus, unsigned int cs)
{
	return bus == 0 && cs < 2;
}

void spi_cs_activate(struct spi_slave *slave)
{
	switch (slave->cs) {
	case 0:
		at91_set_pio_output(AT91_PIO_PORTA, 3, 0);
		break;
	case 1:
		at91_set_pio_output(AT91_PIO_PORTC, 11, 0);
		break;
	case 2:
		at91_set_pio_output(AT91_PIO_PORTB, 17, 0);
		break;
	}
}

void spi_cs_deactivate(struct spi_slave *slave)
{
	switch (slave->cs) {
	case 0:
		at91_set_pio_output(AT91_PIO_PORTA, 3, 1);
		break;
	case 1:
		at91_set_pio_output(AT91_PIO_PORTC, 11, 1);
		break;
	case 2:
		at91_set_pio_output(AT91_PIO_PORTB, 17, 1);
		break;
	}
}
#endif /* CONFIG_ATMEL_SPI */

//dual boot stuff from iobc_set_sd


/* THIS FUNCTION BETTER BE FIRST. DON'T YOU DARE MOVE IT */
int8_t iobc_set_sd(uint8_t sd_byte_in, bool get_set)
{
    int8_t sd_byte = 0;
    debug("in iobc_set_sd\n");
    if (fram_open() < 0)
    {
        return -2;
    } 
    debug("FRAM opened\n");

    if (get_set == GET){
        printf("Reading slot from FRAM\n");
        if (fram_readSD(&sd_byte) < 0)
        {
            debug("Error reading SD byte. Using default SD card\n");
            sd_byte = 0;
        } else {
            printf("Readback is %i\n",sd_byte);
        }
    } else {
        printf("In set mode\n");
        sd_byte = sd_byte_in;

        /* Clear any previously set pins, since it's possible we already did MMC setup */
        clear_gpio(1 << 16);
        clear_gpio(1 << 7);
        clear_gpio(1 << 6);

        //at91_set_pio_output(AT91_PIO_PORTB, 16, 0);
        //at91_set_pio_output(AT91_PIO_PORTB, 7, 0);
        //at91_set_pio_output(AT91_PIO_PORTB, 6, 0);
        fram_writeSD(sd_byte_in);
        printf("Reading back from FRAM\n");
        uint8_t rb = 128;
        fram_readSD(&rb);
        printf("Readback is %d\n",rb);
        debug("GPIO cleared\n");
    }

    


    sd_verify(&sd_byte);

    debug("SD verified\n");

    printf("sd_byte %d\n",sd_byte);

    if (sd_byte == 1)
    {
        /* Set the SD select pin */
        set_gpio(1 << 16);
        printf("Setting GPIO pin 16 to 1\n");
        //at91_set_pio_output(AT91_PIO_PORTB, 16, 1);
        /* Turn on the SD1 power pin */
        set_gpio(1 << 7);
        printf("Setting GPIO pin 7 to 1\n");
        //at91_set_pio_output(AT91_PIO_PORTB, 7, 1);
    }
    else
    {
        //confirm that sd select pin is off
        //printf("Setting GPIO pin 16 to 0\n");
        //at91_set_pio_output(AT91_PIO_PORTB, 16, 0);
        /* Turn on the SD0 power pin */
        set_gpio(1 << 6);
        printf("Setting GPIO pin 6 to 0\n");
        //at91_set_pio_output(AT91_PIO_PORTB, 6, 0);
    }

    fram_protect();
    fram_close();
    printf("FRAM closed\n");
    return sd_byte;
}

int8_t fram_open(void)
{
    debug("In fram_open");
    fram = spi_setup_slave(BUS, CS, MAX_SPEED, MODE); 
    
    if (!fram)
    {
        printf("Unable to setup FRAM\n");
        return -1;
    }

    if (spi_claim_bus(fram))
    {
        printf("Unable to claim SPI bus\n");
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

    printf("Writing %d to FRAM\n",sd_byte);
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
