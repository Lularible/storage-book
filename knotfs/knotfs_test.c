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
 *  knotfs_test.c — KnotFS self-test and demo, complete walkthrough
 */

#include "knotfs.h"
#include <stdio.h>
#include <string.h>

static int ok_ct = 0;
static int all_ct = 0;

#define T(name)                                                                \
  do {                                                                         \
    printf("  [%s] ", name);                                                   \
    all_ct++;                                                                  \
  } while (0)
#define OK()                                                                   \
  do {                                                                         \
    printf("OK\n");                                                            \
    ok_ct++;                                                                   \
  } while (0)
#define FAIL(f, ...) printf("FAIL: " f "\n", ##__VA_ARGS__)

static void sep(const char *title) {
  printf("\n============================================================\n");
  printf("  %s\n", title);
  printf("============================================================\n");
}

/* drive event loop with periodic progress display */
static int drive(void) {
  int t = 0;
  while (knotfs_run()) {
    if (++t % 15 == 0)
      printf("    tick %d...\n", t);
  }
  if (t > 0 && t % 15 != 0)
    printf("    tick %d... done\n", t);
  return knotfs_get_result();
}

/* "power-cycle" the MCU: clear in-memory state, keep flash intact */
static void power_cycle(void) {
  knotfs_init(); /* clears runtime state, does NOT erase flash */
}

/* ---- test cases ---- */

static void t1_format_and_mount(void) {
  sep("Test 1: Format & Mount  (64 KB NOR Flash)");
  printf("  Flash layout: %u blocks x %u B = %u bytes\n", KNOTFS_BLOCK_COUNT,
         KNOTFS_BLOCK_SIZE, KNOTFS_BLOCK_COUNT * KNOTFS_BLOCK_SIZE);
  printf("  Data region: blocks %u–%u  (%u KB)\n", KNOTFS_DATA_START,
         KNOTFS_BLOCK_COUNT - 1, KNOTFS_DATA_BLKS * KNOTFS_BLOCK_SIZE / 1024);

  T("format");
  int r = knotfs_format();
  if (r) {
    FAIL("enqueue=%d", r);
    return;
  }
  r = drive();
  if (r == 0)
    OK();
  else {
    FAIL("result=%d", r);
    return;
  }

  T("mount");
  r = knotfs_mount();
  if (r) {
    FAIL("enqueue=%d", r);
    return;
  }
  r = drive();
  if (r == 0 && knotfs_is_mounted())
    OK();
  else {
    FAIL("result=%d", r);
    return;
  }

  T("stats");
  uint32_t tot, fre;
  knotfs_stats(&tot, &fre);
  drive();
  if (fre == KNOTFS_DATA_BLKS)
    OK();
  else
    FAIL("free=%u expected=%u", fre, KNOTFS_DATA_BLKS);
}

static void t2_write_and_read(void) {
  sep("Test 2: Write & Read (small file)");

  {
    const char *s = "Hello, KnotFS!";
    uint32_t n = (uint32_t)strlen(s);
    T("write 14B");
    if (knotfs_write("greet.txt", s, n)) {
      FAIL("enqueue");
      return;
    }
    if (drive()) {
      FAIL("result");
      return;
    }
    OK();
  }

  {
    char b[32];
    uint32_t br;
    T("read back");
    if (knotfs_read("greet.txt", b, sizeof(b), 0, &br)) {
      FAIL("enqueue");
      return;
    }
    if (drive()) {
      FAIL("result");
      return;
    }
    if (br == 14 && !memcmp(b, "Hello, KnotFS!", 14))
      OK();
    else
      FAIL("got='%.14s' len=%u", b, br);
  }
}

static void t3_large_file(void) {
  sep("Test 3: Large File  (2 blocks = 8192 B)");
  static uint8_t pat[8192];
  for (int i = 0; i < 8192; i++)
    pat[i] = (uint8_t)(i & 0xFF);

  T("write 8KB");
  if (knotfs_write("data.bin", pat, 8192)) {
    FAIL("enqueue");
    return;
  }
  if (drive()) {
    FAIL("result");
    return;
  }
  OK();

  T("read & verify");
  {
    uint8_t v[8192];
    uint32_t br;
    memset(v, 0, sizeof(v));
    if (knotfs_read("data.bin", v, 8192, 0, &br)) {
      FAIL("enqueue");
      return;
    }
    if (drive()) {
      FAIL("result");
      return;
    }
    if (br == 8192 && !memcmp(v, pat, 8192))
      OK();
    else
      FAIL("br=%u match=%d", br, memcmp(v, pat, 8192) == 0);
  }
}

