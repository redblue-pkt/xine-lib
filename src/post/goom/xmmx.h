/*	xmmx.h

	eXtended MultiMedia eXtensions GCC interface library for IA32.

	To use this library, simply include this header file
	and compile with GCC.  You MUST have inlining enabled
	in order for xmmx_ok() to work; this can be done by
	simply using -O on the GCC command line.

	Compiling with -DXMMX_TRACE will cause detailed trace
	output to be sent to stderr for each mmx operation.
	This adds lots of code, and obviously slows execution to
	a crawl, but can be very useful for debugging.

	THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY
	EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
	LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY
	AND FITNESS FOR ANY PARTICULAR PURPOSE.

	1999 by R. Fisher
	Based on libmmx, 1997-99 by H. Dietz and R. Fisher

 Notes:
	It appears that the latest gas has the pand problem fixed, therefore
	  I'll undefine BROKEN_PAND by default.
*/

#ifndef _XMMX_H
#define _XMMX_H

/*	Warning:  at this writing, the version of GAS packaged
	with most Linux distributions does not handle the
	parallel AND operation mnemonic correctly.  If the
	symbol BROKEN_PAND is defined, a slower alternative
	coding will be used.  If execution of mmxtest results
	in an illegal instruction fault, define this symbol.
*/
#undef	BROKEN_PAND


/*	The type of an value that fits in an (Extended) MMX register
	(note that long long constant values MUST be suffixed
	 by LL and unsigned long long values by ULL, lest
	 they be truncated by the compiler)
*/
#ifndef _MMX_H
typedef	union {
	long long		q;	/* Quadword (64-bit) value */
	unsigned long long	uq;	/* Unsigned Quadword */
	int			d[2];	/* 2 Doubleword (32-bit) values */
	unsigned int		ud[2];	/* 2 Unsigned Doubleword */
	short			w[4];	/* 4 Word (16-bit) values */
	unsigned short		uw[4];	/* 4 Unsigned Word */
	char			b[8];	/* 8 Byte (8-bit) values */
	unsigned char		ub[8];	/* 8 Unsigned Byte */
	float			s[2];	/* Single-precision (32-bit) value */
} __attribute__ ((aligned (8))) mmx_t;	/* On an 8-byte (64-bit) boundary */
#endif

/*	Helper functions for the instruction macros that follow...
	(note that memory-to-register, m2r, instructions are nearly
	 as efficient as register-to-register, r2r, instructions;
	 however, memory-to-memory instructions are really simulated
	 as a convenience, and are only 1/3 as efficient)
*/
#ifdef	XMMX_TRACE

/*	Include the stuff for printing a trace to stderr...
*/

#include <stdio.h>

#define	mmx_i2r(op, imm, reg) \
	{ \
		mmx_t mmx_trace; \
		mmx_trace.uq = (imm); \
		fprintf(stderr, #op "_i2r(" #imm "=0x%08x%08x, ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ ("movq %%" #reg ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #reg "=0x%08x%08x) => ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ (#op " %0, %%" #reg \
				      : /* nothing */ \
				      : "X" (imm)); \
		__asm__ __volatile__ ("movq %%" #reg ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #reg "=0x%08x%08x\n", \
			mmx_trace.d[1], mmx_trace.d[0]); \
	}

#define	mmx_m2r(op, mem, reg) \
	{ \
		mmx_t mmx_trace; \
		mmx_trace = (mem); \
		fprintf(stderr, #op "_m2r(" #mem "=0x%08x%08x, ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ ("movq %%" #reg ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #reg "=0x%08x%08x) => ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ (#op " %0, %%" #reg \
				      : /* nothing */ \
				      : "X" (mem)); \
		__asm__ __volatile__ ("movq %%" #reg ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #reg "=0x%08x%08x\n", \
			mmx_trace.d[1], mmx_trace.d[0]); \
	}

