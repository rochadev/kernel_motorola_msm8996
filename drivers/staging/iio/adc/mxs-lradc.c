/*
 * Freescale i.MX28 LRADC driver
 *
 * Copyright (c) 2012 DENX Software Engineering, GmbH.
 * Marek Vasut <marex@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/stmp_device.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/clk.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define DRIVER_NAME		"mxs-lradc"

#define LRADC_MAX_DELAY_CHANS	4
#define LRADC_MAX_MAPPED_CHANS	8
#define LRADC_MAX_TOTAL_CHANS	16

#define LRADC_DELAY_TIMER_HZ	2000

/*
 * Make this runtime configurable if necessary. Currently, if the buffered mode
 * is enabled, the LRADC takes LRADC_DELAY_TIMER_LOOP samples of data before
 * triggering IRQ. The sampling happens every (LRADC_DELAY_TIMER_PER / 2000)
 * seconds. The result is that the samples arrive every 500mS.
 */
#define LRADC_DELAY_TIMER_PER	200
#define LRADC_DELAY_TIMER_LOOP	5

/*
 * Once the pen touches the touchscreen, the touchscreen switches from
 * IRQ-driven mode to polling mode to prevent interrupt storm. The polling
 * is realized by worker thread, which is called every 20 or so milliseconds.
 * This gives the touchscreen enough fluence and does not strain the system
 * too much.
 */
#define LRADC_TS_SAMPLE_DELAY_MS	5

/*
 * The LRADC reads the following amount of samples from each touchscreen
 * channel and the driver then computes avarage of these.
 */
#define LRADC_TS_SAMPLE_AMOUNT		4

enum mxs_lradc_id {
	IMX23_LRADC,
	IMX28_LRADC,
};

static const char * const mx23_lradc_irq_names[] = {
	"mxs-lradc-touchscreen",
	"mxs-lradc-channel0",
	"mxs-lradc-channel1",
	"mxs-lradc-channel2",
	"mxs-lradc-channel3",
	"mxs-lradc-channel4",
	"mxs-lradc-channel5",
	"mxs-lradc-channel6",
	"mxs-lradc-channel7",
};

static const char * const mx28_lradc_irq_names[] = {
	"mxs-lradc-touchscreen",
	"mxs-lradc-thresh0",
	"mxs-lradc-thresh1",
	"mxs-lradc-channel0",
	"mxs-lradc-channel1",
	"mxs-lradc-channel2",
	"mxs-lradc-channel3",
	"mxs-lradc-channel4",
	"mxs-lradc-channel5",
	"mxs-lradc-channel6",
	"mxs-lradc-channel7",
	"mxs-lradc-button0",
	"mxs-lradc-button1",
};

struct mxs_lradc_of_config {
	const int		irq_count;
	const char * const	*irq_name;
};

static const struct mxs_lradc_of_config mxs_lradc_of_config[] = {
	[IMX23_LRADC] = {
		.irq_count	= ARRAY_SIZE(mx23_lradc_irq_names),
		.irq_name	= mx23_lradc_irq_names,
	},
	[IMX28_LRADC] = {
		.irq_count	= ARRAY_SIZE(mx28_lradc_irq_names),
		.irq_name	= mx28_lradc_irq_names,
	},
};

enum mxs_lradc_ts {
	MXS_LRADC_TOUCHSCREEN_NONE = 0,
	MXS_LRADC_TOUCHSCREEN_4WIRE,
	MXS_LRADC_TOUCHSCREEN_5WIRE,
};

/*
 * Touchscreen handling
 */
enum lradc_ts_plate {
	LRADC_TOUCH = 0,
	LRADC_SAMPLE_X,
	LRADC_SAMPLE_Y,
	LRADC_SAMPLE_PRESSURE,
	LRADC_SAMPLE_VALID,
};

struct mxs_lradc {
	struct device		*dev;
	void __iomem		*base;
	int			irq[13];

	struct clk		*clk;

	uint32_t		*buffer;
	struct iio_trigger	*trig;

	struct mutex		lock;

	struct completion	completion;

	/*
	 * Touchscreen LRADC channels receives a private slot in the CTRL4
	 * register, the slot #7. Therefore only 7 slots instead of 8 in the
	 * CTRL4 register can be mapped to LRADC channels when using the
	 * touchscreen.
	 *
	 * Furthermore, certain LRADC channels are shared between touchscreen
	 * and/or touch-buttons and generic LRADC block. Therefore when using
	 * either of these, these channels are not available for the regular
	 * sampling. The shared channels are as follows:
	 *
	 * CH0 -- Touch button #0
	 * CH1 -- Touch button #1
	 * CH2 -- Touch screen XPUL
	 * CH3 -- Touch screen YPLL
	 * CH4 -- Touch screen XNUL
	 * CH5 -- Touch screen YNLR
	 * CH6 -- Touch screen WIPER (5-wire only)
	 *
	 * The bitfields below represents which parts of the LRADC block are
	 * switched into special mode of operation. These channels can not
	 * be sampled as regular LRADC channels. The driver will refuse any
	 * attempt to sample these channels.
	 */
#define CHAN_MASK_TOUCHBUTTON		(0x3 << 0)
#define CHAN_MASK_TOUCHSCREEN_4WIRE	(0xf << 2)
#define CHAN_MASK_TOUCHSCREEN_5WIRE	(0x1f << 2)
	enum mxs_lradc_ts	use_touchscreen;
	bool			use_touchbutton;

	struct input_dev	*ts_input;

	enum mxs_lradc_id	soc;
	enum lradc_ts_plate	cur_plate; /* statemachine */
	bool			ts_valid;
	unsigned		ts_x_pos;
	unsigned		ts_y_pos;
	unsigned		ts_pressure;

	/* handle touchscreen's physical behaviour */
	/* samples per coordinate */
	unsigned		over_sample_cnt;
	/* time clocks between samples */
	unsigned		over_sample_delay;
	/* time in clocks to wait after the plates where switched */
	unsigned		settling_delay;
};

#define	LRADC_CTRL0				0x00
# define LRADC_CTRL0_MX28_TOUCH_DETECT_ENABLE	(1 << 23)
# define LRADC_CTRL0_MX28_TOUCH_SCREEN_TYPE	(1 << 22)
# define LRADC_CTRL0_MX28_YNNSW	/* YM */	(1 << 21)
# define LRADC_CTRL0_MX28_YPNSW	/* YP */	(1 << 20)
# define LRADC_CTRL0_MX28_YPPSW	/* YP */	(1 << 19)
# define LRADC_CTRL0_MX28_XNNSW	/* XM */	(1 << 18)
# define LRADC_CTRL0_MX28_XNPSW	/* XM */	(1 << 17)
# define LRADC_CTRL0_MX28_XPPSW	/* XP */	(1 << 16)

