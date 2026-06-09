/*
 * MIT License
 *
 * Copyright (c) 2026 KnotFS Authors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * -----------------------------------------------------------------------------
 * knotfs.c — KnotFS core implementation (teaching edition, ~1150 lines)
 *  Async log-structured file system using RAM to simulate NOR Flash.
 *  Architecture: request queue → knotfs_run() event loop → state machines → flash[].
 */

#define _POSIX_C_SOURCE 199309L
#include "knotfs.h"
#include <string.h>
#include <time.h>

/* ==================================== internal constants ==================================== */

#define Q_CAPACITY (4U)
#define TICK_NS  (80000000L) /* nanoseconds per tick — NOR Flash block erase ~500ms, roughly 6 ticks */
#define LOG_BUF_CAPACITY (32U)

/* scratch[] slot indices (shared across operations) */
#define SCR_OLD_CNT (0U)            /* number of old blocks being replaced */
#define SCR_OLD_BLK(n) (1U + (n)) /* old block index at slot n */
#define SCR_NEW_NEED (9U)           /* number of new blocks needed */
#define SCR_RD_SBLK (0U)            /* read: start block */
#define SCR_RD_EBLK (1U)            /* read: end block (exclusive) */
#define SCR_APP_LAST_BLK (0U)       /* append: last block of file */
#define SCR_APP_LEFT (1U)           /* append: remaining space in last block */
#define SCR_APP_OFF (2U)            /* append: cumulative write offset */

/* flash operation tick counts (asymmetric to match real NOR Flash ratios) */
#define FDEV_TICK_RD (1U)
#define FDEV_TICK_WR (2U)
#define FDEV_TICK_ER (3U)

/* SB header size = sizeof superblock struct; log entries are stored after it
 * within the same 4 KB block, followed by nothing (block tail stays 0xFF). */
#define SB_HEADER_SZ (sizeof(knot_sb_t))
#define LOG_ENTRY_SZ (8U)
#define LOG_ENTRIES_MAX ((KNOTFS_BLOCK_SIZE - SB_HEADER_SZ) / LOG_ENTRY_SZ) /* (4096-668)/8 = 428 */
#define LOG_THRESHOLD (LOG_ENTRIES_MAX * KNOTFS_LOG_THRESHOLD / 100U) /* 214 */

#define LT_BITMAP (0U) /* log entry type: free_bitmap change */
#define LT_USED (1U)   /* log entry type: block_used_bytes */

/* ==================================== internal types ==================================== */

typedef struct {
  char name[KNOTFS_NAME_LEN];
  uint32_t size;
  uint32_t block_count;
  uint32_t blocks[KNOTFS_DIRECT_BLKS];
} knode_t; /* file entry (the name is deliberately different from SimpleFS) */

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t sequence;
  uint32_t log_offset;
  uint32_t log_count;
  uint32_t free_map; /* bitmask: 16 blocks -> fits in 1 uint32 */
  uint32_t wear[KNOTFS_BLOCK_COUNT];
  knode_t nodes[KNOTFS_MAX_FILES];
  uint32_t crc32;
} knot_sb_t; /* superblock (different from SimpleFS's simplefs_superblock_t) */

typedef struct {
  uint8_t tag; /* LT_BITMAP or LT_USED */
  uint8_t blk;
  uint16_t val;
  uint32_t crc32;
} knot_log_t; /* 8-byte log record (different from SimpleFS's
                 simplefs_sb_log_record_t) */

typedef enum {
  OP_NONE = 0,
  OP_FMT,
  OP_MNT,
  OP_WRT,
  OP_RD,
  OP_APP,
  OP_DEL,
  OP_LS,
  OP_ST,
} knot_op_t; /* operation codes (different from SIMPLEFS_OP_*) */

typedef enum {
  /* format */
  ST_FMT_ERASE = 10,
  ST_FMT_ERASE_WAIT = 11,
  ST_FMT_WR_SB1 = 12,
  ST_FMT_DONE = 13,
  /* mount */
  ST_MNT_RD_SB0 = 20,
  ST_MNT_RD_SB1 = 21,
  ST_MNT_SEL = 22,
  ST_MNT_REPLAY = 23,
  /* write */
  ST_WRT_INIT = 30,
  ST_WRT_ALLOC = 31,
  ST_WRT_DATA = 32,
  ST_WRT_META = 33,
  ST_WRT_META_FLUSH = 34,
  ST_WRT_COMPACT = 35,
  /* read */
  ST_RD_LOOKUP = 40,
  ST_RD_DATA = 41,
  ST_RD_COPY = 42,
  /* append */
  ST_APP_LOOKUP = 50,
  ST_APP_ALLOC = 51,
  ST_APP_RD_BLK = 52,
  ST_APP_DATA = 53,
  ST_APP_META = 54,
  ST_APP_COMPACT = 55,
  /* delete */
  ST_DEL_LOOKUP = 60,
  ST_DEL_META = 61,
  ST_DEL_COMPACT = 62,
  /* admin */
  ST_LS_DO = 70,
  ST_ST_DO = 75,
  /* completion */
  ST_OK = 90,
  ST_ERR = 91,
} knot_st_t; /* state constants (different from SIMPLEFS_STATE_*) */

