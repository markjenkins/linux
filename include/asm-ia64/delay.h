#ifndef _ASM_IA64_DELAY_H
#define _ASM_IA64_DELAY_H

/*
 * Delay routines using a pre-computed "cycles/usec" value.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/compiler.h>

#include <asm/intrinsics.h>
#include <asm/processor.h>

static __inline__ void
ia64_set_itm (unsigned long val)
{
	ia64_setreg(_IA64_REG_CR_ITM, val);
	ia64_srlz_d();
}

static __inline__ unsigned long
ia64_get_itm (void)
{
	unsigned long result;

	result = ia64_getreg(_IA64_REG_CR_ITM);
	ia64_srlz_d();
	return result;
}

static __inline__ void
ia64_set_itv (unsigned long val)
{
	ia64_setreg(_IA64_REG_CR_ITV, val);
	ia64_srlz_d();
}

static __inline__ void
ia64_set_itc (unsigned long val)
{
	ia64_setreg(_IA64_REG_AR_ITC, val);
	ia64_srlz_d();
}

static __inline__ unsigned long
ia64_get_itc (void)
{
	unsigned long result;

	result = ia64_getreg(_IA64_REG_AR_ITC);
	ia64_barrier();
#ifdef CONFIG_ITANIUM
	while (unlikely((__s32) result == -1)) {
		result = ia64_getreg(_IA64_REG_AR_ITC);
		ia64_barrier();
	}
#endif
	return result;
}

static __inline__ void
__delay (unsigned long loops)
{
	if (loops < 1)
		return;

	while (loops--)
		barrier();
}

static __inline__ void
udelay (unsigned long usecs)
{
	unsigned long start = ia64_get_itc();
	unsigned long cycles = usecs*local_cpu_data->cyc_per_usec;

	while (ia64_get_itc() - start < cycles)
		/* skip */;
}

#endif /* _ASM_IA64_DELAY_H */
