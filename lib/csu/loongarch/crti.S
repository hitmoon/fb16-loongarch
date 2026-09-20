#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#ifndef __clang__
	.gnu_attribute 4, 0
#endif
	.section .init,"ax",%progbits
	.align 4
	.globl	_init
	.type	_init,%function
_init:
	addi.d	$sp, $sp, -16
	st.d	$ra, $sp, 0

	.section .fini,"ax",%progbits
	.align 4
	.globl	_fini
	.type	_fini,%function
_fini:
	addi.d	$sp, $sp, -16
	st.d	$ra, $sp, 0

	.section .note.GNU-stack,"",%progbits
