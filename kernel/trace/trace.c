/*
 * ring buffer based function tracer
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2008 Ingo Molnar <mingo@redhat.com>
 *
 * Originally taken from the RT patch by:
 *    Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Based on code from the latency_tracer, that is:
 *  Copyright (C) 2004-2006 Ingo Molnar
 *  Copyright (C) 2004 William Lee Irwin III
 */
#include <linux/ring_buffer.h>
#include <linux/utsrelease.h>
#include <linux/stacktrace.h>
#include <linux/writeback.h>
#include <linux/kallsyms.h>
#include <linux/seq_file.h>
#include <linux/notifier.h>
#include <linux/irqflags.h>
#include <linux/debugfs.h>
#include <linux/pagemap.h>
#include <linux/hardirq.h>
#include <linux/linkage.h>
#include <linux/uaccess.h>
#include <linux/kprobes.h>
#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/splice.h>
#include <linux/kdebug.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/gfp.h>
#include <linux/fs.h>

#include "trace.h"
#include "trace_output.h"

#define TRACE_BUFFER_FLAGS	(RB_FL_OVERWRITE)

unsigned long __read_mostly	tracing_max_latency;
unsigned long __read_mostly	tracing_thresh;

/*
 * On boot up, the ring buffer is set to the minimum size, so that
 * we do not waste memory on systems that are not using tracing.
 */
static int ring_buffer_expanded;

/*
 * We need to change this state when a selftest is running.
 * A selftest will lurk into the ring-buffer to count the
 * entries inserted during the selftest although some concurrent
 * insertions into the ring-buffer such as trace_printk could occurred
 * at the same time, giving false positive or negative results.
 */
static bool __read_mostly tracing_selftest_running;

/*
 * If a tracer is running, we do not want to run SELFTEST.
 */
static bool __read_mostly tracing_selftest_disabled;

/* For tracers that don't implement custom flags */
static struct tracer_opt dummy_tracer_opt[] = {
	{ }
};

static struct tracer_flags dummy_tracer_flags = {
	.val = 0,
	.opts = dummy_tracer_opt
};

static int dummy_set_flag(u32 old_flags, u32 bit, int set)
{
	return 0;
}

/*
 * Kill all tracing for good (never come back).
 * It is initialized to 1 but will turn to zero if the initialization
 * of the tracer is successful. But that is the only place that sets
 * this back to zero.
 */
static int tracing_disabled = 1;

static DEFINE_PER_CPU(local_t, ftrace_cpu_disabled);

static inline void ftrace_disable_cpu(void)
{
	preempt_disable();
	local_inc(&__get_cpu_var(ftrace_cpu_disabled));
}

static inline void ftrace_enable_cpu(void)
{
	local_dec(&__get_cpu_var(ftrace_cpu_disabled));
	preempt_enable();
}

static cpumask_var_t __read_mostly	tracing_buffer_mask;

/* Define which cpu buffers are currently read in trace_pipe */
static cpumask_var_t			tracing_reader_cpumask;

#define for_each_tracing_cpu(cpu)	\
	for_each_cpu(cpu, tracing_buffer_mask)

/*
 * ftrace_dump_on_oops - variable to dump ftrace buffer on oops
 *
 * If there is an oops (or kernel panic) and the ftrace_dump_on_oops
 * is set, then ftrace_dump is called. This will output the contents
 * of the ftrace buffers to the console.  This is very useful for
 * capturing traces that lead to crashes and outputing it to a
 * serial console.
 *
 * It is default off, but you can enable it with either specifying
 * "ftrace_dump_on_oops" in the kernel command line, or setting
 * /proc/sys/kernel/ftrace_dump_on_oops to true.
 */
int ftrace_dump_on_oops;

static int tracing_set_tracer(const char *buf);

#define BOOTUP_TRACER_SIZE		100
static char bootup_tracer_buf[BOOTUP_TRACER_SIZE] __initdata;
static char *default_bootup_tracer;

static int __init set_ftrace(char *str)
{
	strncpy(bootup_tracer_buf, str, BOOTUP_TRACER_SIZE);
	default_bootup_tracer = bootup_tracer_buf;
	/* We are using ftrace early, expand it */
	ring_buffer_expanded = 1;
	return 1;
}
__setup("ftrace=", set_ftrace);

static int __init set_ftrace_dump_on_oops(char *str)
{
	ftrace_dump_on_oops = 1;
	return 1;
}
__setup("ftrace_dump_on_oops", set_ftrace_dump_on_oops);

unsigned long long ns2usecs(cycle_t nsec)
{
	nsec += 500;
	do_div(nsec, 1000);
	return nsec;
}

/*
 * The global_trace is the descriptor that holds the tracing
 * buffers for the live tracing. For each CPU, it contains
 * a link list of pages that will store trace entries. The
 * page descriptor of the pages in the memory is used to hold
 * the link list by linking the lru item in the page descriptor
 * to each of the pages in the buffer per CPU.
 *
 * For each active CPU there is a data field that holds the
 * pages for the buffer for that CPU. Each CPU has the same number
 * of pages allocated for its buffer.
 */
static struct trace_array	global_trace;

static DEFINE_PER_CPU(struct trace_array_cpu, global_trace_cpu);

cycle_t ftrace_now(int cpu)
{
	u64 ts;

	/* Early boot up does not have a buffer yet */
	if (!global_trace.buffer)
		return trace_clock_local();

	ts = ring_buffer_time_stamp(global_trace.buffer, cpu);
	ring_buffer_normalize_time_stamp(global_trace.buffer, cpu, &ts);

	return ts;
}

/*
 * The max_tr is used to snapshot the global_trace when a maximum
 * latency is reached. Some tracers will use this to store a maximum
 * trace while it continues examining live traces.
 *
 * The buffers for the max_tr are set up the same as the global_trace.
 * When a snapshot is taken, the link list of the max_tr is swapped
 * with the link list of the global_trace and the buffers are reset for
 * the global_trace so the tracing can continue.
 */
static struct trace_array	max_tr;

static DEFINE_PER_CPU(struct trace_array_cpu, max_data);

/* tracer_enabled is used to toggle activation of a tracer */
static int			tracer_enabled = 1;

/**
 * tracing_is_enabled - return tracer_enabled status
 *
 * This function is used by other tracers to know the status
 * of the tracer_enabled flag.  Tracers may use this function
 * to know if it should enable their features when starting
 * up. See irqsoff tracer for an example (start_irqsoff_tracer).
 */
int tracing_is_enabled(void)
{
	return tracer_enabled;
}

/*
 * trace_buf_size is the size in bytes that is allocated
 * for a buffer. Note, the number of bytes is always rounded
 * to page size.
 *
 * This number is purposely set to a low number of 16384.
 * If the dump on oops happens, it will be much appreciated
 * to not have to wait for all that output. Anyway this can be
 * boot time and run time configurable.
 */
#define TRACE_BUF_SIZE_DEFAULT	1441792UL /* 16384 * 88 (sizeof(entry)) */

static unsigned long		trace_buf_size = TRACE_BUF_SIZE_DEFAULT;

/* trace_types holds a link list of available tracers. */
static struct tracer		*trace_types __read_mostly;

/* current_trace points to the tracer that is currently active */
static struct tracer		*current_trace __read_mostly;

/*
 * max_tracer_type_len is used to simplify the allocating of
 * buffers to read userspace tracer names. We keep track of
 * the longest tracer name registered.
 */
static int			max_tracer_type_len;

/*
 * trace_types_lock is used to protect the trace_types list.
 * This lock is also used to keep user access serialized.
 * Accesses from userspace will grab this lock while userspace
 * activities happen inside the kernel.
 */
static DEFINE_MUTEX(trace_types_lock);

/* trace_wait is a waitqueue for tasks blocked on trace_poll */
static DECLARE_WAIT_QUEUE_HEAD(trace_wait);

/* trace_flags holds trace_options default values */
unsigned long trace_flags = TRACE_ITER_PRINT_PARENT | TRACE_ITER_PRINTK |
	TRACE_ITER_ANNOTATE | TRACE_ITER_CONTEXT_INFO | TRACE_ITER_SLEEP_TIME;

/**
 * trace_wake_up - wake up tasks waiting for trace input
 *
 * Simply wakes up any task that is blocked on the trace_wait
 * queue. These is used with trace_poll for tasks polling the trace.
 */
void trace_wake_up(void)
{
	/*
	 * The runqueue_is_locked() can fail, but this is the best we
	 * have for now:
	 */
	if (!(trace_flags & TRACE_ITER_BLOCK) && !runqueue_is_locked())
		wake_up(&trace_wait);
}

static int __init set_buf_size(char *str)
{
	unsigned long buf_size;
	int ret;

	if (!str)
		return 0;
	ret = strict_strtoul(str, 0, &buf_size);
	/* nr_entries can not be zero */
	if (ret < 0 || buf_size == 0)
		return 0;
	trace_buf_size = buf_size;
	return 1;
}
__setup("trace_buf_size=", set_buf_size);

unsigned long nsecs_to_usecs(unsigned long nsecs)
{
	return nsecs / 1000;
}

/* These must match the bit postions in trace_iterator_flags */
static const char *trace_options[] = {
	"print-parent",
	"sym-offset",
	"sym-addr",
	"verbose",
	"raw",
	"hex",
	"bin",
	"block",
	"stacktrace",
	"sched-tree",
	"trace_printk",
	"ftrace_preempt",
	"branch",
	"annotate",
	"userstacktrace",
	"sym-userobj",
	"printk-msg-only",
	"context-info",
	"latency-format",
	"global-clock",
	"sleep-time",
	NULL
};

/*
 * ftrace_max_lock is used to protect the swapping of buffers
 * when taking a max snapshot. The buffers themselves are
 * protected by per_cpu spinlocks. But the action of the swap
 * needs its own lock.
 *
 * This is defined as a raw_spinlock_t in order to help
 * with performance when lockdep debugging is enabled.
 */
static raw_spinlock_t ftrace_max_lock =
	(raw_spinlock_t)__RAW_SPIN_LOCK_UNLOCKED;

/*
 * Copy the new maximum trace into the separate maximum-trace
 * structure. (this way the maximum trace is permanently saved,
 * for later retrieval via /debugfs/tracing/latency_trace)
 */
static void
__update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	struct trace_array_cpu *data = tr->data[cpu];

	max_tr.cpu = cpu;
	max_tr.time_start = data->preempt_timestamp;

	data = max_tr.data[cpu];
	data->saved_latency = tracing_max_latency;

	memcpy(data->comm, tsk->comm, TASK_COMM_LEN);
	data->pid = tsk->pid;
	data->uid = task_uid(tsk);
	data->nice = tsk->static_prio - 20 - MAX_RT_PRIO;
	data->policy = tsk->policy;
	data->rt_priority = tsk->rt_priority;

	/* record this tasks comm */
	tracing_record_cmdline(tsk);
}

ssize_t trace_seq_to_user(struct trace_seq *s, char __user *ubuf, size_t cnt)
{
	int len;
	int ret;

	if (!cnt)
		return 0;

	if (s->len <= s->readpos)
		return -EBUSY;

	len = s->len - s->readpos;
	if (cnt > len)
		cnt = len;
	ret = copy_to_user(ubuf, s->buffer + s->readpos, cnt);
	if (ret == cnt)
		return -EFAULT;

	cnt -= ret;

	s->readpos += cnt;
	return cnt;
}

static ssize_t trace_seq_to_buffer(struct trace_seq *s, void *buf, size_t cnt)
{
	int len;
	void *ret;

	if (s->len <= s->readpos)
		return -EBUSY;

	len = s->len - s->readpos;
	if (cnt > len)
		cnt = len;
	ret = memcpy(buf, s->buffer + s->readpos, cnt);
	if (!ret)
		return -EFAULT;

	s->readpos += cnt;
	return cnt;
}

static void
trace_print_seq(struct seq_file *m, struct trace_seq *s)
{
	int len = s->len >= PAGE_SIZE ? PAGE_SIZE - 1 : s->len;

	s->buffer[len] = 0;
	seq_puts(m, s->buffer);

	trace_seq_init(s);
}

/**
 * update_max_tr - snapshot all trace buffers from global_trace to max_tr
 * @tr: tracer
 * @tsk: the task with the latency
 * @cpu: The cpu that initiated the trace.
 *
 * Flip the buffers between the @tr and the max_tr and record information
 * about which task was the cause of this latency.
 */
void
update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	struct ring_buffer *buf = tr->buffer;

	WARN_ON_ONCE(!irqs_disabled());
	__raw_spin_lock(&ftrace_max_lock);

	tr->buffer = max_tr.buffer;
	max_tr.buffer = buf;

	ftrace_disable_cpu();
	ring_buffer_reset(tr->buffer);
	ftrace_enable_cpu();

	__update_max_tr(tr, tsk, cpu);
	__raw_spin_unlock(&ftrace_max_lock);
}

/**
 * update_max_tr_single - only copy one trace over, and reset the rest
 * @tr - tracer
 * @tsk - task with the latency
 * @cpu - the cpu of the buffer to copy.
 *
 * Flip the trace of a single CPU buffer between the @tr and the max_tr.
 */
void
update_max_tr_single(struct trace_array *tr, struct task_struct *tsk, int cpu)
{
	int ret;

	WARN_ON_ONCE(!irqs_disabled());
	__raw_spin_lock(&ftrace_max_lock);

	ftrace_disable_cpu();

	ring_buffer_reset(max_tr.buffer);
	ret = ring_buffer_swap_cpu(max_tr.buffer, tr->buffer, cpu);

	ftrace_enable_cpu();

	WARN_ON_ONCE(ret && ret != -EAGAIN);

	__update_max_tr(tr, tsk, cpu);
	__raw_spin_unlock(&ftrace_max_lock);
}

/**
 * register_tracer - register a tracer with the ftrace system.
 * @type - the plugin for the tracer
 *
 * Register a new plugin tracer.
 */
