/*
 * hraudit.c - High refresh audit (BO-275), a debug aid.
 *
 * Burnout 3 assumes one picture a 60 Hz step. With High refresh the title's
 * Update runs with 0 steps between two steps and only draws; anything it
 * still writes or advances in such a picture is a suspect (the cameras'
 * sway, the sparks' history, the slam rock: found one by one in play). This
 * finds them all at once.
 *
 * Off unless BO3_HRAUDIT_CTL=<file> is set. The file is read about once a
 * second; a line "go <tag> [blend|noblend] [diff samples] [watch samples]"
 * starts a cycle (a scene driver writes it once the scene is on screen):
 *
 * 1. Diff. At the start of a picture with 0 steps (High refresh active,
 *    after the last picture's blends were put back, right before the
 *    title's Update) guest RAM is copied; at the start of the next Update
 *    (its blends put back, before any step) it is compared, by 16-byte
 *    chunk. Main RAM (0x10000 to the heap's top) and the contiguous window
 *    (0x80000000, 64 MB: the D3D pushbuffer, vertex buffers, textures).
 *    Every 4th sample is a control instead: the copy, the title held for as
 *    long as the last picture took, the compare: what the other threads
 *    (audio, the GPU executor, the kernel timer, the title's own workers)
 *    change on their own. "noblend": the pictures of the cycle are drawn at
 *    the last step (u = 1), so a picture between steps draws exactly what
 *    the one before drew and the diff has only what it advances (counters,
 *    histories, clocks, random seeds), not caches recomputed from a blend.
 *
 * 2. Watch (Linux x86-64). The chunks the pictures changed and the controls
 *    never did, outside the guest stacks and runs shorter than
 *    BO3_HRAUDIT_WMAX bytes (default 16 KB), in main RAM: their pages are
 *    made read-only for the length of a sampled 0-step picture. A write
 *    faults; the handler notes the address, the old value and the host
 *    call chain (backtrace: lifted calls are native calls, so the chain is
 *    the guest's sub_XXXXXXXX frames), opens the page and single-steps the
 *    store (the trap flag); the step's trap closes the page again and notes
 *    the new value. Every write on the title's thread and the others is
 *    seen except another thread's in that one-instruction window.
 *
 * BO3_HRAUDIT_SPACING=<n>: at least n Updates between two samples (12),
 * to spread a cycle over more of a scene.
 *
 * Counters (BO-280), with the control file set, High refresh on or off: a
 * line every 10 s of steps, Updates, the HUD's update calls and the time
 * they were given, and the race audio manager's calls and the times its
 * body ran. BO3_HRAUDIT_FLASH=<s>[,<s>...]: s seconds into the game's
 * running state a race-end flash is started on the effects clock (0x5A9A00:
 * t 0, the race end's duration 0x3B1684, mode 1) and its length is logged
 * in steps, Updates and ms when its mode goes back to 0.
 *
 * Output: BO3_HRAUDIT_OUT=<dir> (default the working directory):
 * <tag>.diff.txt (runs: address, bytes, pictures/controls that changed it,
 * the current bytes) and <tag>.watch.txt (per run, its writers: count,
 * thread, old and new value, the chain as offsets in the executable, for
 * tools/headless/hraudit_sym.py or nm). A line per sample to stderr.
 */
#include "hraudit.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "recomp_types.h"
#include "kernel/xbox_memory_layout.h"
#include "region.h"

#if defined(__linux__) && defined(__x86_64__)
#define HRA_WATCH 1
#include <signal.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#endif
#ifndef _WIN32
#include <time.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#define CH        16u                       /* bytes a chunk */
#define CONTIG_VA 0x80000000u
#define CONTIG_SZ (64u * 1024u * 1024u)

typedef struct {
    uint32_t va, size;
    uint8_t  *snap;
    uint16_t *pic, *idle;
} region_t;

static int       s_on = -1;
static const char *s_ctl, *s_out;
static region_t  s_reg[2];
static unsigned  s_nreg;

