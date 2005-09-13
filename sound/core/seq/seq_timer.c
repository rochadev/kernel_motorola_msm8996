/*
 *   ALSA sequencer Timer
 *   Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@coil.demon.nl>
 *                              Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <sound/core.h>
#include <linux/slab.h>
#include "seq_timer.h"
#include "seq_queue.h"
#include "seq_info.h"

extern int seq_default_timer_class;
extern int seq_default_timer_sclass;
extern int seq_default_timer_card;
extern int seq_default_timer_device;
extern int seq_default_timer_subdevice;
extern int seq_default_timer_resolution;

#define SKEW_BASE	0x10000	/* 16bit shift */

static void snd_seq_timer_set_tick_resolution(seq_timer_tick_t *tick,
					      int tempo, int ppq, int nticks)
{
	if (tempo < 1000000)
		tick->resolution = (tempo * 1000) / ppq;
	else {
		/* might overflow.. */
		unsigned int s;
		s = tempo % ppq;
		s = (s * 1000) / ppq;
		tick->resolution = (tempo / ppq) * 1000;
		tick->resolution += s;
	}
	if (tick->resolution <= 0)
		tick->resolution = 1;
	tick->resolution *= nticks;
	snd_seq_timer_update_tick(tick, 0);
}

/* create new timer (constructor) */
seq_timer_t *snd_seq_timer_new(void)
{
	seq_timer_t *tmr;
	
	tmr = kzalloc(sizeof(*tmr), GFP_KERNEL);
	if (tmr == NULL) {
		snd_printd("malloc failed for snd_seq_timer_new() \n");
		return NULL;
	}
	spin_lock_init(&tmr->lock);

	/* reset setup to defaults */
	snd_seq_timer_defaults(tmr);
	
	/* reset time */
	snd_seq_timer_reset(tmr);
	
	return tmr;
}

/* delete timer (destructor) */
void snd_seq_timer_delete(seq_timer_t **tmr)
{
	seq_timer_t *t = *tmr;
	*tmr = NULL;

	if (t == NULL) {
		snd_printd("oops: snd_seq_timer_delete() called with NULL timer\n");
		return;
	}
	t->running = 0;

	/* reset time */
	snd_seq_timer_stop(t);
	snd_seq_timer_reset(t);

	kfree(t);
}

void snd_seq_timer_defaults(seq_timer_t * tmr)
{
	/* setup defaults */
	tmr->ppq = 96;		/* 96 PPQ */
	tmr->tempo = 500000;	/* 120 BPM */
	snd_seq_timer_set_tick_resolution(&tmr->tick, tmr->tempo, tmr->ppq, 1);
	tmr->running = 0;

	tmr->type = SNDRV_SEQ_TIMER_ALSA;
	tmr->alsa_id.dev_class = seq_default_timer_class;
	tmr->alsa_id.dev_sclass = seq_default_timer_sclass;
	tmr->alsa_id.card = seq_default_timer_card;
	tmr->alsa_id.device = seq_default_timer_device;
	tmr->alsa_id.subdevice = seq_default_timer_subdevice;
	tmr->preferred_resolution = seq_default_timer_resolution;

	tmr->skew = tmr->skew_base = SKEW_BASE;
}

void snd_seq_timer_reset(seq_timer_t * tmr)
{
	unsigned long flags;

	spin_lock_irqsave(&tmr->lock, flags);

	/* reset time & songposition */
	tmr->cur_time.tv_sec = 0;
	tmr->cur_time.tv_nsec = 0;

	tmr->tick.cur_tick = 0;
	tmr->tick.fraction = 0;

	spin_unlock_irqrestore(&tmr->lock, flags);
}


/* called by timer interrupt routine. the period time since previous invocation is passed */
static void snd_seq_timer_interrupt(snd_timer_instance_t *timeri,
				    unsigned long resolution,
				    unsigned long ticks)
{
	unsigned long flags;
	queue_t *q = (queue_t *)timeri->callback_data;
	seq_timer_t *tmr;

	if (q == NULL)
		return;
	tmr = q->timer;
	if (tmr == NULL)
		return;
	if (!tmr->running)
		return;

	resolution *= ticks;
	if (tmr->skew != tmr->skew_base) {
		/* FIXME: assuming skew_base = 0x10000 */
		resolution = (resolution >> 16) * tmr->skew +
			(((resolution & 0xffff) * tmr->skew) >> 16);
	}

	spin_lock_irqsave(&tmr->lock, flags);

	/* update timer */
	snd_seq_inc_time_nsec(&tmr->cur_time, resolution);

	/* calculate current tick */
	snd_seq_timer_update_tick(&tmr->tick, resolution);

	/* register actual time of this timer update */
	do_gettimeofday(&tmr->last_update);

	spin_unlock_irqrestore(&tmr->lock, flags);

	/* check queues and dispatch events */
	snd_seq_check_queue(q, 1, 0);
}