int register_tracer(struct tracer *type)
__releases(kernel_lock)
__acquires(kernel_lock)
{
	struct tracer *t;
	int len;
	int ret = 0;

	if (!type->name) {
		pr_info("Tracer must have a name\n");
		return -1;
	}

	/*
	 * When this gets called we hold the BKL which means that
	 * preemption is disabled. Various trace selftests however
	 * need to disable and enable preemption for successful tests.
	 * So we drop the BKL here and grab it after the tests again.
	 */
	unlock_kernel();
	mutex_lock(&trace_types_lock);

	tracing_selftest_running = true;

	for (t = trace_types; t; t = t->next) {
		if (strcmp(type->name, t->name) == 0) {
			/* already found */
			pr_info("Trace %s already registered\n",
				type->name);
			ret = -1;
			goto out;
		}
	}

	if (!type->set_flag)
		type->set_flag = &dummy_set_flag;
	if (!type->flags)
		type->flags = &dummy_tracer_flags;
	else
		if (!type->flags->opts)
			type->flags->opts = dummy_tracer_opt;
	if (!type->wait_pipe)
		type->wait_pipe = default_wait_pipe;


#ifdef CONFIG_FTRACE_STARTUP_TEST
	if (type->selftest && !tracing_selftest_disabled) {
		struct tracer *saved_tracer = current_trace;
		struct trace_array *tr = &global_trace;
		int i;

		/*
		 * Run a selftest on this tracer.
		 * Here we reset the trace buffer, and set the current
		 * tracer to be this tracer. The tracer can then run some
		 * internal tracing to verify that everything is in order.
		 * If we fail, we do not register this tracer.
		 */
		for_each_tracing_cpu(i)
			tracing_reset(tr, i);

		current_trace = type;
		/* the test is responsible for initializing and enabling */
		pr_info("Testing tracer %s: ", type->name);
		ret = type->selftest(type, tr);
		/* the test is responsible for resetting too */
		current_trace = saved_tracer;
		if (ret) {
			printk(KERN_CONT "FAILED!\n");
			goto out;
		}
		/* Only reset on passing, to avoid touching corrupted buffers */
		for_each_tracing_cpu(i)
			tracing_reset(tr, i);

		printk(KERN_CONT "PASSED\n");
	}
#endif

	type->next = trace_types;
	trace_types = type;
	len = strlen(type->name);
	if (len > max_tracer_type_len)
		max_tracer_type_len = len;

 out:
	tracing_selftest_running = false;
	mutex_unlock(&trace_types_lock);

	if (ret || !default_bootup_tracer)
		goto out_unlock;

	if (strncmp(default_bootup_tracer, type->name, BOOTUP_TRACER_SIZE))
		goto out_unlock;

	printk(KERN_INFO "Starting tracer '%s'\n", type->name);
	/* Do we want this tracer to start on bootup? */
	tracing_set_tracer(type->name);
	default_bootup_tracer = NULL;
	/* disable other selftests, since this will break it. */
	tracing_selftest_disabled = 1;
#ifdef CONFIG_FTRACE_STARTUP_TEST
	printk(KERN_INFO "Disabling FTRACE selftests due to running tracer '%s'\n",
	       type->name);
#endif

 out_unlock:
	lock_kernel();
	return ret;
}

void unregister_tracer(struct tracer *type)
{
	struct tracer **t;
	int len;

	mutex_lock(&trace_types_lock);
	for (t = &trace_types; *t; t = &(*t)->next) {
		if (*t == type)
			goto found;
	}
	pr_info("Trace %s not registered\n", type->name);
	goto out;

 found:
	*t = (*t)->next;

	if (type == current_trace && tracer_enabled) {
		tracer_enabled = 0;
		tracing_stop();
		if (current_trace->stop)
			current_trace->stop(&global_trace);
		current_trace = &nop_trace;
	}

	if (strlen(type->name) != max_tracer_type_len)
		goto out;

	max_tracer_type_len = 0;
	for (t = &trace_types; *t; t = &(*t)->next) {
		len = strlen((*t)->name);
		if (len > max_tracer_type_len)
			max_tracer_type_len = len;
	}
 out:
	mutex_unlock(&trace_types_lock);
}

void tracing_reset(struct trace_array *tr, int cpu)
{
	ftrace_disable_cpu();
	ring_buffer_reset_cpu(tr->buffer, cpu);
	ftrace_enable_cpu();
}

void tracing_reset_online_cpus(struct trace_array *tr)
{
	int cpu;

	tr->time_start = ftrace_now(tr->cpu);

	for_each_online_cpu(cpu)
		tracing_reset(tr, cpu);
}

#define SAVED_CMDLINES 128
#define NO_CMDLINE_MAP UINT_MAX
static unsigned map_pid_to_cmdline[PID_MAX_DEFAULT+1];
static unsigned map_cmdline_to_pid[SAVED_CMDLINES];
static char saved_cmdlines[SAVED_CMDLINES][TASK_COMM_LEN];
static int cmdline_idx;
static raw_spinlock_t trace_cmdline_lock = __RAW_SPIN_LOCK_UNLOCKED;

/* temporary disable recording */
static atomic_t trace_record_cmdline_disabled __read_mostly;

static void trace_init_cmdlines(void)
{
	memset(&map_pid_to_cmdline, NO_CMDLINE_MAP, sizeof(map_pid_to_cmdline));
	memset(&map_cmdline_to_pid, NO_CMDLINE_MAP, sizeof(map_cmdline_to_pid));
	cmdline_idx = 0;
}

static int trace_stop_count;
static DEFINE_SPINLOCK(tracing_start_lock);

/**
 * ftrace_off_permanent - disable all ftrace code permanently
 *
 * This should only be called when a serious anomally has
 * been detected.  This will turn off the function tracing,
 * ring buffers, and other tracing utilites. It takes no
 * locks and can be called from any context.
 */
void ftrace_off_permanent(void)
{
	tracing_disabled = 1;
	ftrace_stop();
	tracing_off_permanent();
}

/**
 * tracing_start - quick start of the tracer
 *
 * If tracing is enabled but was stopped by tracing_stop,
 * this will start the tracer back up.
 */
void tracing_start(void)
{
	struct ring_buffer *buffer;
	unsigned long flags;

	if (tracing_disabled)
		return;

	spin_lock_irqsave(&tracing_start_lock, flags);
	if (--trace_stop_count) {
		if (trace_stop_count < 0) {
			/* Someone screwed up their debugging */
			WARN_ON_ONCE(1);
			trace_stop_count = 0;
		}
		goto out;
	}


	buffer = global_trace.buffer;
	if (buffer)
		ring_buffer_record_enable(buffer);

	buffer = max_tr.buffer;
	if (buffer)
		ring_buffer_record_enable(buffer);

	ftrace_start();
 out:
	spin_unlock_irqrestore(&tracing_start_lock, flags);
}

/**
 * tracing_stop - quick stop of the tracer
 *
 * Light weight way to stop tracing. Use in conjunction with
 * tracing_start.
 */
void tracing_stop(void)
{
	struct ring_buffer *buffer;
	unsigned long flags;

	ftrace_stop();
	spin_lock_irqsave(&tracing_start_lock, flags);
	if (trace_stop_count++)
		goto out;

	buffer = global_trace.buffer;
	if (buffer)
		ring_buffer_record_disable(buffer);

	buffer = max_tr.buffer;
	if (buffer)
		ring_buffer_record_disable(buffer);

 out:
	spin_unlock_irqrestore(&tracing_start_lock, flags);
}

void trace_stop_cmdline_recording(void);

static void trace_save_cmdline(struct task_struct *tsk)
{
	unsigned pid, idx;

	if (!tsk->pid || unlikely(tsk->pid > PID_MAX_DEFAULT))
		return;

	/*
	 * It's not the end of the world if we don't get
	 * the lock, but we also don't want to spin
	 * nor do we want to disable interrupts,
	 * so if we miss here, then better luck next time.
	 */
	if (!__raw_spin_trylock(&trace_cmdline_lock))
		return;

	idx = map_pid_to_cmdline[tsk->pid];
	if (idx == NO_CMDLINE_MAP) {
		idx = (cmdline_idx + 1) % SAVED_CMDLINES;

		/*
		 * Check whether the cmdline buffer at idx has a pid
		 * mapped. We are going to overwrite that entry so we
		 * need to clear the map_pid_to_cmdline. Otherwise we
		 * would read the new comm for the old pid.
		 */
		pid = map_cmdline_to_pid[idx];
		if (pid != NO_CMDLINE_MAP)
			map_pid_to_cmdline[pid] = NO_CMDLINE_MAP;

		map_cmdline_to_pid[idx] = tsk->pid;
		map_pid_to_cmdline[tsk->pid] = idx;

		cmdline_idx = idx;
	}

	memcpy(&saved_cmdlines[idx], tsk->comm, TASK_COMM_LEN);

	__raw_spin_unlock(&trace_cmdline_lock);
}

void trace_find_cmdline(int pid, char comm[])
{
	unsigned map;

	if (!pid) {
		strcpy(comm, "<idle>");
		return;
	}

	if (pid > PID_MAX_DEFAULT) {
		strcpy(comm, "<...>");
		return;
	}

	__raw_spin_lock(&trace_cmdline_lock);
	map = map_pid_to_cmdline[pid];
	if (map != NO_CMDLINE_MAP)
		strcpy(comm, saved_cmdlines[map]);
	else
		strcpy(comm, "<...>");

	__raw_spin_unlock(&trace_cmdline_lock);
}

void tracing_record_cmdline(struct task_struct *tsk)
{
	if (atomic_read(&trace_record_cmdline_disabled) || !tracer_enabled ||
	    !tracing_is_on())
		return;

	trace_save_cmdline(tsk);
}

void
tracing_generic_entry_update(struct trace_entry *entry, unsigned long flags,
			     int pc)
{
	struct task_struct *tsk = current;

	entry->preempt_count		= pc & 0xff;
	entry->pid			= (tsk) ? tsk->pid : 0;
	entry->tgid			= (tsk) ? tsk->tgid : 0;
	entry->flags =
#ifdef CONFIG_TRACE_IRQFLAGS_SUPPORT
		(irqs_disabled_flags(flags) ? TRACE_FLAG_IRQS_OFF : 0) |
#else
		TRACE_FLAG_IRQS_NOSUPPORT |
#endif
		((pc & HARDIRQ_MASK) ? TRACE_FLAG_HARDIRQ : 0) |
		((pc & SOFTIRQ_MASK) ? TRACE_FLAG_SOFTIRQ : 0) |
		(need_resched() ? TRACE_FLAG_NEED_RESCHED : 0);
}

struct ring_buffer_event *trace_buffer_lock_reserve(struct trace_array *tr,
						    unsigned char type,
						    unsigned long len,
						    unsigned long flags, int pc)
{
	struct ring_buffer_event *event;

	event = ring_buffer_lock_reserve(tr->buffer, len);
	if (event != NULL) {
		struct trace_entry *ent = ring_buffer_event_data(event);

		tracing_generic_entry_update(ent, flags, pc);
		ent->type = type;
	}

	return event;
}
static void ftrace_trace_stack(struct trace_array *tr,
			       unsigned long flags, int skip, int pc);
static void ftrace_trace_userstack(struct trace_array *tr,
				   unsigned long flags, int pc);

static inline void __trace_buffer_unlock_commit(struct trace_array *tr,
					struct ring_buffer_event *event,
					unsigned long flags, int pc,
					int wake)
{
	ring_buffer_unlock_commit(tr->buffer, event);

	ftrace_trace_stack(tr, flags, 6, pc);
	ftrace_trace_userstack(tr, flags, pc);

	if (wake)
		trace_wake_up();
}

void trace_buffer_unlock_commit(struct trace_array *tr,
					struct ring_buffer_event *event,
					unsigned long flags, int pc)
{
	__trace_buffer_unlock_commit(tr, event, flags, pc, 1);
}

struct ring_buffer_event *
trace_current_buffer_lock_reserve(unsigned char type, unsigned long len,
				  unsigned long flags, int pc)
{
	return trace_buffer_lock_reserve(&global_trace,
					 type, len, flags, pc);
}

void trace_current_buffer_unlock_commit(struct ring_buffer_event *event,
					unsigned long flags, int pc)
{
	return __trace_buffer_unlock_commit(&global_trace, event, flags, pc, 1);
}

void trace_nowake_buffer_unlock_commit(struct ring_buffer_event *event,
					unsigned long flags, int pc)
{
	return __trace_buffer_unlock_commit(&global_trace, event, flags, pc, 0);
}

