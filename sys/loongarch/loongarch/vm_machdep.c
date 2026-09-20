/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/proc.h>
#include <sys/eventhandler.h>
#include <sys/sf_buf.h>
#include <sys/signal.h>
#include <sys/unistd.h>
#include <sys/reboot.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/uma.h>
#include <vm/uma_int.h>

#include <machine/loongarchreg.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/machdep.h>
#include "opt_acpi.h"
#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
/* AcpiGbl_SleepTypeA is set by AcpiEnterSleepStatePrep from the \_S5 object. */
extern UINT8 AcpiGbl_SleepTypeA;
#endif
#include <machine/pcb.h>
#include <machine/frame.h>
#include <machine/vmparam.h>

#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#define	TP_OFFSET	16	/* sizeof(struct tcb) */

/* GED (Generic Event Device) registers */
#define	GED_REG_SLEEP_CTL	0x00
#define	GED_REG_RESET		0x02
#define	GED_RESET_VALUE		0x42
#define	GED_SLP_EN		0x20
#define	GED_SLP_TYP_S5		0x05
#define	GED_SLP_TYP_POS		0x02

static vm_paddr_t ged_paddr;

/*
 * Look up the GED physical address from the FDT.  Called once during boot.
 */
void
ged_init(void)
{
	phandle_t ged_node, reboot_node, child;
	phandle_t regmap;
	pcell_t reg[4];
	int len;

	reboot_node = OF_finddevice("/reboot");
	if (reboot_node == -1)
		return;

	len = OF_getencprop(reboot_node, "regmap", &regmap, sizeof(regmap));
	if (len <= 0)
		return;

	/* Walk root-level children to find the node with matching phandle */
	ged_node = -1;
	for (child = OF_child(OF_peer(0)); child != 0; child = OF_peer(child)) {
		pcell_t phandle;
		len = OF_getencprop(child, "phandle", &phandle,
		    sizeof(phandle));
		if (len > 0 && phandle == regmap) {
			ged_node = child;
			break;
		}
	}
	if (ged_node == -1)
		return;

	len = OF_getencprop(ged_node, "reg", reg, sizeof(reg));
	if (len <= 0)
		return;

	ged_paddr = reg[1];	/* phys address from reg tuple */
}

/*
 * Write a single byte to a GED register via DMAP.
 */
static void
ged_write_1(vm_offset_t offset, uint8_t val)
{

	__compiler_membar();
	*(volatile uint8_t *)PHYS_TO_DMAP(ged_paddr + offset) = val;
	__compiler_membar();
}

void
cpu_reset(void)
{
	intr_disable();

#ifdef DEV_ACPI
	/*
	 * Try the ACPI FADT reset register (firmware-provided address +
	 * value).
	 */
	if (ACPI_SUCCESS(AcpiReset()))
		DELAY(1000000);

	/*
	 * Direct write to the FADT reset register via DMW0 (uncached).  This
	 * bypasses ACPICA's AcpiHwWrite, which may not reach the hardware on
	 * LoongArch.  On the LS7A this is RST_CNT at 0x100d0030.
	 */
	if (AcpiGbl_FADT.ResetRegister.Address != 0 &&
	    AcpiGbl_FADT.ResetRegister.SpaceId ==
	    ACPI_ADR_SPACE_SYSTEM_MEMORY) {
		volatile uint8_t *r = (volatile uint8_t *)
		    (DMW0_BASE_ADDRESS | AcpiGbl_FADT.ResetRegister.Address);
		*r = (uint8_t)AcpiGbl_FADT.ResetValue;
		DELAY(1000000);
	}
#endif

	/* Try GED reset (FDT / QEMU). */
	if (ged_paddr != 0) {
		ged_write_1(GED_REG_RESET, GED_RESET_VALUE);
		DELAY(1000000);
	}

	/* Fallback: spin forever */
	while (1)
		__asm __volatile("idle 0");
}

void
cpu_halt(void)
{
	intr_disable();

	/* Try GED poweroff (S5 sleep) */
	if (ged_paddr != 0) {
		ged_write_1(GED_REG_SLEEP_CTL,
		    GED_SLP_EN | (GED_SLP_TYP_S5 << GED_SLP_TYP_POS));
		DELAY(1000000);
	}

	for (;;)
		__asm __volatile("idle 0");
	/* NOTREACHED */
}