static void t4_partial_read(void) {
  sep("Test 4: Partial Read  (offset=2000, size=32)");
  T("read offset");
  uint8_t buf[32];
  uint32_t br;
  memset(buf, 0, sizeof(buf));
  if (knotfs_read("data.bin", buf, 32, 2000, &br)) {
    FAIL("enqueue");
    return;
  }
  if (drive()) {
    FAIL("result");
    return;
  }
  uint8_t exp[32];
  for (int i = 0; i < 32; i++)
    exp[i] = (uint8_t)((2000 + i) & 0xFF);
  if (br == 32 && !memcmp(buf, exp, 32))
    OK();
  else
    FAIL("br=%u", br);
}

static void t5_overwrite(void) {
  sep("Test 5: Overwrite  (atomic Copy-on-Write)");
  T("overwrite");
  const char *n = "greet.txt";
  const char *s = "This file has been replaced!";
  uint32_t len = (uint32_t)strlen(s);
  if (knotfs_write(n, s, len)) {
    FAIL("enqueue");
    return;
  }
  if (drive()) {
    FAIL("result");
    return;
  }

  char b[64];
  uint32_t br;
  if (knotfs_read(n, b, sizeof(b), 0, &br)) {
    FAIL("enqueue");
    return;
  }
  if (drive()) {
    FAIL("result");
    return;
  }
  if (br == len && !memcmp(b, s, len))
    OK();
  else
    FAIL("got='%s'", b);
}

static void t6_append(void) {
  sep("Test 6: Append");
  T("append");
  const char *add = " (appended)";
  uint32_t alen = (uint32_t)strlen(add);
  if (knotfs_append("greet.txt", add, alen)) {
    FAIL("enqueue");
    return;
  }
  if (drive()) {
    FAIL("result");
    return;
  }

  char b[128];
  uint32_t br;
  if (knotfs_read("greet.txt", b, sizeof(b), 0, &br)) {
    FAIL("enqueue");
    return;
  }
  if (drive()) {
    FAIL("result");
    return;
  }
  const char *exp = "This file has been replaced! (appended)";
  if (br == (uint32_t)strlen(exp) && !strcmp(b, exp))
    OK();
  else
    FAIL("got='%s'", b);
}

static void t7_list(void) {
  sep("Test 7: List Files");
  T("list");
  knotfs_entry_t ents[8];
  uint32_t n = 8;
  knotfs_list(ents, &n);
  drive();
  printf("  %u files:\n", n);
  for (uint32_t i = 0; i < n; i++)
    printf("    %-32s %u B\n", ents[i].name, ents[i].size);
  if (n == 2)
    OK();
  else
    FAIL("expected 2, got %u", n);
}

static void t8_delete(void) {
  sep("Test 8: Delete");
  T("delete data.bin");
  if (knotfs_delete("data.bin")) {
    FAIL("enqueue");
    return;
  }
  if (drive()) {
    FAIL("result");
    return;
  }
  knotfs_entry_t e[8];
  uint32_t n = 8;
  knotfs_list(e, &n);
  drive();
  if (n == 1 && !strcmp(e[0].name, "greet.txt"))
    OK();
  else
    FAIL("n=%u", n);
}

static void t9_nonexistent(void) {
  sep("Test 9: Read Nonexistent");
  T("read missing");
  char b[32];
  uint32_t br = 99;
  knotfs_read("nope.bin", b, sizeof(b), 0, &br);
  int r = drive();
  if (r != 0)
    OK();
  else
    FAIL("should fail, got br=%u", br);
}