static struct {
    int      on, noblend, ndiff, nwatch;
    int      dn, wn, cn;                    /* diff samples, watch samples, controls taken */
    int      pending;                       /* 1 a diff sample, 2 a watch sample open */
    unsigned since;                         /* Updates since the last sample */
    uint64_t t0;                            /* host ns at the sample's start */
    double   last_ms;                       /* the last sampled picture's length */
    char     tag[64];
} s_cyc;
static struct { unsigned calls; double given, scaled; uint32_t step0; } s_hud;
static long      s_ctl_seen;                /* the control file's size when last read */
static unsigned  s_poll;
static unsigned  s_spacing = 12;          /* Updates between samples, at least (BO3_HRAUDIT_SPACING) */

static uint64_t now_ns(void)
{
#ifdef _WIN32
    return 0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static uint8_t *host(uint32_t va)
{
    return (uint8_t *)(uintptr_t)XBOX_PTR(va);
}

static int in_stack(uint32_t va)
{
    return va >= g_xbox_stack_base && va < g_xbox_stack_base + g_xbox_stack_size;
}

static int init(void)
{
    if (s_on >= 0)
        return s_on;
    s_ctl = getenv("BO3_HRAUDIT_CTL");
    s_out = getenv("BO3_HRAUDIT_OUT");
    if (getenv("BO3_HRAUDIT_SPACING"))
        s_spacing = (unsigned)atoi(getenv("BO3_HRAUDIT_SPACING"));
    s_on = s_ctl && *s_ctl;
    if (!s_on)
        return 0;
    s_reg[0].va = 0x10000u;
    s_reg[0].size = XBOX_HEAP_TOP - 0x10000u;
    s_reg[1].va = CONTIG_VA;
    s_reg[1].size = CONTIG_SZ;
    s_nreg = 2;
    for (unsigned i = 0; i < s_nreg; i++) {
        region_t *r = &s_reg[i];
        r->snap = malloc(r->size);
        r->pic = calloc(r->size / CH, sizeof *r->pic);
        r->idle = calloc(r->size / CH, sizeof *r->idle);
        if (!r->snap || !r->pic || !r->idle) {
            fprintf(stderr, "[HRAUDIT] out of memory\n");
            s_on = 0;
            return 0;
        }
    }
    fprintf(stderr, "[HRAUDIT] armed: control file %s, RAM 0x%08X..0x%08X + contiguous window, stacks 0x%08X..0x%08X\n",
            s_ctl, s_reg[0].va, s_reg[0].va + s_reg[0].size, g_xbox_stack_base,
            g_xbox_stack_base + g_xbox_stack_size);
    return 1;
}

static void snapshot(void)
{
    for (unsigned i = 0; i < s_nreg; i++)
        memcpy(s_reg[i].snap, host(s_reg[i].va), s_reg[i].size);
}

/* Compare with the copy; count each changed chunk in cnt (the pictures'
 * or the controls'). Returns the chunks changed. */
static unsigned compare(int idle)
{
    unsigned n = 0;
    for (unsigned i = 0; i < s_nreg; i++) {
        region_t *r = &s_reg[i];
        const uint8_t *cur = host(r->va);
        uint16_t *cnt = idle ? r->idle : r->pic;
        for (uint32_t p = 0; p < r->size; p += 4096) {
            uint32_t len = r->size - p < 4096 ? r->size - p : 4096;
            if (!memcmp(cur + p, r->snap + p, len))
                continue;
            for (uint32_t c = p; c < p + len; c += CH)
                if (memcmp(cur + c, r->snap + c, CH)) {
                    if (cnt[c / CH] < 0xFFFF)
                        cnt[c / CH]++;
                    n++;
                }
        }
    }
    return n;
}

static FILE *out_file(const char *suffix)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.%s", s_out && *s_out ? s_out : ".", s_cyc.tag, suffix);
    return fopen(path, "w");
}

