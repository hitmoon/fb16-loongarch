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
#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/sysent.h>
#include <sys/smp.h>
#ifdef KDB
#include <sys/kdb.h>
#endif

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <vm/vm_param.h>
#include <vm/vm_extern.h>

#include <machine/fpe.h>
#include <machine/frame.h>
#include <machine/loongarchreg.h>
#include <machine/pcb.h>
#include <machine/pcpu.h>
#include <machine/pte.h>
#include <machine/watch.h>
#include <machine/vmparam.h>

#include <machine/resource.h>
#include <machine/intr.h>
#include <machine/machdep.h>
#include <machine/cpufunc.h>

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>
#endif

#ifdef DDB
#include <ddb/ddb.h>
#include <ddb/db_sym.h>
#endif

#define EXC_CODE(estat) ((estat & CSR_ESTAT_EXC) >> CSR_ESTAT_EXC_SHIFT)

int (*dtrace_invop_jump_addr)(struct trapframe *);

/* Called from exception.S */
void do_trap_supervisor(struct trapframe *);
void do_trap_user(struct trapframe *);

static __inline void
call_trapsignal(struct thread *td, int sig, int code, void *addr, int trapno)
{
	ksiginfo_t ksi;

	ksiginfo_init_trap(&ksi);
	ksi.ksi_signo = sig;
	ksi.ksi_code = code;
	ksi.ksi_addr = addr;
	ksi.ksi_trapno = trapno;
	trapsignal(td, &ksi);
}

int
cpu_fetch_syscall_args(struct thread *td)
{
	struct proc *p;
	syscallarg_t *ap, *dst_ap;
	struct syscall_args *sa;

	p = td->td_proc;
	sa = &td->td_sa;

	/* Save original a0 before it is overwritten by return value. */
	td->td_frame->tf_a0 = td->td_frame->tf_a[0];

	ap = &td->td_frame->tf_a[0];
	dst_ap = &sa->args[0];

	sa->code = td->td_frame->tf_a[7];
	sa->original_code = sa->code;

	if (__predict_false(sa->code == SYS_syscall ||
	    sa->code == SYS___syscall)) {
		sa->code = *ap++;
	} else {
		*dst_ap++ = *ap++;
	}

	if (__predict_false(sa->code >= p->p_sysent->sv_size))
		sa->callp = &p->p_sysent->sv_table[0];
	else
		sa->callp = &p->p_sysent->sv_table[sa->code];

	KASSERT(sa->callp->sy_narg <= nitems(sa->args),
	    ("Syscall %d takes too many arguments", sa->code));

	memcpy(dst_ap, ap, (NARGREG - 1) * sizeof(*dst_ap));

	td->td_retval[0] = 0;
	td->td_retval[1] = 0;

	return (0);
}

#include "../../kern/subr_syscall.c"

static void
print_with_symbol(const char *name, uint64_t value)
{
#ifdef DDB
	c_db_sym_t sym;
	db_expr_t sym_value;
	db_expr_t offset;
	const char *sym_name;
#endif

	printf("%7s: 0x%016lx", name, value);

#ifdef DDB
	if (value >= VM_MIN_KERNEL_ADDRESS) {
		sym = db_search_symbol(value, DB_STGY_ANY, &offset);
		if (sym != C_DB_SYM_NULL) {
			db_symbol_values(sym, &sym_name, &sym_value);
			if (offset != 0)
				printf(" (%s + 0x%lx)", sym_name, offset);
			else
				printf(" (%s)", sym_name);
		}
	}
#endif
	printf("\n");
}