#ifdef DEV_ACPI
/*
 * Fallback poweroff for ACPI systems where the FADT Pm1aControlBlock is
 * missing/zero, as observed on the 3A6000, so that ACPICA's sleep path fails
 * with AE_NOT_EXIST before reaching the hardware.  The S5 sleep type (\_S5) was
 * already evaluated by acpi_EnterSleepStatePrep in acpi_poweroff, so
 * AcpiGbl_SleepTypeA holds it.  Write the LS7A PM1_CNT register directly.
 */
#define	LA_LS7A_PM1_CNT		0x100d0014UL
#define	LA_PM1_SLP_TYP_SHIFT	10
#define	LA_PM1_SLP_TYP_MASK	(0x7u << LA_PM1_SLP_TYP_SHIFT)
#define	LA_PM1_SLP_EN		(1u << 13)

static void
la_poweroff_final(void *arg, int howto)
{
	volatile uint32_t *pm1;
	uint32_t slp_typ, v;

	if ((howto & RB_POWEROFF) == 0)
		return;
	slp_typ = AcpiGbl_SleepTypeA;
	if (slp_typ == 0 || slp_typ > 7)
		return;

	pm1 = (volatile uint32_t *)(DMW0_BASE_ADDRESS | LA_LS7A_PM1_CNT);
	v = *pm1;
	v &= ~(LA_PM1_SLP_TYP_MASK | LA_PM1_SLP_EN);
	v |= slp_typ << LA_PM1_SLP_TYP_SHIFT;
	*pm1 = v;			/* write #1: SLP_TYP */
	v |= LA_PM1_SLP_EN;
	*pm1 = v;			/* write #2: SLP_TYP + SLP_EN -> S5 */
	DELAY(1000000);
}

static void
la_poweroff_register(void *arg)
{

	EVENTHANDLER_REGISTER(shutdown_final, la_poweroff_final, NULL,
	    SHUTDOWN_PRI_LAST + 160);
}
SYSINIT(la_poweroff, SI_SUB_DRIVERS, SI_ORDER_ANY, la_poweroff_register, NULL);
#endif /* DEV_ACPI */

/*
 * Finish a fork operation, with process p2 nearly set up.
 * Copy and update the pcb, set up the stack so that the child
 * ready to run and return to user mode.
 */
void
cpu_fork(struct thread *td1, struct proc *p2, struct thread *td2, int flags)
{
	struct pcb *pcb2;
	struct trapframe *tf;

	if ((flags & RFPROC) == 0)
		return;

	pcb2 = (struct pcb *)(td2->td_kstack +
	    td2->td_kstack_pages * PAGE_SIZE) - 1;

	td2->td_pcb = pcb2;
	bcopy(td1->td_pcb, pcb2, sizeof(*pcb2));

	tf = (struct trapframe *)STACKALIGN((struct trapframe *)pcb2 - 1);
	bcopy(td1->td_frame, tf, sizeof(*tf));

	/* Clear syscall error flag */
	tf->tf_t[0] = 0;

	/* Arguments for child */
	tf->tf_a[0] = 0;
	tf->tf_a[1] = 0;

	tf->tf_prmd |= CSR_CRMD_IE; /* Enable interrupts. */
	tf->tf_prmd |= PLV_USER << CSR_CRMD_PLV_SHIFT; /* User mode. */

	td2->td_frame = tf;

	/* Set the return value registers for fork() */
	td2->td_pcb->pcb_s[0] = (uintptr_t)fork_return;
	td2->td_pcb->pcb_s[1] = (uintptr_t)td2;
	td2->td_pcb->pcb_ra = (uintptr_t)fork_trampoline;
	td2->td_pcb->pcb_sp = (uintptr_t)td2->td_frame;

	/* Setup to release spin count in fork_exit(). */
	td2->td_md.md_spinlock_count = 1;
	td2->td_md.md_saved_crmd_ie = CSR_CRMD_IE;

	/* Child starts with no watchpoints. */
	memset(&td2->td_md.md_watch, 0, sizeof(td2->td_md.md_watch));
	td2->td_md.md_ss_active = 0;
}

void
cpu_set_syscall_retval(struct thread *td, int error)
{
	struct trapframe *frame;

	frame = td->td_frame;

	if (__predict_true(error == 0)) {
		frame->tf_a[0] = td->td_retval[0];
		frame->tf_a[1] = td->td_retval[1];
		frame->tf_t[0] = 0;		/* syscall succeeded */
		return;
	}

	switch (error) {
	case ERESTART:
		frame->tf_era -= 4;		/* prev instruction */
		break;
	case EJUSTRETURN:
		break;
	default:
		frame->tf_a[0] = error;
		frame->tf_t[0] = 1;		/* syscall error */
		break;
	}
}