/* ==================================== operation error codes ==================================== */

enum {
  ERR_Q_FULL = -1,    /* queue is full, retry later */
  ERR_BAD_PARAM = -2, /* invalid parameter (null ptr, zero size, etc.) */
  ERR_TOO_MANY = -3,  /* file needs more blocks than KNOTFS_DIRECT_BLKS */
  ERR_NO_NODE = -4,   /* no free file entry slot */
  ERR_WRT_NODE = -5,  /* node vanished during write (find_node failed) */
  ERR_NO_BLK = -6,    /* no free data block */
  ERR_MNT_FAIL = -10, /* both superblock copies are corrupt */
  /* read */
  ERR_RD_NOFILE = -20, /* file not found */
  ERR_RD_NODE = -21,   /* node vanished during read */
  ERR_RD_COPY = -22,   /* node vanished during copy */
  /* append */
  ERR_APP_NOFILE = -30, /* cannot create file entry */
  ERR_APP_OVF = -31,    /* append would exceed max file size */
  ERR_APP_MAXBLK = -32, /* max blocks per file reached */
  ERR_APP_NOBLK = -33,  /* no free block for append */
  /* delete */
  ERR_DEL_NOFILE = -40, /* file not found */
};

/* ==================================== flash simulator ==================================== */

static uint8_t flash[KNOTFS_BLOCK_COUNT * KNOTFS_BLOCK_SIZE];

typedef enum { FOP_NONE, FOP_RD, FOP_WR, FOP_ER } fop_t;

static struct {
  fop_t op;
  uint32_t blk_idx;
  uint32_t blk_off;
  void *buf;
  uint32_t size;
  int ticks_left;
  int result;
} fdev;

static void fdev_read(uint32_t blk, void *dst, uint32_t sz) {
  fdev.op = FOP_RD;
  fdev.blk_idx = blk;
  fdev.blk_off = 0;
  fdev.buf = dst;
  fdev.size = sz;
  fdev.ticks_left = FDEV_TICK_RD;
}

static void fdev_write(uint32_t blk, const void *src, uint32_t off,
                       uint32_t sz) {
  fdev.op = FOP_WR;
  fdev.blk_idx = blk;
  fdev.blk_off = off;
  fdev.buf = (void *)src;
  fdev.size = sz;
  fdev.ticks_left = FDEV_TICK_WR;
}

static void fdev_erase(uint32_t blk) {
  fdev.op = FOP_ER;
  fdev.blk_idx = blk;
  fdev.ticks_left = FDEV_TICK_ER;
}

static bool fdev_idle(void) { return fdev.op == FOP_NONE; }

static void fdev_tick(void) {
  if (fdev.op == FOP_NONE)
    return;
  {
    struct timespec ts = {0, TICK_NS};
    nanosleep(&ts, NULL);
  }
  if (--fdev.ticks_left > 0)
    return;
  uint32_t base = fdev.blk_idx * KNOTFS_BLOCK_SIZE + fdev.blk_off;
  fdev.result = 0;
  switch (fdev.op) {
  case FOP_RD:
    memcpy(fdev.buf, &flash[base], fdev.size);
    break;
  case FOP_WR:
    memcpy(&flash[base], fdev.buf, fdev.size);
    break;
  case FOP_ER:
    memset(&flash[base], 0xFF, KNOTFS_BLOCK_SIZE);
    break;
  default:
    break;
  }
  fdev.op = FOP_NONE;
}

/* ==================================== crc32 (polynomial 0xEDB88320) ==================================== */

static uint32_t k_crc32(const void *data, uint32_t len) {
  uint32_t c = 0xFFFFFFFFU;
  const uint8_t *p = (const uint8_t *)data;
  for (uint32_t i = 0; i < len; i++) {
    c ^= p[i];
    for (int j = 0; j < 8; j++)
      c = (c >> 1) ^ ((c & 1U) ? 0xEDB88320U : 0U);
  }
  return c ^ 0xFFFFFFFFU;
}

static uint32_t sb_checksum(const knot_sb_t *sb) {
  return k_crc32(sb, sizeof(knot_sb_t) - sizeof(uint32_t));
}

static uint32_t log_checksum(const knot_log_t *e) {
  return k_crc32(e, sizeof(knot_log_t) - sizeof(uint32_t));
}

/* ==================================== global state ==================================== */

