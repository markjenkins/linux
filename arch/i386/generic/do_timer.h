/* defines for inline arch setup functions */

static inline void do_timer_interrupt_hook(struct pt_regs *regs)
{
	do_timer(regs);
/*
 * In the SMP case we use the local APIC timer interrupt to do the
 * profiling, except when we simulate SMP mode on a uniprocessor
 * system, in that case we have to call the local interrupt handler.
 */
#ifndef CONFIG_X86_LOCAL_APIC
	if (!user_mode(regs))
		x86_do_profile(regs->eip);
#else
	if (!using_apic_timer)
		smp_local_timer_interrupt(regs);
#endif
}


/* you can safely undefine this if you don't have the Neptune chipset */

#define BUGGY_NEPTUN_TIMER

static inline int do_timer_overflow(int count)
{
	int i;

	spin_lock(&i8259A_lock);
	/*
	 * This is tricky when I/O APICs are used;
	 * see do_timer_interrupt().
	 */
	i = inb(0x20);
	spin_unlock(&i8259A_lock);
	
	/* assumption about timer being IRQ0 */
	if (i & 0x01) {
		/*
		 * We cannot detect lost timer interrupts ... 
		 * well, that's why we call them lost, don't we? :)
		 * [hmm, on the Pentium and Alpha we can ... sort of]
		 */
		count -= LATCH;
	} else {
#ifdef BUGGY_NEPTUN_TIMER
		/*
		 * for the Neptun bug we know that the 'latch'
		 * command doesnt latch the high and low value
		 * of the counter atomically. Thus we have to 
		 * substract 256 from the counter 
		 * ... funny, isnt it? :)
		 */
		
		count -= 256;
#else
		printk("do_slow_gettimeoffset(): hardware timer problem?\n");
#endif
	}
	return count;
}