# define LRADC_CTRL0_MX23_TOUCH_DETECT_ENABLE	(1 << 20)
# define LRADC_CTRL0_MX23_YM			(1 << 19)
# define LRADC_CTRL0_MX23_XM			(1 << 18)
# define LRADC_CTRL0_MX23_YP			(1 << 17)
# define LRADC_CTRL0_MX23_XP			(1 << 16)

# define LRADC_CTRL0_MX28_PLATE_MASK \
		(LRADC_CTRL0_MX28_TOUCH_DETECT_ENABLE | \
		LRADC_CTRL0_MX28_YNNSW | LRADC_CTRL0_MX28_YPNSW | \
		LRADC_CTRL0_MX28_YPPSW | LRADC_CTRL0_MX28_XNNSW | \
		LRADC_CTRL0_MX28_XNPSW | LRADC_CTRL0_MX28_XPPSW)

# define LRADC_CTRL0_MX23_PLATE_MASK \
		(LRADC_CTRL0_MX23_TOUCH_DETECT_ENABLE | \
		LRADC_CTRL0_MX23_YM | LRADC_CTRL0_MX23_XM | \
		LRADC_CTRL0_MX23_YP | LRADC_CTRL0_MX23_XP)

#define	LRADC_CTRL1				0x10
#define	LRADC_CTRL1_TOUCH_DETECT_IRQ_EN		(1 << 24)
#define	LRADC_CTRL1_LRADC_IRQ_EN(n)		(1 << ((n) + 16))
#define	LRADC_CTRL1_MX28_LRADC_IRQ_EN_MASK	(0x1fff << 16)
#define	LRADC_CTRL1_MX23_LRADC_IRQ_EN_MASK	(0x01ff << 16)
#define	LRADC_CTRL1_LRADC_IRQ_EN_OFFSET		16
#define	LRADC_CTRL1_TOUCH_DETECT_IRQ		(1 << 8)
#define	LRADC_CTRL1_LRADC_IRQ(n)		(1 << (n))
#define	LRADC_CTRL1_MX28_LRADC_IRQ_MASK		0x1fff
#define	LRADC_CTRL1_MX23_LRADC_IRQ_MASK		0x01ff
#define	LRADC_CTRL1_LRADC_IRQ_OFFSET		0

#define	LRADC_CTRL2				0x20
#define	LRADC_CTRL2_TEMPSENSE_PWD		(1 << 15)

#define	LRADC_STATUS				0x40
#define	LRADC_STATUS_TOUCH_DETECT_RAW		(1 << 0)

#define	LRADC_CH(n)				(0x50 + (0x10 * (n)))
#define	LRADC_CH_ACCUMULATE			(1 << 29)
#define	LRADC_CH_NUM_SAMPLES_MASK		(0x1f << 24)
#define	LRADC_CH_NUM_SAMPLES_OFFSET		24
#define	LRADC_CH_NUM_SAMPLES(x) \
				((x) << LRADC_CH_NUM_SAMPLES_OFFSET)
#define	LRADC_CH_VALUE_MASK			0x3ffff
#define	LRADC_CH_VALUE_OFFSET			0

#define	LRADC_DELAY(n)				(0xd0 + (0x10 * (n)))
#define	LRADC_DELAY_TRIGGER_LRADCS_MASK		(0xff << 24)
#define	LRADC_DELAY_TRIGGER_LRADCS_OFFSET	24
#define	LRADC_DELAY_TRIGGER(x) \
				(((x) << LRADC_DELAY_TRIGGER_LRADCS_OFFSET) & \
				LRADC_DELAY_TRIGGER_LRADCS_MASK)
#define	LRADC_DELAY_KICK			(1 << 20)
#define	LRADC_DELAY_TRIGGER_DELAYS_MASK		(0xf << 16)
#define	LRADC_DELAY_TRIGGER_DELAYS_OFFSET	16
#define	LRADC_DELAY_TRIGGER_DELAYS(x) \
				(((x) << LRADC_DELAY_TRIGGER_DELAYS_OFFSET) & \
				LRADC_DELAY_TRIGGER_DELAYS_MASK)
#define	LRADC_DELAY_LOOP_COUNT_MASK		(0x1f << 11)
#define	LRADC_DELAY_LOOP_COUNT_OFFSET		11
#define	LRADC_DELAY_LOOP(x) \
				(((x) << LRADC_DELAY_LOOP_COUNT_OFFSET) & \
				LRADC_DELAY_LOOP_COUNT_MASK)
#define	LRADC_DELAY_DELAY_MASK			0x7ff
#define	LRADC_DELAY_DELAY_OFFSET		0
#define	LRADC_DELAY_DELAY(x) \
				(((x) << LRADC_DELAY_DELAY_OFFSET) & \
				LRADC_DELAY_DELAY_MASK)

#define	LRADC_CTRL4				0x140
#define	LRADC_CTRL4_LRADCSELECT_MASK(n)		(0xf << ((n) * 4))
#define	LRADC_CTRL4_LRADCSELECT_OFFSET(n)	((n) * 4)

#define LRADC_RESOLUTION			12
#define LRADC_SINGLE_SAMPLE_MASK		((1 << LRADC_RESOLUTION) - 1)

static void mxs_lradc_reg_set(struct mxs_lradc *lradc, u32 val, u32 reg)
{
	writel(val, lradc->base + reg + STMP_OFFSET_REG_SET);
}

static void mxs_lradc_reg_clear(struct mxs_lradc *lradc, u32 val, u32 reg)
{
	writel(val, lradc->base + reg + STMP_OFFSET_REG_CLR);
}

static void mxs_lradc_reg_wrt(struct mxs_lradc *lradc, u32 val, u32 reg)
{
	writel(val, lradc->base + reg);
}

static u32 mxs_lradc_plate_mask(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL0_MX23_PLATE_MASK;
	else
		return LRADC_CTRL0_MX28_PLATE_MASK;
}

static u32 mxs_lradc_irq_en_mask(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL1_MX23_LRADC_IRQ_EN_MASK;
	else
		return LRADC_CTRL1_MX28_LRADC_IRQ_EN_MASK;
}

static u32 mxs_lradc_irq_mask(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL1_MX23_LRADC_IRQ_MASK;
	else
		return LRADC_CTRL1_MX28_LRADC_IRQ_MASK;
}

static u32 mxs_lradc_touch_detect_bit(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL0_MX23_TOUCH_DETECT_ENABLE;
	else
		return LRADC_CTRL0_MX28_TOUCH_DETECT_ENABLE;
}

static u32 mxs_lradc_drive_x_plate(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL0_MX23_XP | LRADC_CTRL0_MX23_XM;
	else
		return LRADC_CTRL0_MX28_XPPSW | LRADC_CTRL0_MX28_XNNSW;
}

static u32 mxs_lradc_drive_y_plate(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL0_MX23_YP | LRADC_CTRL0_MX23_YM;
	else
		return LRADC_CTRL0_MX28_YPPSW | LRADC_CTRL0_MX28_YNNSW;
}

