/*
 * linux/fs/9p/trans_fd.c
 *
 * Fd transport layer.  Includes deprecated socket layer.
 *
 *  Copyright (C) 2006 by Russ Cox <rsc@swtch.com>
 *  Copyright (C) 2004-2005 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004-2008 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 1997-2002 by Ron Minnich <rminnich@sarnoff.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include <linux/in.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/kthread.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/un.h>
#include <linux/uaccess.h>
#include <linux/inet.h>
#include <linux/idr.h>
#include <linux/file.h>
#include <linux/parser.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>

#define P9_PORT 564
#define MAX_SOCK_BUF (64*1024)
#define ERREQFLUSH	1
#define MAXPOLLWADDR	2

/**
 * struct p9_fd_opts - per-transport options
 * @rfd: file descriptor for reading (trans=fd)
 * @wfd: file descriptor for writing (trans=fd)
 * @port: port to connect to (trans=tcp)
 *
 */

struct p9_fd_opts {
	int rfd;
	int wfd;
	u16 port;
};

/**
 * struct p9_trans_fd - transport state
 * @rd: reference to file to read from
 * @wr: reference of file to write to
 * @conn: connection state reference
 *
 */

struct p9_trans_fd {
	struct file *rd;
	struct file *wr;
	struct p9_conn *conn;
};

/*
  * Option Parsing (code inspired by NFS code)
  *  - a little lazy - parse all fd-transport options
  */

enum {
	/* Options that take integer arguments */
	Opt_port, Opt_rfdno, Opt_wfdno, Opt_err,
};

static const match_table_t tokens = {
	{Opt_port, "port=%u"},
	{Opt_rfdno, "rfdno=%u"},
	{Opt_wfdno, "wfdno=%u"},
	{Opt_err, NULL},
};

enum {
	Rworksched = 1,		/* read work scheduled or running */
	Rpending = 2,		/* can read */
	Wworksched = 4,		/* write work scheduled or running */
	Wpending = 8,		/* can write */
};

enum {
	None,
	Flushing,
	Flushed,
};

/**
 * struct p9_req - fd mux encoding of an rpc transaction
 * @lock: protects req_list
 * @tag: numeric tag for rpc transaction
 * @tcall: request &p9_fcall structure
 * @rcall: response &p9_fcall structure
 * @err: error state
 * @flush: flag to indicate RPC has been flushed
 * @req_list: list link for higher level objects to chain requests
 * @m: connection this request was issued on
 * @wqueue: wait queue that client is blocked on for this rpc
 *
 */

struct p9_req {
	spinlock_t lock;
	int tag;
	struct p9_fcall *tcall;
	struct p9_fcall *rcall;
	int err;
	int flush;
	struct list_head req_list;
	struct p9_conn *m;
	wait_queue_head_t wqueue;
};

struct p9_poll_wait {
	struct p9_conn *conn;
	wait_queue_t wait;
	wait_queue_head_t *wait_addr;
};

/**
 * struct p9_conn - fd mux connection state information
 * @lock: protects mux_list (?)
 * @mux_list: list link for mux to manage multiple connections (?)
 * @client: reference to client instance for this connection
 * @tagpool: id accounting for transactions
 * @err: error state
 * @req_list: accounting for requests which have been sent
 * @unsent_req_list: accounting for requests that haven't been sent
 * @rcall: current response &p9_fcall structure
 * @rpos: read position in current frame
 * @rbuf: current read buffer
 * @wpos: write position for current frame
 * @wsize: amount of data to write for current frame
 * @wbuf: current write buffer
 * @poll_wait: array of wait_q's for various worker threads
 * @poll_waddr: ????
 * @pt: poll state
 * @rq: current read work
 * @wq: current write work
 * @wsched: ????
 *
 */

struct p9_conn {
	spinlock_t lock; /* protect lock structure */
	struct list_head mux_list;
	struct p9_client *client;
	struct p9_idpool *tagpool;
	int err;
	struct list_head req_list;
	struct list_head unsent_req_list;
	struct p9_fcall *rcall;
	int rpos;
	char *rbuf;
	int wpos;
	int wsize;
	char *wbuf;
	struct list_head poll_pending_link;
	struct p9_poll_wait poll_wait[MAXPOLLWADDR];
	poll_table pt;
	struct work_struct rq;
	struct work_struct wq;
	unsigned long wsched;
};

static DEFINE_SPINLOCK(p9_poll_lock);
static LIST_HEAD(p9_poll_pending_list);
static struct workqueue_struct *p9_mux_wq;
static struct task_struct *p9_poll_task;

static u16 p9_mux_get_tag(struct p9_conn *m)
{
	int tag;

	tag = p9_idpool_get(m->tagpool);
	if (tag < 0)
		return P9_NOTAG;
	else
		return (u16) tag;
}

static void p9_mux_put_tag(struct p9_conn *m, u16 tag)
{
	if (tag != P9_NOTAG && p9_idpool_check(tag, m->tagpool))
		p9_idpool_put(tag, m->tagpool);
}