static knot_sb_t sb;
static knot_sb_t sb_alt; /* the other SB copy read during mount */
static bool mounted;

typedef struct {
  knot_op_t op;
  knot_st_t st;
  int result;
  char name[KNOTFS_NAME_LEN];
  const void *w_buf;
  uint32_t w_sz;
  void *r_buf;
  uint32_t r_sz;
  uint32_t r_off;
  uint32_t *r_out;
  const void *a_buf;
  uint32_t a_sz;
  knotfs_entry_t *ls_ents;
  uint32_t *ls_cnt;
  uint32_t *st_tot;
  uint32_t *st_free;
} kreq_t;

static kreq_t q[Q_CAPACITY];
static int q_hd, q_tl, q_cnt;
static kreq_t cur;
static uint32_t prog, subst, scratch[16];
static uint8_t tmp[KNOTFS_BLOCK_SIZE];
static knot_sb_t compact_buf; /* persistent buffer for async compact writes */
static bool comp_wait;

static knot_log_t log_buf[LOG_BUF_CAPACITY];
static uint32_t log_cnt;

/* ==================================== block helpers ==================================== */

static uint32_t pick_lowest_wear(void) {
  uint32_t best = KNOTFS_BLOCK_COUNT;
  uint32_t best_w = 0xFFFFFFFFU;
  for (uint32_t i = KNOTFS_DATA_START; i < KNOTFS_BLOCK_COUNT; i++) {
    if (!(sb.free_map & (1U << i)) && sb.wear[i] < best_w) {
      best_w = sb.wear[i];
      best = i;
    }
  }
  return best;
}

static void mark_free(uint32_t b) { sb.free_map &= ~(1U << b); }
static void mark_used(uint32_t b) { sb.free_map |= (1U << b); }
static bool is_free(uint32_t b) { return !(sb.free_map & (1U << b)); }

/* ==================================== node helpers ==================================== */

static knode_t *find_node(const char *name) {
  for (uint32_t i = 0; i < KNOTFS_MAX_FILES; i++)
    if (sb.nodes[i].size && !strncmp(sb.nodes[i].name, name, KNOTFS_NAME_LEN))
      return &sb.nodes[i];
  return NULL;
}

static knode_t *alloc_node(const char *name) {
  for (uint32_t i = 0; i < KNOTFS_MAX_FILES; i++) {
    if (sb.nodes[i].size == 0) {
      memset(&sb.nodes[i], 0, sizeof(knode_t));
      {
        uint32_t l = 0;
        while (l < KNOTFS_NAME_LEN - 1 && name[l])
          l++;
        memcpy(sb.nodes[i].name, name, l);
        sb.nodes[i].name[l] = '\0';
      }
      return &sb.nodes[i];
    }
  }
  return NULL;
}

/* ==================================== sb log ==================================== */

static void log_reset(void) { log_cnt = 0; }
static void log_commit(void) {
  sb.log_count += log_cnt;
  log_cnt = 0;
}

static int log_push(uint8_t tag, uint8_t blk, uint16_t val) {
  if (log_cnt >= LOG_BUF_CAPACITY)
    return ERR_Q_FULL;
  knot_log_t *e = &log_buf[log_cnt];
  e->tag = tag;
  e->blk = blk;
  e->val = val;
  e->crc32 = log_checksum(e);
  log_cnt++;
  return 0;
}

static uint32_t active_sb_slot(void) {
  /* active slot toggles between SB0 and SB1 on each compact.
   * sequence parity: even → SB0 active, odd → SB1 active. */
  return (sb.sequence & 1U) ? KNOTFS_SB1_BLK : KNOTFS_SB0_BLK;
}

static uint32_t inactive_sb_slot(void) {
  return (sb.sequence & 1U) ? KNOTFS_SB0_BLK : KNOTFS_SB1_BLK;
}

static void log_flush(void) {
  if (log_cnt == 0)
    return;
  uint32_t sz = log_cnt * LOG_ENTRY_SZ;
  fdev_write(active_sb_slot(), log_buf, sb.log_offset, sz);
  sb.log_offset += sz;
}

static void compact_erase(void) {
  uint32_t tgt = inactive_sb_slot();
  fdev_erase(tgt);
}

static void compact_write(void) {
  memcpy(&compact_buf, &sb, sizeof(knot_sb_t));
  compact_buf.sequence = sb.sequence + 1;
  compact_buf.log_offset = SB_HEADER_SZ;
  compact_buf.log_count = 0;
  compact_buf.crc32 = sb_checksum(&compact_buf);

  uint32_t tgt = inactive_sb_slot();
  fdev_write(tgt, &compact_buf, 0, sizeof(knot_sb_t));
}

static void compact_finish(void) {
  sb.sequence++;
  sb.log_offset = SB_HEADER_SZ;
  sb.log_count = 0;
}