static u32 mxs_lradc_drive_pressure(struct mxs_lradc *lradc)
{
	if (lradc->soc == IMX23_LRADC)
		return LRADC_CTRL0_MX23_YP | LRADC_CTRL0_MX23_XM;
	else
		return LRADC_CTRL0_MX28_YPPSW | LRADC_CTRL0_MX28_XNNSW;
}

static bool mxs_lradc_check_touch_event(struct mxs_lradc *lradc)
{
	return !!(readl(lradc->base + LRADC_STATUS) &
					LRADC_STATUS_TOUCH_DETECT_RAW);
}

static void mxs_lradc_setup_ts_channel(struct mxs_lradc *lradc, unsigned ch)
{
	/*
	 * prepare for oversampling conversion
	 *
	 * from the datasheet:
	 * "The ACCUMULATE bit in the appropriate channel register
	 * HW_LRADC_CHn must be set to 1 if NUM_SAMPLES is greater then 0;
	 * otherwise, the IRQs will not fire."
	 */
	mxs_lradc_reg_wrt(lradc, LRADC_CH_ACCUMULATE |
			LRADC_CH_NUM_SAMPLES(lradc->over_sample_cnt - 1),
			LRADC_CH(ch));

	/* from the datasheet:
	 * "Software must clear this register in preparation for a
	 * multi-cycle accumulation.
	 */
	mxs_lradc_reg_clear(lradc, LRADC_CH_VALUE_MASK, LRADC_CH(ch));

	/* prepare the delay/loop unit according to the oversampling count */
	mxs_lradc_reg_wrt(lradc, LRADC_DELAY_TRIGGER(1 << ch) |
		LRADC_DELAY_TRIGGER_DELAYS(0) |
		LRADC_DELAY_LOOP(lradc->over_sample_cnt - 1) |
		LRADC_DELAY_DELAY(lradc->over_sample_delay - 1),
			LRADC_DELAY(3));

	mxs_lradc_reg_clear(lradc, LRADC_CTRL1_LRADC_IRQ(2) |
			LRADC_CTRL1_LRADC_IRQ(3) | LRADC_CTRL1_LRADC_IRQ(4) |
			LRADC_CTRL1_LRADC_IRQ(5), LRADC_CTRL1);

	/* wake us again, when the complete conversion is done */
	mxs_lradc_reg_set(lradc, LRADC_CTRL1_LRADC_IRQ_EN(ch), LRADC_CTRL1);
	/*
	 * after changing the touchscreen plates setting
	 * the signals need some initial time to settle. Start the
	 * SoC's delay unit and start the conversion later
	 * and automatically.
	 */
	mxs_lradc_reg_wrt(lradc, LRADC_DELAY_TRIGGER(0) | /* don't trigger ADC */
		LRADC_DELAY_TRIGGER_DELAYS(1 << 3) | /* trigger DELAY unit#3 */
		LRADC_DELAY_KICK |
		LRADC_DELAY_DELAY(lradc->settling_delay),
			LRADC_DELAY(2));
}

/*
 * Pressure detection is special:
 * We want to do both required measurements for the pressure detection in
 * one turn. Use the hardware features to chain both conversions and let the
 * hardware report one interrupt if both conversions are done
 */
static void mxs_lradc_setup_ts_pressure(struct mxs_lradc *lradc, unsigned ch1,
							unsigned ch2)
{
	u32 reg;

	/*
	 * prepare for oversampling conversion
	 *
	 * from the datasheet:
	 * "The ACCUMULATE bit in the appropriate channel register
	 * HW_LRADC_CHn must be set to 1 if NUM_SAMPLES is greater then 0;
	 * otherwise, the IRQs will not fire."
	 */
	reg = LRADC_CH_ACCUMULATE |
		LRADC_CH_NUM_SAMPLES(lradc->over_sample_cnt - 1);
	mxs_lradc_reg_wrt(lradc, reg, LRADC_CH(ch1));
	mxs_lradc_reg_wrt(lradc, reg, LRADC_CH(ch2));

	/* from the datasheet:
	 * "Software must clear this register in preparation for a
	 * multi-cycle accumulation.
	 */
	mxs_lradc_reg_clear(lradc, LRADC_CH_VALUE_MASK, LRADC_CH(ch1));
	mxs_lradc_reg_clear(lradc, LRADC_CH_VALUE_MASK, LRADC_CH(ch2));

	/* prepare the delay/loop unit according to the oversampling count */
	mxs_lradc_reg_wrt(lradc, LRADC_DELAY_TRIGGER(1 << ch1) |
		LRADC_DELAY_TRIGGER(1 << ch2) | /* start both channels */
		LRADC_DELAY_TRIGGER_DELAYS(0) |
		LRADC_DELAY_LOOP(lradc->over_sample_cnt - 1) |
		LRADC_DELAY_DELAY(lradc->over_sample_delay - 1),
					LRADC_DELAY(3));

	mxs_lradc_reg_clear(lradc, LRADC_CTRL1_LRADC_IRQ(2) |
			LRADC_CTRL1_LRADC_IRQ(3) | LRADC_CTRL1_LRADC_IRQ(4) |
			LRADC_CTRL1_LRADC_IRQ(5), LRADC_CTRL1);

	/* wake us again, when the conversions are done */
	mxs_lradc_reg_set(lradc, LRADC_CTRL1_LRADC_IRQ_EN(ch2), LRADC_CTRL1);
	/*
	 * after changing the touchscreen plates setting
	 * the signals need some initial time to settle. Start the
	 * SoC's delay unit and start the conversion later
	 * and automatically.
	 */
	mxs_lradc_reg_wrt(lradc, LRADC_DELAY_TRIGGER(0) | /* don't trigger ADC */
		LRADC_DELAY_TRIGGER_DELAYS(1 << 3) | /* trigger DELAY unit#3 */
		LRADC_DELAY_KICK |
		LRADC_DELAY_DELAY(lradc->settling_delay), LRADC_DELAY(2));
}

static unsigned mxs_lradc_read_raw_channel(struct mxs_lradc *lradc,
							unsigned channel)
{
	u32 reg;
	unsigned num_samples, val;

	reg = readl(lradc->base + LRADC_CH(channel));
	if (reg & LRADC_CH_ACCUMULATE)
		num_samples = lradc->over_sample_cnt;
	else
		num_samples = 1;

	val = (reg & LRADC_CH_VALUE_MASK) >> LRADC_CH_VALUE_OFFSET;
	return val / num_samples;
}

static unsigned mxs_lradc_read_ts_pressure(struct mxs_lradc *lradc,
						unsigned ch1, unsigned ch2)
{
	u32 reg, mask;
	unsigned pressure, m1, m2;

	mask = LRADC_CTRL1_LRADC_IRQ(ch1) | LRADC_CTRL1_LRADC_IRQ(ch2);
	reg = readl(lradc->base + LRADC_CTRL1) & mask;

	while (reg != mask) {
		reg = readl(lradc->base + LRADC_CTRL1) & mask;
		dev_dbg(lradc->dev, "One channel is still busy: %X\n", reg);
	}

	m1 = mxs_lradc_read_raw_channel(lradc, ch1);
	m2 = mxs_lradc_read_raw_channel(lradc, ch2);

	if (m2 == 0) {
		dev_warn(lradc->dev, "Cannot calculate pressure\n");
		return 1 << (LRADC_RESOLUTION - 1);
	}

	/* simply scale the value from 0 ... max ADC resolution */
	pressure = m1;
	pressure *= (1 << LRADC_RESOLUTION);
	pressure /= m2;

	dev_dbg(lradc->dev, "Pressure = %u\n", pressure);
	return pressure;
}