static void
dump_regs(struct trapframe *frame)
{
	char name[6];
	int i;

	for (i = 0; i < nitems(frame->tf_t); i++) {
		snprintf(name, sizeof(name), "t[%d]", i);
		print_with_symbol(name, frame->tf_t[i]);
	}

	for (i = 0; i < nitems(frame->tf_s); i++) {
		snprintf(name, sizeof(name), "s[%d]", i);
		print_with_symbol(name, frame->tf_s[i]);
	}

	for (i = 0; i < nitems(frame->tf_a); i++) {
		snprintf(name, sizeof(name), "a[%d]", i);
		print_with_symbol(name, frame->tf_a[i]);
	}

	print_with_symbol("ra", frame->tf_ra);
	print_with_symbol("sp", frame->tf_sp);
	print_with_symbol("tp", frame->tf_tp);
	print_with_symbol("fp", frame->tf_fp);
	printf("r21: 0x%016lx\n", frame->tf_regs[21]);
	printf("era: 0x%016lx\n", frame->tf_era);
	printf("crmd: 0x%016lx\n", frame->tf_crmd);
	printf("prmd: 0x%016lx\n", frame->tf_prmd);
	printf("euen: 0x%016lx\n", frame->tf_euen);
	printf("ecfg: 0x%016lx\n", frame->tf_ecfg);
	printf("estat: 0x%016lx\n", frame->tf_estat);
	printf("badvaddr: 0x%016lx\n", frame->tf_badvaddr);
}

static void
syscall_handler(void)
{
	struct thread *td;

	td = curthread;

	syscallenter(td);
	syscallret(td);
}

static volatile int usegv_count;	/* rate-limit the user-SIGSEGV probe */

static void
page_fault_handler(struct trapframe *frame, int usermode)
{
	struct vm_map *map;
	uint64_t evaddr;
	struct thread *td;
	struct pcb *pcb;
	vm_prot_t ftype;
	vm_offset_t va;
	struct proc *p;
	int error, sig, ucode;
#ifdef KDB
	bool handled;
#endif

#ifdef KDB
	if (kdb_active) {
		kdb_reenter();
		return;
	}
#endif

	td = curthread;
	p = td->td_proc;
	pcb = td->td_pcb;
	evaddr = frame->tf_badvaddr;

	if (td->td_critnest != 0 || td->td_intr_nesting_level != 0 ||
	    WITNESS_CHECK(WARN_SLEEPOK | WARN_GIANTOK, NULL,
	    "Kernel page fault") != 0)
		goto fatal;

	if (usermode) {
		if (!VIRT_IS_VALID(evaddr)) {
			call_trapsignal(td, SIGSEGV, SEGV_MAPERR,
			    (void *)evaddr, EXC_CODE(frame->tf_estat));
			goto done;
		}
		map = &p->p_vmspace->vm_map;
	} else {
		if (evaddr >= VM_MIN_KERNEL_ADDRESS) {
			map = kernel_map;
		} else {
			if (pcb->pcb_onfault == 0)
				goto fatal;
			map = &p->p_vmspace->vm_map;
		}
	}

	va = trunc_page(evaddr);

	if (EXC_CODE(frame->tf_estat) == EXCCODE_TLBS ||
	    EXC_CODE(frame->tf_estat) == EXCCODE_TLBM) {
		ftype = VM_PROT_WRITE;
	} else if (EXC_CODE(frame->tf_estat) == EXCCODE_TLBI ||
	    EXC_CODE(frame->tf_estat) == EXCCODE_TLBNX) {
		ftype = VM_PROT_EXECUTE;
	} else {
		ftype = VM_PROT_READ;
	}

	if (VIRT_IS_VALID(va) && pmap_fault(map->pmap, va, ftype))
		goto done;

	error = vm_fault_trap(map, va, ftype, VM_FAULT_NORMAL, &sig, &ucode);
	if (error != KERN_SUCCESS) {
		if (usermode) {
			/*
			 * Debug: a user fault vm_fault could not resolve.  Say
			 * whether the vm_map actually has an entry for va (with
			 * its protection) -- tells "no mapping expected"
			 * (bad/corrupted pointer) apart from a vm_map/pmap
			 * disagreement.  Rate-limited.
			 */
			if (usegv_count++ < 64) {
				vm_map_entry_t tmp_entry;
				uint64_t asid;
				int found;

				__asm __volatile("csrrd %0, %1" : "=r"(asid)
				    : "i"(LOONGARCH_CSR_ASID));
				vm_map_lock_read(map);
				found = vm_map_lookup_entry(map, va,
				    &tmp_entry);
				if (found)
					printf("USEGV pid=%d va=%#lx ftype=%d "
					    "err=%d MAP_ENTRY=[%#lx-%#lx) "
					    "prot=%#x maxprot=%#x asid=%#lx\n",
					    p->p_pid, (unsigned long)va, ftype,
					    error,
					    (unsigned long)tmp_entry->start,
					    (unsigned long)tmp_entry->end,
					    tmp_entry->protection,
					    tmp_entry->max_protection,
					    (unsigned long)asid);
				else
					printf("USEGV pid=%d va=%#lx ftype=%d "
					    "err=%d NO_MAP_ENTRY asid=%#lx\n",
					    p->p_pid, (unsigned long)va, ftype,
					    error, (unsigned long)asid);
				vm_map_unlock_read(map);
			}
			call_trapsignal(td, sig, ucode, (void *)evaddr,
			    EXC_CODE(frame->tf_estat));
		} else {
			if (pcb->pcb_onfault != 0) {
				frame->tf_a[0] = error;
				frame->tf_era = pcb->pcb_onfault;
				return;
			}
			goto fatal;
		}
	}

done:
	if (usermode)
		userret(td, frame);
	return;

fatal:
	dump_regs(frame);
#ifdef KDB
	if (debugger_on_trap) {
		kdb_why = KDB_WHY_TRAP;
		handled = kdb_trap(EXC_CODE(frame->tf_estat), 0, frame);
		kdb_why = KDB_WHY_UNSET;
		if (handled)
			return;
	}
#endif
	panic("Fatal page fault at %#lx: %#016lx", frame->tf_era, evaddr);
}

