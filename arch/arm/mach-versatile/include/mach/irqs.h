/*
 *  arch/arm/mach-versatile/include/mach/irqs.h
 *
 *  Copyright (C) 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <mach/platform.h>

/* 
 *  IRQ interrupts definitions are the same as the INT definitions
 *  held within platform.h
 */
#define IRQ_VIC_START		0
#define IRQ_WDOGINT		(IRQ_VIC_START + INT_WDOGINT)
#define IRQ_SOFTINT		(IRQ_VIC_START + INT_SOFTINT)
#define IRQ_COMMRx		(IRQ_VIC_START + INT_COMMRx)
#define IRQ_COMMTx		(IRQ_VIC_START + INT_COMMTx)
#define IRQ_TIMERINT0_1		(IRQ_VIC_START + INT_TIMERINT0_1)
#define IRQ_TIMERINT2_3		(IRQ_VIC_START + INT_TIMERINT2_3)
#define IRQ_GPIOINT0		(IRQ_VIC_START + INT_GPIOINT0)
#define IRQ_GPIOINT1		(IRQ_VIC_START + INT_GPIOINT1)
#define IRQ_GPIOINT2		(IRQ_VIC_START + INT_GPIOINT2)
#define IRQ_GPIOINT3		(IRQ_VIC_START + INT_GPIOINT3)
#define IRQ_RTCINT		(IRQ_VIC_START + INT_RTCINT)
#define IRQ_SSPINT		(IRQ_VIC_START + INT_SSPINT)
#define IRQ_UARTINT0		(IRQ_VIC_START + INT_UARTINT0)
#define IRQ_UARTINT1		(IRQ_VIC_START + INT_UARTINT1)
#define IRQ_UARTINT2		(IRQ_VIC_START + INT_UARTINT2)
#define IRQ_SCIINT		(IRQ_VIC_START + INT_SCIINT)
#define IRQ_CLCDINT		(IRQ_VIC_START + INT_CLCDINT)
#define IRQ_DMAINT		(IRQ_VIC_START + INT_DMAINT)
#define IRQ_PWRFAILINT 		(IRQ_VIC_START + INT_PWRFAILINT)
#define IRQ_MBXINT		(IRQ_VIC_START + INT_MBXINT)
#define IRQ_GNDINT		(IRQ_VIC_START + INT_GNDINT)
#define IRQ_VICSOURCE21		(IRQ_VIC_START + INT_VICSOURCE21)
#define IRQ_VICSOURCE22		(IRQ_VIC_START + INT_VICSOURCE22)
#define IRQ_VICSOURCE23		(IRQ_VIC_START + INT_VICSOURCE23)
#define IRQ_VICSOURCE24		(IRQ_VIC_START + INT_VICSOURCE24)
#define IRQ_VICSOURCE25		(IRQ_VIC_START + INT_VICSOURCE25)
#define IRQ_VICSOURCE26		(IRQ_VIC_START + INT_VICSOURCE26)
#define IRQ_VICSOURCE27		(IRQ_VIC_START + INT_VICSOURCE27)
#define IRQ_VICSOURCE28		(IRQ_VIC_START + INT_VICSOURCE28)
#define IRQ_VICSOURCE29		(IRQ_VIC_START + INT_VICSOURCE29)
#define IRQ_VICSOURCE30		(IRQ_VIC_START + INT_VICSOURCE30)
#define IRQ_VICSOURCE31		(IRQ_VIC_START + INT_VICSOURCE31)
#define IRQ_VIC_END		(IRQ_VIC_START + 31)

#define IRQMASK_WDOGINT		INTMASK_WDOGINT
#define IRQMASK_SOFTINT		INTMASK_SOFTINT
#define IRQMASK_COMMRx 		INTMASK_COMMRx
#define IRQMASK_COMMTx 		INTMASK_COMMTx
#define IRQMASK_TIMERINT0_1	INTMASK_TIMERINT0_1
#define IRQMASK_TIMERINT2_3	INTMASK_TIMERINT2_3
#define IRQMASK_GPIOINT0	INTMASK_GPIOINT0
#define IRQMASK_GPIOINT1	INTMASK_GPIOINT1
#define IRQMASK_GPIOINT2	INTMASK_GPIOINT2
#define IRQMASK_GPIOINT3	INTMASK_GPIOINT3
#define IRQMASK_RTCINT 		INTMASK_RTCINT
#define IRQMASK_SSPINT 		INTMASK_SSPINT
#define IRQMASK_UARTINT0	INTMASK_UARTINT0
#define IRQMASK_UARTINT1	INTMASK_UARTINT1
#define IRQMASK_UARTINT2	INTMASK_UARTINT2
#define IRQMASK_SCIINT 		INTMASK_SCIINT
#define IRQMASK_CLCDINT		INTMASK_CLCDINT
#define IRQMASK_DMAINT 		INTMASK_DMAINT
#define IRQMASK_PWRFAILINT	INTMASK_PWRFAILINT
#define IRQMASK_MBXINT 		INTMASK_MBXINT
#define IRQMASK_GNDINT 		INTMASK_GNDINT
#define IRQMASK_VICSOURCE21	INTMASK_VICSOURCE21
#define IRQMASK_VICSOURCE22	INTMASK_VICSOURCE22
#define IRQMASK_VICSOURCE23	INTMASK_VICSOURCE23
#define IRQMASK_VICSOURCE24	INTMASK_VICSOURCE24
#define IRQMASK_VICSOURCE25	INTMASK_VICSOURCE25
#define IRQMASK_VICSOURCE26	INTMASK_VICSOURCE26
#define IRQMASK_VICSOURCE27	INTMASK_VICSOURCE27
#define IRQMASK_VICSOURCE28	INTMASK_VICSOURCE28
#define IRQMASK_VICSOURCE29	INTMASK_VICSOURCE29
#define IRQMASK_VICSOURCE30	INTMASK_VICSOURCE30
#define IRQMASK_VICSOURCE31	INTMASK_VICSOURCE31