#define TS_CH_XP 2
#define TS_CH_YP 3
#define TS_CH_XM 4
#define TS_CH_YM 5

static int mxs_lradc_read_ts_channel(struct mxs_lradc *lradc)
{
	u32 reg;
	int val;

	reg = readl(lradc->base + LRADC_CTRL1);

	/* only channels 3 to 5 are of interest here */
	if (reg & LRADC_CTRL1_LRADC_IRQ(TS_CH_YP)) {
		mxs_lradc_reg_clear(lradc, LRADC_CTRL1_LRADC_IRQ_EN(TS_CH_YP) |
			LRADC_CTRL1_LRADC_IRQ(TS_CH_YP), LRADC_CTRL1);
		val = mxs_lradc_read_raw_channel(lradc, TS_CH_YP);
	} else if (reg & LRADC_CTRL1_LRADC_IRQ(TS_CH_XM)) {
		mxs_lradc_reg_clear(lradc, LRADC_CTRL1_LRADC_IRQ_EN(TS_CH_XM) |
			LRADC_CTRL1_LRADC_IRQ(TS_CH_XM), LRADC_CTRL1);
		val = mxs_lradc_read_raw_channel(lradc, TS_CH_XM);
	} else if (reg & LRADC_CTRL1_LRADC_IRQ(TS_CH_YM)) {
		mxs_lradc_reg_clear(lradc, LRADC_CTRL1_LRADC_IRQ_EN(TS_CH_YM) |
			LRADC_CTRL1_LRADC_IRQ(TS_CH_YM), LRADC_CTRL1);
		val = mxs_lradc_read_raw_channel(lradc, TS_CH_YM);
	} else {
		return -EIO;
	}

	mxs_lradc_reg_wrt(lradc, 0, LRADC_DELAY(2));
	mxs_lradc_reg_wrt(lradc, 0, LRADC_DELAY(3));

	return val;
}

/*
 * YP(open)--+-------------+
 *           |             |--+
 *           |             |  |
 *    YM(-)--+-------------+  |
 *             +--------------+
 *             |              |
 *         XP(weak+)        XM(open)
 *
 * "weak+" means 200k Ohm VDDIO
 * (-) means GND
 */
static void mxs_lradc_setup_touch_detection(struct mxs_lradc *lradc)
{
	/*
	 * In order to detect a touch event the 'touch detect enable' bit
	 * enables:
	 *  - a weak pullup to the X+ connector
	 *  - a strong ground at the Y- connector
	 */
	mxs_lradc_reg_clear(lradc, mxs_lradc_plate_mask(lradc), LRADC_CTRL0);
	mxs_lradc_reg_set(lradc, mxs_lradc_touch_detect_bit(lradc),
				LRADC_CTRL0);
}

/*
 * YP(meas)--+-------------+
 *           |             |--+
 *           |             |  |
 * YM(open)--+-------------+  |
 *             +--------------+
 *             |              |
 *           XP(+)          XM(-)
 *
 * (+) means here 1.85 V
 * (-) means here GND
 */
static void mxs_lradc_prepare_x_pos(struct mxs_lradc *lradc)
{
	mxs_lradc_reg_clear(lradc, mxs_lradc_plate_mask(lradc), LRADC_CTRL0);
	mxs_lradc_reg_set(lradc, mxs_lradc_drive_x_plate(lradc), LRADC_CTRL0);

	lradc->cur_plate = LRADC_SAMPLE_X;
	mxs_lradc_setup_ts_channel(lradc, TS_CH_YP);
}

/*
 *   YP(+)--+-------------+
 *          |             |--+
 *          |             |  |
 *   YM(-)--+-------------+  |
 *            +--------------+
 *            |              |
 *         XP(open)        XM(meas)
 *
 * (+) means here 1.85 V
 * (-) means here GND
 */
static void mxs_lradc_prepare_y_pos(struct mxs_lradc *lradc)
{
	mxs_lradc_reg_clear(lradc, mxs_lradc_plate_mask(lradc), LRADC_CTRL0);
	mxs_lradc_reg_set(lradc, mxs_lradc_drive_y_plate(lradc), LRADC_CTRL0);

	lradc->cur_plate = LRADC_SAMPLE_Y;
	mxs_lradc_setup_ts_channel(lradc, TS_CH_XM);
}

/*
 *    YP(+)--+-------------+
 *           |             |--+
 *           |             |  |
 * YM(meas)--+-------------+  |
 *             +--------------+
 *             |              |
 *          XP(meas)        XM(-)
 *
 * (+) means here 1.85 V
 * (-) means here GND
 */
static void mxs_lradc_prepare_pressure(struct mxs_lradc *lradc)
{
	mxs_lradc_reg_clear(lradc, mxs_lradc_plate_mask(lradc), LRADC_CTRL0);
	mxs_lradc_reg_set(lradc, mxs_lradc_drive_pressure(lradc), LRADC_CTRL0);

	lradc->cur_plate = LRADC_SAMPLE_PRESSURE;
	mxs_lradc_setup_ts_pressure(lradc, TS_CH_XP, TS_CH_YM);
}

static void mxs_lradc_enable_touch_detection(struct mxs_lradc *lradc)
{
	mxs_lradc_setup_touch_detection(lradc);

	lradc->cur_plate = LRADC_TOUCH;
	mxs_lradc_reg_clear(lradc, LRADC_CTRL1_TOUCH_DETECT_IRQ |
				LRADC_CTRL1_TOUCH_DETECT_IRQ_EN, LRADC_CTRL1);
	mxs_lradc_reg_set(lradc, LRADC_CTRL1_TOUCH_DETECT_IRQ_EN, LRADC_CTRL1);
}

static void mxs_lradc_report_ts_event(struct mxs_lradc *lradc)
{
	input_report_abs(lradc->ts_input, ABS_X, lradc->ts_x_pos);
	input_report_abs(lradc->ts_input, ABS_Y, lradc->ts_y_pos);
	input_report_abs(lradc->ts_input, ABS_PRESSURE, lradc->ts_pressure);
	input_report_key(lradc->ts_input, BTN_TOUCH, 1);
	input_sync(lradc->ts_input);
}