void
do_trap_supervisor(struct trapframe *frame)
{
	uint64_t exception;

	/* Ensure we came from supervisor mode, interrupts disabled */
	exception = EXC_CODE(frame->tf_estat);

	if (EXC_CODE(frame->tf_estat) == EXCCODE_RSV) {
		/* Interrupt */
		intr_irq_handler(frame, INTR_ROOT_IRQ);
		return;
	}

#ifdef KDTRACE_HOOKS
	if (dtrace_trap_func != NULL && (*dtrace_trap_func)(frame, exception))
		return;
#endif

	switch (exception) {
	case EXCCODE_ADE:
		dump_regs(frame);
		panic("Memory access exception at 0x%016lx\n", frame->tf_era);
		break;
	case EXCCODE_ALE:
		dump_regs(frame);
		panic("Misaligned address exception at %#016lx: %#016lx\n",
		    frame->tf_era, frame->tf_badvaddr);
		break;
	case EXCCODE_TLBL:
	case EXCCODE_TLBS:
	case EXCCODE_TLBI:
	case EXCCODE_TLBM:
	case EXCCODE_TLBNR:
	case EXCCODE_TLBNX:
	case EXCCODE_TLBPE:
		if (curthread->td_md.md_spinlock_count == 0 &&
			(frame->tf_prmd & CSR_PRMD_PIE) != 0)
			intr_enable();
		page_fault_handler(frame, 0);
		break;
	case EXCCODE_BP:
#ifdef KDTRACE_HOOKS
		if (dtrace_invop_jump_addr != NULL &&
		    dtrace_invop_jump_addr(frame) == 0)
				break;
#endif
#ifdef KDB
		kdb_trap(exception, 0, frame);
#else
		dump_regs(frame);
		panic("No debugger in kernel.\n");
#endif
		break;
	case EXCCODE_WATCH:
		la_watch_kern_handler(frame);
#ifdef KDB
		kdb_trap(exception, 0, frame);
#else
		dump_regs(frame);
		panic("Watchpoint exception in kernel at %#lx\n",
		    (unsigned long)frame->tf_era);
#endif
		break;
	case EXCCODE_INE:
		dump_regs(frame);
		panic("Illegal instruction at 0x%016lx\n", frame->tf_era);
		break;
	case EXCCODE_FPDIS:
		dump_regs(frame);
		panic("FPU disabled in kernel at 0x%016lx\n", frame->tf_era);
		break;
	case EXCCODE_FPE:
		dump_regs(frame);
		panic("FPU exception in kernel at 0x%016lx\n", frame->tf_era);
		break;
	default:
		dump_regs(frame);
		panic("Unknown kernel exception %lx trap value %lx\n",
		    exception, frame->tf_badvaddr);
	}
}

