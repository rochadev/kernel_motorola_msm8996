#ifndef __ASM_SH_HP6XX_H
#define __ASM_SH_HP6XX_H

/*
 * Copyright (C) 2003, 2004, 2005  Andriy Skulysh
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#define HP680_BTN_IRQ		IRQ0_IRQ
#define HP680_TS_IRQ		IRQ3_IRQ
#define HP680_HD64461_IRQ	IRQ4_IRQ

#define DAC_LCD_BRIGHTNESS	0
#define DAC_SPEAKER_VOLUME	1

#define PGDR_OPENED		0x01
#define PGDR_MAIN_BATTERY_OUT	0x04
#define PGDR_PLAY_BUTTON	0x08
#define PGDR_REWIND_BUTTON	0x10
#define PGDR_RECORD_BUTTON	0x20

#define PHDR_TS_PEN_DOWN	0x08

#define PJDR_LED_BLINK		0x02

#define PKDR_LED_GREEN		0x10

#define SCPDR_TS_SCAN_ENABLE	0x20
#define SCPDR_TS_SCAN_Y		0x02
#define SCPDR_TS_SCAN_X		0x01

#define SCPCR_TS_ENABLE		0x405
#define SCPCR_TS_MASK		0xc0f

#define ADC_CHANNEL_TS_Y	1
#define ADC_CHANNEL_TS_X	2
#define ADC_CHANNEL_BATTERY	3
#define ADC_CHANNEL_BACKUP	4
#define ADC_CHANNEL_CHARGE	5

#define HD64461_GPADR_SPEAKER	0x01
#define HD64461_GPADR_PCMCIA0	(0x02|0x08)

#define HD64461_GPBDR_LCDOFF	0x01
#define HD64461_GPBDR_LCD_CONTRAST_MASK	0x78
#define HD64461_GPBDR_LED_RED	0x80

#include <asm/hd64461.h>
#include <asm/io.h>

#define PJDR	0xa4000130
#define PKDR	0xa4000132

static inline void hp6xx_led_red(int on)
{
	u16 v16;
	v16 = ctrl_inw(CONFIG_HD64461_IOBASE + HD64461_GPBDR - 0x10000);
	if (on)
	    ctrl_outw(v16 & (~HD64461_GPBDR_LED_RED), CONFIG_HD64461_IOBASE + HD64461_GPBDR - 0x10000);
	else
	    ctrl_outw(v16 | HD64461_GPBDR_LED_RED, CONFIG_HD64461_IOBASE + HD64461_GPBDR - 0x10000);
}

static inline void hp6xx_led_green(int on)
{
	u8 v8;

	v8 = ctrl_inb(PKDR);
	if (on)
	    ctrl_outb(v8 & (~PKDR_LED_GREEN), PKDR);
	else
	    ctrl_outb(v8 | PKDR_LED_GREEN, PKDR);
}


#endif /* __ASM_SH_HP6XX_H */