static void mxs_lradc_complete_touch_event(struct mxs_lradc *lradc)
{
	mxs_lradc_setup_touch_detection(lradc);
	lradc->cur_plate = LRADC_SAMPLE_VALID;
	/*
	 * start a dummy conversion to burn time to settle the signals
	 * note: we are not interested in the conversion's value
	 */
	mxs_lradc_reg_wrt(lradc, 0, LRADC_CH(5));
	mxs_lradc_reg_clear(lradc, LRADC_CTRL1_LRADC_IRQ(5), LRADC_CTRL1);
	mxs_lradc_reg_set(lradc, LRADC_CTRL1_LRADC_IRQ_EN(5), LRADC_CTRL1);
	mxs_lradc_reg_wrt(lradc, LRADC_DELAY_TRIGGER(1 << 5) |
		LRADC_DELAY_KICK | LRADC_DELAY_DELAY(10), /* waste 5 ms */
			LRADC_DELAY(2));
}

/*
 * in order to avoid false measurements, report only samples where
 * the surface is still touched after the position measurement
 */
static void mxs_lradc_finish_touch_event(struct mxs_lradc *lradc, bool valid)
{
	/* if it is still touched, report the sample */
	if (valid && mxs_lradc_check_touch_event(lradc)) {
		lradc->ts_valid = true;
		mxs_lradc_report_ts_event(lradc);
	}

	/* if it is even still touched, continue with the next measurement */
	if (mxs_lradc_check_touch_event(lradc)) {
		mxs_lradc_prepare_y_pos(lradc);
		return;
	}

	if (lradc->ts_valid) {
		/* signal the release */
		lradc->ts_valid = false;
		input_report_key(lradc->ts_input, BTN_TOUCH, 0);
		input_sync(lradc->ts_input);
	}

	/* if it is released, wait for the next touch via IRQ */
	mxs_lradc_reg_clear(lradc, LRADC_CTRL1_TOUCH_DETECT_IRQ, LRADC_CTRL1);
	mxs_lradc_reg_set(lradc, LRADC_CTRL1_TOUCH_DETECT_IRQ_EN, LRADC_CTRL1);
}

/* touchscreen's state machine */
static void mxs_lradc_handle_touch(struct mxs_lradc *lradc)
{
	int val;

	switch (lradc->cur_plate) {
	case LRADC_TOUCH:
		/*
		 * start with the Y-pos, because it uses nearly the same plate
		 * settings like the touch detection
		 */
		if (mxs_lradc_check_touch_event(lradc)) {
			mxs_lradc_reg_clear(lradc,
					LRADC_CTRL1_TOUCH_DETECT_IRQ_EN,
					LRADC_CTRL1);
			mxs_lradc_prepare_y_pos(lradc);
		}
		mxs_lradc_reg_clear(lradc, LRADC_CTRL1_TOUCH_DETECT_IRQ,
					LRADC_CTRL1);
		return;

	case LRADC_SAMPLE_Y:
		val = mxs_lradc_read_ts_channel(lradc);
		if (val < 0) {
			mxs_lradc_enable_touch_detection(lradc); /* re-start */
			return;
		}
		lradc->ts_y_pos = val;
		mxs_lradc_prepare_x_pos(lradc);
		return;

	case LRADC_SAMPLE_X:
		val = mxs_lradc_read_ts_channel(lradc);
		if (val < 0) {
			mxs_lradc_enable_touch_detection(lradc); /* re-start */
			return;
		}
		lradc->ts_x_pos = val;
		mxs_lradc_prepare_pressure(lradc);
		return;

	case LRADC_SAMPLE_PRESSURE:
		lradc->ts_pressure =
			mxs_lradc_read_ts_pressure(lradc, TS_CH_XP, TS_CH_YM);
		mxs_lradc_complete_touch_event(lradc);
		return;

	case LRADC_SAMPLE_VALID:
		val = mxs_lradc_read_ts_channel(lradc); /* ignore the value */
		mxs_lradc_finish_touch_event(lradc, 1);
		break;
	}
}

/*
 * Raw I/O operations
 */