/* ==================================== log replay ==================================== */

static void replay_log(void) {
  knot_log_t e;
  for (uint32_t i = 0; i < sb.log_count; i++) {
    uint32_t pos = SB_HEADER_SZ + i * LOG_ENTRY_SZ;
    memcpy(&e, ((uint8_t *)&sb) + pos, sizeof(e));
    if (e.tag == 0xFF)
      break;
    if (e.crc32 != log_checksum(&e))
      break;
    if (e.tag == LT_BITMAP) {
      if (e.val)
        mark_used(e.blk);
      else
        mark_free(e.blk);
    }
  }
}

/* ==================================== queue helpers ==================================== */

static bool q_empty(void) { return q_cnt == 0; }
static bool q_full(void) { return (uint32_t)q_cnt >= Q_CAPACITY; }

static int q_push(kreq_t *r) {
  if (q_full())
    return ERR_Q_FULL;
  memcpy(&q[q_tl], r, sizeof(kreq_t));
  q_tl = (q_tl + 1) % Q_CAPACITY;
  q_cnt++;
  return 0;
}

static void q_pop(kreq_t *r) {
  if (q_empty()) {
    memset(r, 0, sizeof(*r));
    return;
  }
  memcpy(r, &q[q_hd], sizeof(kreq_t));
  q_hd = (q_hd + 1) % Q_CAPACITY;
  q_cnt--;
}

/* ==================================== state machines ==================================== */

static void do_format(void);
static void do_mount(void);
static void do_write(void);
static void do_read(void);
static void do_append(void);
static void do_delete(void);
static void do_list(void);
static void do_stats(void);

/* ---- format ---- */
static void do_format(void) {
  switch (cur.st) {
  case ST_FMT_ERASE:
    if (prog >= KNOTFS_BLOCK_COUNT) {
      memset(&sb, 0, sizeof(sb));
      sb.magic = KNOTFS_MAGIC;
      sb.version = KNOTFS_VERSION;
      sb.sequence = 0;
      sb.log_offset = SB_HEADER_SZ;
      for (uint32_t i = 0; i < KNOTFS_DATA_START; i++)
        mark_used(i);
      sb.crc32 = sb_checksum(&sb);
      fdev_write(KNOTFS_SB0_BLK, &sb, 0, sizeof(sb));
      cur.st = ST_FMT_WR_SB1;
      return;
    }
    fdev_erase(prog);
    cur.st = ST_FMT_ERASE_WAIT;
    prog++;
    return;
  case ST_FMT_ERASE_WAIT:
    cur.st = ST_FMT_ERASE;
    return;
  case ST_FMT_WR_SB1:
    sb.crc32 = sb_checksum(&sb);
    fdev_write(KNOTFS_SB1_BLK, &sb, 0, sizeof(sb));
    cur.st = ST_FMT_DONE;
    return;
  case ST_FMT_DONE:
    mounted = false;
    cur.st = ST_OK;
    return;
  default:
    return;
  }
}

/* ---- mount ---- */
static void do_mount(void) {
  switch (cur.st) {
  case ST_MNT_RD_SB0:
    fdev_read(KNOTFS_SB0_BLK, &sb, sizeof(sb));
    cur.st = ST_MNT_RD_SB1;
    return;
  case ST_MNT_RD_SB1:
    fdev_read(KNOTFS_SB1_BLK, &sb_alt, sizeof(sb_alt));
    cur.st = ST_MNT_SEL;
    return;
  case ST_MNT_SEL: {
    bool ok0 = (sb.magic == KNOTFS_MAGIC && sb.crc32 == sb_checksum(&sb));
    bool ok1 =
        (sb_alt.magic == KNOTFS_MAGIC && sb_alt.crc32 == sb_checksum(&sb_alt));
    if (!ok0 && !ok1) {
      cur.result = ERR_MNT_FAIL;
      cur.st = ST_ERR;
      return;
    }
    if (!ok0)
      memcpy(&sb, &sb_alt, sizeof(sb));
    else if (ok1 && sb_alt.sequence > sb.sequence)
      memcpy(&sb, &sb_alt, sizeof(sb));
    cur.st = ST_MNT_REPLAY;
    return;
  }

  case ST_MNT_REPLAY:
    replay_log();
    mounted = true;
    cur.st = ST_OK;
    return;
  default:
    return;
  }
}