/* set current tempo */
int snd_seq_timer_set_tempo(seq_timer_t * tmr, int tempo)
{
	unsigned long flags;

	snd_assert(tmr, return -EINVAL);
	if (tempo <= 0)
		return -EINVAL;
	spin_lock_irqsave(&tmr->lock, flags);
	if ((unsigned int)tempo != tmr->tempo) {
		tmr->tempo = tempo;
		snd_seq_timer_set_tick_resolution(&tmr->tick, tmr->tempo, tmr->ppq, 1);
	}
	spin_unlock_irqrestore(&tmr->lock, flags);
	return 0;
}

/* set current ppq */
int snd_seq_timer_set_ppq(seq_timer_t * tmr, int ppq)
{
	unsigned long flags;

	snd_assert(tmr, return -EINVAL);
	if (ppq <= 0)
		return -EINVAL;
	spin_lock_irqsave(&tmr->lock, flags);
	if (tmr->running && (ppq != tmr->ppq)) {
		/* refuse to change ppq on running timers */
		/* because it will upset the song position (ticks) */
		spin_unlock_irqrestore(&tmr->lock, flags);
		snd_printd("seq: cannot change ppq of a running timer\n");
		return -EBUSY;
	}

	tmr->ppq = ppq;
	snd_seq_timer_set_tick_resolution(&tmr->tick, tmr->tempo, tmr->ppq, 1);
	spin_unlock_irqrestore(&tmr->lock, flags);
	return 0;
}

/* set current tick position */
int snd_seq_timer_set_position_tick(seq_timer_t *tmr, snd_seq_tick_time_t position)
{
	unsigned long flags;

	snd_assert(tmr, return -EINVAL);

	spin_lock_irqsave(&tmr->lock, flags);
	tmr->tick.cur_tick = position;
	tmr->tick.fraction = 0;
	spin_unlock_irqrestore(&tmr->lock, flags);
	return 0;
}

/* set current real-time position */
int snd_seq_timer_set_position_time(seq_timer_t *tmr, snd_seq_real_time_t position)
{
	unsigned long flags;

	snd_assert(tmr, return -EINVAL);

	snd_seq_sanity_real_time(&position);
	spin_lock_irqsave(&tmr->lock, flags);
	tmr->cur_time = position;
	spin_unlock_irqrestore(&tmr->lock, flags);
	return 0;
}

/* set timer skew */
int snd_seq_timer_set_skew(seq_timer_t *tmr, unsigned int skew, unsigned int base)
{
	unsigned long flags;

	snd_assert(tmr, return -EINVAL);

	/* FIXME */
	if (base != SKEW_BASE) {
		snd_printd("invalid skew base 0x%x\n", base);
		return -EINVAL;
	}
	spin_lock_irqsave(&tmr->lock, flags);
	tmr->skew = skew;
	spin_unlock_irqrestore(&tmr->lock, flags);
	return 0;
}

int snd_seq_timer_open(queue_t *q)
{
	snd_timer_instance_t *t;
	seq_timer_t *tmr;
	char str[32];
	int err;

	tmr = q->timer;
	snd_assert(tmr != NULL, return -EINVAL);
	if (tmr->timeri)
		return -EBUSY;
	sprintf(str, "sequencer queue %i", q->queue);
	if (tmr->type != SNDRV_SEQ_TIMER_ALSA)	/* standard ALSA timer */
		return -EINVAL;
	if (tmr->alsa_id.dev_class != SNDRV_TIMER_CLASS_SLAVE)
		tmr->alsa_id.dev_sclass = SNDRV_TIMER_SCLASS_SEQUENCER;
	err = snd_timer_open(&t, str, &tmr->alsa_id, q->queue);
	if (err < 0 && tmr->alsa_id.dev_class != SNDRV_TIMER_CLASS_SLAVE) {
		if (tmr->alsa_id.dev_class != SNDRV_TIMER_CLASS_GLOBAL ||
		    tmr->alsa_id.device != SNDRV_TIMER_GLOBAL_SYSTEM) {
			snd_timer_id_t tid;
			memset(&tid, 0, sizeof(tid));
			tid.dev_class = SNDRV_TIMER_CLASS_GLOBAL;
			tid.dev_sclass = SNDRV_TIMER_SCLASS_SEQUENCER;
			tid.card = -1;
			tid.device = SNDRV_TIMER_GLOBAL_SYSTEM;
			err = snd_timer_open(&t, str, &tid, q->queue);
		}
		if (err < 0) {
			snd_printk(KERN_ERR "seq fatal error: cannot create timer (%i)\n", err);
			return err;
		}
	}
	t->callback = snd_seq_timer_interrupt;
	t->callback_data = q;
	t->flags |= SNDRV_TIMER_IFLG_AUTO;
	tmr->timeri = t;
	return 0;
}