static int mxs_lradc_read_raw(struct iio_dev *iio_dev,
			const struct iio_chan_spec *chan,
			int *val, int *val2, long m)
{
	struct mxs_lradc *lradc = iio_priv(iio_dev);
	int ret;

	if (m != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	/* Check for invalid channel */
	if (chan->channel > LRADC_MAX_TOTAL_CHANS)
		return -EINVAL;

	/*
	 * See if there is no buffered operation in progess. If there is, simply
	 * bail out. This can be improved to support both buffered and raw IO at
	 * the same time, yet the code becomes horribly complicated. Therefore I
	 * applied KISS principle here.
	 */
	ret = mutex_trylock(&lradc->lock);
	if (!ret)
		return -EBUSY;

	INIT_COMPLETION(lradc->completion);

	/*
	 * No buffered operation in progress, map the channel and trigger it.
	 * Virtual channel 0 is always used here as the others are always not
	 * used if doing raw sampling.
	 */
	if (lradc->soc == IMX28_LRADC)
		mxs_lradc_reg_clear(lradc, LRADC_CTRL1_MX28_LRADC_IRQ_EN_MASK,
			LRADC_CTRL1);
	mxs_lradc_reg_clear(lradc, 0xff, LRADC_CTRL0);

	/* Clean the slot's previous content, then set new one. */
	mxs_lradc_reg_clear(lradc, LRADC_CTRL4_LRADCSELECT_MASK(0), LRADC_CTRL4);
	mxs_lradc_reg_set(lradc, chan->channel, LRADC_CTRL4);

	mxs_lradc_reg_wrt(lradc, 0, LRADC_CH(0));

	/* Enable the IRQ and start sampling the channel. */
	mxs_lradc_reg_set(lradc, LRADC_CTRL1_LRADC_IRQ_EN(0), LRADC_CTRL1);
	mxs_lradc_reg_set(lradc, 1 << 0, LRADC_CTRL0);

	/* Wait for completion on the channel, 1 second max. */
	ret = wait_for_completion_killable_timeout(&lradc->completion, HZ);
	if (!ret)
		ret = -ETIMEDOUT;
	if (ret < 0)
		goto err;

	/* Read the data. */
	*val = readl(lradc->base + LRADC_CH(0)) & LRADC_CH_VALUE_MASK;
	ret = IIO_VAL_INT;

err:
	mxs_lradc_reg_clear(lradc, LRADC_CTRL1_LRADC_IRQ_EN(0), LRADC_CTRL1);

	mutex_unlock(&lradc->lock);

	return ret;
}

static const struct iio_info mxs_lradc_iio_info = {
	.driver_module		= THIS_MODULE,
	.read_raw		= mxs_lradc_read_raw,
};

static int mxs_lradc_ts_open(struct input_dev *dev)
{
	struct mxs_lradc *lradc = input_get_drvdata(dev);

	/* Enable the touch-detect circuitry. */
	mxs_lradc_enable_touch_detection(lradc);

	return 0;
}

static void mxs_lradc_disable_ts(struct mxs_lradc *lradc)
{
	/* stop all interrupts from firing */
	mxs_lradc_reg_clear(lradc, LRADC_CTRL1_TOUCH_DETECT_IRQ_EN |
		LRADC_CTRL1_LRADC_IRQ_EN(2) | LRADC_CTRL1_LRADC_IRQ_EN(3) |
		LRADC_CTRL1_LRADC_IRQ_EN(4) | LRADC_CTRL1_LRADC_IRQ_EN(5),
		LRADC_CTRL1);

	/* Power-down touchscreen touch-detect circuitry. */
	mxs_lradc_reg_clear(lradc, mxs_lradc_plate_mask(lradc), LRADC_CTRL0);
}

static void mxs_lradc_ts_close(struct input_dev *dev)
{
	struct mxs_lradc *lradc = input_get_drvdata(dev);

	mxs_lradc_disable_ts(lradc);
}

static int mxs_lradc_ts_register(struct mxs_lradc *lradc)
{
	struct input_dev *input;
	struct device *dev = lradc->dev;
	int ret;

	if (!lradc->use_touchscreen)
		return 0;

	input = input_allocate_device();
	if (!input) {
		dev_err(dev, "Failed to allocate TS device!\n");
		return -ENOMEM;
	}

	input->name = DRIVER_NAME;
	input->id.bustype = BUS_HOST;
	input->dev.parent = dev;
	input->open = mxs_lradc_ts_open;
	input->close = mxs_lradc_ts_close;

	__set_bit(EV_ABS, input->evbit);
	__set_bit(EV_KEY, input->evbit);
	__set_bit(BTN_TOUCH, input->keybit);
	input_set_abs_params(input, ABS_X, 0, LRADC_SINGLE_SAMPLE_MASK, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, LRADC_SINGLE_SAMPLE_MASK, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, LRADC_SINGLE_SAMPLE_MASK,
			     0, 0);

	lradc->ts_input = input;
	input_set_drvdata(input, lradc);
	ret = input_register_device(input);
	if (ret)
		input_free_device(lradc->ts_input);

	return ret;
}

static void mxs_lradc_ts_unregister(struct mxs_lradc *lradc)
{
	if (!lradc->use_touchscreen)
		return;

	mxs_lradc_disable_ts(lradc);
	input_unregister_device(lradc->ts_input);
}

/*
 * IRQ Handling
 */
static irqreturn_t mxs_lradc_handle_irq(int irq, void *data)
{
	struct iio_dev *iio = data;
	struct mxs_lradc *lradc = iio_priv(iio);
	unsigned long reg = readl(lradc->base + LRADC_CTRL1);
	const uint32_t ts_irq_mask =
		LRADC_CTRL1_TOUCH_DETECT_IRQ |
		LRADC_CTRL1_LRADC_IRQ(2) |
		LRADC_CTRL1_LRADC_IRQ(3) |
		LRADC_CTRL1_LRADC_IRQ(4) |
		LRADC_CTRL1_LRADC_IRQ(5);

	if (!(reg & mxs_lradc_irq_mask(lradc)))
		return IRQ_NONE;

	if (lradc->use_touchscreen && (reg & ts_irq_mask))
		mxs_lradc_handle_touch(lradc);

	if (iio_buffer_enabled(iio))
		iio_trigger_poll(iio->trig, iio_get_time_ns());
	else if (reg & LRADC_CTRL1_LRADC_IRQ(0))
		complete(&lradc->completion);

	mxs_lradc_reg_clear(lradc, reg & mxs_lradc_irq_mask(lradc), LRADC_CTRL1);

	return IRQ_HANDLED;
}

/*
 * Trigger handling
 */
static irqreturn_t mxs_lradc_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio = pf->indio_dev;
	struct mxs_lradc *lradc = iio_priv(iio);
	const uint32_t chan_value = LRADC_CH_ACCUMULATE |
		((LRADC_DELAY_TIMER_LOOP - 1) << LRADC_CH_NUM_SAMPLES_OFFSET);
	unsigned int i, j = 0;

	for_each_set_bit(i, iio->active_scan_mask, LRADC_MAX_TOTAL_CHANS) {
		lradc->buffer[j] = readl(lradc->base + LRADC_CH(j));
		mxs_lradc_reg_wrt(lradc, chan_value, LRADC_CH(j));
		lradc->buffer[j] &= LRADC_CH_VALUE_MASK;
		lradc->buffer[j] /= LRADC_DELAY_TIMER_LOOP;
		j++;
	}

	iio_push_to_buffers_with_timestamp(iio, lradc->buffer, pf->timestamp);

	iio_trigger_notify_done(iio->trig);

	return IRQ_HANDLED;
}

static int mxs_lradc_configure_trigger(struct iio_trigger *trig, bool state)
{
	struct iio_dev *iio = iio_trigger_get_drvdata(trig);
	struct mxs_lradc *lradc = iio_priv(iio);
	const uint32_t st = state ? STMP_OFFSET_REG_SET : STMP_OFFSET_REG_CLR;

	mxs_lradc_reg_wrt(lradc, LRADC_DELAY_KICK, LRADC_DELAY(0) + st);

	return 0;
}

static const struct iio_trigger_ops mxs_lradc_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = &mxs_lradc_configure_trigger,
};

static int mxs_lradc_trigger_init(struct iio_dev *iio)
{
	int ret;
	struct iio_trigger *trig;
	struct mxs_lradc *lradc = iio_priv(iio);

	trig = iio_trigger_alloc("%s-dev%i", iio->name, iio->id);
	if (trig == NULL)
		return -ENOMEM;

	trig->dev.parent = lradc->dev;
	iio_trigger_set_drvdata(trig, iio);
	trig->ops = &mxs_lradc_trigger_ops;

	ret = iio_trigger_register(trig);
	if (ret) {
		iio_trigger_free(trig);
		return ret;
	}

	lradc->trig = trig;

	return 0;
}

static void mxs_lradc_trigger_remove(struct iio_dev *iio)
{
	struct mxs_lradc *lradc = iio_priv(iio);

	iio_trigger_unregister(lradc->trig);
	iio_trigger_free(lradc->trig);
}

