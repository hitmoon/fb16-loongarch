/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026
 *
 * LoongArch hardware data-watchpoint facility.
 *
 * Per-thread, multi-slot driver using DB0..DB7.  Each thread's desired
 * watchpoint set lives in td->td_md.md_watch (struct la_watch_image) and is
 * programmed into the per-CPU DB CSRs by la_watch_load(), called from
 * pmap_activate_sw after the incoming pmap's ASID is installed, so DBnASID
 * always matches the live ASID.
 *
 * Arming is PLV3-only (user mode), so kernel (PLV0) accesses never fire it and
 * no exception.S enter/exit changes are needed: CRMD.WE is auto-saved to
 * PRMD.PWE and cleared on trap entry, then restored on ertn.  The per-thread
 * user-mode enable comes from tf_prmd.PWE (set in exec_setregs).
 *
 * A data watchpoint is precise: ERA points at the faulting access and the
 * access has not completed.  Re-executing would refire, so the handler
 * disables each firing slot (fire-once).  Re-arming to catch the next access
 * is the caller's job (ptrace PT_SETDBREGS, or the debug.la_watch sysctl).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/atomic.h>
#include <sys/signalvar.h>
#include <sys/kdb.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <machine/frame.h>
#include <machine/kdb.h>
#include <machine/pcb.h>
#include <machine/pcpu.h>
#include <machine/pmap.h>
#include <machine/reg.h>
#include <machine/loongarchreg.h>
#include <machine/watch.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

CTASSERT(LA_WATCH_NSLOTS == LA_WATCH_SLOTS);

static u_int	la_watch_slots;	/* DB slot count, from MWPC */
static u_int	la_ib_slots;	/* IB slot count, from FWPC */

/*
 * User-facing config for the debug sysctl (written then committed via
 * armed).
 */
static pid_t		la_cfg_pid;
static unsigned long	la_cfg_va;
static u_int		la_cfg_size = 8;
static int		la_cfg_mode = 2;	/* 1=load 2=store 3=both */

static u_long
la_size_bits(u_int size)
{

	switch (size) {
	case 1:		return (CSR_DBC_LEN_1);
	case 2:		return (CSR_DBC_LEN_2);
	case 4:		return (CSR_DBC_LEN_4);
	default:	return (CSR_DBC_LEN_8);
	}
}

/*
 * Program DB slot n (address/mask/control/asid).  The LoongArch csrwr
 * instruction takes an immediate CSR number, so the slot cannot be computed
 * arithmetically; expand the eight cases.
 */
