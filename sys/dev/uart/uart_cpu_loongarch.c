/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
 */

/*
 * LoongArch uart_cpu implementation, modeled on arm64's uart_cpu_arm64.c.
 * Provides uart_cpu_getdev (dispatches to ACPI SPCR), uart_cpu_eqres, and
 * the bus_space tags that the uart driver expects.
 */

#include <sys/cdefs.h>
#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_cpu.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_dev_ns8250.h>

#ifdef DEV_ACPI
#include <dev/uart/uart_cpu_acpi.h>
#endif

extern struct bus_space memmap_bus;

bus_space_tag_t uart_bus_space_io;
bus_space_tag_t uart_bus_space_mem = &memmap_bus;

int
uart_cpu_eqres(struct uart_bas *b1, struct uart_bas *b2)
{

	if (pmap_kextract(b1->bsh) == 0)
		return (0);
	if (pmap_kextract(b2->bsh) == 0)
		return (0);
	return ((pmap_kextract(b1->bsh) == pmap_kextract(b2->bsh)) ? 1 : 0);
}

int
uart_cpu_getdev(int devtype, struct uart_devinfo *di)
{
	struct uart_class *class;
	int err;

	class = &uart_ns8250_class;
	err = uart_getenv(devtype, di, class);
	if (err == 0)
		return (0);

#ifdef DEV_ACPI
	if (uart_cpu_acpi_setup(devtype, di) == 0) {
		uart_bus_space_mem = di->bas.bst;
		uart_bus_space_io = NULL;
		return (0);
	}
#endif

	/*
	 * Fallback: the 3A6000 firmware provides no SPCR/DBG2 table, so
	 * hardcode the well-known Loongson LPC ns8250 console UART.
	 */
	if (devtype == UART_DEV_CONSOLE) {
		di->ops = uart_getops(class);
		di->bas.chan = 0;
		di->bas.regshft = 0;
		di->bas.regiowidth = 1;
		di->baudrate = 0;		/* keep firmware baud rate */
		di->bas.rclk = 0;
		di->databits = 8;
		di->stopbits = 1;
		di->parity = UART_PARITY_NONE;
		di->bas.bst = &memmap_bus;
		/* Use the DMW0 uncached direct-map window (same as the early
		 * console) -- pmap_mapdev at cninit time is unreliable. */
		di->bas.bsh = (bus_space_handle_t)0x800000001fe001e0UL;
		uart_bus_space_mem = di->bas.bst;
		uart_bus_space_io = NULL;
		return (0);
	}

	return (ENXIO);
}