/* ── the runs: consecutive changed chunks (a gap of one chunk joined) ── */

typedef struct {
    uint32_t va, len;
    unsigned pic, idle;                     /* the most pictures / controls any chunk changed in */
    int      watched;
} run_t;

static run_t   *s_run;
static unsigned s_nrun, s_caprun;

static void runs_build(void)
{
    s_nrun = 0;
    for (unsigned i = 0; i < s_nreg; i++) {
        region_t *r = &s_reg[i];
        uint32_t n = r->size / CH, c = 0;
        while (c < n) {
            uint32_t s, e, gap;
            unsigned mp = 0, mi = 0;
            if (!r->pic[c]) { c++; continue; }
            s = c; e = c; gap = 0;
            for (; c < n; c++) {
                if (r->pic[c]) {
                    e = c; gap = 0;
                    if (r->pic[c] > mp) mp = r->pic[c];
                    if (r->idle[c] > mi) mi = r->idle[c];
                } else if (++gap > 1) {
                    break;
                }
            }
            if (s_nrun == s_caprun) {
                s_caprun = s_caprun ? s_caprun * 2 : 4096;
                s_run = realloc(s_run, s_caprun * sizeof *s_run);
            }
            s_run[s_nrun].va = r->va + s * CH;
            s_run[s_nrun].len = (e - s + 1) * CH;
            s_run[s_nrun].pic = mp;
            s_run[s_nrun].idle = mi;
            s_run[s_nrun].watched = 0;
            s_nrun++;
        }
    }
}

static int run_of(uint32_t va)
{
    int lo = 0, hi = (int)s_nrun - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (va < s_run[m].va) hi = m - 1;
        else if (va >= s_run[m].va + s_run[m].len) lo = m + 1;
        else return m;
    }
    return -1;
}

static void dump_bytes(FILE *f, uint32_t va, uint32_t len)
{
    const uint8_t *p = host(va), *o = NULL;
    uint32_t n = len < 32 ? len : 32;
    for (unsigned i = 0; i < s_nreg; i++)
        if (va >= s_reg[i].va && va - s_reg[i].va < s_reg[i].size)
            o = s_reg[i].snap + (va - s_reg[i].va);
    if (o && memcmp(o, p, n)) {
        /* the last sample's copy (before its picture), for the change */
        fprintf(f, "   was");
        for (uint32_t k = 0; k + 4 <= n; k += 4) {
            uint32_t w;
            float fl;
            memcpy(&w, o + k, 4);
            memcpy(&fl, o + k, 4);
            fprintf(f, " %08X(%g)", w, fl);
        }
        fprintf(f, "\n");
    }
    fprintf(f, "   ");
    for (uint32_t k = 0; k + 4 <= n; k += 4) {
        uint32_t w;
        float fl;
        memcpy(&w, p + k, 4);
        memcpy(&fl, p + k, 4);
        fprintf(f, " %08X(%g)", w, fl);
    }
    fprintf(f, "\n");
}

static void report_diff(void)
{
    FILE *f = out_file("diff.txt");
    unsigned big = 0;
    runs_build();
    if (!f)
        return;
    fprintf(f, "# BO-275 diff '%s' (%s): %d pictures with 0 steps, %d controls\n"
               "# run: va len pictures controls [stack] / first 32 bytes now (hex(float))\n",
            s_cyc.tag, s_cyc.noblend ? "noblend" : "blend", s_cyc.dn, s_cyc.cn);
    for (unsigned i = 0; i < s_nrun; i++) {
        run_t *r = &s_run[i];
        fprintf(f, "R %08X %u %u %u%s\n", r->va, r->len, r->pic, r->idle,
                in_stack(r->va) ? " stack" : "");
        if (r->len <= 4096)
            dump_bytes(f, r->va, r->len);
        else
            big++;
    }
    fprintf(f, "# %u runs, %u over 4 KB\n", s_nrun, big);
    fprintf(f, "# HUD update (sub_000535E0): %u calls over %u steps, time given %.4f s, after the High refresh scaling %.4f s\n",
            s_hud.calls, MEM32(BO3_VA(0x4A1EB4u, 0x4A21D4u, 0x4A2274u)) - s_hud.step0, s_hud.given, s_hud.scaled);
    memset(&s_hud, 0, sizeof s_hud);
    fclose(f);
    fprintf(stderr, "[HRAUDIT] %s: %u runs written\n", s_cyc.tag, s_nrun);
}

