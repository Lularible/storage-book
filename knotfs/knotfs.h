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
 *  knotfs.h — KnotFS async log-structured embedded file system (teaching edition)
 *  Zero-dependency, RAM-simulated NOR Flash (64 KB = 16 blocks x 4 KB).
 *  Preserves the async state-machine architecture of production SimpleFS
 *  while being fully compilable on any desktop with just libc.
 *
 *  Key educational features:
 *    - Dual superblock + log-structured metadata updates
 *                   - Cooperative async state machine (knotfs_run() event loop)
 *                   - Copy-on-Write atomic file writes
 *                   - Wear leveling (lowest-wear block selection)
 *                   - Power-loss safe remount (superblock redundancy)
 *                   - Tick-based flash delay simulation (observable async behavior)
 *********************************************************************************************************************/

#ifndef KNOTFS_H_
#define KNOTFS_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/*  Constants                                                                 */
/* -------------------------------------------------------------------------- */

#define KNOTFS_BLOCK_SIZE (4096U) /* bytes per flash block            */
#define KNOTFS_BLOCK_COUNT (16U)  /* total blocks (16 x 4K = 64 KB)   */
#define KNOTFS_SB0_BLK (0U)       /* superblock A lives in block 0     */
#define KNOTFS_SB1_BLK (1U)       /* superblock B lives in block 1     */
#define KNOTFS_DATA_START (2U)    /* first data block                 */
#define KNOTFS_DATA_BLKS (KNOTFS_BLOCK_COUNT - KNOTFS_DATA_START) /* 14 */
#define KNOTFS_MAX_FILES (8U)   /* max concurrent files             */
#define KNOTFS_NAME_LEN (32U)   /* max file name length             */
#define KNOTFS_DIRECT_BLKS (8U) /* max data blocks per file         */
#define KNOTFS_FILE_LIMIT (KNOTFS_DIRECT_BLKS * KNOTFS_BLOCK_SIZE) /* 32K */

#define KNOTFS_MAGIC (0x4B4E4653U) /* "KNFS" little-endian           */
#define KNOTFS_VERSION (1U)

#define KNOTFS_LOG_THRESHOLD (50U) /* compact when log 50% full */

/* -------------------------------------------------------------------------- */
/*  Types                                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
  char name[KNOTFS_NAME_LEN];
  uint32_t size;
} knotfs_entry_t;

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */

/* Initialize the flash simulator and runtime state.
 * Does NOT erase flash — call this on every boot, then knotfs_mount().  */
void knotfs_init(void);

/* Format: erase all blocks and write fresh superblocks (async). */
int knotfs_format(void);

/* Mount: read superblocks, select the newer one, replay log (async). */
int knotfs_mount(void);

/* Write a file (async, Copy-on-Write). Creates or overwrites atomically. */
int knotfs_write(const char *name, const void *buf, uint32_t size);

/* Read a file (async). */
int knotfs_read(const char *name, void *buf, uint32_t size, uint32_t offset,
                uint32_t *bytes_read);

/* Append to a file (async). Creates the file if it doesn't exist. */
int knotfs_append(const char *name, const void *buf, uint32_t size);

/* Delete a file (async). */
int knotfs_delete(const char *name);

/* List all files (async). */
int knotfs_list(knotfs_entry_t *entries, uint32_t *count);

/* Filesystem statistics (async). */
int knotfs_stats(uint32_t *total_blocks, uint32_t *free_blocks);

/* ---- Event Loop ---- */

/* Advance the async event loop. Call in a tight loop until idle. */
bool knotfs_run(void);

/* True when no operation is in progress or queued. */
bool knotfs_is_idle(void);

/* Result of the last completed operation (0 = success, < 0 = error). */
int knotfs_get_result(void);

/* Blocking convenience: run until idle, then return the result. */
int knotfs_run_until_idle(void);

/* True when the filesystem is mounted. */
bool knotfs_is_mounted(void);

#ifdef __cplusplus
}
#endif

#endif /* KNOTFS_H_ */
