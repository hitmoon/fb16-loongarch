/*-
 * Copyright (c) 2014 Andrew Turner
 * Copyright (c) 2015-2017 Ruslan Bukin <br@bsdpad.com>
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
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/signalvar.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/ucontext.h>

#include <machine/cpu.h>
#include <machine/fpe.h>
#include <machine/kdb.h>
#include <machine/pcb.h>
#include <machine/pte.h>
#include <machine/loongarchreg.h>
#include <machine/trap.h>
#include <machine/watch.h>
#include <machine/vmparam.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

static void get_fpcontext(struct thread *td, mcontext_t *mcp);
static void set_fpcontext(struct thread *td, mcontext_t *mcp);

#if 0
_Static_assert(sizeof(mcontext_t) == 864, "mcontext_t size incorrect");
_Static_assert(sizeof(ucontext_t) == 936, "ucontext_t size incorrect");
_Static_assert(sizeof(siginfo_t) == 80, "siginfo_t size incorrect");
#endif

int
fill_regs(struct thread *td, struct reg *regs)
{
	struct trapframe *frame;

	frame = td->td_frame;
	regs->orig_a0 = frame->tf_a0;
	regs->csr_era = frame->tf_era;
	regs->csr_badv = frame->tf_badvaddr;
	regs->crmd = frame->tf_crmd;
	regs->prmd = frame->tf_prmd;
	regs->euen = frame->tf_euen;
	regs->misc = frame->tf_misc;
	regs->ecfg = frame->tf_ecfg;
	regs->estat = frame->tf_estat;

	memcpy(regs->regs, frame->tf_regs, sizeof(regs->regs));

	return (0);
}

int
set_regs(struct thread *td, struct reg *regs)
{
	struct trapframe *frame;

	frame = td->td_frame;
	frame->tf_a0 = regs->orig_a0;
	frame->tf_era = regs->csr_era;
	frame->tf_badvaddr = regs->csr_badv;
	frame->tf_crmd = regs->crmd;
	frame->tf_prmd = regs->prmd;
	frame->tf_euen = regs->euen;
	frame->tf_misc = regs->misc;
	frame->tf_ecfg = regs->ecfg;
	frame->tf_estat = regs->estat;

	memcpy(frame->tf_regs, regs->regs, sizeof(frame->tf_regs));

	return (0);
}

int
fill_fpregs(struct thread *td, struct fpreg *regs)
{
	struct trapframe *tf;

	tf = td->td_frame;

	if ((td->td_pcb->pcb_fpflags & PCB_FP_STARTED) != 0) {
		memcpy(regs->fpr, tf->tf_fregs, sizeof(regs->fpr));
		regs->fcc = tf->uf.fcc;
		regs->fcsr = (uint32_t)tf->uf.fcsr0;
	} else
		memset(regs, 0, sizeof(*regs));

	return (0);
}

int
set_fpregs(struct thread *td, struct fpreg *regs)
{
	struct trapframe *tf;

	/*
	 * Write FP state into the trapframe (what load_registers restores on
	 * return to userland), not the PCB -- writing the PCB had no effect on
	 * resume.  fcc and fcsr are written from their explicit fields.
	 */
	tf = td->td_frame;

	memcpy(tf->tf_fregs, regs->fpr, sizeof(regs->fpr));
	tf->uf.fcc = regs->fcc;
	tf->uf.fcsr0 = regs->fcsr;
	td->td_pcb->pcb_fpflags |= PCB_FP_STARTED;

	return (0);
}

int
fill_dbregs(struct thread *td, struct dbreg *regs)
{
	struct la_watch_image *img = &td->td_md.md_watch;
	unsigned i;

	memset(regs, 0, sizeof(*regs));
	regs->db_revision = 1;
	regs->db_nslots = LA_WATCH_SLOTS;
	for (i = 0; i < LA_WATCH_SLOTS; i++) {
		regs->db_slots[i].db_addr = img->slot[i].addr;
		regs->db_slots[i].db_ctrl = img->slot[i].ctrl;
	}
	return (0);
}