static void p9_mux_poll_stop(struct p9_conn *m)
{
	unsigned long flags;
	int i;

	for (i = 0; i < ARRAY_SIZE(m->poll_wait); i++) {
		struct p9_poll_wait *pwait = &m->poll_wait[i];

		if (pwait->wait_addr) {
			remove_wait_queue(pwait->wait_addr, &pwait->wait);
			pwait->wait_addr = NULL;
		}
	}

	spin_lock_irqsave(&p9_poll_lock, flags);
	list_del_init(&m->poll_pending_link);
	spin_unlock_irqrestore(&p9_poll_lock, flags);
}

static void p9_mux_free_request(struct p9_conn *m, struct p9_req *req)
{
	p9_mux_put_tag(m, req->tag);
	kfree(req);
}

static void p9_conn_rpc_cb(struct p9_req *req);

static void p9_mux_flush_cb(struct p9_req *freq)
{
	int tag;
	struct p9_conn *m = freq->m;
	struct p9_req *req, *rreq, *rptr;

	P9_DPRINTK(P9_DEBUG_MUX, "mux %p tc %p rc %p err %d oldtag %d\n", m,
		freq->tcall, freq->rcall, freq->err,
		freq->tcall->params.tflush.oldtag);

	spin_lock(&m->lock);
	tag = freq->tcall->params.tflush.oldtag;
	req = NULL;
	list_for_each_entry_safe(rreq, rptr, &m->req_list, req_list) {
		if (rreq->tag == tag) {
			req = rreq;
			list_del(&req->req_list);
			break;
		}
	}
	spin_unlock(&m->lock);

	if (req) {
		spin_lock(&req->lock);
		req->flush = Flushed;
		spin_unlock(&req->lock);

		p9_conn_rpc_cb(req);
	}

	kfree(freq->tcall);
	kfree(freq->rcall);
	p9_mux_free_request(m, freq);
}

static void p9_conn_rpc_cb(struct p9_req *req)
{
	P9_DPRINTK(P9_DEBUG_MUX, "req %p\n", req);

	if (req->tcall->id == P9_TFLUSH) { /* flush callback */
		P9_DPRINTK(P9_DEBUG_MUX, "flush req %p\n", req);
		p9_mux_flush_cb(req);
	} else {			/* normal wakeup path */
		P9_DPRINTK(P9_DEBUG_MUX, "normal req %p\n", req);
		if (req->flush != None && !req->err)
			req->err = -ERESTARTSYS;

		wake_up(&req->wqueue);
	}
}

/**
 * p9_conn_cancel - cancel all pending requests with error
 * @m: mux data
 * @err: error code
 *
 */

void p9_conn_cancel(struct p9_conn *m, int err)
{
	struct p9_req *req, *rtmp;
	LIST_HEAD(cancel_list);

	P9_DPRINTK(P9_DEBUG_ERROR, "mux %p err %d\n", m, err);
	m->err = err;
	spin_lock(&m->lock);
	list_for_each_entry_safe(req, rtmp, &m->req_list, req_list) {
		list_move(&req->req_list, &cancel_list);
	}
	list_for_each_entry_safe(req, rtmp, &m->unsent_req_list, req_list) {
		list_move(&req->req_list, &cancel_list);
	}
	spin_unlock(&m->lock);

	list_for_each_entry_safe(req, rtmp, &cancel_list, req_list) {
		list_del(&req->req_list);
		if (!req->err)
			req->err = err;

		p9_conn_rpc_cb(req);
	}
}

static void process_request(struct p9_conn *m, struct p9_req *req)
{
	int ecode;
	struct p9_str *ename;

	if (!req->err && req->rcall->id == P9_RERROR) {
		ecode = req->rcall->params.rerror.errno;
		ename = &req->rcall->params.rerror.error;

		P9_DPRINTK(P9_DEBUG_MUX, "Rerror %.*s\n", ename->len,
								ename->str);

		if (m->client->dotu)
			req->err = -ecode;

		if (!req->err) {
			req->err = p9_errstr2errno(ename->str, ename->len);

			/* string match failed */
			if (!req->err) {
				PRINT_FCALL_ERROR("unknown error", req->rcall);
				req->err = -ESERVERFAULT;
			}
		}
	} else if (req->tcall && req->rcall->id != req->tcall->id + 1) {
		P9_DPRINTK(P9_DEBUG_ERROR,
				"fcall mismatch: expected %d, got %d\n",
				req->tcall->id + 1, req->rcall->id);
		if (!req->err)
			req->err = -EIO;
	}
}

static unsigned int
p9_fd_poll(struct p9_client *client, struct poll_table_struct *pt)
{
	int ret, n;
	struct p9_trans_fd *ts = NULL;

	if (client && client->status == Connected)
		ts = client->trans;

	if (!ts)
		return -EREMOTEIO;

	if (!ts->rd->f_op || !ts->rd->f_op->poll)
		return -EIO;

	if (!ts->wr->f_op || !ts->wr->f_op->poll)
		return -EIO;

	ret = ts->rd->f_op->poll(ts->rd, pt);
	if (ret < 0)
		return ret;

	if (ts->rd != ts->wr) {
		n = ts->wr->f_op->poll(ts->wr, pt);
		if (n < 0)
			return n;
		ret = (ret & ~POLLOUT) | (n & ~POLLIN);
	}

	return ret;
}

