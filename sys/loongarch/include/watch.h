/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026
 *
 * LoongArch data-watchpoint debug facility (Phase 1: core).
 */
#ifndef _MACHINE_WATCH_H_
#define	_MACHINE_WATCH_H_

#include <sys/_types.h>

/*
 * Number of hardware data-watchpoint slots tracked per thread.  Must match
 * LA_WATCH_SLOTS (the ptrace struct dbreg slot count) -- asserted in watch.c.
 */
#define	LA_WATCH_NSLOTS	8

/* One armed (or unused) watchpoint slot: address + DBnCTRL control word. */
struct la_watch_slot {
	__uint64_t	addr;		/* watched VA */
	__uint32_t	ctrl; /* DBnCTRL bits (CSR_DBC_*); 0 = unused */
	__uint32_t	_pad;
};

/* Per-thread desired watchpoint state, stored in struct mdthread. */
struct la_watch_image {
	struct la_watch_slot	slot[LA_WATCH_NSLOTS];
};

struct thread;
struct trapframe;

void	la_watch_init(void);
void	la_watch_load(struct thread *td);
void	la_watch_handler(struct thread *td, struct trapframe *frame);
void	la_watch_kern_handler(struct trapframe *frame);

#endif /* !_MACHINE_WATCH_H_ */