#define	mmx_r2m(op, reg, mem) \
	{ \
		mmx_t mmx_trace; \
		__asm__ __volatile__ ("movq %%" #reg ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #op "_r2m(" #reg "=0x%08x%08x, ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		mmx_trace = (mem); \
		fprintf(stderr, #mem "=0x%08x%08x) => ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ (#op " %%" #reg ", %0" \
				      : "=X" (mem) \
				      : /* nothing */ ); \
		mmx_trace = (mem); \
		fprintf(stderr, #mem "=0x%08x%08x\n", \
			mmx_trace.d[1], mmx_trace.d[0]); \
	}

#define	mmx_r2r(op, regs, regd) \
	{ \
		mmx_t mmx_trace; \
		__asm__ __volatile__ ("movq %%" #regs ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #op "_r2r(" #regs "=0x%08x%08x, ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ ("movq %%" #regd ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #regd "=0x%08x%08x) => ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ (#op " %" #regs ", %" #regd); \
		__asm__ __volatile__ ("movq %%" #regd ", %0" \
				      : "=X" (mmx_trace) \
				      : /* nothing */ ); \
		fprintf(stderr, #regd "=0x%08x%08x\n", \
			mmx_trace.d[1], mmx_trace.d[0]); \
	}

#define	mmx_m2m(op, mems, memd) \
	{ \
		mmx_t mmx_trace; \
		mmx_trace = (mems); \
		fprintf(stderr, #op "_m2m(" #mems "=0x%08x%08x, ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		mmx_trace = (memd); \
		fprintf(stderr, #memd "=0x%08x%08x) => ", \
			mmx_trace.d[1], mmx_trace.d[0]); \
		__asm__ __volatile__ ("movq %0, %%mm0\n\t" \
				      #op " %1, %%mm0\n\t" \
				      "movq %%mm0, %0" \
				      : "=X" (memd) \
				      : "X" (mems)); \
		mmx_trace = (memd); \
		fprintf(stderr, #memd "=0x%08x%08x\n", \
			mmx_trace.d[1], mmx_trace.d[0]); \
	}

#else

/*	These macros are a lot simpler without the tracing...
*/

#define	mmx_i2r(op, imm, reg) \
	__asm__ __volatile__ (#op " %0, %%" #reg \
			      : /* nothing */ \
			      : "X" (imm) )

#define	mmx_m2r(op, mem, reg) \
	__asm__ __volatile__ (#op " %0, %%" #reg \
			      : /* nothing */ \
			      : "X" (mem))

#define	mmx_m2ir(op, mem, rs) \
	__asm__ __volatile__ (#op " %0, %%" #rs \
			      : /* nothing */ \
			      : "X" (mem) )

#define	mmx_r2m(op, reg, mem) \
	__asm__ __volatile__ (#op " %%" #reg ", %0" \
			      : "=X" (mem) \
			      : /* nothing */ )

#define	mmx_r2r(op, regs, regd) \
	__asm__ __volatile__ (#op " %" #regs ", %" #regd)

#define	mmx_r2ir(op, rs1, rs2) \
	__asm__ __volatile__ (#op " %%" #rs1 ", %%" #rs2 \
			      : /* nothing */ \
			      : /* nothing */ )

#define	mmx_m2m(op, mems, memd) \
	__asm__ __volatile__ ("movq %0, %%mm0\n\t" \
			      #op " %1, %%mm0\n\t" \
			      "movq %%mm0, %0" \
			      : "=X" (memd) \
			      : "X" (mems))

#endif



/*	1x64 MOVe Quadword
	(this is both a load and a store...
	 in fact, it is the only way to store)
*/
#define	movq_m2r(var, reg)	mmx_m2r(movq, var, reg)
#define	movq_r2m(reg, var)	mmx_r2m(movq, reg, var)
#define	movq_r2r(regs, regd)	mmx_r2r(movq, regs, regd)
#define	movq(vars, vard) \
	__asm__ __volatile__ ("movq %1, %%mm0\n\t" \
			      "movq %%mm0, %0" \
			      : "=X" (vard) \
			      : "X" (vars))


/*	1x32 MOVe Doubleword
	(like movq, this is both load and store...
	 but is most useful for moving things between
	 mmx registers and ordinary registers)
*/
#define	movd_m2r(var, reg)	mmx_m2r(movd, var, reg)
#define	movd_r2m(reg, var)	mmx_r2m(movd, reg, var)
#define	movd_r2r(regs, regd)	mmx_r2r(movd, regs, regd)
#define	movd(vars, vard) \
	__asm__ __volatile__ ("movd %1, %%mm0\n\t" \
			      "movd %%mm0, %0" \
			      : "=X" (vard) \
			      : "X" (vars))



/*	4x16 Parallel MAGnitude
*/
#define	pmagw_m2r(var, reg)	mmx_m2r(pmagw, var, reg)
#define	pmagw_r2r(regs, regd)	mmx_r2r(pmagw, regs, regd)
#define	pmagw(vars, vard)	mmx_m2m(pmagw, vars, vard)


/*	4x16 Parallel ADDs using Saturation arithmetic
	and Implied destination
*/
#define	paddsiw_m2ir(var, rs)		mmx_m2ir(paddsiw, var, rs)
#define	paddsiw_r2ir(rs1, rs2)		mmx_r2ir(paddsiw, rs1, rs2)
#define	paddsiw(vars, vard)		mmx_m2m(paddsiw, vars, vard)


/*	4x16 Parallel SUBs using Saturation arithmetic
	and Implied destination
*/
#define	psubsiw_m2ir(var, rs)		mmx_m2ir(psubsiw, var, rs)
#define	psubsiw_r2ir(rs1, rs2)		mmx_r2ir(psubsiw, rs1, rs2)
#define	psubsiw(vars, vard)		mmx_m2m(psubsiw, vars, vard)


/*	4x16 Parallel MULs giving High 4x16 portions of results
	Rounded with 1/2 bit 15.
*/
#define	pmulhrw_m2r(var, reg)	mmx_m2r(pmulhrw, var, reg)
#define	pmulhrw_r2r(regs, regd)	mmx_r2r(pmulhrw, regs, regd)
#define	pmulhrw(vars, vard)	mmx_m2m(pmulhrw, vars, vard)


/*	4x16 Parallel MULs giving High 4x16 portions of results
	Rounded with 1/2 bit 15, storing to Implied register
*/
#define	pmulhriw_m2ir(var, rs)		mmx_m2ir(pmulhriw, var, rs)
#define	pmulhriw_r2ir(rs1, rs2)		mmx_r2ir(pmulhriw, rs1, rs2)
#define	pmulhriw(vars, vard)		mmx_m2m(pmulhriw, vars, vard)


/*	4x16 Parallel Muls (and ACcumulate) giving High 4x16 portions
	of results Rounded with 1/2 bit 15, accumulating with Implied register
*/
#define	pmachriw_m2ir(var, rs)		mmx_m2ir(pmachriw, var, rs)
#define	pmachriw_r2ir(rs1, rs2)		mmx_r2ir(pmachriw, rs1, rs2)
#define	pmachriw(vars, vard)		mmx_m2m(pmachriw, vars, vard)


/*	8x8u Parallel AVErage
*/
#define	paveb_m2r(var, reg)	mmx_m2r(paveb, var, reg)
#define	paveb_r2r(regs, regd)	mmx_r2r(paveb, regs, regd)
#define	paveb(vars, vard)	mmx_m2m(paveb, vars, vard)


/*	8x8u Parallel DISTance and accumulate with
	unsigned saturation to Implied register
*/
#define	pdistib_m2ir(var, rs)		mmx_m2ir(pdistib, var, rs)
#define	pdistib(vars, vard)		mmx_m2m(pdistib, vars, vard)


/*	8x8 Parallel conditional MoVe
	if implied register field is Zero
*/
#define	pmvzb_m2ir(var, rs)		mmx_m2ir(pmvzb, var, rs)


/*	8x8 Parallel conditional MoVe
	if implied register field is Not Zero
*/
#define	pmvnzb_m2ir(var, rs)		mmx_m2ir(pmvnzb, var, rs)


/*	8x8 Parallel conditional MoVe
	if implied register field is Less than Zero
*/
#define	pmvlzb_m2ir(var, rs)		mmx_m2ir(pmvlzb, var, rs)


/*	8x8 Parallel conditional MoVe
	if implied register field is Greater than or Equal to Zero
*/
#define	pmvgezb_m2ir(var, rs)		mmx_m2ir(pmvgezb, var, rs)


/*	Fast Empty MMx State
	(used to clean-up when going from mmx to float use
	 of the registers that are shared by both; note that
	 there is no float-to-xmmx operation needed, because
	 only the float tag word info is corruptible)
*/
#ifdef	XMMX_TRACE

#define	femms() \
	{ \
		fprintf(stderr, "femms()\n"); \
		__asm__ __volatile__ ("femms"); \
	}

#else

#define	femms()			__asm__ __volatile__ ("femms")

#endif

#endif