void
do_trap_user(struct trapframe *frame)
{
	uint64_t exception;
	struct thread *td;
	struct pcb *pcb;

	td = curthread;
	pcb = td->td_pcb;

#ifdef TRAP_DEBUG
	printf("USER TRAP: exc=%lx era=%lx badv=%lx\n",
	    EXC_CODE(frame->tf_estat), frame->tf_era, frame->tf_badvaddr);
#endif

	KASSERT(td->td_frame == frame,
	    ("%s: td_frame %p != frame %p", __func__, td->td_frame, frame));

	/* Ensure we came from usermode, interrupts disabled */
	exception = EXC_CODE(frame->tf_estat);
	if (EXC_CODE(frame->tf_estat) == EXCCODE_RSV) {
		/* Interrupt */
		intr_irq_handler(frame, INTR_ROOT_IRQ);
		return;
	}

	/*
	 * Re-enable interrupts for synchronous exceptions from userland
	 * (syscall, page faults, ...).  The exception entry runs with IE=0
	 * (hardware clears CRMD.IE on trap); without this the whole syscall
	 * runs interrupts-disabled, so IPIs sent to the syscall CPU (rmlock
	 * smp_rendezvous, TLB shootdown) are deferred until ertn -- which
	 * deadlocks rmlock rendezvous (the reader spins in syscall context
	 * and never services IPI_RENDEZVOUS) and lets stale TLBs persist
	 * during the syscall (buffer-cache corruption).  Mirrors arm64
	 * do_el0_sync(), which calls intr_enable() before the exception
	 * switch.  Interrupts (EXCCODE_RSV, handled above) keep IE=0 until
	 * EOI and are unaffected.
	 */
	intr_enable();

	CTR4(KTR_TRAP, "%s: exception=%lu, pc=%lx, badvaddr=%lx", __func__,
	    exception, frame->tf_era, frame->tf_badvaddr);

	switch (exception) {
	case EXCCODE_ADE:
		call_trapsignal(td, SIGBUS, BUS_ADRERR, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	case EXCCODE_ALE:
		call_trapsignal(td, SIGBUS, BUS_ADRALN, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	case EXCCODE_BCE:
		/*
		 * Bounds Check Error: raised by the asrtle/asrtgt and bounded
		 * load/store (ldle/ldgt/stle/stgt/...) instructions.
		 * Deliver SIGSEGV rather than taking the kernel down.
		 * tf_badvaddr holds the checked address for the
		 * load/store forms.
		 */
		call_trapsignal(td, SIGSEGV, SEGV_ACCERR,
		    (void *)frame->tf_badvaddr, exception);
		userret(td, frame);
		break;
	case EXCCODE_TLBL:
	case EXCCODE_TLBS:
	case EXCCODE_TLBI:
	case EXCCODE_TLBM:
	case EXCCODE_TLBNR:
	case EXCCODE_TLBNX:
	case EXCCODE_TLBPE:
		page_fault_handler(frame, 1);
		break;
	case EXCCODE_SYS:
		frame->tf_era += 4;	/* Next instruction */
		syscall_handler();
		break;
	case EXCCODE_INE:
		call_trapsignal(td, SIGILL, ILL_ILLTRP, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	case EXCCODE_IPE:
		/* Privileged instruction executed from user mode */
		call_trapsignal(td, SIGILL, ILL_PRVOPC, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	case EXCCODE_FPDIS:
		/*
		 * FPU Disabled trap.  Enable FPU and retry the
		 * instruction.  FPU may be disabled after fork()
		 * even though PCB_FP_STARTED is set, so always
		 * re-enable it here.  STARTED only gates whether
		 * this is the first time the thread uses FPU.
		 */
		if ((pcb->pcb_fpflags & PCB_FP_STARTED) == 0)
			pcb->pcb_fpflags |= PCB_FP_STARTED;
		frame->tf_euen |= CSR_EUEN_FPEN;
		break;
	case EXCCODE_LSXDIS:
		/*
		 * LSX (128-bit SIMD) disabled: enable LSX+FP and retry.
		 * Vector state is saved/restored from tf_vregs by exception.S;
		 * first use finds it zeroed (exec_setregs memsets the
		 * trapframe).
		 */
		pcb->pcb_fpflags |= PCB_FP_STARTED | PCB_FP_LSX_STARTED;
		frame->tf_euen |= CSR_EUEN_FPEN | CSR_EUEN_LSXEN;
		break;
	case EXCCODE_LASXDIS:
		/*
		 * LASX (256-bit SIMD) disabled: enable LASX (which implies
		 * LSX+FP) and retry.
		 */
		pcb->pcb_fpflags |= PCB_FP_STARTED | PCB_FP_LSX_STARTED |
		    PCB_FP_LASX_STARTED;
		frame->tf_euen |= CSR_EUEN_FPEN | CSR_EUEN_LSXEN |
		    CSR_EUEN_LASXEN;
		break;
	case EXCCODE_BTDIS:
		/* LBT (binary translation) is not managed; SIGILL. */
		call_trapsignal(td, SIGILL, ILL_ILLOPC, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	case EXCCODE_FPE: {
		uint64_t fcsr = frame->uf.fcsr0;
		uint64_t cause_mask;
		int code;

		/* Decode the FCSR0 Cause bits into si_code. */
		if (fcsr & FPU_CSR_INV_X)
			code = FPE_FLTINV;
		else if (fcsr & FPU_CSR_DIV_X)
			code = FPE_FLTDIV;
		else if (fcsr & FPU_CSR_OVF_X)
			code = FPE_FLTOVF;
		else if (fcsr & FPU_CSR_UDF_X)
			code = FPE_FLTUND;
		else if (fcsr & FPU_CSR_INE_X)
			code = FPE_FLTRES;
		else
			code = FPE_FLTINV;

		/*
		 * Clear the enabled Cause bits in the saved FCSR0 so the
		 * faulting instruction does not re-trap FPE on ertn.
		 * Shift Enable bits [4:0] to align with Cause bits [28:24],
		 * keep only the enabled causes, and clear them.
		 */
		cause_mask = (fcsr & FPU_CSR_ALL_E) <<
		    (ffsll(FPU_CSR_ALL_X) - ffsll(FPU_CSR_ALL_E));
		frame->uf.fcsr0 = fcsr & ~(cause_mask & FPU_CSR_ALL_X);

		call_trapsignal(td, SIGFPE, code, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	}
	case EXCCODE_BP:
		call_trapsignal(td, SIGTRAP, TRAP_BRKPT, (void *)frame->tf_era,
		    exception);
		userret(td, frame);
		break;
	case EXCCODE_WATCH:
		la_watch_handler(td, frame);
		userret(td, frame);
		break;
	default:
		dump_regs(frame);
		panic("Unknown userland exception %lx, trap value %lx\n",
		    exception, frame->tf_badvaddr);
	}
}
