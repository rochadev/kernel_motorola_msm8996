/* Common corrected MCE threshold handler code */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <asm/mce.h>
#include <asm/irq_vectors.h>
#include <asm/idle.h>

static void default_threshold_interrupt(void)
{
	printk(KERN_ERR "Unexpected threshold interrupt at vector %x\n",
			 THRESHOLD_APIC_VECTOR);
}

void (*mce_threshold_vector)(void) = default_threshold_interrupt;

asmlinkage void mce_threshold_interrupt(void)
{
	ack_APIC_irq();
	exit_idle();
	irq_enter();
	inc_irq_stat(irq_threshold_count);
	mce_threshold_vector();
	irq_exit();
}