/* 
 *  FIQ interrupts definitions are the same as the INT definitions.
 */
#define FIQ_WDOGINT		INT_WDOGINT
#define FIQ_SOFTINT		INT_SOFTINT
#define FIQ_COMMRx		INT_COMMRx
#define FIQ_COMMTx		INT_COMMTx
#define FIQ_TIMERINT0_1		INT_TIMERINT0_1
#define FIQ_TIMERINT2_3		INT_TIMERINT2_3
#define FIQ_GPIOINT0		INT_GPIOINT0
#define FIQ_GPIOINT1		INT_GPIOINT1
#define FIQ_GPIOINT2		INT_GPIOINT2
#define FIQ_GPIOINT3		INT_GPIOINT3
#define FIQ_RTCINT		INT_RTCINT
#define FIQ_SSPINT		INT_SSPINT
#define FIQ_UARTINT0		INT_UARTINT0
#define FIQ_UARTINT1		INT_UARTINT1
#define FIQ_UARTINT2		INT_UARTINT2
#define FIQ_SCIINT		INT_SCIINT
#define FIQ_CLCDINT		INT_CLCDINT
#define FIQ_DMAINT		INT_DMAINT
#define FIQ_PWRFAILINT 		INT_PWRFAILINT
#define FIQ_MBXINT		INT_MBXINT
#define FIQ_GNDINT		INT_GNDINT
#define FIQ_VICSOURCE21		INT_VICSOURCE21
#define FIQ_VICSOURCE22		INT_VICSOURCE22
#define FIQ_VICSOURCE23		INT_VICSOURCE23
#define FIQ_VICSOURCE24		INT_VICSOURCE24
#define FIQ_VICSOURCE25		INT_VICSOURCE25
#define FIQ_VICSOURCE26		INT_VICSOURCE26
#define FIQ_VICSOURCE27		INT_VICSOURCE27
#define FIQ_VICSOURCE28		INT_VICSOURCE28
#define FIQ_VICSOURCE29		INT_VICSOURCE29
#define FIQ_VICSOURCE30		INT_VICSOURCE30
#define FIQ_VICSOURCE31		INT_VICSOURCE31


#define FIQMASK_WDOGINT		INTMASK_WDOGINT
#define FIQMASK_SOFTINT		INTMASK_SOFTINT
#define FIQMASK_COMMRx 		INTMASK_COMMRx
#define FIQMASK_COMMTx 		INTMASK_COMMTx
#define FIQMASK_TIMERINT0_1	INTMASK_TIMERINT0_1
#define FIQMASK_TIMERINT2_3	INTMASK_TIMERINT2_3
#define FIQMASK_GPIOINT0	INTMASK_GPIOINT0
#define FIQMASK_GPIOINT1	INTMASK_GPIOINT1
#define FIQMASK_GPIOINT2	INTMASK_GPIOINT2
#define FIQMASK_GPIOINT3	INTMASK_GPIOINT3
#define FIQMASK_RTCINT 		INTMASK_RTCINT
#define FIQMASK_SSPINT 		INTMASK_SSPINT
#define FIQMASK_UARTINT0	INTMASK_UARTINT0
#define FIQMASK_UARTINT1	INTMASK_UARTINT1
#define FIQMASK_UARTINT2	INTMASK_UARTINT2
#define FIQMASK_SCIINT 		INTMASK_SCIINT
#define FIQMASK_CLCDINT		INTMASK_CLCDINT
#define FIQMASK_DMAINT 		INTMASK_DMAINT
#define FIQMASK_PWRFAILINT	INTMASK_PWRFAILINT
#define FIQMASK_MBXINT 		INTMASK_MBXINT
#define FIQMASK_GNDINT 		INTMASK_GNDINT
#define FIQMASK_VICSOURCE21	INTMASK_VICSOURCE21
#define FIQMASK_VICSOURCE22	INTMASK_VICSOURCE22
#define FIQMASK_VICSOURCE23	INTMASK_VICSOURCE23
#define FIQMASK_VICSOURCE24	INTMASK_VICSOURCE24
#define FIQMASK_VICSOURCE25	INTMASK_VICSOURCE25
#define FIQMASK_VICSOURCE26	INTMASK_VICSOURCE26
#define FIQMASK_VICSOURCE27	INTMASK_VICSOURCE27
#define FIQMASK_VICSOURCE28	INTMASK_VICSOURCE28
#define FIQMASK_VICSOURCE29	INTMASK_VICSOURCE29
#define FIQMASK_VICSOURCE30	INTMASK_VICSOURCE30
#define FIQMASK_VICSOURCE31	INTMASK_VICSOURCE31