int
set_dbregs(struct thread *td, struct dbreg *regs)
{
	struct la_watch_image *img = &td->td_md.md_watch;
	uint64_t addr, ctrl;
	unsigned i;

	/*
	 * Validate each requested slot.  ptrace may only arm user-mode
	 * (PLV3) watchpoints on user addresses; strip everything else.
	 */
	for (i = 0; i < LA_WATCH_SLOTS; i++) {
		ctrl = regs->db_slots[i].db_ctrl;
		if (ctrl == 0)
			continue;		/* slot unused */

		addr = regs->db_slots[i].db_addr;
		if (addr >= VM_MAXUSER_ADDRESS)
			return (EINVAL);

		/* Keep only load/store/size, and force PLV3-only. */
		ctrl &= CSR_DBC_LOADEN | CSR_DBC_STOREEN | CSR_DBC_SIZE_MASK;
		ctrl |= CSR_DBC_PLV3;
		if ((ctrl & (CSR_DBC_LOADEN | CSR_DBC_STOREEN)) == 0)
			return (EINVAL);

		regs->db_slots[i].db_addr = addr;
		regs->db_slots[i].db_ctrl = ctrl;
	}

	/* Commit the image. */
	for (i = 0; i < LA_WATCH_SLOTS; i++) {
		img->slot[i].addr = regs->db_slots[i].db_addr;
		img->slot[i].ctrl = regs->db_slots[i].db_ctrl;
	}

	/* Reload the hardware now if the target is the current thread. */
	if (td == curthread)
		la_watch_load(td);

	return (0);
}

void
exec_setregs(struct thread *td, struct image_params *imgp, uintptr_t stack)
{
	struct trapframe *tf;
	struct pcb *pcb;

	tf = td->td_frame;
	pcb = td->td_pcb;

	memset(tf, 0, sizeof(struct trapframe));

	tf->tf_a[0] = stack;
	tf->tf_sp = STACKALIGN(stack);
	tf->tf_era = imgp->entry_addr;
	tf->tf_ra = imgp->entry_addr;
	tf->tf_prmd |= CSR_CRMD_IE; /* Enable interrupts. */
	tf->tf_prmd |= PLV_USER << CSR_CRMD_PLV_SHIFT; /* User mode. */
	tf->tf_prmd |= CSR_PRMD_PWE; /* Enable watchpoints in user mode. */

	/* Exec drops any inherited watchpoints. */
	memset(&td->td_md.md_watch, 0, sizeof(td->td_md.md_watch));
	td->td_md.md_ss_active = 0;

	pcb->pcb_fpflags &= ~PCB_FP_STARTED;
}

/* Sanity check these are the same size, they will be memcpy'd to and from */

CTASSERT(sizeof(((struct trapframe *)0)->tf_a) ==
    sizeof((struct gpregs *)0)->gp_a);
CTASSERT(sizeof(((struct trapframe *)0)->tf_s) ==
    sizeof((struct gpregs *)0)->gp_s);
CTASSERT(sizeof(((struct trapframe *)0)->tf_t) ==
    sizeof((struct gpregs *)0)->gp_t);
CTASSERT(sizeof(((struct trapframe *)0)->tf_a) ==
    sizeof((struct reg *)0)->a);
CTASSERT(sizeof(((struct trapframe *)0)->tf_s) ==
    sizeof((struct reg *)0)->s);
CTASSERT(sizeof(((struct trapframe *)0)->tf_t) ==
    sizeof((struct reg *)0)->t);

int
get_mcontext(struct thread *td, mcontext_t *mcp, int clear_ret)
{
	struct trapframe *tf = td->td_frame;

	memcpy(mcp->mc_gpregs.gp_regs, tf->tf_regs,
	    sizeof(mcp->mc_gpregs.gp_regs));

	if (clear_ret & GET_MC_CLEAR_RET) {
		mcp->mc_gpregs.gp_a[0] = 0;
		mcp->mc_gpregs.gp_t[0] = 0; /* clear syscall error */
	}

	mcp->mc_gpregs.gp_orig_a0 = tf->tf_a0;
	mcp->mc_gpregs.gp_era = tf->tf_era;
	mcp->mc_gpregs.gp_badvaddr = tf->tf_badvaddr;
	mcp->mc_gpregs.gp_crmd = tf->tf_crmd;
	mcp->mc_gpregs.gp_prmd = tf->tf_prmd;
	mcp->mc_gpregs.gp_euen = tf->tf_euen;
	mcp->mc_gpregs.gp_ecfg = tf->tf_ecfg;
	mcp->mc_gpregs.gp_estat = tf->tf_estat;
	get_fpcontext(td, mcp);

	return (0);
}

int
set_mcontext(struct thread *td, mcontext_t *mcp)
{
	struct trapframe *tf;

	tf = td->td_frame;

	memcpy(tf->tf_regs, mcp->mc_gpregs.gp_regs, sizeof(tf->tf_regs));

	tf->tf_a0 = mcp->mc_gpregs.gp_orig_a0;
	tf->tf_era = mcp->mc_gpregs.gp_era;
	tf->tf_badvaddr = mcp->mc_gpregs.gp_badvaddr;
	tf->tf_crmd = mcp->mc_gpregs.gp_crmd;
	tf->tf_prmd = mcp->mc_gpregs.gp_prmd;
	tf->tf_euen = mcp->mc_gpregs.gp_euen;
	tf->tf_ecfg = mcp->mc_gpregs.gp_ecfg;
	tf->tf_estat = mcp->mc_gpregs.gp_estat;
	set_fpcontext(td, mcp);

	return (0);
}

