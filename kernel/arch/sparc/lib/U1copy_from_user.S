

#define EX_LD(x)		\
98:	x;			\
	.section __ex_table,"a";\
	.align 4;		\
	.word 98b, __retl_one;	\
	.text;			\
	.align 4;

#define FUNC_NAME		___copy_from_user
#define LOAD(type,addr,dest)	type##a [addr] %asi, dest
#define LOAD_BLK(addr,dest)	ldda [addr] ASI_BLK_AIUS, dest
#define EX_RETVAL(x)		0

	/* Writing to %asi is _expensive_ so we hardcode it.
	 * Reading %asi to check for KERNEL_DS is comparatively
	 * cheap.
	 */
#define PREAMBLE					\
	rd		%asi, %g1;			\
	cmp		%g1, ASI_AIUS;			\
	bne,pn		%icc, ___copy_in_user;		\
	 nop;						\

#include "U1memcpy.S"