/**
 * p9_fd_read- read from a fd
 * @client: client instance
 * @v: buffer to receive data into
 * @len: size of receive buffer
 *
 */

static int p9_fd_read(struct p9_client *client, void *v, int len)
{
	int ret;
	struct p9_trans_fd *ts = NULL;

	if (client && client->status != Disconnected)
		ts = client->trans;

	if (!ts)
		return -EREMOTEIO;

	if (!(ts->rd->f_flags & O_NONBLOCK))
		P9_DPRINTK(P9_DEBUG_ERROR, "blocking read ...\n");

	ret = kernel_read(ts->rd, ts->rd->f_pos, v, len);
	if (ret <= 0 && ret != -ERESTARTSYS && ret != -EAGAIN)
		client->status = Disconnected;
	return ret;
}

/**
 * p9_read_work - called when there is some data to be read from a transport
 * @work: container of work to be done
 *
 */

static void p9_read_work(struct work_struct *work)
{
	int n, err;
	struct p9_conn *m;
	struct p9_req *req, *rptr, *rreq;
	struct p9_fcall *rcall;
	char *rbuf;

	m = container_of(work, struct p9_conn, rq);

	if (m->err < 0)
		return;

	rcall = NULL;
	P9_DPRINTK(P9_DEBUG_MUX, "start mux %p pos %d\n", m, m->rpos);

	if (!m->rcall) {
		m->rcall =
		    kmalloc(sizeof(struct p9_fcall) + m->client->msize,
								GFP_KERNEL);
		if (!m->rcall) {
			err = -ENOMEM;
			goto error;
		}

		m->rbuf = (char *)m->rcall + sizeof(struct p9_fcall);
		m->rpos = 0;
	}

	clear_bit(Rpending, &m->wsched);
	err = p9_fd_read(m->client, m->rbuf + m->rpos,
						m->client->msize - m->rpos);
	P9_DPRINTK(P9_DEBUG_MUX, "mux %p got %d bytes\n", m, err);
	if (err == -EAGAIN) {
		clear_bit(Rworksched, &m->wsched);
		return;
	}

	if (err <= 0)
		goto error;

	m->rpos += err;
	while (m->rpos > 4) {
		n = le32_to_cpu(*(__le32 *) m->rbuf);
		if (n >= m->client->msize) {
			P9_DPRINTK(P9_DEBUG_ERROR,
				"requested packet size too big: %d\n", n);
			err = -EIO;
			goto error;
		}

		if (m->rpos < n)
			break;

		err =
		    p9_deserialize_fcall(m->rbuf, n, m->rcall, m->client->dotu);
		if (err < 0)
			goto error;

#ifdef CONFIG_NET_9P_DEBUG
		if ((p9_debug_level&P9_DEBUG_FCALL) == P9_DEBUG_FCALL) {
			char buf[150];

			p9_printfcall(buf, sizeof(buf), m->rcall,
				m->client->dotu);
			printk(KERN_NOTICE ">>> %p %s\n", m, buf);
		}
#endif

		rcall = m->rcall;
		rbuf = m->rbuf;
		if (m->rpos > n) {
			m->rcall = kmalloc(sizeof(struct p9_fcall) +
						m->client->msize, GFP_KERNEL);
			if (!m->rcall) {
				err = -ENOMEM;
				goto error;
			}

			m->rbuf = (char *)m->rcall + sizeof(struct p9_fcall);
			memmove(m->rbuf, rbuf + n, m->rpos - n);
			m->rpos -= n;
		} else {
			m->rcall = NULL;
			m->rbuf = NULL;
			m->rpos = 0;
		}

		P9_DPRINTK(P9_DEBUG_MUX, "mux %p fcall id %d tag %d\n", m,
							rcall->id, rcall->tag);

		req = NULL;
		spin_lock(&m->lock);
		list_for_each_entry_safe(rreq, rptr, &m->req_list, req_list) {
			if (rreq->tag == rcall->tag) {
				req = rreq;
				if (req->flush != Flushing)
					list_del(&req->req_list);
				break;
			}
		}
		spin_unlock(&m->lock);

		if (req) {
			req->rcall = rcall;
			process_request(m, req);

			if (req->flush != Flushing)
				p9_conn_rpc_cb(req);
		} else {
			if (err >= 0 && rcall->id != P9_RFLUSH)
				P9_DPRINTK(P9_DEBUG_ERROR,
				  "unexpected response mux %p id %d tag %d\n",
				  m, rcall->id, rcall->tag);
			kfree(rcall);
		}
	}

	if (!list_empty(&m->req_list)) {
		if (test_and_clear_bit(Rpending, &m->wsched))
			n = POLLIN;
		else
			n = p9_fd_poll(m->client, NULL);

		if (n & POLLIN) {
			P9_DPRINTK(P9_DEBUG_MUX, "schedule read work %p\n", m);
			queue_work(p9_mux_wq, &m->rq);
		} else
			clear_bit(Rworksched, &m->wsched);
	} else
		clear_bit(Rworksched, &m->wsched);

	return;

error:
	p9_conn_cancel(m, err);
	clear_bit(Rworksched, &m->wsched);
}