/* ── 2. The watch (Linux x86-64) ─────────────────────────────── */

#ifdef HRA_WATCH
#define RING_N  (1u << 16)
#define BT_N    12
typedef struct {
    uint32_t va;
    uint8_t  other;                         /* not the title's thread */
    uint8_t  nbt;
    uint64_t old, nw;
    void    *bt[BT_N];
} hit_t;
static hit_t           *s_ring;
static volatile unsigned s_ring_n, s_ring_drop;
static uint8_t         *s_wbits;            /* a bit a chunk of main RAM: watched */
static uint8_t         *s_pbits;            /* a bit a 4 KB page of main RAM: protected now */
static uint32_t        *s_pages;            /* the pages to protect */
static unsigned         s_npages;
static volatile int     s_armed;
static pthread_t        s_title;
static struct sigaction s_prev_segv, s_prev_trap;
static __thread uintptr_t t_open;           /* the page this thread opened for its store */
static __thread hit_t    *t_hit;
static uintptr_t        s_exe_base;
static unsigned long    s_undec;

static int wbit(uint32_t va)
{
    uint32_t c = (va - s_reg[0].va) / CH;
    return (s_wbits[c >> 3] >> (c & 7)) & 1;
}

static void hra_segv(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = (ucontext_t *)ucv;
    uintptr_t fault = (uintptr_t)si->si_addr, base = (uintptr_t)host(0);
    struct sigaction *prev = &s_prev_segv;

    if (s_armed && fault >= base + s_reg[0].va && fault < base + s_reg[0].va + s_reg[0].size) {
        uint32_t va = (uint32_t)(fault - base);
        uint32_t pg = (va - s_reg[0].va) >> 12;
        if ((s_pbits[pg >> 3] >> (pg & 7)) & 1) {
            uintptr_t hp = fault & ~(uintptr_t)4095;
            t_hit = NULL;
            if (wbit(va)) {
                unsigned k = __atomic_fetch_add(&s_ring_n, 1, __ATOMIC_RELAXED);
                if (k < RING_N) {
                    hit_t *h = &s_ring[k];
                    h->va = va;
                    h->other = !pthread_equal(pthread_self(), s_title);
                    memcpy(&h->old, (const void *)fault, 8);
                    h->nw = 0;
                    h->nbt = (uint8_t)backtrace(h->bt, BT_N);
                    t_hit = h;
                } else {
                    __atomic_fetch_add(&s_ring_drop, 1, __ATOMIC_RELAXED);
                }
            }
            mprotect((void *)hp, 4096, PROT_READ | PROT_WRITE);
            t_open = hp;
            uc->uc_mcontext.gregs[REG_EFL] |= 0x100;     /* step the store */
            return;
        }
    }
    if (prev->sa_flags & SA_SIGINFO) {
        if (prev->sa_sigaction) { prev->sa_sigaction(sig, si, ucv); return; }
    } else if (prev->sa_handler != SIG_DFL && prev->sa_handler != SIG_IGN) {
        prev->sa_handler(sig);
        return;
    }
    signal(sig, SIG_DFL);
}

