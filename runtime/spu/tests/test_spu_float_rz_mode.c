/* SPU_FLOAT_RZ: spu_run_with_halt runs SPU code with FPCR at round toward zero with
 * flush-to-zero and puts the caller's FPCR back on the way out -- after a normal return, after a
 * halt (longjmp), and around a nested run. A run can happen synchronously on a PPU thread, whose
 * floating point must keep round to nearest with denormals. Without SPU_FLOAT_RZ=1 FPCR is never
 * touched on arm64; the opt-in x86 path also restores the caller's MXCSR. */
#include "../spu_helpers.h"
#include "../spu_interp.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t* vm_base;
uint32_t ppu_vm_size;
extern int spu_run_with_halt(void (*)(spu_context*), spu_context*);
extern void spu_halt(spu_context*);

#if defined(__aarch64__) && defined(__ARM_NEON) && !defined(SPU_SCALAR_HELPERS)
static uint64_t fpcr_get(void) { uint64_t v; __asm__ volatile("mrs %0, fpcr" : "=r"(v)); return v; }
static void fpcr_set(uint64_t v) { __asm__ volatile("msr fpcr, %0" :: "r"(v)); }

static spu_context ctx, inner_ctx;
static uint64_t seen_inner, seen_outer_after;
static int rz;

static void leaf_return(spu_context* c) { (void)c; seen_inner = fpcr_get(); }
static void leaf_halt(spu_context* c) { seen_inner = fpcr_get(); c->status = SPU_STATUS_STOPPED_BY_STOP; spu_halt(c); }
static void outer_nested(spu_context* c)
{
    (void)c;
    spu_context_init(&inner_ctx, 1);
    spu_run_with_halt(leaf_halt, &inner_ctx);
    seen_outer_after = fpcr_get();
}

static void run(void (*entry)(spu_context*), uint64_t caller, const char* what)
{
    fpcr_set(caller);
    seen_inner = seen_outer_after = ~0ull;
    spu_context_init(&ctx, 0);
    spu_run_with_halt(entry, &ctx);
    const uint64_t after = fpcr_get();
    fpcr_set(0);
    const uint64_t want_inner = rz ? SPU_FPCR_RZFZ : caller;
    if (seen_inner != want_inner || after != caller ||
        (entry == outer_nested && seen_outer_after != want_inner)) {
        fprintf(stderr, "FAIL %s (SPU_FLOAT_RZ=%d, caller FPCR %llX): inside %llX (want %llX), "
                "after %llX, outer after nested %llX\n", what, rz, (unsigned long long)caller,
                (unsigned long long)seen_inner, (unsigned long long)want_inner,
                (unsigned long long)after, (unsigned long long)seen_outer_after);
        exit(1);
    }
}

int main(void)
{
    const char* e = getenv("SPU_FLOAT_RZ");
    rz = e && !strcmp(e, "1");
    const uint64_t callers[] = { 0, 0x00400000ull /* round toward +inf */, SPU_FPCR_RZFZ };
    for (unsigned i = 0; i < sizeof callers / sizeof callers[0]; i++) {
        run(leaf_return, callers[i], "return");
        run(leaf_halt, callers[i], "halt");
        run(outer_nested, callers[i], "nested");
    }
    printf("PASS: SPU_FLOAT_RZ=%d FPCR inside runs and restored after return, halt and nesting\n", rz);
    return 0;
}
#elif defined(__AVX2__) && defined(__FMA__) && defined(SPU_X86_FAST_FLOAT) && !defined(SPU_X86_DOUBLE_FLOAT) && !defined(SPU_SCALAR_HELPERS)
static spu_context ctx, inner_ctx;
static int seen_inner, seen_outer_after;
static int x86_rz;
static void leaf_return(spu_context* c) {
    (void)c;
    seen_inner = g_spu_float_rn_scope;
    assert(seen_inner == (x86_rz ? 2 : ((_mm_getcsr() & 0xE040u) == 0)));
    if(x86_rz) assert((_mm_getcsr() & 0xE040u) == 0xE040u);
    u128 a=spu_splat_u32(0x3F800001), b=spu_splat_u32(0x3EAAAAAB);
    u128 got=spu_fa(a,b), ref=spu_fa_ref(a,b);
    assert(!memcmp(&got,&ref,16));
}
static void leaf_halt(spu_context* c) { leaf_return(c); spu_halt(c); }
static void outer_nested(spu_context* c) {
    (void)c;
    spu_context_init(&inner_ctx,0);
    spu_run_with_halt(leaf_halt,&inner_ctx);
    seen_outer_after=g_spu_float_rn_scope;
}
int main(void) {
    x86_rz=getenv("SPU_FLOAT_RZ") && !strcmp(getenv("SPU_FLOAT_RZ"),"1");
    const unsigned original=_mm_getcsr();
    const unsigned modes[]={0x1F80,0x3F80,0x5F80,0x7F80,0x9FC0};
    void (*entries[])(spu_context*)={leaf_return,leaf_halt,outer_nested};
    for(unsigned m=0;m<sizeof modes/sizeof *modes;m++)
        for(unsigned e=0;e<sizeof entries/sizeof *entries;e++) {
            _mm_setcsr(modes[m]);
            g_spu_float_rn_scope=0;
            seen_inner=seen_outer_after=-1;
            spu_context_init(&ctx,0);
            spu_run_with_halt(entries[e],&ctx);
            assert(g_spu_float_rn_scope==0);
            assert((_mm_getcsr() & 0xE040u)==(modes[m] & 0xE040u));
            assert(seen_inner==(x86_rz ? 2 : ((modes[m] & 0xE040u)==0)));
            if(e==2) assert(seen_outer_after==seen_inner);
        }
    _mm_setcsr(original);
    puts("PASS: x86 SPU float scope preserves MXCSR controls and restores scope after return, halt and nesting");
    return 0;
}
#else
int main(void) { puts("SKIP: no enabled SPU floating-point scope"); return 0; }
#endif

/* Link-only boundaries outside this CPU-only fixture. Fail if any is reached. */
void spu_thread_publish_ctx(uint32_t tid, void* c) { (void)tid; (void)c; abort(); }
void ppu_resv_break_store(uint64_t ea) { (void)ea; abort(); }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; abort(); }
unsigned long long ps3_ms_now(void) { abort(); }
uint32_t g_ww_lo = 0xFFFFFFFFu, g_ww_hi;
int g_resv_store_active;
uint32_t g_barrier_sync_watch;
uint32_t g_ydkj_real_spurs_ea, g_ydkj_real_taskset_ea, g_ydkj_real_taskid;
void spurs_ef_set_from_spu(uint32_t ea, uint16_t bits) { (void)ea; (void)bits; abort(); }
void ydkj_wake_all_event_flags(void) { abort(); }
void spurs_pm_build_context(spu_context* c, uint32_t a, uint32_t b, uint32_t d) {
    (void)c; (void)a; (void)b; (void)d; abort();
}
uint32_t g_spu_image_src_ea, g_spu_image_ls_start, g_spu_image_span;
