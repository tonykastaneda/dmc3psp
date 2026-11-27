/* Tests: the heap by call site (build_testroom.ps1 -HeapStat links malloc, memalign, calloc, realloc and free through
 * these wrappers). Each live block's call site and size go in an open-addressed table keyed by pointer; each site's
 * live bytes in a second. heapstat_dump appends both to a text file. */
#include "heapstat.h"
#include <stdio.h>
#include <stddef.h>

#ifdef HEAPSTAT
#define PTRS 16384   /* live blocks tracked (128 KB of tables in the test build only) */
#define SITES 512
static struct { void *p; void *site; unsigned int size; } ptrs[PTRS];
static struct { void *site; unsigned int live, count; } sites[SITES];
static unsigned int untracked, peak, live_total;

void *__real_malloc(size_t);
void *__real_memalign(size_t, size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);

static unsigned int slot_of(void *p) { return ((unsigned int)p >> 4) * 2654435761u % PTRS; }

static void site_add(void *site, int bytes, int count) {
    unsigned int i = ((unsigned int)site >> 2) % SITES, n;
    for (n = 0; n < SITES; ++n, i = (i + 1) % SITES) {
        if (sites[i].site == site || !sites[i].site) {
            sites[i].site = site; sites[i].live += bytes; sites[i].count += count;
            return;
        }
    }
}

static void track(void *p, size_t size, void *site) {
    unsigned int i, n;
    if (!p) return;
    live_total += size;
    if (live_total > peak) peak = live_total;
    for (i = slot_of(p), n = 0; n < PTRS; ++n, i = (i + 1) % PTRS) {
        if (!ptrs[i].p || ptrs[i].p == (void *)1) {
            ptrs[i].p = p; ptrs[i].site = site; ptrs[i].size = size;
            site_add(site, (int)size, 1);
            return;
        }
    }
    ++untracked;
}

static void untrack(void *p) {
    unsigned int i, n;
    if (!p) return;
    for (i = slot_of(p), n = 0; n < PTRS && ptrs[i].p; ++n, i = (i + 1) % PTRS) {
        if (ptrs[i].p == p) {
            site_add(ptrs[i].site, -(int)ptrs[i].size, -1);
            live_total -= ptrs[i].size;
            ptrs[i].p = (void *)1;   /* a tombstone: later probes go on past it */
            return;
        }
    }
}

void *__wrap_malloc(size_t n) { void *p = __real_malloc(n); track(p, n, __builtin_return_address(0)); return p; }
void *__wrap_memalign(size_t a, size_t n) { void *p = __real_memalign(a, n); track(p, n, __builtin_return_address(0)); return p; }
void *__wrap_calloc(size_t c, size_t n) { void *p = __real_calloc(c, n); track(p, c * n, __builtin_return_address(0)); return p; }
void *__wrap_realloc(void *o, size_t n) {
    void *p = __real_realloc(o, n);
    if (p || !n) { untrack(o); track(p, n, __builtin_return_address(0)); }
    return p;
}
void __wrap_free(void *p) { untrack(p); __real_free(p); }

void heapstat_dump(const char *path, const char *label) {
    FILE *f = fopen(path, "a");
    int i;
    if (!f) return;
    fprintf(f, "== %s live=%u peak=%u untracked=%u\n", label, live_total, peak, untracked);
    for (i = 0; i < SITES; ++i)
        if (sites[i].site && sites[i].live >= 16384u) fprintf(f, "%08x %u %u\n", (unsigned int)sites[i].site, sites[i].live, sites[i].count);
    fclose(f);
}
#else
void heapstat_dump(const char *path, const char *label) { (void)path; (void)label; }
#endif