/*
 * Initialize machine state, mostly pcb and trap frame for a new
 * thread, about to return to userspace.  Put enough state in the new
 * thread's PCB to get it to go back to the fork_return(), which
 * finalizes the thread state and handles peculiarities of the first
 * return to userspace for the new thread.
 */
void
cpu_copy_thread(struct thread *td, struct thread *td0)
{

	bcopy(td0->td_frame, td->td_frame, sizeof(struct trapframe));
	bcopy(td0->td_pcb, td->td_pcb, sizeof(struct pcb));

	td->td_pcb->pcb_s[0] = (uintptr_t)fork_return;
	td->td_pcb->pcb_s[1] = (uintptr_t)td;
	td->td_pcb->pcb_ra = (uintptr_t)fork_trampoline;
	td->td_pcb->pcb_sp = (uintptr_t)td->td_frame;

	/* Setup to release spin count in fork_exit(). */
	td->td_md.md_spinlock_count = 1;
	td->td_md.md_saved_crmd_ie = CSR_CRMD_IE;

	/* New threads start with no watchpoints. */
	memset(&td->td_md.md_watch, 0, sizeof(td->td_md.md_watch));
	td->td_md.md_ss_active = 0;
}

/*
 * Set that machine state for performing an upcall that starts
 * the entry function with the given argument.
 */
int
cpu_set_upcall(struct thread *td, void (*entry)(void *), void *arg,
	stack_t *stack)
{
	struct trapframe *tf;

	tf = td->td_frame;

	tf->tf_sp = STACKALIGN((uintptr_t)stack->ss_sp + stack->ss_size);
	tf->tf_era = (register_t)entry;
	tf->tf_a[0] = (register_t)arg;

	return (0);
}

int
cpu_set_user_tls(struct thread *td, void *tls_base, int flags __unused)
{

	if ((uintptr_t)tls_base >= VM_MAXUSER_ADDRESS)
		return (EINVAL);

	/*
	 * The user TLS is set by modifying the trapframe's tp value, which
	 * will be restored when returning to userspace.
	 */
	td->td_frame->tf_tp = (register_t)tls_base + TP_OFFSET;

	return (0);
}

void
cpu_thread_exit(struct thread *td)
{
}

void
cpu_thread_alloc(struct thread *td)
{

	td->td_pcb = (struct pcb *)(td->td_kstack +
	    td->td_kstack_pages * PAGE_SIZE) - 1;
	td->td_frame = (struct trapframe *)STACKALIGN(
	    (caddr_t)td->td_pcb - 8 - sizeof(struct trapframe));
}

void
cpu_thread_free(struct thread *td)
{
}

void
cpu_thread_clean(struct thread *td)
{
}

/*
 * Intercept the return address from a freshly forked process that has NOT
 * been scheduled yet.
 *
 * This is needed to make kernel threads stay in kernel mode.
 */
void
cpu_fork_kthread_handler(struct thread *td, void (*func)(void *), void *arg)
{

	td->td_pcb->pcb_s[0] = (uintptr_t)func;
	td->td_pcb->pcb_s[1] = (uintptr_t)arg;
	td->td_pcb->pcb_ra = (uintptr_t)fork_trampoline;
	td->td_pcb->pcb_sp = (uintptr_t)td->td_frame;
}

void
cpu_exit(struct thread *td)
{
}

bool
cpu_exec_vmspace_reuse(struct proc *p __unused, vm_map_t map __unused)
{

	return (true);
}

int
cpu_procctl(struct thread *td __unused, int idtype __unused, id_t id __unused,
    int com __unused, void *data __unused)
{

	return (EINVAL);
}

void
cpu_update_pcb(struct thread *td)
{
}

void
cpu_thread_new_kstack(struct thread *td)
{
	td->td_pcb = (struct pcb *)(td->td_kstack +
	    td->td_kstack_pages * PAGE_SIZE) - 1;
	td->td_frame = (struct trapframe *)STACKALIGN(
	    (caddr_t)td->td_pcb - 8 - sizeof(struct trapframe));
}

void
cpu_sync_core(void)
{
	flush_icache();
}