static int mxs_lradc_buffer_preenable(struct iio_dev *iio)
{
	struct mxs_lradc *lradc = iio_priv(iio);
	int ret = 0, chan, ofs = 0;
	unsigned long enable = 0;
	uint32_t ctrl4_set = 0;
	uint32_t ctrl4_clr = 0;
	uint32_t ctrl1_irq = 0;
	const uint32_t chan_value = LRADC_CH_ACCUMULATE |
		((LRADC_DELAY_TIMER_LOOP - 1) << LRADC_CH_NUM_SAMPLES_OFFSET);
	const int len = bitmap_weight(iio->active_scan_mask, LRADC_MAX_TOTAL_CHANS);

	if (!len)
		return -EINVAL;

	/*
	 * Lock the driver so raw access can not be done during buffered
	 * operation. This simplifies the code a lot.
	 */
	ret = mutex_trylock(&lradc->lock);
	if (!ret)
		return -EBUSY;

	lradc->buffer = kmalloc(len * sizeof(*lradc->buffer), GFP_KERNEL);
	if (!lradc->buffer) {
		ret = -ENOMEM;
		goto err_mem;
	}

	if (lradc->soc == IMX28_LRADC)
		mxs_lradc_reg_clear(lradc, LRADC_CTRL1_MX28_LRADC_IRQ_EN_MASK,
							LRADC_CTRL1);
	mxs_lradc_reg_clear(lradc, 0xff, LRADC_CTRL0);

	for_each_set_bit(chan, iio->active_scan_mask, LRADC_MAX_TOTAL_CHANS) {
		ctrl4_set |= chan << LRADC_CTRL4_LRADCSELECT_OFFSET(ofs);
		ctrl4_clr |= LRADC_CTRL4_LRADCSELECT_MASK(ofs);
		ctrl1_irq |= LRADC_CTRL1_LRADC_IRQ_EN(ofs);
		mxs_lradc_reg_wrt(lradc, chan_value, LRADC_CH(ofs));
		bitmap_set(&enable, ofs, 1);
		ofs++;
	}

	mxs_lradc_reg_clear(lradc, LRADC_DELAY_TRIGGER_LRADCS_MASK |
					LRADC_DELAY_KICK, LRADC_DELAY(0));
	mxs_lradc_reg_clear(lradc, ctrl4_clr, LRADC_CTRL4);
	mxs_lradc_reg_set(lradc, ctrl4_set, LRADC_CTRL4);
	mxs_lradc_reg_set(lradc, ctrl1_irq, LRADC_CTRL1);
	mxs_lradc_reg_set(lradc, enable << LRADC_DELAY_TRIGGER_LRADCS_OFFSET,
					LRADC_DELAY(0));

	return 0;

err_mem:
	mutex_unlock(&lradc->lock);
	return ret;
}

static int mxs_lradc_buffer_postdisable(struct iio_dev *iio)
{
	struct mxs_lradc *lradc = iio_priv(iio);

	mxs_lradc_reg_clear(lradc, LRADC_DELAY_TRIGGER_LRADCS_MASK |
					LRADC_DELAY_KICK, LRADC_DELAY(0));

	mxs_lradc_reg_clear(lradc, 0xff, LRADC_CTRL0);
	if (lradc->soc == IMX28_LRADC)
		mxs_lradc_reg_clear(lradc, LRADC_CTRL1_MX28_LRADC_IRQ_EN_MASK,
					LRADC_CTRL1);

	kfree(lradc->buffer);
	mutex_unlock(&lradc->lock);

	return 0;
}

static bool mxs_lradc_validate_scan_mask(struct iio_dev *iio,
					const unsigned long *mask)
{
	struct mxs_lradc *lradc = iio_priv(iio);
	const int map_chans = bitmap_weight(mask, LRADC_MAX_TOTAL_CHANS);
	int rsvd_chans = 0;
	unsigned long rsvd_mask = 0;

	if (lradc->use_touchbutton)
		rsvd_mask |= CHAN_MASK_TOUCHBUTTON;
	if (lradc->use_touchscreen == MXS_LRADC_TOUCHSCREEN_4WIRE)
		rsvd_mask |= CHAN_MASK_TOUCHSCREEN_4WIRE;
	if (lradc->use_touchscreen == MXS_LRADC_TOUCHSCREEN_5WIRE)
		rsvd_mask |= CHAN_MASK_TOUCHSCREEN_5WIRE;

	if (lradc->use_touchbutton)
		rsvd_chans++;
	if (lradc->use_touchscreen)
		rsvd_chans++;

	/* Test for attempts to map channels with special mode of operation. */
	if (bitmap_intersects(mask, &rsvd_mask, LRADC_MAX_TOTAL_CHANS))
		return false;

	/* Test for attempts to map more channels then available slots. */
	if (map_chans + rsvd_chans > LRADC_MAX_MAPPED_CHANS)
		return false;

	return true;
}

static const struct iio_buffer_setup_ops mxs_lradc_buffer_ops = {
	.preenable = &mxs_lradc_buffer_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
	.postdisable = &mxs_lradc_buffer_postdisable,
	.validate_scan_mask = &mxs_lradc_validate_scan_mask,
};

/*
 * Driver initialization
 */

#define MXS_ADC_CHAN(idx, chan_type) {				\
	.type = (chan_type),					\
	.indexed = 1,						\
	.scan_index = (idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.channel = (idx),					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = LRADC_RESOLUTION,			\
		.storagebits = 32,				\
	},							\
}

static const struct iio_chan_spec mxs_lradc_chan_spec[] = {
	MXS_ADC_CHAN(0, IIO_VOLTAGE),
	MXS_ADC_CHAN(1, IIO_VOLTAGE),
	MXS_ADC_CHAN(2, IIO_VOLTAGE),
	MXS_ADC_CHAN(3, IIO_VOLTAGE),
	MXS_ADC_CHAN(4, IIO_VOLTAGE),
	MXS_ADC_CHAN(5, IIO_VOLTAGE),
	MXS_ADC_CHAN(6, IIO_VOLTAGE),
	MXS_ADC_CHAN(7, IIO_VOLTAGE),	/* VBATT */
	MXS_ADC_CHAN(8, IIO_TEMP),	/* Temp sense 0 */
	MXS_ADC_CHAN(9, IIO_TEMP),	/* Temp sense 1 */
	MXS_ADC_CHAN(10, IIO_VOLTAGE),	/* VDDIO */
	MXS_ADC_CHAN(11, IIO_VOLTAGE),	/* VTH */
	MXS_ADC_CHAN(12, IIO_VOLTAGE),	/* VDDA */
	MXS_ADC_CHAN(13, IIO_VOLTAGE),	/* VDDD */
	MXS_ADC_CHAN(14, IIO_VOLTAGE),	/* VBG */
	MXS_ADC_CHAN(15, IIO_VOLTAGE),	/* VDD5V */
};

static int mxs_lradc_hw_init(struct mxs_lradc *lradc)
{
	/* The ADC always uses DELAY CHANNEL 0. */
	const uint32_t adc_cfg =
		(1 << (LRADC_DELAY_TRIGGER_DELAYS_OFFSET + 0)) |
		(LRADC_DELAY_TIMER_PER << LRADC_DELAY_DELAY_OFFSET);

	int ret = stmp_reset_block(lradc->base);
	if (ret)
		return ret;

	/* Configure DELAY CHANNEL 0 for generic ADC sampling. */
	mxs_lradc_reg_wrt(lradc, adc_cfg, LRADC_DELAY(0));

	/* Disable remaining DELAY CHANNELs */
	mxs_lradc_reg_wrt(lradc, 0, LRADC_DELAY(1));
	mxs_lradc_reg_wrt(lradc, 0, LRADC_DELAY(2));
	mxs_lradc_reg_wrt(lradc, 0, LRADC_DELAY(3));

	/* Configure the touchscreen type */
	if (lradc->soc == IMX28_LRADC) {
		mxs_lradc_reg_clear(lradc, LRADC_CTRL0_MX28_TOUCH_SCREEN_TYPE,
							LRADC_CTRL0);

	if (lradc->use_touchscreen == MXS_LRADC_TOUCHSCREEN_5WIRE)
		mxs_lradc_reg_set(lradc, LRADC_CTRL0_MX28_TOUCH_SCREEN_TYPE,
				LRADC_CTRL0);
	}

	/* Start internal temperature sensing. */
	mxs_lradc_reg_wrt(lradc, 0, LRADC_CTRL2);

	return 0;
}

