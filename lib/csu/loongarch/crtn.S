#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#ifndef __clang__
	.gnu_attribute 4, 0
#endif
	.section .init,"ax",%progbits
	ld.d	$ra, $sp, 0
	addi.d	$sp, $sp, 16
	jr	$ra

	.section .fini,"ax",%progbits
	ld.d	$ra, $sp, 0
	addi.d	$sp, $sp, 16
	jr	$ra

	.section .note.GNU-stack,"",%progbits