static void hra_trap(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = (ucontext_t *)ucv;
    struct sigaction *prev = &s_prev_trap;
    if (t_open) {
        if (t_hit)
            memcpy(&t_hit->nw, host(t_hit->va), 8);
        if (s_armed)
            mprotect((void *)t_open, 4096, PROT_READ);
        t_open = 0;
        t_hit = NULL;
        uc->uc_mcontext.gregs[REG_EFL] &= ~(long long)0x100;
        return;
    }
    if (prev->sa_flags & SA_SIGINFO) {
        if (prev->sa_sigaction) { prev->sa_sigaction(sig, si, ucv); return; }
    } else if (prev->sa_handler != SIG_DFL && prev->sa_handler != SIG_IGN) {
        prev->sa_handler(sig);
        return;
    }
    signal(sig, SIG_DFL);
}

static void watch_setup(void)
{
    static int installed;
    uint32_t wmax = 16384;
    const char *e = getenv("BO3_HRAUDIT_WMAX");
    size_t nch = s_reg[0].size / CH, npg = s_reg[0].size / 4096;

    if (e && *e)
        wmax = (uint32_t)strtoul(e, NULL, 0);
    if (!s_ring) {
        void *probe[2];
        Dl_info di;
        s_ring = calloc(RING_N, sizeof *s_ring);
        s_wbits = calloc(nch / 8 + 1, 1);
        s_pbits = calloc(npg / 8 + 1, 1);
        s_pages = calloc(npg, sizeof *s_pages);
        backtrace(probe, 2);                 /* loads the unwinder outside a handler */
        if (dladdr((void *)&watch_setup, &di))
            s_exe_base = (uintptr_t)di.dli_fbase;
    }
    memset(s_wbits, 0, nch / 8 + 1);
    memset(s_pbits, 0, npg / 8 + 1);
    s_npages = 0;
    for (unsigned i = 0; i < s_nrun; i++) {
        run_t *r = &s_run[i];
        if (r->va < s_reg[0].va || r->va >= s_reg[0].va + s_reg[0].size || r->idle
                || in_stack(r->va) || r->len > wmax)
            continue;
        r->watched = 1;
        for (uint32_t a = r->va; a < r->va + r->len; a += CH) {
            uint32_t c = (a - s_reg[0].va) / CH, pg = (a - s_reg[0].va) >> 12;
            s_wbits[c >> 3] |= (uint8_t)(1u << (c & 7));
            if (!((s_pbits[pg >> 3] >> (pg & 7)) & 1)) {
                s_pbits[pg >> 3] |= (uint8_t)(1u << (pg & 7));
                s_pages[s_npages++] = pg;
            }
        }
    }
    if (!installed) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = hra_segv;
        sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, &s_prev_segv);
        sa.sa_sigaction = hra_trap;
        sigaction(SIGTRAP, &sa, &s_prev_trap);
        installed = 1;
    }
    s_ring_n = s_ring_drop = 0;
    fprintf(stderr, "[HRAUDIT] %s: watching %u pages\n", s_cyc.tag, s_npages);
}

static void watch_arm(int on)
{
    if (on) {
        s_title = pthread_self();
        s_armed = 1;
    }
    for (unsigned i = 0; i < s_npages; i++)
        mprotect(host(s_reg[0].va + (s_pages[i] << 12)), 4096, on ? PROT_READ : PROT_READ | PROT_WRITE);
    if (!on)
        s_armed = 0;
}

/* The hits so far, folded by run and chain. */
typedef struct {
    int      run;
    uint8_t  other, nbt;
    void    *bt[BT_N];
    unsigned n;
    uint32_t va;
    uint64_t old, nw;
} agg_t;
static agg_t   *s_agg;
static unsigned s_nagg, s_capagg;

