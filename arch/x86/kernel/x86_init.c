/*
 * Copyright (C) 2009 Thomas Gleixner <tglx@linutronix.de>
 *
 *  For licencing details see kernel-base/COPYING
 */
#include <linux/init.h>

#include <asm/bios_ebda.h>
#include <asm/paravirt.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/apic.h>
#include <asm/e820.h>
#include <asm/irq.h>

void __cpuinit x86_init_noop(void) { }
void __init x86_init_uint_noop(unsigned int unused) { }
void __init x86_init_pgd_noop(pgd_t *unused) { }

/*
 * The platform setup functions are preset with the default functions
 * for standard PC hardware.
 */
struct __initdata x86_init_ops x86_init = {

	.resources = {
		.probe_roms		= x86_init_noop,
		.reserve_resources	= reserve_standard_io_resources,
		.reserve_ebda_region	= reserve_ebda_region,
		.memory_setup		= default_machine_specific_memory_setup,
	},

	.mpparse = {
		.mpc_record		= x86_init_uint_noop,
		.setup_ioapic_ids	= x86_init_noop,
		.mpc_apic_id		= default_mpc_apic_id,
		.smp_read_mpc_oem	= default_smp_read_mpc_oem,
		.mpc_oem_bus_info	= default_mpc_oem_bus_info,
		.find_smp_config	= default_find_smp_config,
		.get_smp_config		= default_get_smp_config,
	},

	.irqs = {
		.pre_vector_init	= init_ISA_irqs,
		.intr_init		= native_init_IRQ,
		.trap_init		= x86_init_noop,
	},

	.oem = {
		.arch_setup		= x86_init_noop,
		.banner			= default_banner,
	},

	.paging = {
		.pagetable_setup_start	= native_pagetable_setup_start,
		.pagetable_setup_done	= native_pagetable_setup_done,
	},

	.timers = {
		.setup_percpu_clockev	= setup_boot_APIC_clock,
	},
};

__cpuinitdata struct x86_cpuinit_ops x86_cpuinit = {
	.setup_percpu_clockev		= setup_secondary_APIC_clock,
};