void
trace_function(struct trace_array *tr,
	       unsigned long ip, unsigned long parent_ip, unsigned long flags,
	       int pc)
{
	struct ring_buffer_event *event;
	struct ftrace_entry *entry;

	/* If we are reading the ring buffer, don't trace */
	if (unlikely(local_read(&__get_cpu_var(ftrace_cpu_disabled))))
		return;

	event = trace_buffer_lock_reserve(tr, TRACE_FN, sizeof(*entry),
					  flags, pc);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->ip			= ip;
	entry->parent_ip		= parent_ip;
	ring_buffer_unlock_commit(tr->buffer, event);
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static int __trace_graph_entry(struct trace_array *tr,
				struct ftrace_graph_ent *trace,
				unsigned long flags,
				int pc)
{
	struct ring_buffer_event *event;
	struct ftrace_graph_ent_entry *entry;

	if (unlikely(local_read(&__get_cpu_var(ftrace_cpu_disabled))))
		return 0;

	event = trace_buffer_lock_reserve(&global_trace, TRACE_GRAPH_ENT,
					  sizeof(*entry), flags, pc);
	if (!event)
		return 0;
	entry	= ring_buffer_event_data(event);
	entry->graph_ent			= *trace;
	ring_buffer_unlock_commit(global_trace.buffer, event);

	return 1;
}

static void __trace_graph_return(struct trace_array *tr,
				struct ftrace_graph_ret *trace,
				unsigned long flags,
				int pc)
{
	struct ring_buffer_event *event;
	struct ftrace_graph_ret_entry *entry;

	if (unlikely(local_read(&__get_cpu_var(ftrace_cpu_disabled))))
		return;

	event = trace_buffer_lock_reserve(&global_trace, TRACE_GRAPH_RET,
					  sizeof(*entry), flags, pc);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->ret				= *trace;
	ring_buffer_unlock_commit(global_trace.buffer, event);
}
#endif

void
ftrace(struct trace_array *tr, struct trace_array_cpu *data,
       unsigned long ip, unsigned long parent_ip, unsigned long flags,
       int pc)
{
	if (likely(!atomic_read(&data->disabled)))
		trace_function(tr, ip, parent_ip, flags, pc);
}

static void __ftrace_trace_stack(struct trace_array *tr,
				 unsigned long flags,
				 int skip, int pc)
{
#ifdef CONFIG_STACKTRACE
	struct ring_buffer_event *event;
	struct stack_entry *entry;
	struct stack_trace trace;

	event = trace_buffer_lock_reserve(tr, TRACE_STACK,
					  sizeof(*entry), flags, pc);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	memset(&entry->caller, 0, sizeof(entry->caller));

	trace.nr_entries	= 0;
	trace.max_entries	= FTRACE_STACK_ENTRIES;
	trace.skip		= skip;
	trace.entries		= entry->caller;

	save_stack_trace(&trace);
	ring_buffer_unlock_commit(tr->buffer, event);
#endif
}

static void ftrace_trace_stack(struct trace_array *tr,
			       unsigned long flags,
			       int skip, int pc)
{
	if (!(trace_flags & TRACE_ITER_STACKTRACE))
		return;

	__ftrace_trace_stack(tr, flags, skip, pc);
}

void __trace_stack(struct trace_array *tr,
		   unsigned long flags,
		   int skip, int pc)
{
	__ftrace_trace_stack(tr, flags, skip, pc);
}

static void ftrace_trace_userstack(struct trace_array *tr,
				   unsigned long flags, int pc)
{
#ifdef CONFIG_STACKTRACE
	struct ring_buffer_event *event;
	struct userstack_entry *entry;
	struct stack_trace trace;

	if (!(trace_flags & TRACE_ITER_USERSTACKTRACE))
		return;

	event = trace_buffer_lock_reserve(tr, TRACE_USER_STACK,
					  sizeof(*entry), flags, pc);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);

	memset(&entry->caller, 0, sizeof(entry->caller));

	trace.nr_entries	= 0;
	trace.max_entries	= FTRACE_STACK_ENTRIES;
	trace.skip		= 0;
	trace.entries		= entry->caller;

	save_stack_trace_user(&trace);
	ring_buffer_unlock_commit(tr->buffer, event);
#endif
}

#ifdef UNUSED
static void __trace_userstack(struct trace_array *tr, unsigned long flags)
{
	ftrace_trace_userstack(tr, flags, preempt_count());
}
#endif /* UNUSED */

static void
ftrace_trace_special(void *__tr,
		     unsigned long arg1, unsigned long arg2, unsigned long arg3,
		     int pc)
{
	struct ring_buffer_event *event;
	struct trace_array *tr = __tr;
	struct special_entry *entry;

	event = trace_buffer_lock_reserve(tr, TRACE_SPECIAL,
					  sizeof(*entry), 0, pc);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->arg1			= arg1;
	entry->arg2			= arg2;
	entry->arg3			= arg3;
	trace_buffer_unlock_commit(tr, event, 0, pc);
}

void
__trace_special(void *__tr, void *__data,
		unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
	ftrace_trace_special(__tr, arg1, arg2, arg3, preempt_count());
}

void
tracing_sched_switch_trace(struct trace_array *tr,
			   struct task_struct *prev,
			   struct task_struct *next,
			   unsigned long flags, int pc)
{
	struct ring_buffer_event *event;
	struct ctx_switch_entry *entry;

	event = trace_buffer_lock_reserve(tr, TRACE_CTX,
					  sizeof(*entry), flags, pc);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->prev_pid			= prev->pid;
	entry->prev_prio		= prev->prio;
	entry->prev_state		= prev->state;
	entry->next_pid			= next->pid;
	entry->next_prio		= next->prio;
	entry->next_state		= next->state;
	entry->next_cpu	= task_cpu(next);
	trace_buffer_unlock_commit(tr, event, flags, pc);
}

void
tracing_sched_wakeup_trace(struct trace_array *tr,
			   struct task_struct *wakee,
			   struct task_struct *curr,
			   unsigned long flags, int pc)
{
	struct ring_buffer_event *event;
	struct ctx_switch_entry *entry;

	event = trace_buffer_lock_reserve(tr, TRACE_WAKE,
					  sizeof(*entry), flags, pc);
	if (!event)
		return;
	entry	= ring_buffer_event_data(event);
	entry->prev_pid			= curr->pid;
	entry->prev_prio		= curr->prio;
	entry->prev_state		= curr->state;
	entry->next_pid			= wakee->pid;
	entry->next_prio		= wakee->prio;
	entry->next_state		= wakee->state;
	entry->next_cpu			= task_cpu(wakee);

	ring_buffer_unlock_commit(tr->buffer, event);
	ftrace_trace_stack(tr, flags, 6, pc);
	ftrace_trace_userstack(tr, flags, pc);
}

void
ftrace_special(unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	int cpu;
	int pc;

	if (tracing_disabled)
		return;

	pc = preempt_count();
	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];

	if (likely(atomic_inc_return(&data->disabled) == 1))
		ftrace_trace_special(tr, arg1, arg2, arg3, pc);

	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
int trace_graph_entry(struct ftrace_graph_ent *trace)
{
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int ret;
	int cpu;
	int pc;

	if (!ftrace_trace_task(current))
		return 0;

	if (!ftrace_graph_addr(trace->func))
		return 0;

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);
	if (likely(disabled == 1)) {
		pc = preempt_count();
		ret = __trace_graph_entry(tr, trace, flags, pc);
	} else {
		ret = 0;
	}
	/* Only do the atomic if it is not already set */
	if (!test_tsk_trace_graph(current))
		set_tsk_trace_graph(current);

	atomic_dec(&data->disabled);
	local_irq_restore(flags);

	return ret;
}

void trace_graph_return(struct ftrace_graph_ret *trace)
{
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	unsigned long flags;
	long disabled;
	int cpu;
	int pc;

	local_irq_save(flags);
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];
	disabled = atomic_inc_return(&data->disabled);
	if (likely(disabled == 1)) {
		pc = preempt_count();
		__trace_graph_return(tr, trace, flags, pc);
	}
	if (!trace->depth)
		clear_tsk_trace_graph(current);
	atomic_dec(&data->disabled);
	local_irq_restore(flags);
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */


/**
 * trace_vbprintk - write binary msg to tracing buffer
 *
 */
int trace_vbprintk(unsigned long ip, const char *fmt, va_list args)
{
	static raw_spinlock_t trace_buf_lock =
		(raw_spinlock_t)__RAW_SPIN_LOCK_UNLOCKED;
	static u32 trace_buf[TRACE_BUF_SIZE];

	struct ring_buffer_event *event;
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	struct bprint_entry *entry;
	unsigned long flags;
	int resched;
	int cpu, len = 0, size, pc;

	if (unlikely(tracing_selftest_running || tracing_disabled))
		return 0;

	/* Don't pollute graph traces with trace_vprintk internals */
	pause_graph_tracing();

	pc = preempt_count();
	resched = ftrace_preempt_disable();
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];

	if (unlikely(atomic_read(&data->disabled)))
		goto out;

	/* Lockdep uses trace_printk for lock tracing */
	local_irq_save(flags);
	__raw_spin_lock(&trace_buf_lock);
	len = vbin_printf(trace_buf, TRACE_BUF_SIZE, fmt, args);

	if (len > TRACE_BUF_SIZE || len < 0)
		goto out_unlock;

	size = sizeof(*entry) + sizeof(u32) * len;
	event = trace_buffer_lock_reserve(tr, TRACE_BPRINT, size, flags, pc);
	if (!event)
		goto out_unlock;
	entry = ring_buffer_event_data(event);
	entry->ip			= ip;
	entry->fmt			= fmt;

	memcpy(entry->buf, trace_buf, sizeof(u32) * len);
	ring_buffer_unlock_commit(tr->buffer, event);

out_unlock:
	__raw_spin_unlock(&trace_buf_lock);
	local_irq_restore(flags);

out:
	ftrace_preempt_enable(resched);
	unpause_graph_tracing();

	return len;
}
EXPORT_SYMBOL_GPL(trace_vbprintk);

int trace_vprintk(unsigned long ip, const char *fmt, va_list args)
{
	static raw_spinlock_t trace_buf_lock = __RAW_SPIN_LOCK_UNLOCKED;
	static char trace_buf[TRACE_BUF_SIZE];

	struct ring_buffer_event *event;
	struct trace_array *tr = &global_trace;
	struct trace_array_cpu *data;
	int cpu, len = 0, size, pc;
	struct print_entry *entry;
	unsigned long irq_flags;

	if (tracing_disabled || tracing_selftest_running)
		return 0;

	pc = preempt_count();
	preempt_disable_notrace();
	cpu = raw_smp_processor_id();
	data = tr->data[cpu];

	if (unlikely(atomic_read(&data->disabled)))
		goto out;

	pause_graph_tracing();
	raw_local_irq_save(irq_flags);
	__raw_spin_lock(&trace_buf_lock);
	len = vsnprintf(trace_buf, TRACE_BUF_SIZE, fmt, args);

	len = min(len, TRACE_BUF_SIZE-1);
	trace_buf[len] = 0;

	size = sizeof(*entry) + len + 1;
	event = trace_buffer_lock_reserve(tr, TRACE_PRINT, size, irq_flags, pc);
	if (!event)
		goto out_unlock;
	entry = ring_buffer_event_data(event);
	entry->ip			= ip;

	memcpy(&entry->buf, trace_buf, len);
	entry->buf[len] = 0;
	ring_buffer_unlock_commit(tr->buffer, event);

 out_unlock:
	__raw_spin_unlock(&trace_buf_lock);
	raw_local_irq_restore(irq_flags);
	unpause_graph_tracing();
 out:
	preempt_enable_notrace();

	return len;
}
EXPORT_SYMBOL_GPL(trace_vprintk);

enum trace_file_type {
	TRACE_FILE_LAT_FMT	= 1,
	TRACE_FILE_ANNOTATE	= 2,
};

static void trace_iterator_increment(struct trace_iterator *iter)
{
	/* Don't allow ftrace to trace into the ring buffers */
	ftrace_disable_cpu();

	iter->idx++;
	if (iter->buffer_iter[iter->cpu])
		ring_buffer_read(iter->buffer_iter[iter->cpu], NULL);

	ftrace_enable_cpu();
}

static struct trace_entry *
peek_next_entry(struct trace_iterator *iter, int cpu, u64 *ts)
{
	struct ring_buffer_event *event;
	struct ring_buffer_iter *buf_iter = iter->buffer_iter[cpu];

	/* Don't allow ftrace to trace into the ring buffers */
	ftrace_disable_cpu();

	if (buf_iter)
		event = ring_buffer_iter_peek(buf_iter, ts);
	else
		event = ring_buffer_peek(iter->tr->buffer, cpu, ts);

	ftrace_enable_cpu();

	return event ? ring_buffer_event_data(event) : NULL;
}

static struct trace_entry *
__find_next_entry(struct trace_iterator *iter, int *ent_cpu, u64 *ent_ts)
{
	struct ring_buffer *buffer = iter->tr->buffer;
	struct trace_entry *ent, *next = NULL;
	int cpu_file = iter->cpu_file;
	u64 next_ts = 0, ts;
	int next_cpu = -1;
	int cpu;

	/*
	 * If we are in a per_cpu trace file, don't bother by iterating over
	 * all cpu and peek directly.
	 */
	if (cpu_file > TRACE_PIPE_ALL_CPU) {
		if (ring_buffer_empty_cpu(buffer, cpu_file))
			return NULL;
		ent = peek_next_entry(iter, cpu_file, ent_ts);
		if (ent_cpu)
			*ent_cpu = cpu_file;

		return ent;
	}

	for_each_tracing_cpu(cpu) {

		if (ring_buffer_empty_cpu(buffer, cpu))
			continue;

		ent = peek_next_entry(iter, cpu, &ts);

		/*
		 * Pick the entry with the smallest timestamp:
		 */
		if (ent && (!next || ts < next_ts)) {
			next = ent;
			next_cpu = cpu;
			next_ts = ts;
		}
	}

	if (ent_cpu)
		*ent_cpu = next_cpu;

	if (ent_ts)
		*ent_ts = next_ts;

	return next;
}

/* Find the next real entry, without updating the iterator itself */
struct trace_entry *trace_find_next_entry(struct trace_iterator *iter,
					  int *ent_cpu, u64 *ent_ts)
{
	return __find_next_entry(iter, ent_cpu, ent_ts);
}

/* Find the next real entry, and increment the iterator to the next entry */
static void *find_next_entry_inc(struct trace_iterator *iter)
{
	iter->ent = __find_next_entry(iter, &iter->cpu, &iter->ts);

	if (iter->ent)
		trace_iterator_increment(iter);

	return iter->ent ? iter : NULL;
}

static void trace_consume(struct trace_iterator *iter)
{
	/* Don't allow ftrace to trace into the ring buffers */
	ftrace_disable_cpu();
	ring_buffer_consume(iter->tr->buffer, iter->cpu, &iter->ts);
	ftrace_enable_cpu();
}

static void *s_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct trace_iterator *iter = m->private;
	int i = (int)*pos;
	void *ent;

	(*pos)++;

	/* can't go backwards */
	if (iter->idx > i)
		return NULL;

	if (iter->idx < 0)
		ent = find_next_entry_inc(iter);
	else
		ent = iter;

	while (ent && iter->idx < i)
		ent = find_next_entry_inc(iter);

	iter->pos = *pos;

	return ent;
}

/*
 * No necessary locking here. The worst thing which can
 * happen is loosing events consumed at the same time
 * by a trace_pipe reader.
 * Other than that, we don't risk to crash the ring buffer
 * because it serializes the readers.
 *
 * The current tracer is copied to avoid a global locking
 * all around.
 */