static void watch_fold(void)
{
    unsigned n = s_ring_n < RING_N ? s_ring_n : RING_N;
    for (unsigned i = 0; i < n; i++) {
        hit_t *h = &s_ring[i];
        int r = run_of(h->va);
        unsigned k;
        /* frames 0-1 are the handler and the signal trampoline */
        for (k = 0; k < s_nagg; k++)
            if (s_agg[k].run == r && s_agg[k].other == h->other && s_agg[k].nbt == h->nbt
                    && !memcmp(s_agg[k].bt, h->bt, h->nbt * sizeof(void *)))
                break;
        if (k == s_nagg) {
            if (s_nagg == s_capagg) {
                s_capagg = s_capagg ? s_capagg * 2 : 1024;
                s_agg = realloc(s_agg, s_capagg * sizeof *s_agg);
            }
            memset(&s_agg[k], 0, sizeof s_agg[k]);
            s_agg[k].run = r;
            s_agg[k].other = h->other;
            s_agg[k].nbt = h->nbt;
            memcpy(s_agg[k].bt, h->bt, sizeof h->bt);
            s_nagg++;
        }
        s_agg[k].n++;
        s_agg[k].va = h->va;
        s_agg[k].old = h->old;
        s_agg[k].nw = h->nw;
    }
    if (s_ring_drop)
        fprintf(stderr, "[HRAUDIT] %u writes not recorded (ring full)\n", s_ring_drop);
    s_ring_n = s_ring_drop = 0;
}

static void report_watch(void)
{
    FILE *f = out_file("watch.txt");
    if (!f)
        return;
    fprintf(f, "# BO-275 watch '%s' (%s): %d pictures with 0 steps watched; exe base %p\n"
               "# R va len pictures controls / W count thread va old new : chain (exe offsets, innermost first)\n",
            s_cyc.tag, s_cyc.noblend ? "noblend" : "blend", s_cyc.wn, (void *)s_exe_base);
    for (unsigned i = 0; i < s_nrun; i++) {
        run_t *r = &s_run[i];
        unsigned seen = 0;
        if (!r->watched)
            continue;
        fprintf(f, "R %08X %u %u %u\n", r->va, r->len, r->pic, r->idle);
        for (unsigned k = 0; k < s_nagg; k++) {
            agg_t *a = &s_agg[k];
            if (a->run != (int)i)
                continue;
            seen++;
            fprintf(f, "  W %u %s %08X %016llX %016llX :", a->n, a->other ? "other" : "title", a->va,
                    (unsigned long long)a->old, (unsigned long long)a->nw);
            for (unsigned b = 2; b < a->nbt; b++)
                fprintf(f, " %lx", (unsigned long)((uintptr_t)a->bt[b] - s_exe_base));
            fprintf(f, "\n");
        }
        if (!seen)
            fprintf(f, "  (no write seen)\n");
    }
    fclose(f);
    s_nagg = 0;
    (void)s_undec;
}
#endif /* HRA_WATCH */

/* ── the cycle ──────────────────────────────────────────────── */

static void poll_ctl(void)
{
    struct stat st;
    FILE *f;
    char line[256], tag[64], mode[16];
    int nd = 30, nw = 20;

    if (++s_poll % 60 || stat(s_ctl, &st) || (long)st.st_size == s_ctl_seen)
        return;
    f = fopen(s_ctl, "r");
    if (!f)
        return;
    if (fseek(f, s_ctl_seen, SEEK_SET) == 0) {
        while (fgets(line, sizeof line, f)) {
            mode[0] = 0;
            if (sscanf(line, "go %63s %15s %d %d", tag, mode, &nd, &nw) >= 1 && !s_cyc.on) {
                memset(&s_cyc, 0, sizeof s_cyc);
                s_cyc.on = 1;
                s_cyc.noblend = !strcmp(mode, "noblend");
                s_cyc.ndiff = nd;
                s_cyc.nwatch = nw;
                snprintf(s_cyc.tag, sizeof s_cyc.tag, "%s", tag);
                for (unsigned i = 0; i < s_nreg; i++) {
                    memset(s_reg[i].pic, 0, s_reg[i].size / CH * sizeof *s_reg[i].pic);
                    memset(s_reg[i].idle, 0, s_reg[i].size / CH * sizeof *s_reg[i].idle);
                }
                fprintf(stderr, "[HRAUDIT] cycle %s (%s): %d diff samples, %d watch samples\n",
                        tag, s_cyc.noblend ? "noblend" : "blend", nd, nw);
            }
        }
    }
    s_ctl_seen = (long)st.st_size;
    fclose(f);
}