/**
 * p9_fd_write - write to a socket
 * @client: client instance
 * @v: buffer to send data from
 * @len: size of send buffer
 *
 */

static int p9_fd_write(struct p9_client *client, void *v, int len)
{
	int ret;
	mm_segment_t oldfs;
	struct p9_trans_fd *ts = NULL;

	if (client && client->status != Disconnected)
		ts = client->trans;

	if (!ts)
		return -EREMOTEIO;

	if (!(ts->wr->f_flags & O_NONBLOCK))
		P9_DPRINTK(P9_DEBUG_ERROR, "blocking write ...\n");

	oldfs = get_fs();
	set_fs(get_ds());
	/* The cast to a user pointer is valid due to the set_fs() */
	ret = vfs_write(ts->wr, (void __user *)v, len, &ts->wr->f_pos);
	set_fs(oldfs);

	if (ret <= 0 && ret != -ERESTARTSYS && ret != -EAGAIN)
		client->status = Disconnected;
	return ret;
}

/**
 * p9_write_work - called when a transport can send some data
 * @work: container for work to be done
 *
 */

static void p9_write_work(struct work_struct *work)
{
	int n, err;
	struct p9_conn *m;
	struct p9_req *req;

	m = container_of(work, struct p9_conn, wq);

	if (m->err < 0) {
		clear_bit(Wworksched, &m->wsched);
		return;
	}

	if (!m->wsize) {
		if (list_empty(&m->unsent_req_list)) {
			clear_bit(Wworksched, &m->wsched);
			return;
		}

		spin_lock(&m->lock);
again:
		req = list_entry(m->unsent_req_list.next, struct p9_req,
			       req_list);
		list_move_tail(&req->req_list, &m->req_list);
		if (req->err == ERREQFLUSH)
			goto again;

		m->wbuf = req->tcall->sdata;
		m->wsize = req->tcall->size;
		m->wpos = 0;
		spin_unlock(&m->lock);
	}

	P9_DPRINTK(P9_DEBUG_MUX, "mux %p pos %d size %d\n", m, m->wpos,
								m->wsize);
	clear_bit(Wpending, &m->wsched);
	err = p9_fd_write(m->client, m->wbuf + m->wpos, m->wsize - m->wpos);
	P9_DPRINTK(P9_DEBUG_MUX, "mux %p sent %d bytes\n", m, err);
	if (err == -EAGAIN) {
		clear_bit(Wworksched, &m->wsched);
		return;
	}

	if (err < 0)
		goto error;
	else if (err == 0) {
		err = -EREMOTEIO;
		goto error;
	}

	m->wpos += err;
	if (m->wpos == m->wsize)
		m->wpos = m->wsize = 0;

	if (m->wsize == 0 && !list_empty(&m->unsent_req_list)) {
		if (test_and_clear_bit(Wpending, &m->wsched))
			n = POLLOUT;
		else
			n = p9_fd_poll(m->client, NULL);

		if (n & POLLOUT) {
			P9_DPRINTK(P9_DEBUG_MUX, "schedule write work %p\n", m);
			queue_work(p9_mux_wq, &m->wq);
		} else
			clear_bit(Wworksched, &m->wsched);
	} else
		clear_bit(Wworksched, &m->wsched);

	return;

error:
	p9_conn_cancel(m, err);
	clear_bit(Wworksched, &m->wsched);
}

static int p9_pollwake(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	struct p9_poll_wait *pwait =
		container_of(wait, struct p9_poll_wait, wait);
	struct p9_conn *m = pwait->conn;
	unsigned long flags;
	DECLARE_WAITQUEUE(dummy_wait, p9_poll_task);

	spin_lock_irqsave(&p9_poll_lock, flags);
	if (list_empty(&m->poll_pending_link))
		list_add_tail(&m->poll_pending_link, &p9_poll_pending_list);
	spin_unlock_irqrestore(&p9_poll_lock, flags);

	/* perform the default wake up operation */
	return default_wake_function(&dummy_wait, mode, sync, key);
}

/**
 * p9_pollwait - add poll task to the wait queue
 * @filp: file pointer being polled
 * @wait_address: wait_q to block on
 * @p: poll state
 *
 * called by files poll operation to add v9fs-poll task to files wait queue
 */

static void
p9_pollwait(struct file *filp, wait_queue_head_t *wait_address, poll_table *p)
{
	struct p9_conn *m = container_of(p, struct p9_conn, pt);
	struct p9_poll_wait *pwait = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(m->poll_wait); i++) {
		if (m->poll_wait[i].wait_addr == NULL) {
			pwait = &m->poll_wait[i];
			break;
		}
	}

	if (!pwait) {
		P9_DPRINTK(P9_DEBUG_ERROR, "not enough wait_address slots\n");
		return;
	}

	if (!wait_address) {
		P9_DPRINTK(P9_DEBUG_ERROR, "no wait_address\n");
		pwait->wait_addr = ERR_PTR(-EIO);
		return;
	}

	pwait->conn = m;
	pwait->wait_addr = wait_address;
	init_waitqueue_func_entry(&pwait->wait, p9_pollwake);
	add_wait_queue(wait_address, &pwait->wait);
}