static void
get_fpcontext(struct thread *td, mcontext_t *mcp)
{
	struct pcb *curpcb;

	critical_enter();

	curpcb = curthread->td_pcb;

	KASSERT(td->td_pcb == curpcb, ("Invalid fpe pcb"));

	if ((curpcb->pcb_fpflags & PCB_FP_STARTED) != 0) {
		/*
		 * If we have just been running FPE instructions we will
		 * need to save the state to memcpy it below.
		 */
		fpe_state_save(td);

		KASSERT((curpcb->pcb_fpflags & ~(PCB_FP_USERMASK |
		    PCB_FP_LSX_STARTED | PCB_FP_LASX_STARTED)) == 0,
		    ("Non-userspace FPE flags set in get_fpcontext"));
		memcpy(mcp->mc_fpregs.fp_regs, curpcb->pcb_fregs,
		    sizeof(mcp->mc_fpregs.fp_regs));
		mcp->mc_fpregs.fp_fcsr = curpcb->uf.pcb_fcsr0;
		/*
		 * fcc0-fcc7 are not stored in the PCB (only FPRs and fcsr0
		 * are), but the trapframe captured them on entry, so read them
		 * from there to round-trip FP condition codes across signals.
		 */
		mcp->mc_fpregs.fcc = td->td_frame->uf.fcc;
		mcp->mc_fpregs.fp_flags = curpcb->pcb_fpflags;
		mcp->mc_flags |= _MC_FP_VALID;
	}

	critical_exit();
}

static void
set_fpcontext(struct thread *td, mcontext_t *mcp)
{
	struct trapframe *tf;

	critical_enter();

	if ((mcp->mc_flags & _MC_FP_VALID) != 0) {
		tf = td->td_frame;
		/*
		 * On return to userland load_registers restores the FP
		 * register set from the trapframe (tf_fregs / uf.fcsr0), not
		 * from the PCB.  Install the saved FP state into the trapframe
		 * here: the previous code wrote the PCB, which load_registers
		 * ignores, so on sigreturn the signal handler's FPRs/fcsr
		 * resumed and any async signal corrupted the victim's FP state.
		 * The PCB copy is re-synced from hardware on the next
		 * fpe_state_save, so it needs no separate update.
		 */
		memcpy(tf->tf_fregs, mcp->mc_fpregs.fp_regs,
		    sizeof(mcp->mc_fpregs.fp_regs));
		tf->uf.fcsr0 = mcp->mc_fpregs.fp_fcsr;
		tf->uf.fcc = mcp->mc_fpregs.fcc;
		td->td_pcb->pcb_fpflags =
		    mcp->mc_fpregs.fp_flags & PCB_FP_USERMASK;
	}

	critical_exit();
}

int
sys_sigreturn(struct thread *td, struct sigreturn_args *uap)
{
	ucontext_t uc;
	int error;

	if (copyin(uap->sigcntxp, &uc, sizeof(uc)))
		return (EFAULT);

	error = set_mcontext(td, &uc.uc_mcontext);
	if (error != 0)
		return (error);

	/*
	 * Restore SIMD vector state if the signal frame carried it.
	 * set_mcontext already restored tf_euen (incl. the SIMD bits) from
	 * gp_euen and the scalar FP set, but cleared the LSX/LASX STARTED
	 * latches (USERMASK); re-establish them and put the saved vectors
	 * back into the trapframe so load_registers reinstates them on
	 * return to user.
	 */
	if ((uc.uc_mcontext.mc_flags & _MC_FP_SIMD) != 0) {
		struct loongarch_simd_ctx sc;
		struct trapframe *tf = td->td_frame;

		error = copyin((void *)(uintptr_t)uc.uc_mcontext.mc_spare[0],
		    &sc, sizeof(sc));
		if (error != 0)
			return (EFAULT);
		if (sc.d_size != sizeof(sc) ||
		    (sc.d_magic != LOONGARCH_SIMD_MAGIC_LSX &&
		    sc.d_magic != LOONGARCH_SIMD_MAGIC_LASX))
			return (EINVAL);
		if (sc.d_magic == LOONGARCH_SIMD_MAGIC_LASX) {
			for (int i = 0; i < 32; i++)
				memcpy(tf->tf_vregs[i], sc.d_regs[i], 32);
			td->td_pcb->pcb_fpflags |= PCB_FP_STARTED |
			    PCB_FP_LSX_STARTED | PCB_FP_LASX_STARTED;
		} else {
			for (int i = 0; i < 32; i++)
				memcpy(tf->tf_vregs[i], sc.d_regs[i], 16);
			td->td_pcb->pcb_fpflags |=
			    PCB_FP_STARTED | PCB_FP_LSX_STARTED;
		}
	}

	/* Restore signal mask. */
	kern_sigprocmask(td, SIG_SETMASK, &uc.uc_sigmask, NULL, 0);

	return (EJUSTRETURN);
}