static void cycle_done(void)
{
    FILE *f;
    char path[1024];
    fprintf(stderr, "[HRAUDIT] cycle %s done\n", s_cyc.tag);
    snprintf(path, sizeof path, "%s/%s.done", s_out && *s_out ? s_out : ".", s_cyc.tag);
    if ((f = fopen(path, "w")))
        fclose(f);
    s_cyc.on = 0;
}

/* ── counters (BO-280) ───────────────────────────────────── */

static struct {
    double   t0;
    uint32_t step0;
    unsigned upd, aud_calls, aud_runs, hud_calls;
    double   hud_given;
} s_cnt;
static struct { double at[8]; int n, next, on; uint32_t step0; unsigned upd; double t0; } s_fl;

void bo3_hraudit_count(int kind, unsigned runs)
{
    if (s_on <= 0)
        return;
    if (kind == 1) {
        s_cnt.aud_calls++;
        s_cnt.aud_runs += runs;
    }
}

static void counters(void)
{
    double now = (double)now_ns() / 1e9;
    uint32_t step = MEM32(BO3_VA(0x4A1EB4u, 0x4A21D4u, 0x4A2274u));

    if (s_cnt.t0 == 0.0) {
        s_cnt.t0 = now;
        s_cnt.step0 = step;
        return;
    }
    s_cnt.upd++;
    if (now - s_cnt.t0 >= 10.0) {
        fprintf(stderr, "[HRAUDIT] counters %.1f s: steps %u, Updates %u; HUD %u calls, %.4f s given;"
                        " race audio %u calls, %u runs\n", now - s_cnt.t0, step - s_cnt.step0,
                s_cnt.upd, s_cnt.hud_calls, s_cnt.hud_given, s_cnt.aud_calls, s_cnt.aud_runs);
        memset(&s_cnt, 0, sizeof s_cnt);
        s_cnt.t0 = now;
        s_cnt.step0 = step;
    }
}

static void flash_probe(void)
{
    static double run0;
    double now = (double)now_ns() / 1e9;
    const uint32_t m = BO3_VA(0x5A9A00u, 0x5A9D10u, 0x5A9DB0u);

    if (s_fl.n == 0 && s_fl.next == 0) {
        const char *e = getenv("BO3_HRAUDIT_FLASH");
        char *q;
        s_fl.next = -1;
        while (e && *e && s_fl.n < 8) {
            s_fl.at[s_fl.n++] = strtod(e, &q);
            if (q == e) break;
            e = *q == ',' ? q + 1 : q;
        }
        if (s_fl.n)
            s_fl.next = 0;
    }
    if (s_fl.next < 0)
        return;
    if (MEM32(BO3_VA(0x4D53B4u, 0x4D56D4u, 0x4D5774u)) != 5) {     /* the game's running state */
        run0 = 0.0;
        return;
    }
    if (run0 == 0.0)
        run0 = now;
    if (s_fl.on) {
        s_fl.upd++;
        if (MEM32(m + 8u) == 0) {
            fprintf(stderr, "[HRAUDIT] flash: %u steps, %u Updates, %.1f ms (t %.4f, duration %.4f)\n",
                    MEM32(BO3_VA(0x4A1EB4u, 0x4A21D4u, 0x4A2274u)) - s_fl.step0, s_fl.upd, (now - s_fl.t0) * 1000.0, MEMF(m),
                    MEMF(m + 4u));
            s_fl.on = 0;
        }
        return;
    }
    if (s_fl.next < s_fl.n && now - run0 >= s_fl.at[s_fl.next]) {
        MEMF(m) = 0.0f;
        MEMF(m + 4u) = MEMF(BO3_VA(0x3B1684u, 0x3B19A4u, 0x3B1A44u));
        MEM32(m + 8u) = 1;
        s_fl.on = 1;
        s_fl.upd = 0;
        s_fl.step0 = MEM32(BO3_VA(0x4A1EB4u, 0x4A21D4u, 0x4A2274u));
        s_fl.t0 = now;
        s_fl.next++;
        fprintf(stderr, "[HRAUDIT] flash started at step %u\n", s_fl.step0);
    }
}

