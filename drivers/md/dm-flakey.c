/*
 * Copyright (C) 2003 Sistina Software (UK) Limited.
 * Copyright (C) 2004, 2010 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include <linux/device-mapper.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>

#define DM_MSG_PREFIX "flakey"

/*
 * Flakey: Used for testing only, simulates intermittent,
 * catastrophic device failure.
 */
struct flakey_c {
	struct dm_dev *dev;
	unsigned long start_time;
	sector_t start;
	unsigned up_interval;
	unsigned down_interval;
	unsigned long flags;
};

enum feature_flag_bits {
	DROP_WRITES
};

static int parse_features(struct dm_arg_set *as, struct flakey_c *fc,
			  struct dm_target *ti)
{
	int r;
	unsigned argc;
	const char *arg_name;

	static struct dm_arg _args[] = {
		{0, 1, "Invalid number of feature args"},
	};

	/* No feature arguments supplied. */
	if (!as->argc)
		return 0;

	r = dm_read_arg_group(_args, as, &argc, &ti->error);
	if (r)
		return -EINVAL;

	while (argc && !r) {
		arg_name = dm_shift_arg(as);
		argc--;

		/*
		 * drop_writes
		 */
		if (!strcasecmp(arg_name, "drop_writes")) {
			if (test_and_set_bit(DROP_WRITES, &fc->flags)) {
				ti->error = "Feature drop_writes duplicated";
				return -EINVAL;
			}

			continue;
		}

		ti->error = "Unrecognised flakey feature requested";
		r = -EINVAL;
	}

	return r;
}

/*
 * Construct a flakey mapping:
 * <dev_path> <offset> <up interval> <down interval> [<#feature args> [<arg>]*]
 *
 *   Feature args:
 *     [drop_writes]
 */
static int flakey_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	static struct dm_arg _args[] = {
		{0, UINT_MAX, "Invalid up interval"},
		{0, UINT_MAX, "Invalid down interval"},
	};

	int r;
	struct flakey_c *fc;
	unsigned long long tmpll;
	struct dm_arg_set as;
	const char *devname;

	as.argc = argc;
	as.argv = argv;

	if (argc < 4) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	fc = kzalloc(sizeof(*fc), GFP_KERNEL);
	if (!fc) {
		ti->error = "Cannot allocate linear context";
		return -ENOMEM;
	}
	fc->start_time = jiffies;

	devname = dm_shift_arg(&as);

	if (sscanf(dm_shift_arg(&as), "%llu", &tmpll) != 1) {
		ti->error = "Invalid device sector";
		goto bad;
	}
	fc->start = tmpll;

	r = dm_read_arg(_args, &as, &fc->up_interval, &ti->error);
	if (r)
		goto bad;

	r = dm_read_arg(_args, &as, &fc->down_interval, &ti->error);
	if (r)
		goto bad;

	if (!(fc->up_interval + fc->down_interval)) {
		ti->error = "Total (up + down) interval is zero";
		goto bad;
	}

	if (fc->up_interval + fc->down_interval < fc->up_interval) {
		ti->error = "Interval overflow";
		goto bad;
	}

	r = parse_features(&as, fc, ti);
	if (r)
		goto bad;

	if (dm_get_device(ti, devname, dm_table_get_mode(ti->table), &fc->dev)) {
		ti->error = "Device lookup failed";
		goto bad;
	}

	ti->num_flush_requests = 1;
	ti->num_discard_requests = 1;
	ti->private = fc;
	return 0;

bad:
	kfree(fc);
	return -EINVAL;
}

static void flakey_dtr(struct dm_target *ti)
{
	struct flakey_c *fc = ti->private;

	dm_put_device(ti, fc->dev);
	kfree(fc);
}

static sector_t flakey_map_sector(struct dm_target *ti, sector_t bi_sector)
{
	struct flakey_c *fc = ti->private;

	return fc->start + dm_target_offset(ti, bi_sector);
}

static void flakey_map_bio(struct dm_target *ti, struct bio *bio)
{
	struct flakey_c *fc = ti->private;

	bio->bi_bdev = fc->dev->bdev;
	if (bio_sectors(bio))
		bio->bi_sector = flakey_map_sector(ti, bio->bi_sector);
}

static int flakey_map(struct dm_target *ti, struct bio *bio,
		      union map_info *map_context)
{
	struct flakey_c *fc = ti->private;
	unsigned elapsed;
	unsigned rw;

	/* Are we alive ? */
	elapsed = (jiffies - fc->start_time) / HZ;
	if (elapsed % (fc->up_interval + fc->down_interval) >= fc->up_interval) {
		rw = bio_data_dir(bio);

		/*
		 * Drop writes.  Map reads as normal.
		 */
		if (test_bit(DROP_WRITES, &fc->flags)) {
			if (rw == WRITE) {
				bio_endio(bio, 0);
				return DM_MAPIO_SUBMITTED;
			}
			goto map_bio;
		}

		/*
		 * Default setting errors all I/O.
		 */
		return -EIO;
	}

map_bio:
	flakey_map_bio(ti, bio);

	return DM_MAPIO_REMAPPED;
}

static int flakey_status(struct dm_target *ti, status_type_t type,
			 char *result, unsigned int maxlen)
{
	unsigned sz = 0;
	struct flakey_c *fc = ti->private;
	unsigned drop_writes;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%s %llu %u %u ", fc->dev->name,
		       (unsigned long long)fc->start, fc->up_interval,
		       fc->down_interval);

		drop_writes = test_bit(DROP_WRITES, &fc->flags);
		DMEMIT("%u ", drop_writes);
		if (drop_writes)
			DMEMIT("drop_writes ");
		break;
	}
	return 0;
}

static int flakey_ioctl(struct dm_target *ti, unsigned int cmd, unsigned long arg)
{
	struct flakey_c *fc = ti->private;

	return __blkdev_driver_ioctl(fc->dev->bdev, fc->dev->mode, cmd, arg);
}

static int flakey_merge(struct dm_target *ti, struct bvec_merge_data *bvm,
			struct bio_vec *biovec, int max_size)
{
	struct flakey_c *fc = ti->private;
	struct request_queue *q = bdev_get_queue(fc->dev->bdev);

	if (!q->merge_bvec_fn)
		return max_size;

	bvm->bi_bdev = fc->dev->bdev;
	bvm->bi_sector = flakey_map_sector(ti, bvm->bi_sector);

	return min(max_size, q->merge_bvec_fn(q, bvm, biovec));
}

static int flakey_iterate_devices(struct dm_target *ti, iterate_devices_callout_fn fn, void *data)
{
	struct flakey_c *fc = ti->private;

	return fn(ti, fc->dev, fc->start, ti->len, data);
}

static struct target_type flakey_target = {
	.name   = "flakey",
	.version = {1, 2, 0},
	.module = THIS_MODULE,
	.ctr    = flakey_ctr,
	.dtr    = flakey_dtr,
	.map    = flakey_map,
	.status = flakey_status,
	.ioctl	= flakey_ioctl,
	.merge	= flakey_merge,
	.iterate_devices = flakey_iterate_devices,
};

static int __init dm_flakey_init(void)
{
	int r = dm_register_target(&flakey_target);

	if (r < 0)
		DMERR("register failed %d", r);

	return r;
}

static void __exit dm_flakey_exit(void)
{
	dm_unregister_target(&flakey_target);
}

/* Module hooks */
module_init(dm_flakey_init);
module_exit(dm_flakey_exit);

MODULE_DESCRIPTION(DM_NAME " flakey target");
MODULE_AUTHOR("Joe Thornber <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
