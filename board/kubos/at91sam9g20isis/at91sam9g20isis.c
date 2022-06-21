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

#include <netdev.h>

#define DEFAULT_WATCHDOG_COUNT 100000

DECLARE_GLOBAL_DATA_PTR;

/* ------------------------------------------------------------------------- */
/*
 * Miscellaneous platform dependent initializations
 */


#ifdef CONFIG_HW_WATCHDOG

static int wdc;

void hw_watchdog_init(void)
{
	/* Mark watchdog pin as output */
	wdc = 0;
	at91_set_pio_output(AT91_PIO_PORTA, 30, 1);
}

void hw_watchdog_reset_count(int val)
{
	int i = 0;

	if (wdc > val)
	{

		for (i = 0; i < 10; i++)
		{
			at91_set_pio_value(AT91_PIO_PORTA, 30, 0);
			at91_set_pio_value(AT91_PIO_PORTA, 30, 1);
		}

		wdc = 0;
	}

	wdc++;

	return;
}

void hw_watchdog_reset(void)
{
	hw_watchdog_reset_count(DEFAULT_WATCHDOG_COUNT);
}


void hw_watchdog_force(void)
{
	wdc = DEFAULT_WATCHDOG_COUNT + 1;

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

        /*if(!slot)
        {
            sprintf(go_cmd, "go 0x%p\n", (void *)CONFIG_STANDALONE_LOAD_ADDR);
        }
        else
        {
            sprintf(go_cmd, "go 0x%p %s\n", (void *)CONFIG_STANDALONE_LOAD_ADDR, sd_byte);
        }*/
        sprintf(go_cmd, "go 0x%p %s\n", (void *)CONFIG_STANDALONE_LOAD_ADDR, sd_byte);

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