void bo3_hraudit_begin(void)
{
    unsigned n;
    if (!init())
        return;
    counters();
    flash_probe();
    s_cyc.since++;
    if (s_cyc.pending == 1) {
        n = compare(0);
        s_cyc.last_ms = (double)(now_ns() - s_cyc.t0) / 1e6;
        s_cyc.dn++;
        fprintf(stderr, "[HRAUDIT] %s diff %d: %u chunks changed, %.2f ms, step %u\n", s_cyc.tag,
                s_cyc.dn, n, s_cyc.last_ms, MEM32(BO3_VA(0x4A1EB4u, 0x4A21D4u, 0x4A2274u)));
        s_cyc.pending = 0;
        if (s_cyc.dn >= s_cyc.ndiff) {
            report_diff();
#ifdef HRA_WATCH
            if (s_cyc.nwatch > 0)
                watch_setup();
            else
#endif
                cycle_done();
        }
    }
#ifdef HRA_WATCH
    else if (s_cyc.pending == 2) {
        watch_arm(0);
        s_cyc.wn++;
        fprintf(stderr, "[HRAUDIT] %s watch %d: %u writes\n", s_cyc.tag, s_cyc.wn, s_ring_n);
        watch_fold();
        s_cyc.pending = 0;
        if (s_cyc.wn >= s_cyc.nwatch) {
            report_watch();
            cycle_done();
        }
    }
#endif
    if (!s_cyc.on)
        poll_ctl();
}

void bo3_hraudit_picture(int steps)
{
    if (s_on <= 0 || !s_cyc.on || steps != 0 || s_cyc.since < s_spacing)
        return;
    s_cyc.since = 0;
    if (s_cyc.dn < s_cyc.ndiff) {
        if ((s_cyc.dn + s_cyc.cn) % 4 == 3) {
            /* the control: the other threads alone, for a picture's time */
            double ms = s_cyc.last_ms > 2.0 && s_cyc.last_ms < 30.0 ? s_cyc.last_ms : 8.0;
            struct timespec ts = { 0, (long)(ms * 1e6) };
            unsigned n;
            snapshot();
#ifdef _WIN32
            (void)ts;
            Sleep((DWORD)(ms + 0.5));
#else
            nanosleep(&ts, NULL);
#endif
            n = compare(1);
            s_cyc.cn++;
            fprintf(stderr, "[HRAUDIT] %s control %d: %u chunks changed in %.2f ms\n", s_cyc.tag,
                    s_cyc.cn, n, ms);
            return;     /* this picture goes on unsampled */
        }
        snapshot();
        s_cyc.t0 = now_ns();
        s_cyc.pending = 1;
        return;
    }
#ifdef HRA_WATCH
    if (s_cyc.wn < s_cyc.nwatch) {
        watch_arm(1);
        s_cyc.pending = 2;
    }
#endif
}

/* The HUD's update (bo3_hud_update): how often it runs and what time it is
 * given against the steps, while a cycle runs; written with the diff. */

void bo3_hraudit_hud(float given, float scaled)
{
    if (s_on > 0) {
        s_cnt.hud_calls++;
        s_cnt.hud_given += given;
    }
    if (s_on <= 0 || !s_cyc.on)
        return;
    if (!s_hud.calls)
        s_hud.step0 = MEM32(BO3_VA(0x4A1EB4u, 0x4A21D4u, 0x4A2274u));
    s_hud.calls++;
    s_hud.given += given;
    s_hud.scaled += scaled;
}

int bo3_hraudit_noblend(void)
{
    return s_on > 0 && s_cyc.on && s_cyc.noblend;
}
