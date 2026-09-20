/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
 */

#ifndef _MACHINE_IODEV_H_
#define	_MACHINE_IODEV_H_

/*
 * Memory-mapped I/O read/write macros.  LoongArch accesses MMIO through
 * normal load/store with the uncached attribute (DMW0 or device PTE).
 */

#define	iodev_read_1(a)	(*(volatile uint8_t *)(uintptr_t)(a))
#define	iodev_read_2(a)	(*(volatile uint16_t *)(uintptr_t)(a))
#define	iodev_read_4(a)	(*(volatile uint32_t *)(uintptr_t)(a))

#define	iodev_write_1(a, v)					\
	(*(volatile uint8_t *)(uintptr_t)(a) = (v))
#define	iodev_write_2(a, v)					\
	(*(volatile uint16_t *)(uintptr_t)(a) = (v))
#define	iodev_write_4(a, v)					\
	(*(volatile uint32_t *)(uintptr_t)(a) = (v))

#endif /* _MACHINE_IODEV_H_ */