void
sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask)
{
	struct sigframe *fp, frame;
	struct sysentvec *sysent;
	struct trapframe *tf;
	struct sigacts *psp;
	struct thread *td;
	struct proc *p;
	int onstack;
	int sig;

	td = curthread;
	p = td->td_proc;
	PROC_LOCK_ASSERT(p, MA_OWNED);

	sig = ksi->ksi_signo;
	psp = p->p_sigacts;
	mtx_assert(&psp->ps_mtx, MA_OWNED);

	tf = td->td_frame;
	onstack = sigonstack(tf->tf_regs[3]);

	CTR4(KTR_SIG, "sendsig: td=%p (%s) catcher=%p sig=%d", td, p->p_comm,
	    catcher, sig);

	/* Allocate and validate space for the signal handler context. */
	if ((td->td_pflags & TDP_ALTSTACK) != 0 && !onstack &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		fp = (struct sigframe *)((uintptr_t)td->td_sigstk.ss_sp +
		    td->td_sigstk.ss_size);
	} else {
		fp = (struct sigframe *)td->td_frame->tf_regs[3];
	}

	/* Make room, keeping the stack aligned */
	fp--;
	fp = (struct sigframe *)STACKALIGN(fp);

	/* Fill in the frame to copy out */
	bzero(&frame, sizeof(frame));
	get_mcontext(td, &frame.sf_uc.uc_mcontext, 0);
	/*
	 * If the thread has LSX/LASX active, snapshot its vector state into
	 * the sigframe's SIMD block and record it in the mcontext; sigreturn
	 * restores it so a handler that uses SIMD cannot clobber the preempted
	 * thread's vectors.  The block rides along with the frame copyout.
	 */
	if ((td->td_pcb->pcb_fpflags &
	    (PCB_FP_LSX_STARTED | PCB_FP_LASX_STARTED)) != 0) {
		frame.sf_simd.d_size = sizeof(frame.sf_simd);
		frame.sf_simd.d_magic =
		    (td->td_pcb->pcb_fpflags & PCB_FP_LASX_STARTED) != 0 ?
		    LOONGARCH_SIMD_MAGIC_LASX : LOONGARCH_SIMD_MAGIC_LSX;
		for (int i = 0; i < 32; i++)
			memcpy(frame.sf_simd.d_regs[i], tf->tf_vregs[i], 32);
		frame.sf_uc.uc_mcontext.mc_flags |= _MC_FP_SIMD;
		frame.sf_uc.uc_mcontext.mc_spare[0] =
		    (uint64_t)(uintptr_t)&fp->sf_simd;
	}
	frame.sf_si = ksi->ksi_info;
	frame.sf_uc.uc_sigmask = *mask;
	frame.sf_uc.uc_stack = td->td_sigstk;
	frame.sf_uc.uc_stack.ss_flags = (td->td_pflags & TDP_ALTSTACK) != 0 ?
	    (onstack ? SS_ONSTACK : 0) : SS_DISABLE;
	mtx_unlock(&psp->ps_mtx);
	PROC_UNLOCK(td->td_proc);

	/* Copy the sigframe out to the user's stack. */
	if (copyout(&frame, fp, sizeof(*fp)) != 0) {
		/* Process has trashed its stack. Kill it. */
		CTR2(KTR_SIG, "sendsig: sigexit td=%p fp=%p", td, fp);
		PROC_LOCK(p);
		sigexit(td, SIGILL);
	}

	tf->tf_regs[4] = sig;
	tf->tf_regs[5] = (register_t)&fp->sf_si;
	tf->tf_regs[6] = (register_t)&fp->sf_uc;

	tf->tf_regs[3] = (register_t)fp;

	sysent = p->p_sysent;
	if (PROC_HAS_SHP(p))
		tf->tf_regs[1] = (register_t)PROC_SIGCODE(p);
	else
		tf->tf_regs[1] = (register_t)(PROC_PS_STRINGS(p) -
		    *(sysent->sv_szsigcode));

	tf->tf_era = (register_t)catcher;

	CTR3(KTR_SIG, "sendsig: return td=%p pc=%#x sp=%#x", td, tf->tf_era,
	    tf->tf_regs[3]);

	PROC_LOCK(p);
	mtx_lock(&psp->ps_mtx);
}