/**
 * p9_conn_create - allocate and initialize the per-session mux data
 * @client: client instance
 *
 * Note: Creates the polling task if this is the first session.
 */

static struct p9_conn *p9_conn_create(struct p9_client *client)
{
	int i, n;
	struct p9_conn *m;

	P9_DPRINTK(P9_DEBUG_MUX, "client %p msize %d\n", client, client->msize);
	m = kzalloc(sizeof(struct p9_conn), GFP_KERNEL);
	if (!m)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&m->lock);
	INIT_LIST_HEAD(&m->mux_list);
	m->client = client;
	m->tagpool = p9_idpool_create();
	if (IS_ERR(m->tagpool)) {
		kfree(m);
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&m->req_list);
	INIT_LIST_HEAD(&m->unsent_req_list);
	INIT_WORK(&m->rq, p9_read_work);
	INIT_WORK(&m->wq, p9_write_work);
	INIT_LIST_HEAD(&m->poll_pending_link);
	init_poll_funcptr(&m->pt, p9_pollwait);

	n = p9_fd_poll(client, &m->pt);
	if (n & POLLIN) {
		P9_DPRINTK(P9_DEBUG_MUX, "mux %p can read\n", m);
		set_bit(Rpending, &m->wsched);
	}

	if (n & POLLOUT) {
		P9_DPRINTK(P9_DEBUG_MUX, "mux %p can write\n", m);
		set_bit(Wpending, &m->wsched);
	}

	for (i = 0; i < ARRAY_SIZE(m->poll_wait); i++) {
		if (IS_ERR(m->poll_wait[i].wait_addr)) {
			p9_mux_poll_stop(m);
			kfree(m);
			/* return the error code */
			return (void *)m->poll_wait[i].wait_addr;
		}
	}

	return m;
}

/**
 * p9_poll_mux - polls a mux and schedules read or write works if necessary
 * @m: connection to poll
 *
 */

static void p9_poll_mux(struct p9_conn *m)
{
	int n;

	if (m->err < 0)
		return;

	n = p9_fd_poll(m->client, NULL);
	if (n < 0 || n & (POLLERR | POLLHUP | POLLNVAL)) {
		P9_DPRINTK(P9_DEBUG_MUX, "error mux %p err %d\n", m, n);
		if (n >= 0)
			n = -ECONNRESET;
		p9_conn_cancel(m, n);
	}

	if (n & POLLIN) {
		set_bit(Rpending, &m->wsched);
		P9_DPRINTK(P9_DEBUG_MUX, "mux %p can read\n", m);
		if (!test_and_set_bit(Rworksched, &m->wsched)) {
			P9_DPRINTK(P9_DEBUG_MUX, "schedule read work %p\n", m);
			queue_work(p9_mux_wq, &m->rq);
		}
	}

	if (n & POLLOUT) {
		set_bit(Wpending, &m->wsched);
		P9_DPRINTK(P9_DEBUG_MUX, "mux %p can write\n", m);
		if ((m->wsize || !list_empty(&m->unsent_req_list))
		    && !test_and_set_bit(Wworksched, &m->wsched)) {
			P9_DPRINTK(P9_DEBUG_MUX, "schedule write work %p\n", m);
			queue_work(p9_mux_wq, &m->wq);
		}
	}
}

/**
 * p9_send_request - send 9P request
 * The function can sleep until the request is scheduled for sending.
 * The function can be interrupted. Return from the function is not
 * a guarantee that the request is sent successfully. Can return errors
 * that can be retrieved by PTR_ERR macros.
 *
 * @m: mux data
 * @tc: request to be sent
 *
 */

static struct p9_req *p9_send_request(struct p9_conn *m, struct p9_fcall *tc)
{
	int n;
	struct p9_req *req;

	P9_DPRINTK(P9_DEBUG_MUX, "mux %p task %p tcall %p id %d\n", m, current,
		tc, tc->id);
	if (m->err < 0)
		return ERR_PTR(m->err);

	req = kmalloc(sizeof(struct p9_req), GFP_KERNEL);
	if (!req)
		return ERR_PTR(-ENOMEM);

	if (tc->id == P9_TVERSION)
		n = P9_NOTAG;
	else
		n = p9_mux_get_tag(m);

	if (n < 0) {
		kfree(req);
		return ERR_PTR(-ENOMEM);
	}

	p9_set_tag(tc, n);

#ifdef CONFIG_NET_9P_DEBUG
	if ((p9_debug_level&P9_DEBUG_FCALL) == P9_DEBUG_FCALL) {
		char buf[150];

		p9_printfcall(buf, sizeof(buf), tc, m->client->dotu);
		printk(KERN_NOTICE "<<< %p %s\n", m, buf);
	}
#endif