static void *s_start(struct seq_file *m, loff_t *pos)
{
	struct trace_iterator *iter = m->private;
	static struct tracer *old_tracer;
	int cpu_file = iter->cpu_file;
	void *p = NULL;
	loff_t l = 0;
	int cpu;

	/* copy the tracer to avoid using a global lock all around */
	mutex_lock(&trace_types_lock);
	if (unlikely(old_tracer != current_trace && current_trace)) {
		old_tracer = current_trace;
		*iter->trace = *current_trace;
	}
	mutex_unlock(&trace_types_lock);

	atomic_inc(&trace_record_cmdline_disabled);

	if (*pos != iter->pos) {
		iter->ent = NULL;
		iter->cpu = 0;
		iter->idx = -1;

		ftrace_disable_cpu();

		if (cpu_file == TRACE_PIPE_ALL_CPU) {
			for_each_tracing_cpu(cpu)
				ring_buffer_iter_reset(iter->buffer_iter[cpu]);
		} else
			ring_buffer_iter_reset(iter->buffer_iter[cpu_file]);


		ftrace_enable_cpu();

		for (p = iter; p && l < *pos; p = s_next(m, p, &l))
			;

	} else {
		l = *pos - 1;
		p = s_next(m, p, &l);
	}

	return p;
}

static void s_stop(struct seq_file *m, void *p)
{
	atomic_dec(&trace_record_cmdline_disabled);
}

static void print_lat_help_header(struct seq_file *m)
{
	seq_puts(m, "#                  _------=> CPU#            \n");
	seq_puts(m, "#                 / _-----=> irqs-off        \n");
	seq_puts(m, "#                | / _----=> need-resched    \n");
	seq_puts(m, "#                || / _---=> hardirq/softirq \n");
	seq_puts(m, "#                ||| / _--=> preempt-depth   \n");
	seq_puts(m, "#                |||| /                      \n");
	seq_puts(m, "#                |||||     delay             \n");
	seq_puts(m, "#  cmd     pid   ||||| time  |   caller      \n");
	seq_puts(m, "#     \\   /      |||||   \\   |   /           \n");
}

static void print_func_help_header(struct seq_file *m)
{
	seq_puts(m, "#           TASK-PID    CPU#    TIMESTAMP  FUNCTION\n");
	seq_puts(m, "#              | |       |          |         |\n");
}


static void
print_trace_header(struct seq_file *m, struct trace_iterator *iter)
{
	unsigned long sym_flags = (trace_flags & TRACE_ITER_SYM_MASK);
	struct trace_array *tr = iter->tr;
	struct trace_array_cpu *data = tr->data[tr->cpu];
	struct tracer *type = current_trace;
	unsigned long total;
	unsigned long entries;
	const char *name = "preemption";

	if (type)
		name = type->name;

	entries = ring_buffer_entries(iter->tr->buffer);
	total = entries +
		ring_buffer_overruns(iter->tr->buffer);

	seq_printf(m, "# %s latency trace v1.1.5 on %s\n",
		   name, UTS_RELEASE);
	seq_puts(m, "# -----------------------------------"
		 "---------------------------------\n");
	seq_printf(m, "# latency: %lu us, #%lu/%lu, CPU#%d |"
		   " (M:%s VP:%d, KP:%d, SP:%d HP:%d",
		   nsecs_to_usecs(data->saved_latency),
		   entries,
		   total,
		   tr->cpu,
#if defined(CONFIG_PREEMPT_NONE)
		   "server",
#elif defined(CONFIG_PREEMPT_VOLUNTARY)
		   "desktop",
#elif defined(CONFIG_PREEMPT)
		   "preempt",
#else
		   "unknown",
#endif
		   /* These are reserved for later use */
		   0, 0, 0, 0);
#ifdef CONFIG_SMP
	seq_printf(m, " #P:%d)\n", num_online_cpus());
#else
	seq_puts(m, ")\n");
#endif
	seq_puts(m, "#    -----------------\n");
	seq_printf(m, "#    | task: %.16s-%d "
		   "(uid:%d nice:%ld policy:%ld rt_prio:%ld)\n",
		   data->comm, data->pid, data->uid, data->nice,
		   data->policy, data->rt_priority);
	seq_puts(m, "#    -----------------\n");

	if (data->critical_start) {
		seq_puts(m, "#  => started at: ");
		seq_print_ip_sym(&iter->seq, data->critical_start, sym_flags);
		trace_print_seq(m, &iter->seq);
		seq_puts(m, "\n#  => ended at:   ");
		seq_print_ip_sym(&iter->seq, data->critical_end, sym_flags);
		trace_print_seq(m, &iter->seq);
		seq_puts(m, "#\n");
	}

	seq_puts(m, "#\n");
}

static void test_cpu_buff_start(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;

	if (!(trace_flags & TRACE_ITER_ANNOTATE))
		return;

	if (!(iter->iter_flags & TRACE_FILE_ANNOTATE))
		return;

	if (cpumask_test_cpu(iter->cpu, iter->started))
		return;

	cpumask_set_cpu(iter->cpu, iter->started);

	/* Don't print started cpu buffer for the first entry of the trace */
	if (iter->idx > 1)
		trace_seq_printf(s, "##### CPU %u buffer started ####\n",
				iter->cpu);
}

static enum print_line_t print_trace_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	unsigned long sym_flags = (trace_flags & TRACE_ITER_SYM_MASK);
	struct trace_entry *entry;
	struct trace_event *event;

	entry = iter->ent;

	test_cpu_buff_start(iter);

	event = ftrace_find_event(entry->type);

	if (trace_flags & TRACE_ITER_CONTEXT_INFO) {
		if (iter->iter_flags & TRACE_FILE_LAT_FMT) {
			if (!trace_print_lat_context(iter))
				goto partial;
		} else {
			if (!trace_print_context(iter))
				goto partial;
		}
	}

	if (event)
		return event->trace(iter, sym_flags);

	if (!trace_seq_printf(s, "Unknown type %d\n", entry->type))
		goto partial;

	return TRACE_TYPE_HANDLED;
partial:
	return TRACE_TYPE_PARTIAL_LINE;
}

static enum print_line_t print_raw_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry;
	struct trace_event *event;

	entry = iter->ent;

	if (trace_flags & TRACE_ITER_CONTEXT_INFO) {
		if (!trace_seq_printf(s, "%d %d %llu ",
				      entry->pid, iter->cpu, iter->ts))
			goto partial;
	}

	event = ftrace_find_event(entry->type);
	if (event)
		return event->raw(iter, 0);

	if (!trace_seq_printf(s, "%d ?\n", entry->type))
		goto partial;

	return TRACE_TYPE_HANDLED;
partial:
	return TRACE_TYPE_PARTIAL_LINE;
}

static enum print_line_t print_hex_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	unsigned char newline = '\n';
	struct trace_entry *entry;
	struct trace_event *event;

	entry = iter->ent;

	if (trace_flags & TRACE_ITER_CONTEXT_INFO) {
		SEQ_PUT_HEX_FIELD_RET(s, entry->pid);
		SEQ_PUT_HEX_FIELD_RET(s, iter->cpu);
		SEQ_PUT_HEX_FIELD_RET(s, iter->ts);
	}

	event = ftrace_find_event(entry->type);
	if (event) {
		enum print_line_t ret = event->hex(iter, 0);
		if (ret != TRACE_TYPE_HANDLED)
			return ret;
	}

	SEQ_PUT_FIELD_RET(s, newline);

	return TRACE_TYPE_HANDLED;
}

static enum print_line_t print_bin_fmt(struct trace_iterator *iter)
{
	struct trace_seq *s = &iter->seq;
	struct trace_entry *entry;
	struct trace_event *event;

	entry = iter->ent;

	if (trace_flags & TRACE_ITER_CONTEXT_INFO) {
		SEQ_PUT_FIELD_RET(s, entry->pid);
		SEQ_PUT_FIELD_RET(s, iter->cpu);
		SEQ_PUT_FIELD_RET(s, iter->ts);
	}

	event = ftrace_find_event(entry->type);
	return event ? event->binary(iter, 0) : TRACE_TYPE_HANDLED;
}

static int trace_empty(struct trace_iterator *iter)
{
	int cpu;

	/* If we are looking at one CPU buffer, only check that one */
	if (iter->cpu_file != TRACE_PIPE_ALL_CPU) {
		cpu = iter->cpu_file;
		if (iter->buffer_iter[cpu]) {
			if (!ring_buffer_iter_empty(iter->buffer_iter[cpu]))
				return 0;
		} else {
			if (!ring_buffer_empty_cpu(iter->tr->buffer, cpu))
				return 0;
		}
		return 1;
	}

	for_each_tracing_cpu(cpu) {
		if (iter->buffer_iter[cpu]) {
			if (!ring_buffer_iter_empty(iter->buffer_iter[cpu]))
				return 0;
		} else {
			if (!ring_buffer_empty_cpu(iter->tr->buffer, cpu))
				return 0;
		}
	}

	return 1;
}

static enum print_line_t print_trace_line(struct trace_iterator *iter)
{
	enum print_line_t ret;

	if (iter->trace && iter->trace->print_line) {
		ret = iter->trace->print_line(iter);
		if (ret != TRACE_TYPE_UNHANDLED)
			return ret;
	}

	if (iter->ent->type == TRACE_BPRINT &&
			trace_flags & TRACE_ITER_PRINTK &&
			trace_flags & TRACE_ITER_PRINTK_MSGONLY)
		return trace_print_bprintk_msg_only(iter);

	if (iter->ent->type == TRACE_PRINT &&
			trace_flags & TRACE_ITER_PRINTK &&
			trace_flags & TRACE_ITER_PRINTK_MSGONLY)
		return trace_print_printk_msg_only(iter);

	if (trace_flags & TRACE_ITER_BIN)
		return print_bin_fmt(iter);

	if (trace_flags & TRACE_ITER_HEX)
		return print_hex_fmt(iter);

	if (trace_flags & TRACE_ITER_RAW)
		return print_raw_fmt(iter);

	return print_trace_fmt(iter);
}

static int s_show(struct seq_file *m, void *v)
{
	struct trace_iterator *iter = v;

	if (iter->ent == NULL) {
		if (iter->tr) {
			seq_printf(m, "# tracer: %s\n", iter->trace->name);
			seq_puts(m, "#\n");
		}
		if (iter->trace && iter->trace->print_header)
			iter->trace->print_header(m);
		else if (iter->iter_flags & TRACE_FILE_LAT_FMT) {
			/* print nothing if the buffers are empty */
			if (trace_empty(iter))
				return 0;
			print_trace_header(m, iter);
			if (!(trace_flags & TRACE_ITER_VERBOSE))
				print_lat_help_header(m);
		} else {
			if (!(trace_flags & TRACE_ITER_VERBOSE))
				print_func_help_header(m);
		}
	} else {
		print_trace_line(iter);
		trace_print_seq(m, &iter->seq);
	}

	return 0;
}

static struct seq_operations tracer_seq_ops = {
	.start		= s_start,
	.next		= s_next,
	.stop		= s_stop,
	.show		= s_show,
};

static struct trace_iterator *
__tracing_open(struct inode *inode, struct file *file)
{
	long cpu_file = (long) inode->i_private;
	void *fail_ret = ERR_PTR(-ENOMEM);
	struct trace_iterator *iter;
	struct seq_file *m;
	int cpu, ret;

	if (tracing_disabled)
		return ERR_PTR(-ENODEV);

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return ERR_PTR(-ENOMEM);

	/*
	 * We make a copy of the current tracer to avoid concurrent
	 * changes on it while we are reading.
	 */
	mutex_lock(&trace_types_lock);
	iter->trace = kzalloc(sizeof(*iter->trace), GFP_KERNEL);
	if (!iter->trace)
		goto fail;

	if (current_trace)
		*iter->trace = *current_trace;

	if (!alloc_cpumask_var(&iter->started, GFP_KERNEL))
		goto fail;

	cpumask_clear(iter->started);

	if (current_trace && current_trace->print_max)
		iter->tr = &max_tr;
	else
		iter->tr = &global_trace;
	iter->pos = -1;
	mutex_init(&iter->mutex);
	iter->cpu_file = cpu_file;

	/* Notify the tracer early; before we stop tracing. */
	if (iter->trace && iter->trace->open)
		iter->trace->open(iter);

	/* Annotate start of buffers if we had overruns */
	if (ring_buffer_overruns(iter->tr->buffer))
		iter->iter_flags |= TRACE_FILE_ANNOTATE;

	if (iter->cpu_file == TRACE_PIPE_ALL_CPU) {
		for_each_tracing_cpu(cpu) {

			iter->buffer_iter[cpu] =
				ring_buffer_read_start(iter->tr->buffer, cpu);
		}
	} else {
		cpu = iter->cpu_file;
		iter->buffer_iter[cpu] =
				ring_buffer_read_start(iter->tr->buffer, cpu);
	}

	/* TODO stop tracer */
	ret = seq_open(file, &tracer_seq_ops);
	if (ret < 0) {
		fail_ret = ERR_PTR(ret);
		goto fail_buffer;
	}

	m = file->private_data;
	m->private = iter;

	/* stop the trace while dumping */
	tracing_stop();

	mutex_unlock(&trace_types_lock);

	return iter;

 fail_buffer:
	for_each_tracing_cpu(cpu) {
		if (iter->buffer_iter[cpu])
			ring_buffer_read_finish(iter->buffer_iter[cpu]);
	}
	free_cpumask_var(iter->started);
 fail:
	mutex_unlock(&trace_types_lock);
	kfree(iter->trace);
	kfree(iter);

	return fail_ret;
}

int tracing_open_generic(struct inode *inode, struct file *filp)
{
	if (tracing_disabled)
		return -ENODEV;

	filp->private_data = inode->i_private;
	return 0;
}

