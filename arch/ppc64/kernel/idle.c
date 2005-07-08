/*
 * Idle daemon for PowerPC.  Idle daemon will handle any action
 * that needs to be taken when the system becomes idle.
 *
 * Originally Written by Cort Dougan (cort@cs.nmt.edu)
 *
 * iSeries supported added by Mike Corrigan <mikejc@us.ibm.com>
 *
 * Additional shared processor, SMT, and firmware support
 *    Copyright (c) 2003 Dave Engebretsen <engebret@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/smp.h>

#include <asm/system.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/cputable.h>
#include <asm/time.h>
#include <asm/iSeries/HvCall.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/plpar_wrappers.h>
#include <asm/systemcfg.h>
#include <asm/machdep.h>

extern void power4_idle(void);

static int (*idle_loop)(void);

int default_idle(void)
{
	long oldval;
	unsigned int cpu = smp_processor_id();

	while (1) {
		oldval = test_and_clear_thread_flag(TIF_NEED_RESCHED);

		if (!oldval) {
			set_thread_flag(TIF_POLLING_NRFLAG);

			while (!need_resched() && !cpu_is_offline(cpu)) {
				barrier();
				/*
				 * Go into low thread priority and possibly
				 * low power mode.
				 */
				HMT_low();
				HMT_very_low();
			}

			HMT_medium();
			clear_thread_flag(TIF_POLLING_NRFLAG);
		} else {
			set_need_resched();
		}

		schedule();
		if (cpu_is_offline(cpu) && system_state == SYSTEM_RUNNING)
			cpu_die();
	}

	return 0;
}

#ifdef CONFIG_PPC_PSERIES

DECLARE_PER_CPU(unsigned long, smt_snooze_delay);

int dedicated_idle(void)
{
	long oldval;
	struct paca_struct *lpaca = get_paca(), *ppaca;
	unsigned long start_snooze;
	unsigned long *smt_snooze_delay = &__get_cpu_var(smt_snooze_delay);
	unsigned int cpu = smp_processor_id();

	ppaca = &paca[cpu ^ 1];

	while (1) {
		/*
		 * Indicate to the HV that we are idle. Now would be
		 * a good time to find other work to dispatch.
		 */
		lpaca->lppaca.idle = 1;

		oldval = test_and_clear_thread_flag(TIF_NEED_RESCHED);
		if (!oldval) {
			set_thread_flag(TIF_POLLING_NRFLAG);
			start_snooze = __get_tb() +
				*smt_snooze_delay * tb_ticks_per_usec;
			while (!need_resched() && !cpu_is_offline(cpu)) {
				/*
				 * Go into low thread priority and possibly
				 * low power mode.
				 */
				HMT_low();
				HMT_very_low();

				if (*smt_snooze_delay == 0 ||
				    __get_tb() < start_snooze)
					continue;

				HMT_medium();

				if (!(ppaca->lppaca.idle)) {
					local_irq_disable();

					/*
					 * We are about to sleep the thread
					 * and so wont be polling any
					 * more.
					 */
					clear_thread_flag(TIF_POLLING_NRFLAG);

					/*
					 * SMT dynamic mode. Cede will result
					 * in this thread going dormant, if the
					 * partner thread is still doing work.
					 * Thread wakes up if partner goes idle,
					 * an interrupt is presented, or a prod
					 * occurs.  Returning from the cede
					 * enables external interrupts.
					 */
					if (!need_resched())
						cede_processor();
					else
						local_irq_enable();
				} else {
					/*
					 * Give the HV an opportunity at the
					 * processor, since we are not doing
					 * any work.
					 */
					poll_pending();
				}
			}

			clear_thread_flag(TIF_POLLING_NRFLAG);
		} else {
			set_need_resched();
		}

		HMT_medium();
		lpaca->lppaca.idle = 0;
		schedule();
		if (cpu_is_offline(cpu) && system_state == SYSTEM_RUNNING)
			cpu_die();
	}
	return 0;
}

static int shared_idle(void)
{
	struct paca_struct *lpaca = get_paca();
	unsigned int cpu = smp_processor_id();

	while (1) {
		/*
		 * Indicate to the HV that we are idle. Now would be
		 * a good time to find other work to dispatch.
		 */
		lpaca->lppaca.idle = 1;

		while (!need_resched() && !cpu_is_offline(cpu)) {
			local_irq_disable();

			/*
			 * Yield the processor to the hypervisor.  We return if
			 * an external interrupt occurs (which are driven prior
			 * to returning here) or if a prod occurs from another 
			 * processor. When returning here, external interrupts
			 * are enabled.
			 *
			 * Check need_resched() again with interrupts disabled
			 * to avoid a race.
			 */
			if (!need_resched())
				cede_processor();
			else
				local_irq_enable();
		}

		HMT_medium();
		lpaca->lppaca.idle = 0;
		schedule();
		if (cpu_is_offline(smp_processor_id()) &&
		    system_state == SYSTEM_RUNNING)
			cpu_die();
	}

	return 0;
}

#endif /* CONFIG_PPC_PSERIES */

int native_idle(void)
{
	while(1) {
		/* check CPU type here */
		if (!need_resched())
			power4_idle();
		if (need_resched())
			schedule();

		if (cpu_is_offline(raw_smp_processor_id()) &&
		    system_state == SYSTEM_RUNNING)
			cpu_die();
	}
	return 0;
}

void cpu_idle(void)
{
	BUG_ON(NULL == ppc_md.idle_loop);
	ppc_md.idle_loop();
}

int powersave_nap;

#ifdef CONFIG_SYSCTL
/*
 * Register the sysctl to set/clear powersave_nap.
 */
static ctl_table powersave_nap_ctl_table[]={
	{
		.ctl_name	= KERN_PPC_POWERSAVE_NAP,
		.procname	= "powersave-nap",
		.data		= &powersave_nap,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ 0, },
};
static ctl_table powersave_nap_sysctl_root[] = {
	{ 1, "kernel", NULL, 0, 0755, powersave_nap_ctl_table, },
 	{ 0,},
};

static int __init
register_powersave_nap_sysctl(void)
{
	register_sysctl_table(powersave_nap_sysctl_root, 0);

	return 0;
}
__initcall(register_powersave_nap_sysctl);
#endif

int idle_setup(void)
{
	/*
	 * Move that junk to each platform specific file, eventually define
	 * a pSeries_idle for shared processor stuff
	 */
#ifdef CONFIG_PPC_ISERIES
	idle_loop = iSeries_idle;
	return 1;
#else
	idle_loop = default_idle;
#endif
#ifdef CONFIG_PPC_PSERIES
	if (systemcfg->platform & PLATFORM_PSERIES) {
		if (cur_cpu_spec->firmware_features & FW_FEATURE_SPLPAR) {
			if (get_paca()->lppaca.shared_proc) {
				printk(KERN_INFO "Using shared processor idle loop\n");
				idle_loop = shared_idle;
			} else {
				printk(KERN_INFO "Using dedicated idle loop\n");
				idle_loop = dedicated_idle;
			}
		} else {
			printk(KERN_INFO "Using default idle loop\n");
			idle_loop = default_idle;
		}
	}
#endif /* CONFIG_PPC_PSERIES */
#ifndef CONFIG_PPC_ISERIES
	if (systemcfg->platform == PLATFORM_POWERMAC ||
	    systemcfg->platform == PLATFORM_MAPLE) {
		printk(KERN_INFO "Using native/NAP idle loop\n");
		idle_loop = native_idle;
	}
#endif /* CONFIG_PPC_ISERIES */

	return 1;
}