	spin_lock_init(&req->lock);
	req->m = m;
	init_waitqueue_head(&req->wqueue);
	req->tag = n;
	req->tcall = tc;
	req->rcall = NULL;
	req->err = 0;
	req->flush = None;

	spin_lock(&m->lock);
	list_add_tail(&req->req_list, &m->unsent_req_list);
	spin_unlock(&m->lock);

	if (test_and_clear_bit(Wpending, &m->wsched))
		n = POLLOUT;
	else
		n = p9_fd_poll(m->client, NULL);

	if (n & POLLOUT && !test_and_set_bit(Wworksched, &m->wsched))
		queue_work(p9_mux_wq, &m->wq);

	return req;
}

static int
p9_mux_flush_request(struct p9_conn *m, struct p9_req *req)
{
	struct p9_fcall *fc;
	struct p9_req *rreq, *rptr;

	P9_DPRINTK(P9_DEBUG_MUX, "mux %p req %p tag %d\n", m, req, req->tag);

	/* if a response was received for a request, do nothing */
	spin_lock(&req->lock);
	if (req->rcall || req->err) {
		spin_unlock(&req->lock);
		P9_DPRINTK(P9_DEBUG_MUX,
			"mux %p req %p response already received\n", m, req);
		return 0;
	}

	req->flush = Flushing;
	spin_unlock(&req->lock);

	spin_lock(&m->lock);
	/* if the request is not sent yet, just remove it from the list */
	list_for_each_entry_safe(rreq, rptr, &m->unsent_req_list, req_list) {
		if (rreq->tag == req->tag) {
			P9_DPRINTK(P9_DEBUG_MUX,
			   "mux %p req %p request is not sent yet\n", m, req);
			list_del(&rreq->req_list);
			req->flush = Flushed;
			spin_unlock(&m->lock);
			p9_conn_rpc_cb(req);
			return 0;
		}
	}
	spin_unlock(&m->lock);

	clear_thread_flag(TIF_SIGPENDING);
	fc = p9_create_tflush(req->tag);
	p9_send_request(m, fc);
	return 1;
}

/**
 * p9_fd_rpc- sends 9P request and waits until a response is available.
 *	The function can be interrupted.
 * @client: client instance
 * @tc: request to be sent
 * @rc: pointer where a pointer to the response is stored
 *
 */

int
p9_fd_rpc(struct p9_client *client, struct p9_fcall *tc, struct p9_fcall **rc)
{
	struct p9_trans_fd *p = client->trans;
	struct p9_conn *m = p->conn;
	int err, sigpending;
	unsigned long flags;
	struct p9_req *req;

	if (rc)
		*rc = NULL;

	sigpending = 0;
	if (signal_pending(current)) {
		sigpending = 1;
		clear_thread_flag(TIF_SIGPENDING);
	}

	req = p9_send_request(m, tc);
	if (IS_ERR(req)) {
		err = PTR_ERR(req);
		P9_DPRINTK(P9_DEBUG_MUX, "error %d\n", err);
		return err;
	}

	err = wait_event_interruptible(req->wqueue, req->rcall != NULL ||
								req->err < 0);
	if (req->err < 0)
		err = req->err;

	if (err == -ERESTARTSYS && client->status == Connected
							&& m->err == 0) {
		if (p9_mux_flush_request(m, req)) {
			/* wait until we get response of the flush message */
			do {
				clear_thread_flag(TIF_SIGPENDING);
				err = wait_event_interruptible(req->wqueue,
					req->rcall || req->err);
			} while (!req->rcall && !req->err &&
					err == -ERESTARTSYS &&
					client->status == Connected && !m->err);

			err = -ERESTARTSYS;
		}
		sigpending = 1;
	}

	if (sigpending) {
		spin_lock_irqsave(&current->sighand->siglock, flags);
		recalc_sigpending();
		spin_unlock_irqrestore(&current->sighand->siglock, flags);
	}

	if (rc)
		*rc = req->rcall;
	else
		kfree(req->rcall);

	p9_mux_free_request(m, req);
	if (err > 0)
		err = -EIO;

	return err;
}

/**
 * parse_options - parse mount options into session structure
 * @options: options string passed from mount
 * @opts: transport-specific structure to parse options into
 *
 * Returns 0 upon success, -ERRNO upon failure
 */

static int parse_opts(char *params, struct p9_fd_opts *opts)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	char *options;
	int ret;

	opts->port = P9_PORT;
	opts->rfd = ~0;
	opts->wfd = ~0;

	if (!params)
		return 0;

	options = kstrdup(params, GFP_KERNEL);
	if (!options) {
		P9_DPRINTK(P9_DEBUG_ERROR,
				"failed to allocate copy of option string\n");
		return -ENOMEM;
	}

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		int r;
		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		r = match_int(&args[0], &option);
		if (r < 0) {
			P9_DPRINTK(P9_DEBUG_ERROR,
			 "integer field, but no integer?\n");
			ret = r;
			continue;
		}
		switch (token) {
		case Opt_port:
			opts->port = option;
			break;
		case Opt_rfdno:
			opts->rfd = option;
			break;
		case Opt_wfdno:
			opts->wfd = option;
			break;
		default:
			continue;
		}
	}
	kfree(options);
	return 0;
}