/* ---- write ---- */
static void do_write(void) {
  knode_t *nd;
  uint32_t need, oldb[KNOTFS_DIRECT_BLKS], oldcnt, i;

  switch (cur.st) {
  case ST_WRT_INIT:
    nd = find_node(cur.name);
    oldcnt = 0;
    if (nd) {
      for (i = 0; i < nd->block_count && i < KNOTFS_DIRECT_BLKS; i++)
        oldb[i] = nd->blocks[i];
      oldcnt = nd->block_count;
      nd->size = 0; /* invalidate old entry so alloc_node won't skip it */
    }
    need = (cur.w_sz + KNOTFS_BLOCK_SIZE - 1) / KNOTFS_BLOCK_SIZE;
    if (need > KNOTFS_DIRECT_BLKS) {
      cur.result = ERR_TOO_MANY;
      cur.st = ST_ERR;
      return;
    }
    nd = alloc_node(cur.name);
    if (!nd) {
      cur.result = ERR_NO_NODE;
      cur.st = ST_ERR;
      return;
    }
    nd->size = cur.w_sz;
    nd->block_count = (uint32_t)need;

    scratch[0] = oldcnt;
    for (i = 0; i < oldcnt && i < KNOTFS_DIRECT_BLKS; i++)
      scratch[1 + i] = oldb[i];
    scratch[9] = need;
    subst = 0;
    cur.st = ST_WRT_ALLOC;
    return;

  case ST_WRT_ALLOC:
    nd = find_node(cur.name);
    if (!nd) {
      cur.result = ERR_RD_COPY;
      cur.st = ST_ERR;
      return;
    }
    need = scratch[SCR_NEW_NEED];
    if (subst >= need) {
      subst = 0;
      cur.st = ST_WRT_DATA;
      return;
    }

    {
      uint32_t blk = pick_lowest_wear();
      if (blk >= KNOTFS_BLOCK_COUNT) {
        cur.result = ERR_NO_BLK;
        cur.st = ST_ERR;
        return;
      }
      mark_used(blk);
      sb.wear[blk]++;
      nd->blocks[subst] = blk;
    }
    subst++;
    return;

  case ST_WRT_DATA: {
    nd = find_node(cur.name);
    if (!nd) {
      cur.result = ERR_RD_COPY;
      cur.st = ST_ERR;
      return;
    }
    need = scratch[SCR_NEW_NEED];
    if (subst >= need) {
      cur.st = ST_WRT_META;
      return;
    }

    uint32_t blk = nd->blocks[subst];
    uint32_t off = subst * KNOTFS_BLOCK_SIZE;
    uint32_t rem = cur.w_sz - off;
    uint32_t n = (rem < KNOTFS_BLOCK_SIZE) ? rem : KNOTFS_BLOCK_SIZE;
    const uint8_t *s = (const uint8_t *)cur.w_buf + off;

    memcpy(tmp, s, n);
    if (n < KNOTFS_BLOCK_SIZE)
      memset(tmp + n, 0xFF, KNOTFS_BLOCK_SIZE - n);
    fdev_write(blk, tmp, 0, KNOTFS_BLOCK_SIZE);
    subst++;
    return;
  }

  case ST_WRT_META: {
    log_reset();
    oldcnt = scratch[SCR_OLD_CNT];
    for (i = 0; i < oldcnt && i < KNOTFS_DIRECT_BLKS; i++) {
      uint32_t blk = scratch[SCR_OLD_BLK(i)];
      if (blk < KNOTFS_BLOCK_COUNT) {
        log_push(LT_BITMAP, (uint8_t)blk, 0);
        mark_free(blk);
      }
    }
    need = scratch[SCR_NEW_NEED];
    nd = find_node(cur.name);
    if (!nd) {
      cur.result = ERR_RD_COPY;
      cur.st = ST_ERR;
      return;
    }
    for (i = 0; i < need && i < KNOTFS_DIRECT_BLKS; i++)
      log_push(LT_BITMAP, (uint8_t)nd->blocks[i], 1);

    /* compact SB to persist file entries + log */
    compact_erase();
    comp_wait = true;
    cur.st = ST_WRT_META_FLUSH;
    return;
  }

  case ST_WRT_META_FLUSH:
    compact_write();
    cur.st = ST_WRT_COMPACT;
    return;

  case ST_WRT_COMPACT:
    compact_finish();
    log_commit();
    cur.st = ST_OK;
    return;

  default:
    return;
  }
}