int snd_seq_timer_close(queue_t *q)
{
	seq_timer_t *tmr;
	
	tmr = q->timer;
	snd_assert(tmr != NULL, return -EINVAL);
	if (tmr->timeri) {
		snd_timer_stop(tmr->timeri);
		snd_timer_close(tmr->timeri);
		tmr->timeri = NULL;
	}
	return 0;
}

int snd_seq_timer_stop(seq_timer_t * tmr)
{
	if (! tmr->timeri)
		return -EINVAL;
	if (!tmr->running)
		return 0;
	tmr->running = 0;
	snd_timer_pause(tmr->timeri);
	return 0;
}

static int initialize_timer(seq_timer_t *tmr)
{
	snd_timer_t *t;
	t = tmr->timeri->timer;
	snd_assert(t, return -EINVAL);

	tmr->ticks = 1;
	if (tmr->preferred_resolution &&
	    ! (t->hw.flags & SNDRV_TIMER_HW_SLAVE)) {
		unsigned long r = t->hw.resolution;
		if (! r && t->hw.c_resolution)
			r = t->hw.c_resolution(t);
		if (r) {
			tmr->ticks = (unsigned int)(1000000000uL / (r * tmr->preferred_resolution));
			if (! tmr->ticks)
				tmr->ticks = 1;
		}
	}
	tmr->initialized = 1;
	return 0;
}

int snd_seq_timer_start(seq_timer_t * tmr)
{
	if (! tmr->timeri)
		return -EINVAL;
	if (tmr->running)
		snd_seq_timer_stop(tmr);
	snd_seq_timer_reset(tmr);
	if (initialize_timer(tmr) < 0)
		return -EINVAL;
	snd_timer_start(tmr->timeri, tmr->ticks);
	tmr->running = 1;
	do_gettimeofday(&tmr->last_update);
	return 0;
}

int snd_seq_timer_continue(seq_timer_t * tmr)
{
	if (! tmr->timeri)
		return -EINVAL;
	if (tmr->running)
		return -EBUSY;
	if (! tmr->initialized) {
		snd_seq_timer_reset(tmr);
		if (initialize_timer(tmr) < 0)
			return -EINVAL;
	}
	snd_timer_start(tmr->timeri, tmr->ticks);
	tmr->running = 1;
	do_gettimeofday(&tmr->last_update);
	return 0;
}

/* return current 'real' time. use timeofday() to get better granularity. */
snd_seq_real_time_t snd_seq_timer_get_cur_time(seq_timer_t *tmr)
{
	snd_seq_real_time_t cur_time;

	cur_time = tmr->cur_time;
	if (tmr->running) { 
		struct timeval tm;
		int usec;
		do_gettimeofday(&tm);
		usec = (int)(tm.tv_usec - tmr->last_update.tv_usec);
		if (usec < 0) {
			cur_time.tv_nsec += (1000000 + usec) * 1000;
			cur_time.tv_sec += tm.tv_sec - tmr->last_update.tv_sec - 1;
		} else {
			cur_time.tv_nsec += usec * 1000;
			cur_time.tv_sec += tm.tv_sec - tmr->last_update.tv_sec;
		}
		snd_seq_sanity_real_time(&cur_time);
	}
                
	return cur_time;	
}

/* TODO: use interpolation on tick queue (will only be useful for very
 high PPQ values) */
snd_seq_tick_time_t snd_seq_timer_get_cur_tick(seq_timer_t *tmr)
{
	return tmr->tick.cur_tick;
}


/* exported to seq_info.c */
void snd_seq_info_timer_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	int idx;
	queue_t *q;
	seq_timer_t *tmr;
	snd_timer_instance_t *ti;
	unsigned long resolution;
	
	for (idx = 0; idx < SNDRV_SEQ_MAX_QUEUES; idx++) {
		q = queueptr(idx);
		if (q == NULL)
			continue;
		if ((tmr = q->timer) == NULL ||
		    (ti = tmr->timeri) == NULL) {
			queuefree(q);
			continue;
		}
		snd_iprintf(buffer, "Timer for queue %i : %s\n", q->queue, ti->timer->name);
		resolution = snd_timer_resolution(ti) * tmr->ticks;
		snd_iprintf(buffer, "  Period time : %lu.%09lu\n", resolution / 1000000000, resolution % 1000000000);
		snd_iprintf(buffer, "  Skew : %u / %u\n", tmr->skew, tmr->skew_base);
		queuefree(q);
 	}
}
