/**
 * \file
 *
 * \brief Unit tests for at24cxx driver.
 *
 * Copyright (c) 2011 - 2013 Atmel Corporation. All rights reserved.
 *
 * \asf_license_start
 *
 * \page License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. The name of Atmel may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. This software may only be redistributed and used in connection with an
 *    Atmel microcontroller product.
 *
 * THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * EXPRESSLY AND SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \asf_license_stop
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include <board.h>
#include <sysclk.h>
#include <string.h>
#include <unit_test/suite.h>
#include <stdio_serial.h>
#include <conf_clock.h>
#include <conf_board.h>
#include <conf_test.h>
#include <at24cxx.h>
#include "twi.h"

/**
 * \mainpage
 *
 * \section intro Introduction
 * This is the unit test application for the at24cxx driver.
 * It consists of test cases for the following functionality:
 * - at24cxx data read/write
 *
 * \section files Main Files
 * - \ref unit_tests.c
 * - \ref conf_test.h
 * - \ref conf_board.h
 * - \ref conf_clock.h
 * - \ref conf_usart_serial.h
 *
 * \section device_info Device Info
 * SAM3X devices can be used.
 * This example has been tested with the following setup:
 * - sam3x8h_sam3x_ek
 *
 * \section compinfo Compilation info
 * This software was written for the GNU GCC and IAR for ARM. Other compilers
 * may or may not work.
 *
 * \section contactinfo Contact Information
 * For further information, visit <a href="http://www.atmel.com/">Atmel</a>.\n
 * Support and FAQ: http://support.atmel.no/
 */

//! \name Unit test configuration
//@{
/**
 * \def CONF_TEST_USART
 * \brief USART to redirect STDIO to
 */
/**
 * \def CONF_TEST_BAUDRATE
 * \brief Baudrate of USART
 */
/**
 * \def CONF_TEST_CHARLENGTH
 * \brief Character length (bits) of USART
 */
/**
 * \def CONF_TEST_PARITY
 * \brief Parity mode of USART
 */
/**
 * \def CONF_TEST_STOPBITS
 * \brief Stopbit configuration of USART
 */
//@}

/** Memory Start Address of AT24CXX chips */
#define AT24C_MEM_ADDR  0
/** TWI Bus Clock 400kHz */
#define AT24C_TWI_CLK   400000

/** Global timestamp in milliseconds since the start of application */
volatile uint32_t dw_ms_ticks = 0;

/**
 *  \brief Handler for System Tick interrupt.
 *
 *  Process System Tick Event
 *  increments the timestamp counter.
 */
void SysTick_Handler(void)
{
	dw_ms_ticks++;
}

/**
 *  \brief Wait for the given number of milliseconds (using the dwTimeStamp
 *         generated by the SAM3 microcontrollers' system tick).
 *  \param dw_dly_ticks  Delay to wait for, in milliseconds.
 */
static void mdelay(uint32_t dw_dly_ticks)
{
	uint32_t dw_cur_ticks;

	dw_cur_ticks = dw_ms_ticks;
	while ((dw_ms_ticks - dw_cur_ticks) < dw_dly_ticks);
}

/**
 * \brief Test data read/write API functions.
 *
 * This test calls the data read/write API functions and check the data consistency.
 *
 * \param test Current test case.
 */
static void run_test_data_read_write(const struct test_case *test)
{
	twi_options_t opt;
	uint8_t data;
	
	/* Configure the options of TWI driver */
	opt.master_clk = sysclk_get_main_hz();
	opt.speed = AT24C_TWI_CLK;

	if (twi_master_init(BOARD_AT24C_TWI_INSTANCE, &opt) != TWI_SUCCESS) {
		puts("AT24CXX initialization is failed.\r");
	}

	if (at24cxx_write_byte(AT24C_MEM_ADDR, 0xA5) != AT24C_WRITE_SUCCESS) {
		puts("AT24CXX write packet is failed.\r");
	}

	mdelay(10);

	if (at24cxx_read_byte(AT24C_MEM_ADDR,  &data) != AT24C_READ_SUCCESS) {
		puts("AT24CXX read packet is failed.\r");
	}
	test_assert_true(test, data == 0xA5, "Data is not consistent!");
}

/**
 * \brief Run ili9325 driver unit tests.
 */
int main(void)
{
	const usart_serial_options_t usart_serial_options = {
		.baudrate   = CONF_TEST_BAUDRATE,
		.charlength = CONF_TEST_CHARLENGTH,
		.paritytype = CONF_TEST_PARITY,
		.stopbits   = CONF_TEST_STOPBITS
	};

	sysclk_init();
	board_init();
	/* reset EEPROM state to release TWI */
	at24cxx_reset();

	sysclk_enable_peripheral_clock(CONSOLE_UART_ID);
	stdio_serial_init(CONF_TEST_USART, &usart_serial_options);

	/* Configure systick for 1 ms */
	puts("Configure system tick to get 1 ms tick period.\r");
	if (SysTick_Config(sysclk_get_cpu_hz() / 1000)) {
		puts("-E- Systick configuration error\r");
		while (1) {
			/* Capture error */
		}
	}

	/* Enable peripheral clock */
	pmc_enable_periph_clk(BOARD_ID_TWI_EEPROM);

	/* Define all the test cases */
	DEFINE_TEST_CASE(at24cxx_test_data_read_write, NULL, run_test_data_read_write, NULL,
			"at24cxx read and write data test");

	/* Put test case addresses in an array */
	DEFINE_TEST_ARRAY(at24cxx_test_array) = {
		&at24cxx_test_data_read_write,};

	/* Define the test suite */
	DEFINE_TEST_SUITE(at24cxx_suite, at24cxx_test_array,
			"at24cxx driver test suite");

	/* Run all tests in the test suite */
	test_suite_run(&at24cxx_suite);

	while (1) {
		/* Busy-wait forever */
	}
}