/* ---- read ---- */
static void do_read(void) {
  knode_t *nd;
  uint32_t sblk, eblk;

  switch (cur.st) {
  case ST_RD_LOOKUP:
    nd = find_node(cur.name);
    if (!nd) {
      cur.result = ERR_RD_NOFILE;
      cur.st = ST_ERR;
      return;
    }
    if (cur.r_off >= nd->size) {
      if (cur.r_out)
        *cur.r_out = 0;
      cur.st = ST_OK;
      return;
    }
    {
      uint32_t end = cur.r_off + cur.r_sz;
      if (end > nd->size)
        end = nd->size;
      sblk = cur.r_off / KNOTFS_BLOCK_SIZE;
      eblk = (end + KNOTFS_BLOCK_SIZE - 1) / KNOTFS_BLOCK_SIZE;
      if (eblk > nd->block_count)
        eblk = nd->block_count;
    }
    scratch[SCR_RD_SBLK] = sblk;
    scratch[SCR_RD_EBLK] = eblk;
    subst = sblk;
    prog = 0;
    cur.st = ST_RD_DATA;
    return;

  case ST_RD_DATA: {
    nd = find_node(cur.name);
    if (!nd) {
      cur.result = ERR_RD_NODE;
      cur.st = ST_ERR;
      return;
    }
    eblk = scratch[SCR_RD_EBLK];
    if (subst >= eblk) {
      if (cur.r_out)
        *cur.r_out = prog;
      cur.st = ST_OK;
      return;
    }
    fdev_read(nd->blocks[subst], tmp, KNOTFS_BLOCK_SIZE);
    cur.st = ST_RD_COPY;
    return;
  }

  case ST_RD_COPY: {
    nd = find_node(cur.name);
    if (!nd) {
      cur.result = ERR_RD_COPY;
      cur.st = ST_ERR;
      return;
    }

    uint32_t bs = subst * KNOTFS_BLOCK_SIZE;
    uint32_t be = bs + KNOTFS_BLOCK_SIZE;
    uint32_t rs = cur.r_off;
    uint32_t re = cur.r_off + cur.r_sz;
    if (re > nd->size)
      re = nd->size;

    uint32_t seg_s = (rs > bs) ? rs : bs;
    uint32_t seg_e = (re < be) ? re : be;
    uint32_t seg_n = (seg_e > seg_s) ? seg_e - seg_s : 0;
    uint32_t src_o = seg_s - bs;

    memcpy((uint8_t *)cur.r_buf + prog, tmp + src_o, seg_n);
    prog += seg_n;
    subst++;
    cur.st = ST_RD_DATA;
    return;
  }

  default:
    return;
  }
}

/* ---- append ---- */
static void do_append(void) {
  knode_t *nd;
  uint32_t last_used, free_in_last, last_blk, new_blk, write_off;

  switch (cur.st) {
  case ST_APP_LOOKUP:
    nd = find_node(cur.name);
    if (!nd) {
      nd = alloc_node(cur.name);
      if (!nd) {
        cur.result = ERR_APP_NOFILE;
        cur.st = ST_ERR;
        return;
      }
    }
    last_used = nd->size;
    if (last_used + cur.a_sz > KNOTFS_FILE_LIMIT) {
      cur.result = ERR_APP_OVF;
      cur.st = ST_ERR;
      return;
    }
    nd->size = last_used + cur.a_sz;

    if (nd->block_count == 0) {
      scratch[SCR_APP_LAST_BLK] = 0;
      scratch[SCR_APP_LEFT] = KNOTFS_BLOCK_SIZE;
    } else {
      uint32_t li = nd->block_count - 1;
      last_blk = nd->blocks[li];
      free_in_last = KNOTFS_BLOCK_SIZE - (last_used % KNOTFS_BLOCK_SIZE);
      if (last_used % KNOTFS_BLOCK_SIZE == 0)
        free_in_last = 0;
      scratch[SCR_APP_LAST_BLK] = last_blk;
      scratch[SCR_APP_LEFT] = free_in_last;
    }
    scratch[SCR_APP_OFF] = last_used;
    subst = 0;
    prog = 0;
    cur.st = ST_APP_ALLOC;
    return;

  case ST_APP_ALLOC:
    if (scratch[SCR_APP_LEFT] == 0) {
      nd = find_node(cur.name);
      if (nd->block_count >= KNOTFS_DIRECT_BLKS) {
        cur.result = ERR_APP_MAXBLK;
        cur.st = ST_ERR;
        return;
      }
      new_blk = pick_lowest_wear();
      if (new_blk >= KNOTFS_BLOCK_COUNT) {
        cur.result = ERR_APP_NOBLK;
        cur.st = ST_ERR;
        return;
      }
      mark_used(new_blk);
      sb.wear[new_blk]++;
      nd->blocks[nd->block_count] = new_blk;
      nd->block_count++;
      scratch[SCR_APP_LAST_BLK] = new_blk;
      scratch[SCR_APP_LEFT] = KNOTFS_BLOCK_SIZE;
    }
    /* read existing block first to merge append data */
    fdev_read(scratch[SCR_APP_LAST_BLK], tmp, KNOTFS_BLOCK_SIZE);
    cur.st = ST_APP_RD_BLK;
    return;

  case ST_APP_RD_BLK: {
    uint32_t blk = scratch[SCR_APP_LAST_BLK];
    write_off = scratch[SCR_APP_OFF] % KNOTFS_BLOCK_SIZE;
    uint32_t space = KNOTFS_BLOCK_SIZE - write_off;
    uint32_t n = (cur.a_sz - prog < space) ? cur.a_sz - prog : space;

    const uint8_t *s = (const uint8_t *)cur.a_buf + prog;
    memcpy(tmp + write_off, s, n);
    fdev_write(blk, tmp, 0, KNOTFS_BLOCK_SIZE);
    prog += n;
    scratch[SCR_APP_OFF] += n;
    cur.st = ST_APP_DATA;
    return;
  }

  case ST_APP_DATA:
    if (prog >= cur.a_sz) {
      log_reset();
      log_push(LT_BITMAP, (uint8_t)scratch[SCR_APP_LAST_BLK], 1);
      log_flush();
      /* compact to persist file entries */
      compact_erase();
      comp_wait = true;
      cur.st = ST_APP_META;
      return;
    }
    scratch[SCR_APP_LEFT] = 0;
    cur.st = ST_APP_ALLOC;
    return;

  case ST_APP_META:
    compact_write();
    cur.st = ST_APP_COMPACT;
    return;

  case ST_APP_COMPACT:
    compact_finish();
    log_commit();
    cur.st = ST_OK;
    return;

  default:
    return;
  }
}

