/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fs/f2fs/gc.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#define GC_THREAD_MIN_WB_PAGES		1	/*
						 * a threshold to determine
						 * whether IO subsystem is idle
						 * or not
						 */
#define DEF_GC_THREAD_URGENT_SLEEP_TIME	500	/* 500 ms */
#define DEF_GC_THREAD_MIN_SLEEP_TIME	10000	/* milliseconds */
#define DEF_GC_THREAD_MAX_SLEEP_TIME	60000
#define DEF_GC_THREAD_NOGC_SLEEP_TIME	300000	/* wait 5 min */

/* choose candidates from sections which has age of more than 7 days */
#define DEF_GC_THREAD_AGE_THRESHOLD		(60 * 60 * 24 * 7)
#define DEF_GC_THREAD_CANDIDATE_RATIO		20	/* select 20% oldest sections as candidates */
#define DEF_GC_THREAD_MAX_CANDIDATE_COUNT	10	/* select at most 10 sections as candidates */
#define DEF_GC_THREAD_AGE_WEIGHT		60	/* age weight */
#define DEFAULT_ACCURACY_CLASS			10000	/* accuracy class */

#define LIMIT_INVALID_BLOCK	40 /* percentage over total user space */
#define LIMIT_FREE_BLOCK	40 /* percentage over invalid + free space */
#define MAX_GC_COUNT		64 /* max # of recycle section once */
#define TOPLIMIT_FREE_SEGMENT(sbi)	(reserved_segments(sbi) * 4)

#define DEF_GC_FAILED_PINNED_FILES	2048

/* Search max. number of dirty segments to select a victim segment */
#define DEF_MAX_VICTIM_SEARCH 4096 /* covers 8GB */

struct f2fs_gc_kthread {
	struct task_struct *f2fs_gc_task;
	wait_queue_head_t gc_wait_queue_head;

	/* for gc sleep time */
	unsigned int urgent_sleep_time;
	unsigned int min_sleep_time;
	unsigned int max_sleep_time;
	unsigned int no_gc_sleep_time;
	bool io_aware;

	/* for changing gc mode */
	unsigned int gc_wake;

	/* for GC_MERGE mount option */
	wait_queue_head_t fggc_wq;		/*
						 * caller of f2fs_balance_fs()
						 * will wait on this wait queue.
						 */
};

struct gc_inode_list {
	struct list_head ilist;
	struct radix_tree_root iroot;
};

struct victim_info {
	unsigned long long mtime;	/* mtime of section */
	unsigned int segno;		/* section No. */
};

struct victim_entry {
	struct rb_node rb_node;		/* rb node located in rb-tree */
	union {
		struct {
			unsigned long long mtime;	/* mtime of section */
			unsigned int segno;		/* segment No. */
		};
		struct victim_info vi;	/* victim info */
	};
	struct list_head list;
};

/*
 * inline functions
 */
static inline block_t free_user_blocks(struct f2fs_sb_info *sbi)
{
	if (free_segments(sbi) < overprovision_segments(sbi))
		return 0;
	else
		return (free_segments(sbi) - overprovision_segments(sbi))
			<< sbi->log_blocks_per_seg;
}

static inline block_t limit_invalid_user_blocks(struct f2fs_sb_info *sbi)
{
	return (long)(sbi->user_block_count * LIMIT_INVALID_BLOCK) / 100;
}

static inline block_t limit_free_user_blocks(struct f2fs_sb_info *sbi)
{
	block_t reclaimable_user_blocks = sbi->user_block_count -
		written_block_count(sbi);
	return (long)(reclaimable_user_blocks * LIMIT_FREE_BLOCK) / 100;
}

static inline void increase_sleep_time(struct f2fs_gc_kthread *gc_th,
							unsigned int *wait)
{
	unsigned int min_time = gc_th->min_sleep_time;
	unsigned int max_time = gc_th->max_sleep_time;

	if (*wait == gc_th->no_gc_sleep_time)
		return;

	if ((long long)*wait + (long long)min_time > (long long)max_time)
		*wait = max_time;
	else
		*wait += min_time;
}

static inline void decrease_sleep_time(struct f2fs_gc_kthread *gc_th,
							unsigned int *wait)
{
	unsigned int min_time = gc_th->min_sleep_time;

	if (*wait == gc_th->no_gc_sleep_time)
		*wait = gc_th->max_sleep_time;

	if ((long long)*wait - (long long)min_time < (long long)min_time)
		*wait = min_time;
	else
		*wait -= min_time;
}

static inline bool has_enough_invalid_blocks(struct f2fs_sb_info *sbi)
{
	block_t invalid_user_blocks = sbi->user_block_count -
					written_block_count(sbi);
	/*
	 * Background GC is triggered with the following conditions.
	 * 1. There are a number of invalid blocks.
	 * 2. There is not enough free space.
	 */
	if (invalid_user_blocks > limit_invalid_user_blocks(sbi) &&
			free_user_blocks(sbi) < limit_free_user_blocks(sbi))
		return true;
	return false;
}

static inline void calculate_sleep_time(struct f2fs_sb_info *sbi,
			struct f2fs_gc_kthread *gc_th, unsigned int *wait)
{
	unsigned int free = free_user_blocks(sbi) >> sbi->log_blocks_per_seg;
	unsigned int wait_time = gc_th->min_sleep_time;

	if (wait == NULL)
		return;

	if (free > TOPLIMIT_FREE_SEGMENT(sbi))
		wait_time = gc_th->max_sleep_time;
	else
		wait_time += (unsigned int)div_u64(
			(gc_th->max_sleep_time - gc_th->min_sleep_time) * free,
			TOPLIMIT_FREE_SEGMENT(sbi));

	*wait = wait_time;
}

static inline unsigned int get_gc_count(struct f2fs_sb_info *sbi)
{
	block_t invalid_user_blocks = sbi->user_block_count -
			written_block_count(sbi) - free_user_blocks(sbi);
	unsigned int gc_count = (unsigned int)div_u64(
			MAX_GC_COUNT * invalid_user_blocks,
			sbi->user_block_count);

	return gc_count ? gc_count : 1;
}