static void mxs_lradc_hw_stop(struct mxs_lradc *lradc)
{
	int i;

	mxs_lradc_reg_clear(lradc, mxs_lradc_irq_en_mask(lradc), LRADC_CTRL1);

	for (i = 0; i < LRADC_MAX_DELAY_CHANS; i++)
		mxs_lradc_reg_wrt(lradc, 0, LRADC_DELAY(i));
}

static const struct of_device_id mxs_lradc_dt_ids[] = {
	{ .compatible = "fsl,imx23-lradc", .data = (void *)IMX23_LRADC, },
	{ .compatible = "fsl,imx28-lradc", .data = (void *)IMX28_LRADC, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_lradc_dt_ids);

static int mxs_lradc_probe_touchscreen(struct mxs_lradc *lradc,
						struct device_node *lradc_node)
{
	int ret;
	u32 ts_wires = 0, adapt;

	ret = of_property_read_u32(lradc_node, "fsl,lradc-touchscreen-wires",
				&ts_wires);
	if (ret)
		return -ENODEV; /* touchscreen feature disabled */

	switch (ts_wires) {
	case 4:
		lradc->use_touchscreen = MXS_LRADC_TOUCHSCREEN_4WIRE;
		break;
	case 5:
		if (lradc->soc == IMX28_LRADC) {
			lradc->use_touchscreen = MXS_LRADC_TOUCHSCREEN_5WIRE;
			break;
		}
		/* fall through an error message for i.MX23 */
	default:
		dev_err(lradc->dev,
			"Unsupported number of touchscreen wires (%d)\n",
			ts_wires);
		return -EINVAL;
	}

	lradc->over_sample_cnt = 4;
	ret = of_property_read_u32(lradc_node, "fsl,ave-ctrl", &adapt);
	if (ret == 0)
		lradc->over_sample_cnt = adapt;

	lradc->over_sample_delay = 2;
	ret = of_property_read_u32(lradc_node, "fsl,ave-delay", &adapt);
	if (ret == 0)
		lradc->over_sample_delay = adapt;

	lradc->settling_delay = 10;
	ret = of_property_read_u32(lradc_node, "fsl,settling", &adapt);
	if (ret == 0)
		lradc->settling_delay = adapt;

	return 0;
}

static int mxs_lradc_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
		of_match_device(mxs_lradc_dt_ids, &pdev->dev);
	const struct mxs_lradc_of_config *of_cfg =
		&mxs_lradc_of_config[(enum mxs_lradc_id)of_id->data];
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct mxs_lradc *lradc;
	struct iio_dev *iio;
	struct resource *iores;
	int ret = 0, touch_ret;
	int i;

	/* Allocate the IIO device. */
	iio = devm_iio_device_alloc(dev, sizeof(*lradc));
	if (!iio) {
		dev_err(dev, "Failed to allocate IIO device\n");
		return -ENOMEM;
	}

	lradc = iio_priv(iio);
	lradc->soc = (enum mxs_lradc_id)of_id->data;

	/* Grab the memory area */
	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lradc->dev = &pdev->dev;
	lradc->base = devm_ioremap_resource(dev, iores);
	if (IS_ERR(lradc->base))
		return PTR_ERR(lradc->base);

	lradc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(lradc->clk)) {
		dev_err(dev, "Failed to get the delay unit clock\n");
		return PTR_ERR(lradc->clk);
	}
	ret = clk_prepare_enable(lradc->clk);
	if (ret != 0) {
		dev_err(dev, "Failed to enable the delay unit clock\n");
		return ret;
	}

	touch_ret = mxs_lradc_probe_touchscreen(lradc, node);

	/* Grab all IRQ sources */
	for (i = 0; i < of_cfg->irq_count; i++) {
		lradc->irq[i] = platform_get_irq(pdev, i);
		if (lradc->irq[i] < 0)
			return -EINVAL;

		ret = devm_request_irq(dev, lradc->irq[i],
					mxs_lradc_handle_irq, 0,
					of_cfg->irq_name[i], iio);
		if (ret)
			return ret;
	}

	platform_set_drvdata(pdev, iio);

	init_completion(&lradc->completion);
	mutex_init(&lradc->lock);

	iio->name = pdev->name;
	iio->dev.parent = &pdev->dev;
	iio->info = &mxs_lradc_iio_info;
	iio->modes = INDIO_DIRECT_MODE;
	iio->channels = mxs_lradc_chan_spec;
	iio->num_channels = ARRAY_SIZE(mxs_lradc_chan_spec);
	iio->masklength = LRADC_MAX_TOTAL_CHANS;

	ret = iio_triggered_buffer_setup(iio, &iio_pollfunc_store_time,
				&mxs_lradc_trigger_handler,
				&mxs_lradc_buffer_ops);
	if (ret)
		return ret;

	ret = mxs_lradc_trigger_init(iio);
	if (ret)
		goto err_trig;

	/* Configure the hardware. */
	ret = mxs_lradc_hw_init(lradc);
	if (ret)
		goto err_dev;

	/* Register the touchscreen input device. */
	if (touch_ret == 0) {
		ret = mxs_lradc_ts_register(lradc);
		if (ret)
			goto err_ts_register;
	}

	/* Register IIO device. */
	ret = iio_device_register(iio);
	if (ret) {
		dev_err(dev, "Failed to register IIO device\n");
		goto err_ts;
	}

	return 0;

err_ts:
	mxs_lradc_ts_unregister(lradc);
err_ts_register:
	mxs_lradc_hw_stop(lradc);
err_dev:
	mxs_lradc_trigger_remove(iio);
err_trig:
	iio_triggered_buffer_cleanup(iio);
	return ret;
}

static int mxs_lradc_remove(struct platform_device *pdev)
{
	struct iio_dev *iio = platform_get_drvdata(pdev);
	struct mxs_lradc *lradc = iio_priv(iio);

	iio_device_unregister(iio);
	mxs_lradc_ts_unregister(lradc);
	mxs_lradc_hw_stop(lradc);
	mxs_lradc_trigger_remove(iio);
	iio_triggered_buffer_cleanup(iio);

	clk_disable_unprepare(lradc->clk);
	return 0;
}

static struct platform_driver mxs_lradc_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = mxs_lradc_dt_ids,
	},
	.probe	= mxs_lradc_probe,
	.remove	= mxs_lradc_remove,
};

module_platform_driver(mxs_lradc_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Freescale i.MX28 LRADC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