static int tracing_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct trace_iterator *iter;
	int cpu;

	if (!(file->f_mode & FMODE_READ))
		return 0;

	iter = m->private;

	mutex_lock(&trace_types_lock);
	for_each_tracing_cpu(cpu) {
		if (iter->buffer_iter[cpu])
			ring_buffer_read_finish(iter->buffer_iter[cpu]);
	}

	if (iter->trace && iter->trace->close)
		iter->trace->close(iter);

	/* reenable tracing if it was previously enabled */
	tracing_start();
	mutex_unlock(&trace_types_lock);

	seq_release(inode, file);
	mutex_destroy(&iter->mutex);
	free_cpumask_var(iter->started);
	kfree(iter->trace);
	kfree(iter);
	return 0;
}

static int tracing_open(struct inode *inode, struct file *file)
{
	struct trace_iterator *iter;
	int ret = 0;

	/* If this file was open for write, then erase contents */
	if ((file->f_mode & FMODE_WRITE) &&
	    !(file->f_flags & O_APPEND)) {
		long cpu = (long) inode->i_private;

		if (cpu == TRACE_PIPE_ALL_CPU)
			tracing_reset_online_cpus(&global_trace);
		else
			tracing_reset(&global_trace, cpu);
	}

	if (file->f_mode & FMODE_READ) {
		iter = __tracing_open(inode, file);
		if (IS_ERR(iter))
			ret = PTR_ERR(iter);
		else if (trace_flags & TRACE_ITER_LATENCY_FMT)
			iter->iter_flags |= TRACE_FILE_LAT_FMT;
	}
	return ret;
}

static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct tracer *t = m->private;

	(*pos)++;

	if (t)
		t = t->next;

	m->private = t;

	return t;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct tracer *t = m->private;
	loff_t l = 0;

	mutex_lock(&trace_types_lock);
	for (; t && l < *pos; t = t_next(m, t, &l))
		;

	return t;
}

static void t_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&trace_types_lock);
}

static int t_show(struct seq_file *m, void *v)
{
	struct tracer *t = v;

	if (!t)
		return 0;

	seq_printf(m, "%s", t->name);
	if (t->next)
		seq_putc(m, ' ');
	else
		seq_putc(m, '\n');

	return 0;
}

static struct seq_operations show_traces_seq_ops = {
	.start		= t_start,
	.next		= t_next,
	.stop		= t_stop,
	.show		= t_show,
};

static int show_traces_open(struct inode *inode, struct file *file)
{
	int ret;

	if (tracing_disabled)
		return -ENODEV;

	ret = seq_open(file, &show_traces_seq_ops);
	if (!ret) {
		struct seq_file *m = file->private_data;
		m->private = trace_types;
	}

	return ret;
}

static ssize_t
tracing_write_stub(struct file *filp, const char __user *ubuf,
		   size_t count, loff_t *ppos)
{
	return count;
}

static const struct file_operations tracing_fops = {
	.open		= tracing_open,
	.read		= seq_read,
	.write		= tracing_write_stub,
	.llseek		= seq_lseek,
	.release	= tracing_release,
};

static const struct file_operations show_traces_fops = {
	.open		= show_traces_open,
	.read		= seq_read,
	.release	= seq_release,
};

/*
 * Only trace on a CPU if the bitmask is set:
 */
static cpumask_var_t tracing_cpumask;

/*
 * The tracer itself will not take this lock, but still we want
 * to provide a consistent cpumask to user-space:
 */
static DEFINE_MUTEX(tracing_cpumask_update_lock);

/*
 * Temporary storage for the character representation of the
 * CPU bitmask (and one more byte for the newline):
 */
static char mask_str[NR_CPUS + 1];

static ssize_t
tracing_cpumask_read(struct file *filp, char __user *ubuf,
		     size_t count, loff_t *ppos)
{
	int len;

	mutex_lock(&tracing_cpumask_update_lock);

	len = cpumask_scnprintf(mask_str, count, tracing_cpumask);
	if (count - len < 2) {
		count = -EINVAL;
		goto out_err;
	}
	len += sprintf(mask_str + len, "\n");
	count = simple_read_from_buffer(ubuf, count, ppos, mask_str, NR_CPUS+1);

out_err:
	mutex_unlock(&tracing_cpumask_update_lock);

	return count;
}

static ssize_t
tracing_cpumask_write(struct file *filp, const char __user *ubuf,
		      size_t count, loff_t *ppos)
{
	int err, cpu;
	cpumask_var_t tracing_cpumask_new;

	if (!alloc_cpumask_var(&tracing_cpumask_new, GFP_KERNEL))
		return -ENOMEM;

	mutex_lock(&tracing_cpumask_update_lock);
	err = cpumask_parse_user(ubuf, count, tracing_cpumask_new);
	if (err)
		goto err_unlock;

	local_irq_disable();
	__raw_spin_lock(&ftrace_max_lock);
	for_each_tracing_cpu(cpu) {
		/*
		 * Increase/decrease the disabled counter if we are
		 * about to flip a bit in the cpumask:
		 */
		if (cpumask_test_cpu(cpu, tracing_cpumask) &&
				!cpumask_test_cpu(cpu, tracing_cpumask_new)) {
			atomic_inc(&global_trace.data[cpu]->disabled);
		}
		if (!cpumask_test_cpu(cpu, tracing_cpumask) &&
				cpumask_test_cpu(cpu, tracing_cpumask_new)) {
			atomic_dec(&global_trace.data[cpu]->disabled);
		}
	}
	__raw_spin_unlock(&ftrace_max_lock);
	local_irq_enable();

	cpumask_copy(tracing_cpumask, tracing_cpumask_new);

	mutex_unlock(&tracing_cpumask_update_lock);
	free_cpumask_var(tracing_cpumask_new);

	return count;

err_unlock:
	mutex_unlock(&tracing_cpumask_update_lock);
	free_cpumask_var(tracing_cpumask);

	return err;
}

static const struct file_operations tracing_cpumask_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_cpumask_read,
	.write		= tracing_cpumask_write,
};

static ssize_t
tracing_trace_options_read(struct file *filp, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	struct tracer_opt *trace_opts;
	u32 tracer_flags;
	int len = 0;
	char *buf;
	int r = 0;
	int i;


	/* calculate max size */
	for (i = 0; trace_options[i]; i++) {
		len += strlen(trace_options[i]);
		len += 3; /* "no" and newline */
	}

	mutex_lock(&trace_types_lock);
	tracer_flags = current_trace->flags->val;
	trace_opts = current_trace->flags->opts;

	/*
	 * Increase the size with names of options specific
	 * of the current tracer.
	 */
	for (i = 0; trace_opts[i].name; i++) {
		len += strlen(trace_opts[i].name);
		len += 3; /* "no" and newline */
	}

	/* +2 for \n and \0 */
	buf = kmalloc(len + 2, GFP_KERNEL);
	if (!buf) {
		mutex_unlock(&trace_types_lock);
		return -ENOMEM;
	}

	for (i = 0; trace_options[i]; i++) {
		if (trace_flags & (1 << i))
			r += sprintf(buf + r, "%s\n", trace_options[i]);
		else
			r += sprintf(buf + r, "no%s\n", trace_options[i]);
	}

	for (i = 0; trace_opts[i].name; i++) {
		if (tracer_flags & trace_opts[i].bit)
			r += sprintf(buf + r, "%s\n",
				trace_opts[i].name);
		else
			r += sprintf(buf + r, "no%s\n",
				trace_opts[i].name);
	}
	mutex_unlock(&trace_types_lock);

	WARN_ON(r >= len + 2);

	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);

	kfree(buf);
	return r;
}

/* Try to assign a tracer specific option */
static int set_tracer_option(struct tracer *trace, char *cmp, int neg)
{
	struct tracer_flags *trace_flags = trace->flags;
	struct tracer_opt *opts = NULL;
	int ret = 0, i = 0;
	int len;

	for (i = 0; trace_flags->opts[i].name; i++) {
		opts = &trace_flags->opts[i];
		len = strlen(opts->name);

		if (strncmp(cmp, opts->name, len) == 0) {
			ret = trace->set_flag(trace_flags->val,
				opts->bit, !neg);
			break;
		}
	}
	/* Not found */
	if (!trace_flags->opts[i].name)
		return -EINVAL;

	/* Refused to handle */
	if (ret)
		return ret;

	if (neg)
		trace_flags->val &= ~opts->bit;
	else
		trace_flags->val |= opts->bit;

	return 0;
}

static void set_tracer_flags(unsigned int mask, int enabled)
{
	/* do nothing if flag is already set */
	if (!!(trace_flags & mask) == !!enabled)
		return;

	if (enabled)
		trace_flags |= mask;
	else
		trace_flags &= ~mask;

	if (mask == TRACE_ITER_GLOBAL_CLK) {
		u64 (*func)(void);

		if (enabled)
			func = trace_clock_global;
		else
			func = trace_clock_local;

		mutex_lock(&trace_types_lock);
		ring_buffer_set_clock(global_trace.buffer, func);

		if (max_tr.buffer)
			ring_buffer_set_clock(max_tr.buffer, func);
		mutex_unlock(&trace_types_lock);
	}
}

static ssize_t
tracing_trace_options_write(struct file *filp, const char __user *ubuf,
			size_t cnt, loff_t *ppos)
{
	char buf[64];
	char *cmp = buf;
	int neg = 0;
	int ret;
	int i;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	if (strncmp(buf, "no", 2) == 0) {
		neg = 1;
		cmp += 2;
	}

	for (i = 0; trace_options[i]; i++) {
		int len = strlen(trace_options[i]);

		if (strncmp(cmp, trace_options[i], len) == 0) {
			set_tracer_flags(1 << i, !neg);
			break;
		}
	}

	/* If no option could be set, test the specific tracer options */
	if (!trace_options[i]) {
		mutex_lock(&trace_types_lock);
		ret = set_tracer_option(current_trace, cmp, neg);
		mutex_unlock(&trace_types_lock);
		if (ret)
			return ret;
	}

	filp->f_pos += cnt;

	return cnt;
}

static const struct file_operations tracing_iter_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_trace_options_read,
	.write		= tracing_trace_options_write,
};

static const char readme_msg[] =
	"tracing mini-HOWTO:\n\n"
	"# mkdir /debug\n"
	"# mount -t debugfs nodev /debug\n\n"
	"# cat /debug/tracing/available_tracers\n"
	"wakeup preemptirqsoff preemptoff irqsoff function sched_switch nop\n\n"
	"# cat /debug/tracing/current_tracer\n"
	"nop\n"
	"# echo sched_switch > /debug/tracing/current_tracer\n"
	"# cat /debug/tracing/current_tracer\n"
	"sched_switch\n"
	"# cat /debug/tracing/trace_options\n"
	"noprint-parent nosym-offset nosym-addr noverbose\n"
	"# echo print-parent > /debug/tracing/trace_options\n"
	"# echo 1 > /debug/tracing/tracing_enabled\n"
	"# cat /debug/tracing/trace > /tmp/trace.txt\n"
	"echo 0 > /debug/tracing/tracing_enabled\n"
;

static ssize_t
tracing_readme_read(struct file *filp, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	return simple_read_from_buffer(ubuf, cnt, ppos,
					readme_msg, strlen(readme_msg));
}

static const struct file_operations tracing_readme_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_readme_read,
};