static void t10_stats(void) {
  sep("Test 10: Stats");
  uint32_t tot, fre;
  knotfs_stats(&tot, &fre);
  drive();
  printf("  total: %u  free: %u  used: %u\n", tot, fre, tot - fre);
  T("stats");
  if (tot == KNOTFS_DATA_BLKS && fre < tot)
    OK();
  else
    FAIL("tot=%u free=%u", tot, fre);
}

static void t11_power_loss(void) {
  sep("Test 11: Power-Loss Recovery  (remount after simulated reset)");

  /* capture state before power-cycle */
  knotfs_entry_t before[8];
  uint32_t bn = 8;
  knotfs_list(before, &bn);
  drive();

  char b64[64];
  uint32_t br64;
  knotfs_read("greet.txt", b64, sizeof(b64), 0, &br64);
  drive();
  printf("  Before reset: '%s'\n", b64);
  printf(
      "  Simulating MCU power-cycle (in-memory state lost, flash intact)...\n");

  /* power-cycle */
  power_cycle();

  T("remount");
  if (knotfs_mount()) {
    FAIL("enqueue");
    return;
  }
  if (drive()) {
    FAIL("result");
    return;
  }
  if (!knotfs_is_mounted()) {
    FAIL("not mounted");
    return;
  }
  OK();

  knotfs_entry_t after[8];
  uint32_t an = 8;
  knotfs_list(after, &an);
  drive();
  printf("  After reset: %u files\n", an);

  char a64[64];
  uint32_t ar64;
  knotfs_read("greet.txt", a64, sizeof(a64), 0, &ar64);
  drive();
  printf("  After reset: '%s'\n", a64);

  T("data intact");
  if (bn == an && ar64 == br64 && !strcmp(b64, a64))
    OK();
  else
    FAIL("before='%s' after='%s'", b64, a64);
}

static void t12_format_and_remount(void) {
  sep("Test 12: Full Remount After Format");

  /* fresh format */
  power_cycle();
  knotfs_format();
  drive();
  printf("  Fresh format done.\n");

  T("mount fresh");
  if (knotfs_mount()) {
    FAIL("enqueue");
    return;
  }
  if (drive()) {
    FAIL("result");
    return;
  }
  OK();

  /* write a file and remount */
  const char *msg = "Persistent content across boots.";
  uint32_t mlen = (uint32_t)strlen(msg);
  knotfs_write("boot.txt", msg, mlen);
  drive();

  power_cycle();

  T("remount & verify");
  if (knotfs_mount()) {
    FAIL("enqueue");
    return;
  }
  if (drive()) {
    FAIL("result");
    return;
  }
  char chk[64];
  uint32_t cr;
  memset(chk, 0, sizeof(chk));
  knotfs_read("boot.txt", chk, sizeof(chk), 0, &cr);
  drive();
  if (cr == mlen && !memcmp(chk, msg, mlen))
    OK();
  else
    FAIL("cr=%u expected=%u '%.32s'", cr, mlen, chk);
}

/* ---- main ---- */

int main(void) {
  printf("\n");
  printf("  _  __            _   _____ ____  \n");
  printf(" | |/ /_ __   ___ | |_|  ___/ ___| \n");
  printf(" | ' /| '_ \\ / _ \\| __| |_  \\___ \\ \n");
  printf(" | . \\| | | | (_) | |_|  _|  ___) |\n");
  printf(" |_|\\_\\_| |_|\\___/ \\__|_|   |____/ \n");
  printf("\n");
  printf("  Teaching File System — Async + Log-Structured + Power-Loss Safe\n");
  printf("  Simulated NOR Flash: %u KB\n\n",
         KNOTFS_BLOCK_COUNT * KNOTFS_BLOCK_SIZE / 1024);

  knotfs_init(); /* first-ever boot: flash initialised to 0xFF */

  t1_format_and_mount();
  t2_write_and_read();
  t3_large_file();
  t4_partial_read();
  t5_overwrite();
  t6_append();
  t7_list();
  t8_delete();
  t9_nonexistent();
  t10_stats();
  t11_power_loss();
  t12_format_and_remount();

  printf("\n============================================================\n");
  printf("  RESULTS: %d / %d tests passed\n", ok_ct, all_ct);
  printf("============================================================\n\n");
  return (ok_ct == all_ct) ? 0 : 1;
}