/* ---- delete ---- */
static void do_delete(void) {
  knode_t *nd;
  uint32_t i;

  switch (cur.st) {
  case ST_DEL_LOOKUP:
    nd = find_node(cur.name);
    if (!nd) {
      cur.result = ERR_DEL_NOFILE;
      cur.st = ST_ERR;
      return;
    }
    log_reset();
    for (i = 0; i < nd->block_count && i < KNOTFS_DIRECT_BLKS; i++) {
      if (nd->blocks[i] < KNOTFS_BLOCK_COUNT) {
        log_push(LT_BITMAP, (uint8_t)nd->blocks[i], 0);
        log_push(LT_USED, (uint8_t)nd->blocks[i], 0);
        mark_free(nd->blocks[i]);
      }
    }
    memset(nd, 0, sizeof(knode_t));
    log_flush();
    compact_erase();
    comp_wait = true;
    cur.st = ST_DEL_META;
    return;

  case ST_DEL_META:
    compact_write();
    cur.st = ST_DEL_COMPACT;
    return;

  case ST_DEL_COMPACT:
    compact_finish();
    log_commit();
    cur.st = ST_OK;
    return;

  default:
    return;
  }
}

/* ---- list ---- */
static void do_list(void) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < KNOTFS_MAX_FILES; i++) {
    if (sb.nodes[i].size > 0) {
      if (cur.ls_ents && n < *cur.ls_cnt) {
        strncpy(cur.ls_ents[n].name, sb.nodes[i].name, KNOTFS_NAME_LEN - 1);
        cur.ls_ents[n].size = sb.nodes[i].size;
      }
      n++;
    }
  }
  if (cur.ls_cnt)
    *cur.ls_cnt = n;
  cur.st = ST_OK;
}

/* ---- stats ---- */
static void do_stats(void) {
  uint32_t free = 0;
  for (uint32_t i = KNOTFS_DATA_START; i < KNOTFS_BLOCK_COUNT; i++)
    if (is_free(i))
      free++;
  if (cur.st_tot)
    *cur.st_tot = KNOTFS_DATA_BLKS;
  if (cur.st_free)
    *cur.st_free = free;
  cur.st = ST_OK;
}

/* ==================================== public API ==================================== */

void knotfs_init(void) {
  /* init flash to factory-fresh (all 0xFF) on first call */
  static bool flash_inited;
  if (!flash_inited) {
    memset(flash, 0xFF, sizeof(flash));
    flash_inited = true;
  }

  memset(&fdev, 0, sizeof(fdev));
  memset(&sb, 0, sizeof(sb));
  memset(&sb_alt, 0, sizeof(sb_alt));
  memset(q, 0, sizeof(q));
  memset(&cur, 0, sizeof(cur));
  q_hd = q_tl = q_cnt = 0;
  mounted = false;
  /* compact done */
  comp_wait = false;
  memset(log_buf, 0, sizeof(log_buf));
  log_cnt = 0;
}

int knotfs_format(void) {
  if (!q_empty() || cur.op != OP_NONE)
    return ERR_Q_FULL;
  kreq_t r;
  memset(&r, 0, sizeof(r));
  r.op = OP_FMT;
  r.st = ST_FMT_ERASE;
  return q_push(&r);
}

int knotfs_mount(void) {
  if (!q_empty() || cur.op != OP_NONE)
    return ERR_Q_FULL;
  kreq_t r;
  memset(&r, 0, sizeof(r));
  r.op = OP_MNT;
  r.st = ST_MNT_RD_SB0;
  return q_push(&r);
}