static ssize_t
tracing_ctrl_read(struct file *filp, char __user *ubuf,
		  size_t cnt, loff_t *ppos)
{
	char buf[64];
	int r;

	r = sprintf(buf, "%u\n", tracer_enabled);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
tracing_ctrl_write(struct file *filp, const char __user *ubuf,
		   size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	char buf[64];
	unsigned long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	val = !!val;

	mutex_lock(&trace_types_lock);
	if (tracer_enabled ^ val) {
		if (val) {
			tracer_enabled = 1;
			if (current_trace->start)
				current_trace->start(tr);
			tracing_start();
		} else {
			tracer_enabled = 0;
			tracing_stop();
			if (current_trace->stop)
				current_trace->stop(tr);
		}
	}
	mutex_unlock(&trace_types_lock);

	filp->f_pos += cnt;

	return cnt;
}

static ssize_t
tracing_set_trace_read(struct file *filp, char __user *ubuf,
		       size_t cnt, loff_t *ppos)
{
	char buf[max_tracer_type_len+2];
	int r;

	mutex_lock(&trace_types_lock);
	if (current_trace)
		r = sprintf(buf, "%s\n", current_trace->name);
	else
		r = sprintf(buf, "\n");
	mutex_unlock(&trace_types_lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

int tracer_init(struct tracer *t, struct trace_array *tr)
{
	tracing_reset_online_cpus(tr);
	return t->init(tr);
}

static int tracing_resize_ring_buffer(unsigned long size)
{
	int ret;

	/*
	 * If kernel or user changes the size of the ring buffer
	 * we use the size that was given, and we can forget about
	 * expanding it later.
	 */
	ring_buffer_expanded = 1;

	ret = ring_buffer_resize(global_trace.buffer, size);
	if (ret < 0)
		return ret;

	ret = ring_buffer_resize(max_tr.buffer, size);
	if (ret < 0) {
		int r;

		r = ring_buffer_resize(global_trace.buffer,
				       global_trace.entries);
		if (r < 0) {
			/*
			 * AARGH! We are left with different
			 * size max buffer!!!!
			 * The max buffer is our "snapshot" buffer.
			 * When a tracer needs a snapshot (one of the
			 * latency tracers), it swaps the max buffer
			 * with the saved snap shot. We succeeded to
			 * update the size of the main buffer, but failed to
			 * update the size of the max buffer. But when we tried
			 * to reset the main buffer to the original size, we
			 * failed there too. This is very unlikely to
			 * happen, but if it does, warn and kill all
			 * tracing.
			 */
			WARN_ON(1);
			tracing_disabled = 1;
		}
		return ret;
	}

	global_trace.entries = size;

	return ret;
}

/**
 * tracing_update_buffers - used by tracing facility to expand ring buffers
 *
 * To save on memory when the tracing is never used on a system with it
 * configured in. The ring buffers are set to a minimum size. But once
 * a user starts to use the tracing facility, then they need to grow
 * to their default size.
 *
 * This function is to be called when a tracer is about to be used.
 */
int tracing_update_buffers(void)
{
	int ret = 0;

	mutex_lock(&trace_types_lock);
	if (!ring_buffer_expanded)
		ret = tracing_resize_ring_buffer(trace_buf_size);
	mutex_unlock(&trace_types_lock);

	return ret;
}

struct trace_option_dentry;

static struct trace_option_dentry *
create_trace_option_files(struct tracer *tracer);

static void
destroy_trace_option_files(struct trace_option_dentry *topts);

static int tracing_set_tracer(const char *buf)
{
	static struct trace_option_dentry *topts;
	struct trace_array *tr = &global_trace;
	struct tracer *t;
	int ret = 0;

	mutex_lock(&trace_types_lock);

	if (!ring_buffer_expanded) {
		ret = tracing_resize_ring_buffer(trace_buf_size);
		if (ret < 0)
			goto out;
		ret = 0;
	}

	for (t = trace_types; t; t = t->next) {
		if (strcmp(t->name, buf) == 0)
			break;
	}
	if (!t) {
		ret = -EINVAL;
		goto out;
	}
	if (t == current_trace)
		goto out;

	trace_branch_disable();
	if (current_trace && current_trace->reset)
		current_trace->reset(tr);

	destroy_trace_option_files(topts);

	current_trace = t;

	topts = create_trace_option_files(current_trace);

	if (t->init) {
		ret = tracer_init(t, tr);
		if (ret)
			goto out;
	}

	trace_branch_enable(tr);
 out:
	mutex_unlock(&trace_types_lock);

	return ret;
}

static ssize_t
tracing_set_trace_write(struct file *filp, const char __user *ubuf,
			size_t cnt, loff_t *ppos)
{
	char buf[max_tracer_type_len+1];
	int i;
	size_t ret;
	int err;

	ret = cnt;

	if (cnt > max_tracer_type_len)
		cnt = max_tracer_type_len;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	/* strip ending whitespace. */
	for (i = cnt - 1; i > 0 && isspace(buf[i]); i--)
		buf[i] = 0;

	err = tracing_set_tracer(buf);
	if (err)
		return err;

	filp->f_pos += ret;

	return ret;
}

static ssize_t
tracing_max_lat_read(struct file *filp, char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	unsigned long *ptr = filp->private_data;
	char buf[64];
	int r;

	r = snprintf(buf, sizeof(buf), "%ld\n",
		     *ptr == (unsigned long)-1 ? -1 : nsecs_to_usecs(*ptr));
	if (r > sizeof(buf))
		r = sizeof(buf);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
tracing_max_lat_write(struct file *filp, const char __user *ubuf,
		      size_t cnt, loff_t *ppos)
{
	unsigned long *ptr = filp->private_data;
	char buf[64];
	unsigned long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	*ptr = val * 1000;

	return cnt;
}

static int tracing_open_pipe(struct inode *inode, struct file *filp)
{
	long cpu_file = (long) inode->i_private;
	struct trace_iterator *iter;
	int ret = 0;

	if (tracing_disabled)
		return -ENODEV;

	mutex_lock(&trace_types_lock);

	/* We only allow one reader per cpu */
	if (cpu_file == TRACE_PIPE_ALL_CPU) {
		if (!cpumask_empty(tracing_reader_cpumask)) {
			ret = -EBUSY;
			goto out;
		}
		cpumask_setall(tracing_reader_cpumask);
	} else {
		if (!cpumask_test_cpu(cpu_file, tracing_reader_cpumask))
			cpumask_set_cpu(cpu_file, tracing_reader_cpumask);
		else {
			ret = -EBUSY;
			goto out;
		}
	}

	/* create a buffer to store the information to pass to userspace */
	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * We make a copy of the current tracer to avoid concurrent
	 * changes on it while we are reading.
	 */
	iter->trace = kmalloc(sizeof(*iter->trace), GFP_KERNEL);
	if (!iter->trace) {
		ret = -ENOMEM;
		goto fail;
	}
	if (current_trace)
		*iter->trace = *current_trace;

	if (!alloc_cpumask_var(&iter->started, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto fail;
	}

	/* trace pipe does not show start of buffer */
	cpumask_setall(iter->started);

	iter->cpu_file = cpu_file;
	iter->tr = &global_trace;
	mutex_init(&iter->mutex);
	filp->private_data = iter;

	if (iter->trace->pipe_open)
		iter->trace->pipe_open(iter);

out:
	mutex_unlock(&trace_types_lock);
	return ret;

fail:
	kfree(iter->trace);
	kfree(iter);
	mutex_unlock(&trace_types_lock);
	return ret;
}

static int tracing_release_pipe(struct inode *inode, struct file *file)
{
	struct trace_iterator *iter = file->private_data;

	mutex_lock(&trace_types_lock);

	if (iter->cpu_file == TRACE_PIPE_ALL_CPU)
		cpumask_clear(tracing_reader_cpumask);
	else
		cpumask_clear_cpu(iter->cpu_file, tracing_reader_cpumask);

	mutex_unlock(&trace_types_lock);

	free_cpumask_var(iter->started);
	mutex_destroy(&iter->mutex);
	kfree(iter->trace);
	kfree(iter);

	return 0;
}

static unsigned int
tracing_poll_pipe(struct file *filp, poll_table *poll_table)
{
	struct trace_iterator *iter = filp->private_data;

	if (trace_flags & TRACE_ITER_BLOCK) {
		/*
		 * Always select as readable when in blocking mode
		 */
		return POLLIN | POLLRDNORM;
	} else {
		if (!trace_empty(iter))
			return POLLIN | POLLRDNORM;
		poll_wait(filp, &trace_wait, poll_table);
		if (!trace_empty(iter))
			return POLLIN | POLLRDNORM;

		return 0;
	}
}


void default_wait_pipe(struct trace_iterator *iter)
{
	DEFINE_WAIT(wait);

	prepare_to_wait(&trace_wait, &wait, TASK_INTERRUPTIBLE);

	if (trace_empty(iter))
		schedule();

	finish_wait(&trace_wait, &wait);
}

/*
 * This is a make-shift waitqueue.
 * A tracer might use this callback on some rare cases:
 *
 *  1) the current tracer might hold the runqueue lock when it wakes up
 *     a reader, hence a deadlock (sched, function, and function graph tracers)
 *  2) the function tracers, trace all functions, we don't want
 *     the overhead of calling wake_up and friends
 *     (and tracing them too)
 *
 *     Anyway, this is really very primitive wakeup.
 */
void poll_wait_pipe(struct trace_iterator *iter)
{
	set_current_state(TASK_INTERRUPTIBLE);
	/* sleep for 100 msecs, and try again. */
	schedule_timeout(HZ / 10);
}

/* Must be called with trace_types_lock mutex held. */
static int tracing_wait_pipe(struct file *filp)
{
	struct trace_iterator *iter = filp->private_data;

	while (trace_empty(iter)) {

		if ((filp->f_flags & O_NONBLOCK)) {
			return -EAGAIN;
		}

		mutex_unlock(&iter->mutex);

		iter->trace->wait_pipe(iter);

		mutex_lock(&iter->mutex);

		if (signal_pending(current))
			return -EINTR;

		/*
		 * We block until we read something and tracing is disabled.
		 * We still block if tracing is disabled, but we have never
		 * read anything. This allows a user to cat this file, and
		 * then enable tracing. But after we have read something,
		 * we give an EOF when tracing is again disabled.
		 *
		 * iter->pos will be 0 if we haven't read anything.
		 */
		if (!tracer_enabled && iter->pos)
			break;
	}

	return 1;
}

/*
 * Consumer reader.
 */
static ssize_t
tracing_read_pipe(struct file *filp, char __user *ubuf,
		  size_t cnt, loff_t *ppos)
{
	struct trace_iterator *iter = filp->private_data;
	static struct tracer *old_tracer;
	ssize_t sret;

	/* return any leftover data */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (sret != -EBUSY)
		return sret;

	trace_seq_init(&iter->seq);

	/* copy the tracer to avoid using a global lock all around */
	mutex_lock(&trace_types_lock);
	if (unlikely(old_tracer != current_trace && current_trace)) {
		old_tracer = current_trace;
		*iter->trace = *current_trace;
	}
	mutex_unlock(&trace_types_lock);

	/*
	 * Avoid more than one consumer on a single file descriptor
	 * This is just a matter of traces coherency, the ring buffer itself
	 * is protected.
	 */
	mutex_lock(&iter->mutex);
	if (iter->trace->read) {
		sret = iter->trace->read(iter, filp, ubuf, cnt, ppos);
		if (sret)
			goto out;
	}

waitagain:
	sret = tracing_wait_pipe(filp);
	if (sret <= 0)
		goto out;

	/* stop when tracing is finished */
	if (trace_empty(iter)) {
		sret = 0;
		goto out;
	}

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	/* reset all but tr, trace, and overruns */
	memset(&iter->seq, 0,
	       sizeof(struct trace_iterator) -
	       offsetof(struct trace_iterator, seq));
	iter->pos = -1;

	while (find_next_entry_inc(iter) != NULL) {
		enum print_line_t ret;
		int len = iter->seq.len;

		ret = print_trace_line(iter);
		if (ret == TRACE_TYPE_PARTIAL_LINE) {
			/* don't print partial lines */
			iter->seq.len = len;
			break;
		}
		if (ret != TRACE_TYPE_NO_CONSUME)
			trace_consume(iter);

		if (iter->seq.len >= cnt)
			break;
	}

	/* Now copy what we have to the user */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (iter->seq.readpos >= iter->seq.len)
		trace_seq_init(&iter->seq);

	/*
	 * If there was nothing to send to user, inspite of consuming trace
	 * entries, go back to wait for more entries.
	 */
	if (sret == -EBUSY)
		goto waitagain;

out:
	mutex_unlock(&iter->mutex);

	return sret;
}

static void tracing_pipe_buf_release(struct pipe_inode_info *pipe,
				     struct pipe_buffer *buf)
{
	__free_page(buf->page);
}

static void tracing_spd_release_pipe(struct splice_pipe_desc *spd,
				     unsigned int idx)
{
	__free_page(spd->pages[idx]);
}

static struct pipe_buf_operations tracing_pipe_buf_ops = {
	.can_merge		= 0,
	.map			= generic_pipe_buf_map,
	.unmap			= generic_pipe_buf_unmap,
	.confirm		= generic_pipe_buf_confirm,
	.release		= tracing_pipe_buf_release,
	.steal			= generic_pipe_buf_steal,
	.get			= generic_pipe_buf_get,
};

static size_t
tracing_fill_pipe_page(size_t rem, struct trace_iterator *iter)
{
	size_t count;
	int ret;

	/* Seq buffer is page-sized, exactly what we need. */
	for (;;) {
		count = iter->seq.len;
		ret = print_trace_line(iter);
		count = iter->seq.len - count;
		if (rem < count) {
			rem = 0;
			iter->seq.len -= count;
			break;
		}
		if (ret == TRACE_TYPE_PARTIAL_LINE) {
			iter->seq.len -= count;
			break;
		}

		trace_consume(iter);
		rem -= count;
		if (!find_next_entry_inc(iter))	{
			rem = 0;
			iter->ent = NULL;
			break;
		}
	}

	return rem;
}

static ssize_t tracing_splice_read_pipe(struct file *filp,
					loff_t *ppos,
					struct pipe_inode_info *pipe,
					size_t len,
					unsigned int flags)
{
	struct page *pages[PIPE_BUFFERS];
	struct partial_page partial[PIPE_BUFFERS];
	struct trace_iterator *iter = filp->private_data;
	struct splice_pipe_desc spd = {
		.pages		= pages,
		.partial	= partial,
		.nr_pages	= 0, /* This gets updated below. */
		.flags		= flags,
		.ops		= &tracing_pipe_buf_ops,
		.spd_release	= tracing_spd_release_pipe,
	};
	static struct tracer *old_tracer;
	ssize_t ret;
	size_t rem;
	unsigned int i;

	/* copy the tracer to avoid using a global lock all around */
	mutex_lock(&trace_types_lock);
	if (unlikely(old_tracer != current_trace && current_trace)) {
		old_tracer = current_trace;
		*iter->trace = *current_trace;
	}
	mutex_unlock(&trace_types_lock);

	mutex_lock(&iter->mutex);

	if (iter->trace->splice_read) {
		ret = iter->trace->splice_read(iter, filp,
					       ppos, pipe, len, flags);
		if (ret)
			goto out_err;
	}

	ret = tracing_wait_pipe(filp);
	if (ret <= 0)
		goto out_err;

	if (!iter->ent && !find_next_entry_inc(iter)) {
		ret = -EFAULT;
		goto out_err;
	}

	/* Fill as many pages as possible. */
	for (i = 0, rem = len; i < PIPE_BUFFERS && rem; i++) {
		pages[i] = alloc_page(GFP_KERNEL);
		if (!pages[i])
			break;

		rem = tracing_fill_pipe_page(rem, iter);

		/* Copy the data into the page, so we can start over. */
		ret = trace_seq_to_buffer(&iter->seq,
					  page_address(pages[i]),
					  iter->seq.len);
		if (ret < 0) {
			__free_page(pages[i]);
			break;
		}
		partial[i].offset = 0;
		partial[i].len = iter->seq.len;

		trace_seq_init(&iter->seq);
	}

	mutex_unlock(&iter->mutex);

	spd.nr_pages = i;

	return splice_to_pipe(pipe, &spd);

out_err:
	mutex_unlock(&iter->mutex);

	return ret;
}

static ssize_t
tracing_entries_read(struct file *filp, char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	struct trace_array *tr = filp->private_data;
	char buf[96];
	int r;

	mutex_lock(&trace_types_lock);
	if (!ring_buffer_expanded)
		r = sprintf(buf, "%lu (expanded: %lu)\n",
			    tr->entries >> 10,
			    trace_buf_size >> 10);
	else
		r = sprintf(buf, "%lu\n", tr->entries >> 10);
	mutex_unlock(&trace_types_lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t
tracing_entries_write(struct file *filp, const char __user *ubuf,
		      size_t cnt, loff_t *ppos)
{
	unsigned long val;
	char buf[64];
	int ret, cpu;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	/* must have at least 1 entry */
	if (!val)
		return -EINVAL;

	mutex_lock(&trace_types_lock);

	tracing_stop();

	/* disable all cpu buffers */
	for_each_tracing_cpu(cpu) {
		if (global_trace.data[cpu])
			atomic_inc(&global_trace.data[cpu]->disabled);
		if (max_tr.data[cpu])
			atomic_inc(&max_tr.data[cpu]->disabled);
	}

	/* value is in KB */
	val <<= 10;

	if (val != global_trace.entries) {
		ret = tracing_resize_ring_buffer(val);
		if (ret < 0) {
			cnt = ret;
			goto out;
		}
	}

	filp->f_pos += cnt;

	/* If check pages failed, return ENOMEM */
	if (tracing_disabled)
		cnt = -ENOMEM;
 out:
	for_each_tracing_cpu(cpu) {
		if (global_trace.data[cpu])
			atomic_dec(&global_trace.data[cpu]->disabled);
		if (max_tr.data[cpu])
			atomic_dec(&max_tr.data[cpu]->disabled);
	}

	tracing_start();
	max_tr.entries = global_trace.entries;
	mutex_unlock(&trace_types_lock);

	return cnt;
}

static int mark_printk(const char *fmt, ...)
{
	int ret;
	va_list args;
	va_start(args, fmt);
	ret = trace_vprintk(0, fmt, args);
	va_end(args);
	return ret;
}

static ssize_t
tracing_mark_write(struct file *filp, const char __user *ubuf,
					size_t cnt, loff_t *fpos)
{
	char *buf;
	char *end;

	if (tracing_disabled)
		return -EINVAL;

	if (cnt > TRACE_BUF_SIZE)
		cnt = TRACE_BUF_SIZE;

	buf = kmalloc(cnt + 1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, cnt)) {
		kfree(buf);
		return -EFAULT;
	}

	/* Cut from the first nil or newline. */
	buf[cnt] = '\0';
	end = strchr(buf, '\n');
	if (end)
		*end = '\0';

	cnt = mark_printk("%s\n", buf);
	kfree(buf);
	*fpos += cnt;

	return cnt;
}

static const struct file_operations tracing_max_lat_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_max_lat_read,
	.write		= tracing_max_lat_write,
};

static const struct file_operations tracing_ctrl_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_ctrl_read,
	.write		= tracing_ctrl_write,
};

static const struct file_operations set_tracer_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_set_trace_read,
	.write		= tracing_set_trace_write,
};

static const struct file_operations tracing_pipe_fops = {
	.open		= tracing_open_pipe,
	.poll		= tracing_poll_pipe,
	.read		= tracing_read_pipe,
	.splice_read	= tracing_splice_read_pipe,
	.release	= tracing_release_pipe,
};

static const struct file_operations tracing_entries_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_entries_read,
	.write		= tracing_entries_write,
};

static const struct file_operations tracing_mark_fops = {
	.open		= tracing_open_generic,
	.write		= tracing_mark_write,
};

struct ftrace_buffer_info {
	struct trace_array	*tr;
	void			*spare;
	int			cpu;
	unsigned int		read;
};

static int tracing_buffers_open(struct inode *inode, struct file *filp)
{
	int cpu = (int)(long)inode->i_private;
	struct ftrace_buffer_info *info;

	if (tracing_disabled)
		return -ENODEV;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->tr	= &global_trace;
	info->cpu	= cpu;
	info->spare	= NULL;
	/* Force reading ring buffer for first read */
	info->read	= (unsigned int)-1;

	filp->private_data = info;

	return nonseekable_open(inode, filp);
}

static ssize_t
tracing_buffers_read(struct file *filp, char __user *ubuf,
		     size_t count, loff_t *ppos)
{
	struct ftrace_buffer_info *info = filp->private_data;
	unsigned int pos;
	ssize_t ret;
	size_t size;

	if (!count)
		return 0;

	if (!info->spare)
		info->spare = ring_buffer_alloc_read_page(info->tr->buffer);
	if (!info->spare)
		return -ENOMEM;

	/* Do we have previous read data to read? */
	if (info->read < PAGE_SIZE)
		goto read;

	info->read = 0;

	ret = ring_buffer_read_page(info->tr->buffer,
				    &info->spare,
				    count,
				    info->cpu, 0);
	if (ret < 0)
		return 0;

	pos = ring_buffer_page_len(info->spare);

	if (pos < PAGE_SIZE)
		memset(info->spare + pos, 0, PAGE_SIZE - pos);

read:
	size = PAGE_SIZE - info->read;
	if (size > count)
		size = count;

	ret = copy_to_user(ubuf, info->spare + info->read, size);
	if (ret == size)
		return -EFAULT;
	size -= ret;

	*ppos += size;
	info->read += size;

	return size;
}

static int tracing_buffers_release(struct inode *inode, struct file *file)
{
	struct ftrace_buffer_info *info = file->private_data;

	if (info->spare)
		ring_buffer_free_read_page(info->tr->buffer, info->spare);
	kfree(info);

	return 0;
}

struct buffer_ref {
	struct ring_buffer	*buffer;
	void			*page;
	int			ref;
};

static void buffer_pipe_buf_release(struct pipe_inode_info *pipe,
				    struct pipe_buffer *buf)
{
	struct buffer_ref *ref = (struct buffer_ref *)buf->private;

	if (--ref->ref)
		return;

	ring_buffer_free_read_page(ref->buffer, ref->page);
	kfree(ref);
	buf->private = 0;
}

static int buffer_pipe_buf_steal(struct pipe_inode_info *pipe,
				 struct pipe_buffer *buf)
{
	return 1;
}

static void buffer_pipe_buf_get(struct pipe_inode_info *pipe,
				struct pipe_buffer *buf)
{
	struct buffer_ref *ref = (struct buffer_ref *)buf->private;

	ref->ref++;
}

/* Pipe buffer operations for a buffer. */
static struct pipe_buf_operations buffer_pipe_buf_ops = {
	.can_merge		= 0,
	.map			= generic_pipe_buf_map,
	.unmap			= generic_pipe_buf_unmap,
	.confirm		= generic_pipe_buf_confirm,
	.release		= buffer_pipe_buf_release,
	.steal			= buffer_pipe_buf_steal,
	.get			= buffer_pipe_buf_get,
};

/*
 * Callback from splice_to_pipe(), if we need to release some pages
 * at the end of the spd in case we error'ed out in filling the pipe.
 */
static void buffer_spd_release(struct splice_pipe_desc *spd, unsigned int i)
{
	struct buffer_ref *ref =
		(struct buffer_ref *)spd->partial[i].private;

	if (--ref->ref)
		return;

	ring_buffer_free_read_page(ref->buffer, ref->page);
	kfree(ref);
	spd->partial[i].private = 0;
}

static ssize_t
tracing_buffers_splice_read(struct file *file, loff_t *ppos,
			    struct pipe_inode_info *pipe, size_t len,
			    unsigned int flags)
{
	struct ftrace_buffer_info *info = file->private_data;
	struct partial_page partial[PIPE_BUFFERS];
	struct page *pages[PIPE_BUFFERS];
	struct splice_pipe_desc spd = {
		.pages		= pages,
		.partial	= partial,
		.flags		= flags,
		.ops		= &buffer_pipe_buf_ops,
		.spd_release	= buffer_spd_release,
	};
	struct buffer_ref *ref;
	int size, i;
	size_t ret;

	if (*ppos & (PAGE_SIZE - 1)) {
		WARN_ONCE(1, "Ftrace: previous read must page-align\n");
		return -EINVAL;
	}

	if (len & (PAGE_SIZE - 1)) {
		WARN_ONCE(1, "Ftrace: splice_read should page-align\n");
		if (len < PAGE_SIZE)
			return -EINVAL;
		len &= PAGE_MASK;
	}

	for (i = 0; i < PIPE_BUFFERS && len; i++, len -= PAGE_SIZE) {
		struct page *page;
		int r;

		ref = kzalloc(sizeof(*ref), GFP_KERNEL);
		if (!ref)
			break;

		ref->buffer = info->tr->buffer;
		ref->page = ring_buffer_alloc_read_page(ref->buffer);
		if (!ref->page) {
			kfree(ref);
			break;
		}

		r = ring_buffer_read_page(ref->buffer, &ref->page,
					  len, info->cpu, 0);
		if (r < 0) {
			ring_buffer_free_read_page(ref->buffer,
						   ref->page);
			kfree(ref);
			break;
		}

		/*
		 * zero out any left over data, this is going to
		 * user land.
		 */
		size = ring_buffer_page_len(ref->page);
		if (size < PAGE_SIZE)
			memset(ref->page + size, 0, PAGE_SIZE - size);

		page = virt_to_page(ref->page);

		spd.pages[i] = page;
		spd.partial[i].len = PAGE_SIZE;
		spd.partial[i].offset = 0;
		spd.partial[i].private = (unsigned long)ref;
		spd.nr_pages++;
		*ppos += PAGE_SIZE;
	}

	spd.nr_pages = i;

	/* did we read anything? */
	if (!spd.nr_pages) {
		if (flags & SPLICE_F_NONBLOCK)
			ret = -EAGAIN;
		else
			ret = 0;
		/* TODO: block */
		return ret;
	}

	ret = splice_to_pipe(pipe, &spd);

	return ret;
}

static const struct file_operations tracing_buffers_fops = {
	.open		= tracing_buffers_open,
	.read		= tracing_buffers_read,
	.release	= tracing_buffers_release,
	.splice_read	= tracing_buffers_splice_read,
	.llseek		= no_llseek,
};

#ifdef CONFIG_DYNAMIC_FTRACE

int __weak ftrace_arch_read_dyn_info(char *buf, int size)
{
	return 0;
}

static ssize_t
tracing_read_dyn_info(struct file *filp, char __user *ubuf,
		  size_t cnt, loff_t *ppos)
{
	static char ftrace_dyn_info_buffer[1024];
	static DEFINE_MUTEX(dyn_info_mutex);
	unsigned long *p = filp->private_data;
	char *buf = ftrace_dyn_info_buffer;
	int size = ARRAY_SIZE(ftrace_dyn_info_buffer);
	int r;

	mutex_lock(&dyn_info_mutex);
	r = sprintf(buf, "%ld ", *p);

	r += ftrace_arch_read_dyn_info(buf+r, (size-1)-r);
	buf[r++] = '\n';

	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);

	mutex_unlock(&dyn_info_mutex);

	return r;
}

static const struct file_operations tracing_dyn_info_fops = {
	.open		= tracing_open_generic,
	.read		= tracing_read_dyn_info,
};
#endif

static struct dentry *d_tracer;

struct dentry *tracing_init_dentry(void)
{
	static int once;

	if (d_tracer)
		return d_tracer;

	if (!debugfs_initialized())
		return NULL;

	d_tracer = debugfs_create_dir("tracing", NULL);

	if (!d_tracer && !once) {
		once = 1;
		pr_warning("Could not create debugfs directory 'tracing'\n");
		return NULL;
	}

	return d_tracer;
}

static struct dentry *d_percpu;

struct dentry *tracing_dentry_percpu(void)
{
	static int once;
	struct dentry *d_tracer;

	if (d_percpu)
		return d_percpu;

	d_tracer = tracing_init_dentry();

	if (!d_tracer)
		return NULL;

	d_percpu = debugfs_create_dir("per_cpu", d_tracer);

	if (!d_percpu && !once) {
		once = 1;
		pr_warning("Could not create debugfs directory 'per_cpu'\n");
		return NULL;
	}

	return d_percpu;
}

static void tracing_init_debugfs_percpu(long cpu)
{
	struct dentry *d_percpu = tracing_dentry_percpu();
	struct dentry *entry, *d_cpu;
	/* strlen(cpu) + MAX(log10(cpu)) + '\0' */
	char cpu_dir[7];

	if (cpu > 999 || cpu < 0)
		return;

	sprintf(cpu_dir, "cpu%ld", cpu);
	d_cpu = debugfs_create_dir(cpu_dir, d_percpu);
	if (!d_cpu) {
		pr_warning("Could not create debugfs '%s' entry\n", cpu_dir);
		return;
	}

	/* per cpu trace_pipe */
	entry = debugfs_create_file("trace_pipe", 0444, d_cpu,
				(void *) cpu, &tracing_pipe_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'trace_pipe' entry\n");

	/* per cpu trace */
	entry = debugfs_create_file("trace", 0644, d_cpu,
				(void *) cpu, &tracing_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'trace' entry\n");

	entry = debugfs_create_file("trace_pipe_raw", 0444, d_cpu,
				    (void *) cpu, &tracing_buffers_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'trace_pipe_raw' entry\n");
}

#ifdef CONFIG_FTRACE_SELFTEST
/* Let selftest have access to static functions in this file */
#include "trace_selftest.c"
#endif

struct trace_option_dentry {
	struct tracer_opt		*opt;
	struct tracer_flags		*flags;
	struct dentry			*entry;
};

static ssize_t
trace_options_read(struct file *filp, char __user *ubuf, size_t cnt,
			loff_t *ppos)
{
	struct trace_option_dentry *topt = filp->private_data;
	char *buf;

	if (topt->flags->val & topt->opt->bit)
		buf = "1\n";
	else
		buf = "0\n";

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, 2);
}

static ssize_t
trace_options_write(struct file *filp, const char __user *ubuf, size_t cnt,
			 loff_t *ppos)
{
	struct trace_option_dentry *topt = filp->private_data;
	unsigned long val;
	char buf[64];
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	ret = 0;
	switch (val) {
	case 0:
		/* do nothing if already cleared */
		if (!(topt->flags->val & topt->opt->bit))
			break;

		mutex_lock(&trace_types_lock);
		if (current_trace->set_flag)
			ret = current_trace->set_flag(topt->flags->val,
						      topt->opt->bit, 0);
		mutex_unlock(&trace_types_lock);
		if (ret)
			return ret;
		topt->flags->val &= ~topt->opt->bit;
		break;
	case 1:
		/* do nothing if already set */
		if (topt->flags->val & topt->opt->bit)
			break;

		mutex_lock(&trace_types_lock);
		if (current_trace->set_flag)
			ret = current_trace->set_flag(topt->flags->val,
						      topt->opt->bit, 1);
		mutex_unlock(&trace_types_lock);
		if (ret)
			return ret;
		topt->flags->val |= topt->opt->bit;
		break;

	default:
		return -EINVAL;
	}

	*ppos += cnt;

	return cnt;
}


static const struct file_operations trace_options_fops = {
	.open = tracing_open_generic,
	.read = trace_options_read,
	.write = trace_options_write,
};

static ssize_t
trace_options_core_read(struct file *filp, char __user *ubuf, size_t cnt,
			loff_t *ppos)
{
	long index = (long)filp->private_data;
	char *buf;

	if (trace_flags & (1 << index))
		buf = "1\n";
	else
		buf = "0\n";

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, 2);
}

static ssize_t
trace_options_core_write(struct file *filp, const char __user *ubuf, size_t cnt,
			 loff_t *ppos)
{
	long index = (long)filp->private_data;
	char buf[64];
	unsigned long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	switch (val) {
	case 0:
		trace_flags &= ~(1 << index);
		break;
	case 1:
		trace_flags |= 1 << index;
		break;

	default:
		return -EINVAL;
	}

	*ppos += cnt;

	return cnt;
}

static const struct file_operations trace_options_core_fops = {
	.open = tracing_open_generic,
	.read = trace_options_core_read,
	.write = trace_options_core_write,
};

static struct dentry *trace_options_init_dentry(void)
{
	struct dentry *d_tracer;
	static struct dentry *t_options;

	if (t_options)
		return t_options;

	d_tracer = tracing_init_dentry();
	if (!d_tracer)
		return NULL;

	t_options = debugfs_create_dir("options", d_tracer);
	if (!t_options) {
		pr_warning("Could not create debugfs directory 'options'\n");
		return NULL;
	}

	return t_options;
}

static void
create_trace_option_file(struct trace_option_dentry *topt,
			 struct tracer_flags *flags,
			 struct tracer_opt *opt)
{
	struct dentry *t_options;
	struct dentry *entry;

	t_options = trace_options_init_dentry();
	if (!t_options)
		return;

	topt->flags = flags;
	topt->opt = opt;

	entry = debugfs_create_file(opt->name, 0644, t_options, topt,
				    &trace_options_fops);

	topt->entry = entry;

}

static struct trace_option_dentry *
create_trace_option_files(struct tracer *tracer)
{
	struct trace_option_dentry *topts;
	struct tracer_flags *flags;
	struct tracer_opt *opts;
	int cnt;

	if (!tracer)
		return NULL;

	flags = tracer->flags;

	if (!flags || !flags->opts)
		return NULL;

	opts = flags->opts;

	for (cnt = 0; opts[cnt].name; cnt++)
		;

	topts = kcalloc(cnt + 1, sizeof(*topts), GFP_KERNEL);
	if (!topts)
		return NULL;

	for (cnt = 0; opts[cnt].name; cnt++)
		create_trace_option_file(&topts[cnt], flags,
					 &opts[cnt]);

	return topts;
}

static void
destroy_trace_option_files(struct trace_option_dentry *topts)
{
	int cnt;

	if (!topts)
		return;

	for (cnt = 0; topts[cnt].opt; cnt++) {
		if (topts[cnt].entry)
			debugfs_remove(topts[cnt].entry);
	}

	kfree(topts);
}

static struct dentry *
create_trace_option_core_file(const char *option, long index)
{
	struct dentry *t_options;
	struct dentry *entry;

	t_options = trace_options_init_dentry();
	if (!t_options)
		return NULL;

	entry = debugfs_create_file(option, 0644, t_options, (void *)index,
				    &trace_options_core_fops);

	return entry;
}

static __init void create_trace_options_dir(void)
{
	struct dentry *t_options;
	struct dentry *entry;
	int i;

	t_options = trace_options_init_dentry();
	if (!t_options)
		return;

	for (i = 0; trace_options[i]; i++) {
		entry = create_trace_option_core_file(trace_options[i], i);
		if (!entry)
			pr_warning("Could not create debugfs %s entry\n",
				   trace_options[i]);
	}
}

static __init int tracer_init_debugfs(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;
	int cpu;

	d_tracer = tracing_init_dentry();

	entry = debugfs_create_file("tracing_enabled", 0644, d_tracer,
				    &global_trace, &tracing_ctrl_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'tracing_enabled' entry\n");

	entry = debugfs_create_file("trace_options", 0644, d_tracer,
				    NULL, &tracing_iter_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'trace_options' entry\n");

	create_trace_options_dir();

	entry = debugfs_create_file("tracing_cpumask", 0644, d_tracer,
				    NULL, &tracing_cpumask_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'tracing_cpumask' entry\n");

	entry = debugfs_create_file("trace", 0644, d_tracer,
				 (void *) TRACE_PIPE_ALL_CPU, &tracing_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'trace' entry\n");

	entry = debugfs_create_file("available_tracers", 0444, d_tracer,
				    &global_trace, &show_traces_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'available_tracers' entry\n");

	entry = debugfs_create_file("current_tracer", 0444, d_tracer,
				    &global_trace, &set_tracer_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'current_tracer' entry\n");

	entry = debugfs_create_file("tracing_max_latency", 0644, d_tracer,
				    &tracing_max_latency,
				    &tracing_max_lat_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'tracing_max_latency' entry\n");

	entry = debugfs_create_file("tracing_thresh", 0644, d_tracer,
				    &tracing_thresh, &tracing_max_lat_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'tracing_thresh' entry\n");
	entry = debugfs_create_file("README", 0644, d_tracer,
				    NULL, &tracing_readme_fops);
	if (!entry)
		pr_warning("Could not create debugfs 'README' entry\n");

	entry = debugfs_create_file("trace_pipe", 0444, d_tracer,
			(void *) TRACE_PIPE_ALL_CPU, &tracing_pipe_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'trace_pipe' entry\n");

	entry = debugfs_create_file("buffer_size_kb", 0644, d_tracer,
				    &global_trace, &tracing_entries_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'buffer_size_kb' entry\n");

	entry = debugfs_create_file("trace_marker", 0220, d_tracer,
				    NULL, &tracing_mark_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'trace_marker' entry\n");

#ifdef CONFIG_DYNAMIC_FTRACE
	entry = debugfs_create_file("dyn_ftrace_total_info", 0444, d_tracer,
				    &ftrace_update_tot_cnt,
				    &tracing_dyn_info_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'dyn_ftrace_total_info' entry\n");
#endif
#ifdef CONFIG_SYSPROF_TRACER
	init_tracer_sysprof_debugfs(d_tracer);
#endif

	for_each_tracing_cpu(cpu)
		tracing_init_debugfs_percpu(cpu);

	return 0;
}

static int trace_panic_handler(struct notifier_block *this,
			       unsigned long event, void *unused)
{
	if (ftrace_dump_on_oops)
		ftrace_dump();
	return NOTIFY_OK;
}

static struct notifier_block trace_panic_notifier = {
	.notifier_call  = trace_panic_handler,
	.next           = NULL,
	.priority       = 150   /* priority: INT_MAX >= x >= 0 */
};

static int trace_die_handler(struct notifier_block *self,
			     unsigned long val,
			     void *data)
{
	switch (val) {
	case DIE_OOPS:
		if (ftrace_dump_on_oops)
			ftrace_dump();
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block trace_die_notifier = {
	.notifier_call = trace_die_handler,
	.priority = 200
};

/*
 * printk is set to max of 1024, we really don't need it that big.
 * Nothing should be printing 1000 characters anyway.
 */
#define TRACE_MAX_PRINT		1000

/*
 * Define here KERN_TRACE so that we have one place to modify
 * it if we decide to change what log level the ftrace dump
 * should be at.
 */
#define KERN_TRACE		KERN_EMERG

static void
trace_printk_seq(struct trace_seq *s)
{
	/* Probably should print a warning here. */
	if (s->len >= 1000)
		s->len = 1000;

	/* should be zero ended, but we are paranoid. */
	s->buffer[s->len] = 0;

	printk(KERN_TRACE "%s", s->buffer);

	trace_seq_init(s);
}

static void __ftrace_dump(bool disable_tracing)
{
	static DEFINE_SPINLOCK(ftrace_dump_lock);
	/* use static because iter can be a bit big for the stack */
	static struct trace_iterator iter;
	unsigned int old_userobj;
	static int dump_ran;
	unsigned long flags;
	int cnt = 0, cpu;

	/* only one dump */
	spin_lock_irqsave(&ftrace_dump_lock, flags);
	if (dump_ran)
		goto out;

	dump_ran = 1;

	tracing_off();

	if (disable_tracing)
		ftrace_kill();

	for_each_tracing_cpu(cpu) {
		atomic_inc(&global_trace.data[cpu]->disabled);
	}

	old_userobj = trace_flags & TRACE_ITER_SYM_USEROBJ;

	/* don't look at user memory in panic mode */
	trace_flags &= ~TRACE_ITER_SYM_USEROBJ;

	printk(KERN_TRACE "Dumping ftrace buffer:\n");

	/* Simulate the iterator */
	iter.tr = &global_trace;
	iter.trace = current_trace;
	iter.cpu_file = TRACE_PIPE_ALL_CPU;

	/*
	 * We need to stop all tracing on all CPUS to read the
	 * the next buffer. This is a bit expensive, but is
	 * not done often. We fill all what we can read,
	 * and then release the locks again.
	 */

	while (!trace_empty(&iter)) {

		if (!cnt)
			printk(KERN_TRACE "---------------------------------\n");

		cnt++;

		/* reset all but tr, trace, and overruns */
		memset(&iter.seq, 0,
		       sizeof(struct trace_iterator) -
		       offsetof(struct trace_iterator, seq));
		iter.iter_flags |= TRACE_FILE_LAT_FMT;
		iter.pos = -1;

		if (find_next_entry_inc(&iter) != NULL) {
			print_trace_line(&iter);
			trace_consume(&iter);
		}

		trace_printk_seq(&iter.seq);
	}

	if (!cnt)
		printk(KERN_TRACE "   (ftrace buffer empty)\n");
	else
		printk(KERN_TRACE "---------------------------------\n");

	/* Re-enable tracing if requested */
	if (!disable_tracing) {
		trace_flags |= old_userobj;

		for_each_tracing_cpu(cpu) {
			atomic_dec(&global_trace.data[cpu]->disabled);
		}
		tracing_on();
	}

 out:
	spin_unlock_irqrestore(&ftrace_dump_lock, flags);
}

/* By default: disable tracing after the dump */
void ftrace_dump(void)
{
	__ftrace_dump(true);
}

__init static int tracer_alloc_buffers(void)
{
	struct trace_array_cpu *data;
	int ring_buf_size;
	int i;
	int ret = -ENOMEM;

	if (!alloc_cpumask_var(&tracing_buffer_mask, GFP_KERNEL))
		goto out;

	if (!alloc_cpumask_var(&tracing_cpumask, GFP_KERNEL))
		goto out_free_buffer_mask;

	if (!alloc_cpumask_var(&tracing_reader_cpumask, GFP_KERNEL))
		goto out_free_tracing_cpumask;

	/* To save memory, keep the ring buffer size to its minimum */
	if (ring_buffer_expanded)
		ring_buf_size = trace_buf_size;
	else
		ring_buf_size = 1;

	cpumask_copy(tracing_buffer_mask, cpu_possible_mask);
	cpumask_copy(tracing_cpumask, cpu_all_mask);
	cpumask_clear(tracing_reader_cpumask);

	/* TODO: make the number of buffers hot pluggable with CPUS */
	global_trace.buffer = ring_buffer_alloc(ring_buf_size,
						   TRACE_BUFFER_FLAGS);
	if (!global_trace.buffer) {
		printk(KERN_ERR "tracer: failed to allocate ring buffer!\n");
		WARN_ON(1);
		goto out_free_cpumask;
	}
	global_trace.entries = ring_buffer_size(global_trace.buffer);


#ifdef CONFIG_TRACER_MAX_TRACE
	max_tr.buffer = ring_buffer_alloc(ring_buf_size,
					     TRACE_BUFFER_FLAGS);
	if (!max_tr.buffer) {
		printk(KERN_ERR "tracer: failed to allocate max ring buffer!\n");
		WARN_ON(1);
		ring_buffer_free(global_trace.buffer);
		goto out_free_cpumask;
	}
	max_tr.entries = ring_buffer_size(max_tr.buffer);
	WARN_ON(max_tr.entries != global_trace.entries);
#endif

	/* Allocate the first page for all buffers */
	for_each_tracing_cpu(i) {
		data = global_trace.data[i] = &per_cpu(global_trace_cpu, i);
		max_tr.data[i] = &per_cpu(max_data, i);
	}

	trace_init_cmdlines();

	register_tracer(&nop_trace);
	current_trace = &nop_trace;
#ifdef CONFIG_BOOT_TRACER
	register_tracer(&boot_tracer);
#endif
	/* All seems OK, enable tracing */
	tracing_disabled = 0;

	atomic_notifier_chain_register(&panic_notifier_list,
				       &trace_panic_notifier);

	register_die_notifier(&trace_die_notifier);

	return 0;

out_free_cpumask:
	free_cpumask_var(tracing_reader_cpumask);
out_free_tracing_cpumask:
	free_cpumask_var(tracing_cpumask);
out_free_buffer_mask:
	free_cpumask_var(tracing_buffer_mask);
out:
	return ret;
}

__init static int clear_boot_tracer(void)
{
	/*
	 * The default tracer at boot buffer is an init section.
	 * This function is called in lateinit. If we did not
	 * find the boot tracer, then clear it out, to prevent
	 * later registration from accessing the buffer that is
	 * about to be freed.
	 */
	if (!default_bootup_tracer)
		return 0;

	printk(KERN_INFO "ftrace bootup tracer '%s' not registered.\n",
	       default_bootup_tracer);
	default_bootup_tracer = NULL;

	return 0;
}

early_initcall(tracer_alloc_buffers);
fs_initcall(tracer_init_debugfs);
late_initcall(clear_boot_tracer);