static int p9_fd_open(struct p9_client *client, int rfd, int wfd)
{
	struct p9_trans_fd *ts = kmalloc(sizeof(struct p9_trans_fd),
					   GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->rd = fget(rfd);
	ts->wr = fget(wfd);
	if (!ts->rd || !ts->wr) {
		if (ts->rd)
			fput(ts->rd);
		if (ts->wr)
			fput(ts->wr);
		kfree(ts);
		return -EIO;
	}

	client->trans = ts;
	client->status = Connected;

	return 0;
}

static int p9_socket_open(struct p9_client *client, struct socket *csocket)
{
	int fd, ret;

	csocket->sk->sk_allocation = GFP_NOIO;
	fd = sock_map_fd(csocket, 0);
	if (fd < 0) {
		P9_EPRINTK(KERN_ERR, "p9_socket_open: failed to map fd\n");
		return fd;
	}

	ret = p9_fd_open(client, fd, fd);
	if (ret < 0) {
		P9_EPRINTK(KERN_ERR, "p9_socket_open: failed to open fd\n");
		sockfd_put(csocket);
		return ret;
	}

	((struct p9_trans_fd *)client->trans)->rd->f_flags |= O_NONBLOCK;

	return 0;
}

/**
 * p9_mux_destroy - cancels all pending requests and frees mux resources
 * @m: mux to destroy
 *
 */

static void p9_conn_destroy(struct p9_conn *m)
{
	P9_DPRINTK(P9_DEBUG_MUX, "mux %p prev %p next %p\n", m,
		m->mux_list.prev, m->mux_list.next);

	p9_mux_poll_stop(m);
	cancel_work_sync(&m->rq);
	cancel_work_sync(&m->wq);

	p9_conn_cancel(m, -ECONNRESET);

	m->client = NULL;
	p9_idpool_destroy(m->tagpool);
	kfree(m);
}

/**
 * p9_fd_close - shutdown file descriptor transport
 * @client: client instance
 *
 */

static void p9_fd_close(struct p9_client *client)
{
	struct p9_trans_fd *ts;

	if (!client)
		return;

	ts = client->trans;
	if (!ts)
		return;

	client->status = Disconnected;

	p9_conn_destroy(ts->conn);

	if (ts->rd)
		fput(ts->rd);
	if (ts->wr)
		fput(ts->wr);

	kfree(ts);
}

/*
 * stolen from NFS - maybe should be made a generic function?
 */
static inline int valid_ipaddr4(const char *buf)
{
	int rc, count, in[4];

	rc = sscanf(buf, "%d.%d.%d.%d", &in[0], &in[1], &in[2], &in[3]);
	if (rc != 4)
		return -EINVAL;
	for (count = 0; count < 4; count++) {
		if (in[count] > 255)
			return -EINVAL;
	}
	return 0;
}

static int
p9_fd_create_tcp(struct p9_client *client, const char *addr, char *args)
{
	int err;
	struct socket *csocket;
	struct sockaddr_in sin_server;
	struct p9_fd_opts opts;
	struct p9_trans_fd *p = NULL; /* this gets allocated in p9_fd_open */

	err = parse_opts(args, &opts);
	if (err < 0)
		return err;

	if (valid_ipaddr4(addr) < 0)
		return -EINVAL;

	csocket = NULL;

	sin_server.sin_family = AF_INET;
	sin_server.sin_addr.s_addr = in_aton(addr);
	sin_server.sin_port = htons(opts.port);
	sock_create_kern(PF_INET, SOCK_STREAM, IPPROTO_TCP, &csocket);

	if (!csocket) {
		P9_EPRINTK(KERN_ERR, "p9_trans_tcp: problem creating socket\n");
		err = -EIO;
		goto error;
	}

	err = csocket->ops->connect(csocket,
				    (struct sockaddr *)&sin_server,
				    sizeof(struct sockaddr_in), 0);
	if (err < 0) {
		P9_EPRINTK(KERN_ERR,
			"p9_trans_tcp: problem connecting socket to %s\n",
			addr);
		goto error;
	}

	err = p9_socket_open(client, csocket);
	if (err < 0)
		goto error;

	p = (struct p9_trans_fd *) client->trans;
	p->conn = p9_conn_create(client);
	if (IS_ERR(p->conn)) {
		err = PTR_ERR(p->conn);
		p->conn = NULL;
		goto error;
	}

	return 0;

error:
	if (csocket)
		sock_release(csocket);

	kfree(p);

	return err;
}

static int
p9_fd_create_unix(struct p9_client *client, const char *addr, char *args)
{
	int err;
	struct socket *csocket;
	struct sockaddr_un sun_server;
	struct p9_trans_fd *p = NULL; /* this gets allocated in p9_fd_open */

	csocket = NULL;

	if (strlen(addr) > UNIX_PATH_MAX) {
		P9_EPRINTK(KERN_ERR, "p9_trans_unix: address too long: %s\n",
			addr);
		err = -ENAMETOOLONG;
		goto error;
	}

	sun_server.sun_family = PF_UNIX;
	strcpy(sun_server.sun_path, addr);
	sock_create_kern(PF_UNIX, SOCK_STREAM, 0, &csocket);
	err = csocket->ops->connect(csocket, (struct sockaddr *)&sun_server,
			sizeof(struct sockaddr_un) - 1, 0);
	if (err < 0) {
		P9_EPRINTK(KERN_ERR,
			"p9_trans_unix: problem connecting socket: %s: %d\n",
			addr, err);
		goto error;
	}

	err = p9_socket_open(client, csocket);
	if (err < 0)
		goto error;

	p = (struct p9_trans_fd *) client->trans;
	p->conn = p9_conn_create(client);
	if (IS_ERR(p->conn)) {
		err = PTR_ERR(p->conn);
		p->conn = NULL;
		goto error;
	}

	return 0;

error:
	if (csocket)
		sock_release(csocket);

	kfree(p);
	return err;
}

static int
p9_fd_create(struct p9_client *client, const char *addr, char *args)
{
	int err;
	struct p9_fd_opts opts;
	struct p9_trans_fd *p = NULL; /* this get allocated in p9_fd_open */

	parse_opts(args, &opts);

	if (opts.rfd == ~0 || opts.wfd == ~0) {
		printk(KERN_ERR "v9fs: Insufficient options for proto=fd\n");
		return -ENOPROTOOPT;
	}

	err = p9_fd_open(client, opts.rfd, opts.wfd);
	if (err < 0)
		goto error;

	p = (struct p9_trans_fd *) client->trans;
	p->conn = p9_conn_create(client);
	if (IS_ERR(p->conn)) {
		err = PTR_ERR(p->conn);
		p->conn = NULL;
		goto error;
	}

	return 0;

error:
	kfree(p);
	return err;
}

static struct p9_trans_module p9_tcp_trans = {
	.name = "tcp",
	.maxsize = MAX_SOCK_BUF,
	.def = 1,
	.create = p9_fd_create_tcp,
	.close = p9_fd_close,
	.rpc = p9_fd_rpc,
	.owner = THIS_MODULE,
};

static struct p9_trans_module p9_unix_trans = {
	.name = "unix",
	.maxsize = MAX_SOCK_BUF,
	.def = 0,
	.create = p9_fd_create_unix,
	.close = p9_fd_close,
	.rpc = p9_fd_rpc,
	.owner = THIS_MODULE,
};

static struct p9_trans_module p9_fd_trans = {
	.name = "fd",
	.maxsize = MAX_SOCK_BUF,
	.def = 0,
	.create = p9_fd_create,
	.close = p9_fd_close,
	.rpc = p9_fd_rpc,
	.owner = THIS_MODULE,
};

/**
 * p9_poll_proc - poll worker thread
 * @a: thread state and arguments
 *
 * polls all v9fs transports for new events and queues the appropriate
 * work to the work queue
 *
 */

static int p9_poll_proc(void *a)
{
	unsigned long flags;

	P9_DPRINTK(P9_DEBUG_MUX, "start %p\n", current);
 repeat:
	spin_lock_irqsave(&p9_poll_lock, flags);
	while (!list_empty(&p9_poll_pending_list)) {
		struct p9_conn *conn = list_first_entry(&p9_poll_pending_list,
							struct p9_conn,
							poll_pending_link);
		list_del_init(&conn->poll_pending_link);
		spin_unlock_irqrestore(&p9_poll_lock, flags);

		p9_poll_mux(conn);

		spin_lock_irqsave(&p9_poll_lock, flags);
	}
	spin_unlock_irqrestore(&p9_poll_lock, flags);

	set_current_state(TASK_INTERRUPTIBLE);
	if (list_empty(&p9_poll_pending_list)) {
		P9_DPRINTK(P9_DEBUG_MUX, "sleeping...\n");
		schedule();
	}
	__set_current_state(TASK_RUNNING);

	if (!kthread_should_stop())
		goto repeat;

	P9_DPRINTK(P9_DEBUG_MUX, "finish\n");
	return 0;
}

int p9_trans_fd_init(void)
{
	p9_mux_wq = create_workqueue("v9fs");
	if (!p9_mux_wq) {
		printk(KERN_WARNING "v9fs: mux: creating workqueue failed\n");
		return -ENOMEM;
	}

	p9_poll_task = kthread_run(p9_poll_proc, NULL, "v9fs-poll");
	if (IS_ERR(p9_poll_task)) {
		destroy_workqueue(p9_mux_wq);
		printk(KERN_WARNING "v9fs: mux: creating poll task failed\n");
		return PTR_ERR(p9_poll_task);
	}

	v9fs_register_trans(&p9_tcp_trans);
	v9fs_register_trans(&p9_unix_trans);
	v9fs_register_trans(&p9_fd_trans);

	return 0;
}

void p9_trans_fd_exit(void)
{
	kthread_stop(p9_poll_task);
	v9fs_unregister_trans(&p9_tcp_trans);
	v9fs_unregister_trans(&p9_unix_trans);
	v9fs_unregister_trans(&p9_fd_trans);

	destroy_workqueue(p9_mux_wq);
}