#define	DB_SLOT_WRITE(n)					\
	case (n):						\
		csr_write64(addr, LOONGARCH_CSR_DB##n##ADDR);	\
		csr_write64(0,    LOONGARCH_CSR_DB##n##MASK);	\
		csr_write64(ctrl, LOONGARCH_CSR_DB##n##CTRL);	\
		csr_write64(asid, LOONGARCH_CSR_DB##n##ASID);	\
		break

static void
la_db_program(unsigned slot, uint64_t addr, uint64_t ctrl, uint64_t asid)
{

	switch (slot) {
		DB_SLOT_WRITE(0);
		DB_SLOT_WRITE(1);
		DB_SLOT_WRITE(2);
		DB_SLOT_WRITE(3);
		DB_SLOT_WRITE(4);
		DB_SLOT_WRITE(5);
		DB_SLOT_WRITE(6);
		DB_SLOT_WRITE(7);
	}
}
#undef DB_SLOT_WRITE

/*
 * Program IB slot n (instruction breakpoint / single-step).  ctrl == 0
 * disarms.
 */
#define	IB_SLOT_WRITE(n)					\
	case (n):						\
		csr_write64(addr, LOONGARCH_CSR_IB##n##ADDR);	\
		csr_write64(0,    LOONGARCH_CSR_IB##n##MASK);	\
		csr_write64(ctrl, LOONGARCH_CSR_IB##n##CTRL);	\
		csr_write64(asid, LOONGARCH_CSR_IB##n##ASID);	\
		break

static void
la_ib_program(unsigned slot, uint64_t addr, uint64_t ctrl, uint64_t asid)
{

	switch (slot) {
		IB_SLOT_WRITE(0);
		IB_SLOT_WRITE(1);
		IB_SLOT_WRITE(2);
		IB_SLOT_WRITE(3);
		IB_SLOT_WRITE(4);
		IB_SLOT_WRITE(5);
		IB_SLOT_WRITE(6);
		IB_SLOT_WRITE(7);
	}
}
#undef IB_SLOT_WRITE

void
la_watch_init(void)
{
	uint64_t crmd;
	unsigned i;

	la_watch_slots = csr_read64(LOONGARCH_CSR_MWPC) & CSR_MWPC_SLOTS;
	if (la_watch_slots == 0 || la_watch_slots > LA_WATCH_NSLOTS)
		la_watch_slots = LA_WATCH_NSLOTS;

	la_ib_slots = csr_read64(LOONGARCH_CSR_FWPC) & CSR_MWPC_SLOTS;
	if (la_ib_slots == 0 || la_ib_slots > LA_WATCH_NSLOTS)
		la_ib_slots = LA_WATCH_NSLOTS;

	for (i = 0; i < la_watch_slots; i++)
		la_db_program(i, 0, 0, 0);
	for (i = 0; i < la_ib_slots; i++)
		la_ib_program(i, 0, 0, 0);

	/* Global watchpoint enable (user mode is gated per-thread via PWE). */
	crmd = csr_read64(LOONGARCH_CSR_CRMD);
	csr_write64(crmd | CSR_CRMD_WE, LOONGARCH_CSR_CRMD);

	printf("la_watch: %u data watchpoint + %u instruction breakpoint "
	    "slot(s), CRMD.WE enabled\n", la_watch_slots, la_ib_slots);
}

/*
 * Called from pmap_activate_sw on every context switch, after the incoming
 * pmap's ASID is installed.  Program all DB slots from the incoming thread's
 * desired image.  Empty slots are disarmed so a previous thread's watchpoints
 * cannot fire for the new one.
 */
void
la_watch_load(struct thread *td)
{
	struct la_watch_image *img = &td->td_md.md_watch;
	struct pmap *pm;
	uint64_t asid;
	unsigned i, cpuid;

	cpuid = PCPU_GET(cpuid);
	pm = vmspace_pmap(td->td_proc->p_vmspace);
	asid = pmap_get_asid(pm, cpuid);

	for (i = 0; i < la_watch_slots; i++) {
		if (img->slot[i].ctrl != 0)
			la_db_program(i, img->slot[i].addr, img->slot[i].ctrl,
			    asid);
		else
			la_db_program(i, 0, 0, 0);
	}

	/*
	 * Single-step (ptrace): arm IB0 at the stepped instruction and set
	 * FWPS.SKIP so exactly one instruction executes before the
	 * fetch-watchpoint exception fires.  When not stepping, keep IB0
	 * disarmed, so a previous step cannot linger.
	 */
	if (td->td_md.md_ss_active &&
	    td->td_frame->tf_era == td->td_md.md_ss_addr) {
		csr_write64(td->td_md.md_ss_addr, LOONGARCH_CSR_IB0ADDR);
		csr_write64(0, LOONGARCH_CSR_IB0MASK);
		csr_write64(CSR_DBC_PLV3, LOONGARCH_CSR_IB0CTRL);
		csr_write64(0, LOONGARCH_CSR_IB0ASID);
		csr_write64(CSR_FWPC_SKIP, LOONGARCH_CSR_FWPS);
	} else {
		csr_write64(0, LOONGARCH_CSR_IB0CTRL);
	}
}

/*
 * EXCCODE_WATCH handler.  tf_era is the faulting access instruction;
 * tf_badvaddr is the watched address.  Disable each firing slot in hardware
 * and in the thread image (fire-once), then deliver SIGTRAP/TRAP_TRACE with
 * si_addr = the watched data address.
 */
void
la_watch_handler(struct thread *td, struct trapframe *frame)
{
	uint64_t mwps, fwps;
	ksiginfo_t ksi;
	unsigned i;
	bool fired;
	void *addr;

	mwps = csr_read64(LOONGARCH_CSR_MWPS);
	if (mwps != 0)
		csr_write64(mwps, LOONGARCH_CSR_MWPS);	/* W1C */

	addr = NULL;
	fired = false;

	/* Single-step via IB0 + FWPS.SKIP (instruction fetch watchpoint). */
	fwps = csr_read64(LOONGARCH_CSR_FWPS);
	if ((fwps & 0x1ul) != 0) {
		csr_write64(fwps & 0xfful, LOONGARCH_CSR_FWPS);	/* W1C */
		csr_write64(0, LOONGARCH_CSR_IB0CTRL);
		td->td_md.md_ss_active = 0;
		fired = true;
		addr = (void *)frame->tf_era;
	}

	/* Data watchpoint(s). */
	for (i = 0; i < la_watch_slots; i++) {
		if ((mwps & (1ul << i)) == 0)
			continue;
		la_db_program(i, 0, 0, 0);
		td->td_md.md_watch.slot[i].addr = 0;
		td->td_md.md_watch.slot[i].ctrl = 0;
		fired = true;
		addr = (void *)frame->tf_badvaddr;
	}

	if (!fired)
		return;

	printf("WATCHPOINT HIT: pid=%d (%s) badvaddr=%#lx era=%#lx "
	    "MWPS=%#lx FWPS=%#lx\n", td->td_proc->p_pid, td->td_proc->p_comm,
	    (unsigned long)frame->tf_badvaddr, (unsigned long)frame->tf_era,
	    (unsigned long)mwps, (unsigned long)fwps);

	ksiginfo_init_trap(&ksi);
	ksi.ksi_signo = SIGTRAP;
	ksi.ksi_code = TRAP_TRACE;
	ksi.ksi_addr = addr;
	ksi.ksi_trapno = EXCCODE_WATCH;
	trapsignal(td, &ksi);
}

/*
 * Sysctl debug knob: arm/clear slot 0 on a target process's first thread.
 * Best-effort (no synchronization with the target running on another CPU);
 * for precise control use ptrace PT_SETDBREGS.
 */
SYSCTL_NODE(_debug, OID_AUTO, la_watch, CTLFLAG_RW | CTLFLAG_MPSAFE, 0,
    "LoongArch data watchpoint (debug)");

SYSCTL_UINT(_debug_la_watch, OID_AUTO, slots, CTLFLAG_RD, &la_watch_slots, 0,
    "number of data watchpoint slots");
SYSCTL_INT(_debug_la_watch, OID_AUTO, target_pid, CTLFLAG_RW, &la_cfg_pid, 0,
    "target process pid");
SYSCTL_ULONG(_debug_la_watch, OID_AUTO, va, CTLFLAG_RW, &la_cfg_va, 0,
    "watched virtual address");
SYSCTL_UINT(_debug_la_watch, OID_AUTO, size, CTLFLAG_RW, &la_cfg_size, 8,
    "watch size (1/2/4/8)");
SYSCTL_INT(_debug_la_watch, OID_AUTO, mode, CTLFLAG_RW, &la_cfg_mode, 0,
    "access mode (1=load 2=store 3=both)");

static u_int la_cfg_armed;

static int
sysctl_la_watch_arm(SYSCTL_HANDLER_ARGS)
{
	struct proc *p;
	struct thread *td2;
	u_int mode_bits, sizeb;
	int error, val;

	val = la_cfg_armed;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	mode_bits = (la_cfg_mode & 1 ? CSR_DBC_LOADEN : 0) |
	    (la_cfg_mode & 2 ? CSR_DBC_STOREEN : 0);
	if (mode_bits == 0)
		return (EINVAL);
	sizeb = la_size_bits(la_cfg_size);

	p = pfind(la_cfg_pid);
	if (p == NULL) {
		la_cfg_armed = 0;
		return (ESRCH);
	}
	td2 = FIRST_THREAD_IN_PROC(p);
	if (td2 != NULL) {
		if (val) {
			td2->td_md.md_watch.slot[0].addr = la_cfg_va;
			td2->td_md.md_watch.slot[0].ctrl =
			    CSR_DBC_PLV3 | mode_bits | sizeb;
		} else {
			td2->td_md.md_watch.slot[0].addr = 0;
			td2->td_md.md_watch.slot[0].ctrl = 0;
		}
	}
	PROC_UNLOCK(p);
	la_cfg_armed = (val != 0);
	return (0);
}
SYSCTL_PROC(_debug_la_watch, OID_AUTO, armed, CTLTYPE_INT | CTLFLAG_RW |
    CTLFLAG_MPSAFE, NULL, 0, sysctl_la_watch_arm, "I",
    "arm/disarm slot 0 on target_pid");

/*
 * DDB kernel watchpoints (hwatch/dhwatch).  Kernel (PLV0) watchpoints armed
 * by the debugger on the current CPU; ASID is don't-care (0).  Because trap
 * entry clears CRMD.WE, tf_prmd.PWE is toggled so WE re-enables in kernel mode
 * on ertn after `continue'.
 *
 * These share the DB CSRs with per-thread user watchpoints; a kernel
 * watchpoint survives until the next context switch on this CPU (la_watch_load
 * reprograms the slots from the thread image) -- sufficient for "set,
 * continue, catch the write" debugging.
 */
#ifdef DDB
struct la_kern_watch_entry {
	uint64_t	addr;
	vm_size_t	size;
	uint32_t	ctrl;		/* DBnCTRL bits; 0 = unused */
};
static struct la_kern_watch_entry la_kern_watch[LA_WATCH_NSLOTS];
static unsigned la_kern_narmed;

/* DDB instruction breakpoints (hbreak); IB0 is reserved for single-step. */
static struct la_kern_watch_entry la_kern_hbreak[LA_WATCH_NSLOTS];
static unsigned la_kern_hbreak_count;

static u_int
la_access_bits(int access)
{

	switch (access) {
	case KDB_DBG_ACCESS_R:	return (CSR_DBC_LOADEN);
	case KDB_DBG_ACCESS_W:	return (CSR_DBC_STOREEN);
	case KDB_DBG_ACCESS_RW:	return (CSR_DBC_LOADEN |
				CSR_DBC_STOREEN);
	default:		return (0);
	}
}

int
kdb_cpu_set_watchpoint(vm_offset_t addr, vm_size_t size, int access)
{
	uint32_t ctrl, mode;
	unsigned i;

	mode = la_access_bits(access);
	if (mode == 0)
		return (EINVAL);

	for (i = 0; i < LA_WATCH_NSLOTS; i++)
		if (la_kern_watch[i].ctrl == 0)
			break;
	if (i == LA_WATCH_NSLOTS)
		return (EBUSY);

	ctrl = CSR_DBC_PLV0 | mode | la_size_bits(size);
	la_kern_watch[i].addr = addr;
	la_kern_watch[i].size = size;
	la_kern_watch[i].ctrl = ctrl;
	la_db_program(i, addr, ctrl, 0);	/* ASID 0 = don't-care */

	if (la_kern_narmed++ == 0 && kdb_frame != NULL)
		kdb_frame->tf_prmd |= CSR_PRMD_PWE;
	return (0);
}

int
kdb_cpu_clr_watchpoint(vm_offset_t addr, vm_size_t size)
{
	unsigned i;

	for (i = 0; i < LA_WATCH_NSLOTS; i++) {
		if (la_kern_watch[i].ctrl == 0 ||
		    la_kern_watch[i].addr != addr ||
		    la_kern_watch[i].size != size)
			continue;
		la_kern_watch[i].ctrl = 0;
		la_db_program(i, 0, 0, 0);
		if (--la_kern_narmed == 0 && kdb_frame != NULL)
			kdb_frame->tf_prmd &= ~CSR_PRMD_PWE;
		return (0);
	}
	return (EINVAL);
}

/*
 * DDB instruction breakpoint (hbreak).  Uses IB slots >= 1 (IB0 is reserved
 * for single-step); armed PLV0 so kernel-mode execution traps.  On a hit DDB
 * steps over via single-step then re-arms (db_restart_at_pc).
 */
int
kdb_cpu_set_breakpoint(vm_offset_t addr)
{
	unsigned i;

	for (i = 1; i < la_ib_slots; i++)
		if (la_kern_hbreak[i].ctrl == 0)
			break;
	if (i == la_ib_slots)
		return (EBUSY);

	la_kern_hbreak[i].addr = addr;
	la_kern_hbreak[i].ctrl = CSR_DBC_PLV0;
	la_ib_program(i, addr, CSR_DBC_PLV0, 0);
	la_kern_hbreak_count++;

	if (kdb_frame != NULL)
		kdb_frame->tf_prmd |= CSR_PRMD_PWE;
	return (0);
}

int
kdb_cpu_clr_breakpoint(vm_offset_t addr)
{
	unsigned i;

	for (i = 1; i < la_ib_slots; i++) {
		if (la_kern_hbreak[i].ctrl == 0 ||
		    la_kern_hbreak[i].addr != addr)
			continue;
		la_kern_hbreak[i].ctrl = 0;
		la_ib_program(i, 0, 0, 0);
		if (la_kern_hbreak_count > 0)
			la_kern_hbreak_count--;
		return (0);
	}
	return (EINVAL);
}

/*
 * Fire-once: disarm the firing kernel slot(s) before dropping into DDB so
 * `continue' does not immediately refire on the same instruction (there is no
 * per-instruction RF for data watchpoints).
 */
void
la_watch_kern_handler(struct trapframe *frame)
{
	uint64_t mwps, fwps;
	unsigned i;

	mwps = csr_read64(LOONGARCH_CSR_MWPS);
	if (mwps != 0)
		csr_write64(mwps, LOONGARCH_CSR_MWPS);	/* W1C */

	/*
	 * Fetch watchpoints (instruction breakpoints / single-step).  Clear the
	 * fired slot status; only the single-step slot IB0 is one-shot -- the
	 * instruction-breakpoint slots are managed by DDB, which clears and
	 * re-arms them around the step-over (db_restart_at_pc).
	 */
	fwps = csr_read64(LOONGARCH_CSR_FWPS);
	if (fwps != 0) {
		csr_write64(fwps & 0xfful, LOONGARCH_CSR_FWPS);	/* W1C */
		if ((fwps & 0x1ul) != 0)
			csr_write64(0, LOONGARCH_CSR_IB0CTRL);
	}

	for (i = 0; i < la_watch_slots; i++) {
		if ((mwps & (1ul << i)) == 0)
			continue;
		la_db_program(i, 0, 0, 0);
		if (la_kern_watch[i].ctrl != 0) {
			la_kern_watch[i].ctrl = 0;
			if (la_kern_narmed > 0)
				la_kern_narmed--;
		}
	}
	if (kdb_frame != NULL) {
		if (la_kern_narmed > 0 || la_kern_hbreak_count > 0)
			kdb_frame->tf_prmd |= CSR_PRMD_PWE;
		else
			kdb_frame->tf_prmd &= ~CSR_PRMD_PWE;
	}

	printf("KWATCH HIT: badvaddr=%#lx era=%#lx MWPS=%#lx FWPS=%#lx\n",
	    (unsigned long)frame->tf_badvaddr, (unsigned long)frame->tf_era,
	    (unsigned long)mwps, (unsigned long)fwps);
}

/*
 * DDB single-step: arm IB0 (PLV0) at the current PC and set FWPS.SKIP so one
 * kernel instruction executes before EXCCODE_WATCH re-enters the debugger.
 * tf_prmd.PWE is set so CRMD.WE re-enables in kernel mode on ertn.
 */
void
kdb_cpu_set_singlestep(void)
{

	if (kdb_frame == NULL)
		return;
	csr_write64(kdb_frame->tf_era, LOONGARCH_CSR_IB0ADDR);
	csr_write64(0, LOONGARCH_CSR_IB0MASK);
	csr_write64(CSR_DBC_PLV0, LOONGARCH_CSR_IB0CTRL);
	csr_write64(0, LOONGARCH_CSR_IB0ASID);
	csr_write64(CSR_FWPC_SKIP, LOONGARCH_CSR_FWPS);
	kdb_frame->tf_prmd |= CSR_PRMD_PWE;
}

void
kdb_cpu_clear_singlestep(void)
{

	csr_write64(0, LOONGARCH_CSR_IB0CTRL);
}

void
db_md_list_watchpoints(void)
{
	unsigned i;

	for (i = 0; i < LA_WATCH_NSLOTS; i++) {
		if (la_kern_watch[i].ctrl == 0)
			continue;
		db_printf("  slot %u: addr=%#lx size=%lu ctrl=%#x (PLV0)\n",
		    i, (unsigned long)la_kern_watch[i].addr,
		    (unsigned long)la_kern_watch[i].size,
		    la_kern_watch[i].ctrl);
	}
}

#ifdef HAS_HW_BREAKPOINT
void
db_md_list_breakpoints(void)
{
	unsigned i;

	for (i = 1; i < la_ib_slots; i++) {
		if (la_kern_hbreak[i].ctrl == 0)
			continue;
		db_printf("  slot %u: addr=%#lx ctrl=%#x (PLV0 hbreak)\n",
		    i, (unsigned long)la_kern_hbreak[i].addr,
		    la_kern_hbreak[i].ctrl);
	}
}
#endif /* HAS_HW_BREAKPOINT */

#else /* !DDB */

int
kdb_cpu_set_watchpoint(vm_offset_t addr, vm_size_t size, int access)
{

	return (ENXIO);
}

int
kdb_cpu_clr_watchpoint(vm_offset_t addr, vm_size_t size)
{

	return (0);
}

void
la_watch_kern_handler(struct trapframe *frame)
{
}

void
kdb_cpu_set_singlestep(void)
{
}

void
kdb_cpu_clear_singlestep(void)
{
}

int
kdb_cpu_set_breakpoint(vm_offset_t addr)
{

	return (ENXIO);
}

int
kdb_cpu_clr_breakpoint(vm_offset_t addr)
{

	return (0);
}

#endif /* DDB */