/*
 * Secondary interrupt controller
 */
#define IRQ_SIC_START		32
#define IRQ_SIC_MMCI0B 		(IRQ_SIC_START + SIC_INT_MMCI0B)
#define IRQ_SIC_MMCI1B 		(IRQ_SIC_START + SIC_INT_MMCI1B)
#define IRQ_SIC_KMI0		(IRQ_SIC_START + SIC_INT_KMI0)
#define IRQ_SIC_KMI1		(IRQ_SIC_START + SIC_INT_KMI1)
#define IRQ_SIC_SCI3		(IRQ_SIC_START + SIC_INT_SCI3)
#define IRQ_SIC_UART3		(IRQ_SIC_START + SIC_INT_UART3)
#define IRQ_SIC_CLCD		(IRQ_SIC_START + SIC_INT_CLCD)
#define IRQ_SIC_TOUCH		(IRQ_SIC_START + SIC_INT_TOUCH)
#define IRQ_SIC_KEYPAD 		(IRQ_SIC_START + SIC_INT_KEYPAD)
#define IRQ_SIC_DoC		(IRQ_SIC_START + SIC_INT_DoC)
#define IRQ_SIC_MMCI0A 		(IRQ_SIC_START + SIC_INT_MMCI0A)
#define IRQ_SIC_MMCI1A 		(IRQ_SIC_START + SIC_INT_MMCI1A)
#define IRQ_SIC_AACI		(IRQ_SIC_START + SIC_INT_AACI)
#define IRQ_SIC_ETH		(IRQ_SIC_START + SIC_INT_ETH)
#define IRQ_SIC_USB		(IRQ_SIC_START + SIC_INT_USB)
#define IRQ_SIC_PCI0		(IRQ_SIC_START + SIC_INT_PCI0)
#define IRQ_SIC_PCI1		(IRQ_SIC_START + SIC_INT_PCI1)
#define IRQ_SIC_PCI2		(IRQ_SIC_START + SIC_INT_PCI2)
#define IRQ_SIC_PCI3		(IRQ_SIC_START + SIC_INT_PCI3)
#define IRQ_SIC_END		63

#define SIC_IRQMASK_MMCI0B	SIC_INTMASK_MMCI0B
#define SIC_IRQMASK_MMCI1B	SIC_INTMASK_MMCI1B
#define SIC_IRQMASK_KMI0	SIC_INTMASK_KMI0
#define SIC_IRQMASK_KMI1	SIC_INTMASK_KMI1
#define SIC_IRQMASK_SCI3	SIC_INTMASK_SCI3
#define SIC_IRQMASK_UART3	SIC_INTMASK_UART3
#define SIC_IRQMASK_CLCD	SIC_INTMASK_CLCD
#define SIC_IRQMASK_TOUCH	SIC_INTMASK_TOUCH
#define SIC_IRQMASK_KEYPAD	SIC_INTMASK_KEYPAD
#define SIC_IRQMASK_DoC		SIC_INTMASK_DoC
#define SIC_IRQMASK_MMCI0A	SIC_INTMASK_MMCI0A
#define SIC_IRQMASK_MMCI1A	SIC_INTMASK_MMCI1A
#define SIC_IRQMASK_AACI	SIC_INTMASK_AACI
#define SIC_IRQMASK_ETH		SIC_INTMASK_ETH
#define SIC_IRQMASK_USB		SIC_INTMASK_USB
#define SIC_IRQMASK_PCI0	SIC_INTMASK_PCI0
#define SIC_IRQMASK_PCI1	SIC_INTMASK_PCI1
#define SIC_IRQMASK_PCI2	SIC_INTMASK_PCI2
#define SIC_IRQMASK_PCI3	SIC_INTMASK_PCI3

#define NR_IRQS			64