int knotfs_write(const char *name, const void *buf, uint32_t size) {
  if (!name || !buf || !size || size > KNOTFS_FILE_LIMIT)
    return ERR_BAD_PARAM;
  if (!q_empty() || cur.op != OP_NONE)
    return ERR_Q_FULL;
  kreq_t r;
  memset(&r, 0, sizeof(r));
  r.op = OP_WRT;
  r.st = ST_WRT_INIT;
  strncpy(r.name, name, KNOTFS_NAME_LEN - 1);
  r.w_buf = buf;
  r.w_sz = size;
  return q_push(&r);
}

int knotfs_read(const char *name, void *buf, uint32_t size, uint32_t offset,
                uint32_t *bytes_read) {
  if (!name || !buf || !size)
    return ERR_BAD_PARAM;
  if (!q_empty() || cur.op != OP_NONE)
    return ERR_Q_FULL;
  kreq_t r;
  memset(&r, 0, sizeof(r));
  r.op = OP_RD;
  r.st = ST_RD_LOOKUP;
  strncpy(r.name, name, KNOTFS_NAME_LEN - 1);
  r.r_buf = buf;
  r.r_sz = size;
  r.r_off = offset;
  r.r_out = bytes_read;
  return q_push(&r);
}

int knotfs_append(const char *name, const void *buf, uint32_t size) {
  if (!name || !buf || !size)
    return ERR_BAD_PARAM;
  if (!q_empty() || cur.op != OP_NONE)
    return ERR_Q_FULL;
  kreq_t r;
  memset(&r, 0, sizeof(r));
  r.op = OP_APP;
  r.st = ST_APP_LOOKUP;
  strncpy(r.name, name, KNOTFS_NAME_LEN - 1);
  r.a_buf = buf;
  r.a_sz = size;
  return q_push(&r);
}

int knotfs_delete(const char *name) {
  if (!name)
    return ERR_BAD_PARAM;
  if (!q_empty() || cur.op != OP_NONE)
    return ERR_Q_FULL;
  kreq_t r;
  memset(&r, 0, sizeof(r));
  r.op = OP_DEL;
  r.st = ST_DEL_LOOKUP;
  strncpy(r.name, name, KNOTFS_NAME_LEN - 1);
  return q_push(&r);
}

int knotfs_list(knotfs_entry_t *entries, uint32_t *count) {
  if (!count)
    return ERR_BAD_PARAM;
  if (!q_empty() || cur.op != OP_NONE)
    return ERR_Q_FULL;
  kreq_t r;
  memset(&r, 0, sizeof(r));
  r.op = OP_LS;
  r.st = ST_LS_DO;
  r.ls_ents = entries;
  r.ls_cnt = count;
  return q_push(&r);
}

int knotfs_stats(uint32_t *total_blocks, uint32_t *free_blocks) {
  if (!q_empty() || cur.op != OP_NONE)
    return ERR_Q_FULL;
  kreq_t r;
  memset(&r, 0, sizeof(r));
  r.op = OP_ST;
  r.st = ST_ST_DO;
  r.st_tot = total_blocks;
  r.st_free = free_blocks;
  return q_push(&r);
}

bool knotfs_is_idle(void) {
  return cur.op == OP_NONE && q_empty() && fdev_idle();
}

int knotfs_get_result(void) { return cur.result; }
bool knotfs_is_mounted(void) { return mounted; }

int knotfs_run_until_idle(void) {
  while (!knotfs_is_idle())
    knotfs_run();
  return cur.result;
}

/* ==================================== event loop ==================================== */

bool knotfs_run(void) {
  fdev_tick();

  /* handle compact completion */
  if (comp_wait && fdev_idle()) {
    comp_wait = false;
    /* after erase completes, we wrote the compact SB in the state machine */
    /* compact_finish() was already called by the state machine */
  }

  if (!fdev_idle())
    return true;

  /* if current request done, dequeue next */
  if (cur.op != OP_NONE && (cur.st == ST_OK || cur.st == ST_ERR)) {
    if (!q_empty())
      q_pop(&cur);
    else
      cur.op = OP_NONE;
  }

  /* dequeue if idle */
  if (cur.op == OP_NONE && !q_empty()) {
    q_pop(&cur);
    prog = 0;
    subst = 0;
    memset(scratch, 0, sizeof(scratch));
  }

  /* advance state machine */
  if (cur.op != OP_NONE && cur.st != ST_OK && cur.st != ST_ERR) {
    switch (cur.op) {
    case OP_FMT:
      do_format();
      break;
    case OP_MNT:
      do_mount();
      break;
    case OP_WRT:
      do_write();
      break;
    case OP_RD:
      do_read();
      break;
    case OP_APP:
      do_append();
      break;
    case OP_DEL:
      do_delete();
      break;
    case OP_LS:
      do_list();
      break;
    case OP_ST:
      do_stats();
      break;
    default:
      break;
    }
  }

  return !knotfs_is_idle();
}
