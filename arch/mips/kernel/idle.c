/*
 * MIPS idle loop and WAIT instruction support.
 *
 * Copyright (C) xxxx  the Anonymous
 * Copyright (C) 1994 - 2006 Ralf Baechle
 * Copyright (C) 2003, 2004  Maciej W. Rozycki
 * Copyright (C) 2001, 2004, 2011, 2012	 MIPS Technologies, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/export.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <asm/cpu.h>
#include <asm/cpu-info.h>
#include <asm/mipsregs.h>

/*
 * Not all of the MIPS CPUs have the "wait" instruction available. Moreover,
 * the implementation of the "wait" feature differs between CPU families. This
 * points to the function that implements CPU specific wait.
 * The wait instruction stops the pipeline and reduces the power consumption of
 * the CPU very much.
 */
void (*cpu_wait)(void);
EXPORT_SYMBOL(cpu_wait);

static void r3081_wait(void)
{
	unsigned long cfg = read_c0_conf();
	write_c0_conf(cfg | R30XX_CONF_HALT);
}

static void r39xx_wait(void)
{
	local_irq_disable();
	if (!need_resched())
		write_c0_conf(read_c0_conf() | TX39_CONF_HALT);
	local_irq_enable();
}

extern void r4k_wait(void);

/*
 * This variant is preferable as it allows testing need_resched and going to
 * sleep depending on the outcome atomically.  Unfortunately the "It is
 * implementation-dependent whether the pipeline restarts when a non-enabled
 * interrupt is requested" restriction in the MIPS32/MIPS64 architecture makes
 * using this version a gamble.
 */
void r4k_wait_irqoff(void)
{
	local_irq_disable();
	if (!need_resched())
		__asm__("	.set	push		\n"
			"	.set	mips3		\n"
			"	wait			\n"
			"	.set	pop		\n");
	local_irq_enable();
	__asm__("	.globl __pastwait	\n"
		"__pastwait:			\n");
}

/*
 * The RM7000 variant has to handle erratum 38.	 The workaround is to not
 * have any pending stores when the WAIT instruction is executed.
 */
static void rm7k_wait_irqoff(void)
{
	local_irq_disable();
	if (!need_resched())
		__asm__(
		"	.set	push					\n"
		"	.set	mips3					\n"
		"	.set	noat					\n"
		"	mfc0	$1, $12					\n"
		"	sync						\n"
		"	mtc0	$1, $12		# stalls until W stage	\n"
		"	wait						\n"
		"	mtc0	$1, $12		# stalls until W stage	\n"
		"	.set	pop					\n");
	local_irq_enable();
}

/*
 * The Au1xxx wait is available only if using 32khz counter or
 * external timer source, but specifically not CP0 Counter.
 * alchemy/common/time.c may override cpu_wait!
 */
static void au1k_wait(void)
{
	__asm__("	.set	mips3			\n"
		"	cache	0x14, 0(%0)		\n"
		"	cache	0x14, 32(%0)		\n"
		"	sync				\n"
		"	nop				\n"
		"	wait				\n"
		"	nop				\n"
		"	nop				\n"
		"	nop				\n"
		"	nop				\n"
		"	.set	mips0			\n"
		: : "r" (au1k_wait));
}

static int __initdata nowait;

static int __init wait_disable(char *s)
{
	nowait = 1;

	return 1;
}

__setup("nowait", wait_disable);

void __init check_wait(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	if (nowait) {
		printk("Wait instruction disabled.\n");
		return;
	}

	switch (c->cputype) {
	case CPU_R3081:
	case CPU_R3081E:
		cpu_wait = r3081_wait;
		break;
	case CPU_TX3927:
		cpu_wait = r39xx_wait;
		break;
	case CPU_R4200:
/*	case CPU_R4300: */
	case CPU_R4600:
	case CPU_R4640:
	case CPU_R4650:
	case CPU_R4700:
	case CPU_R5000:
	case CPU_R5500:
	case CPU_NEVADA:
	case CPU_4KC:
	case CPU_4KEC:
	case CPU_4KSC:
	case CPU_5KC:
	case CPU_25KF:
	case CPU_PR4450:
	case CPU_BMIPS3300:
	case CPU_BMIPS4350:
	case CPU_BMIPS4380:
	case CPU_BMIPS5000:
	case CPU_CAVIUM_OCTEON:
	case CPU_CAVIUM_OCTEON_PLUS:
	case CPU_CAVIUM_OCTEON2:
	case CPU_JZRISC:
	case CPU_LOONGSON1:
	case CPU_XLR:
	case CPU_XLP:
		cpu_wait = r4k_wait;
		break;

	case CPU_RM7000:
		cpu_wait = rm7k_wait_irqoff;
		break;

	case CPU_M14KC:
	case CPU_M14KEC:
	case CPU_24K:
	case CPU_34K:
	case CPU_1004K:
		cpu_wait = r4k_wait;
		if (read_c0_config7() & MIPS_CONF7_WII)
			cpu_wait = r4k_wait_irqoff;
		break;

	case CPU_74K:
		cpu_wait = r4k_wait;
		if ((c->processor_id & 0xff) >= PRID_REV_ENCODE_332(2, 1, 0))
			cpu_wait = r4k_wait_irqoff;
		break;

	case CPU_TX49XX:
		cpu_wait = r4k_wait_irqoff;
		break;
	case CPU_ALCHEMY:
		cpu_wait = au1k_wait;
		break;
	case CPU_20KC:
		/*
		 * WAIT on Rev1.0 has E1, E2, E3 and E16.
		 * WAIT on Rev2.0 and Rev3.0 has E16.
		 * Rev3.1 WAIT is nop, why bother
		 */
		if ((c->processor_id & 0xff) <= 0x64)
			break;

		/*
		 * Another rev is incremeting c0_count at a reduced clock
		 * rate while in WAIT mode.  So we basically have the choice
		 * between using the cp0 timer as clocksource or avoiding
		 * the WAIT instruction.  Until more details are known,
		 * disable the use of WAIT for 20Kc entirely.
		   cpu_wait = r4k_wait;
		 */
		break;
	case CPU_RM9000:
		if ((c->processor_id & 0x00ff) >= 0x40)
			cpu_wait = r4k_wait;
		break;
	default:
		break;
	}
}

static void smtc_idle_hook(void)
{
#ifdef CONFIG_MIPS_MT_SMTC
	void smtc_idle_loop_hook(void);

	smtc_idle_loop_hook();
#endif
}

void arch_cpu_idle(void)
{
	smtc_idle_hook();
	if (cpu_wait)
		(*cpu_wait)();
	else
		local_irq_enable();
}
