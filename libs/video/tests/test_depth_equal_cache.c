/* Drive real draw submission and conversion caching with a counting backend. */
#define main prior_engine_tests_main
#include "test_rsx_draw_engine.c"
#undef main
#include "rsx_draw_engine.c"
#include <assert.h>
static unsigned conversions;
static u32 convert(void* u,u32 depth,u32 w,u32 h,u32 format)
{ (void)u;(void)depth;(void)w;(void)h;(void)format; conversions++;return stub_handle(); }
int main(void)
{
    vm_base=calloc(1,GUEST_BYTES); assert(vm_base); ppu_vm_size=GUEST_BYTES;
    rsx_draw_backend be=g_stub_backend; be.depth_as_color=convert;
    engine_up_with(&be);
    m(M_CONTEXT_DMA_ZETA,DMA_LOCAL); m(M_ZETA_OFFSET,ZETA_OFFSET);
    m(M_SURFACE_FORMAT,8u|(2u<<5)); m(M_ZETA_PITCH,1024);
    m(M_DEPTH_TEST_ENABLE,1); m(M_DEPTH_WRITE,1); m(M_DEPTH_FUNC,0x0207);
    draw_triangle(); assert(stub.n_draw==1);
    u32 slot=0;
    while(slot<g.n_zdepths && g.zdepths[slot].handle!=stub.bound_depth) slot++;
    assert(slot<g.n_zdepths);
    eng_zdepth* z=&g.zdepths[slot];
    rsx_dsp_texture t={.format=0xa5,.width=256,.height=256,.pitch=1024,.dimension=2,.mipmaps=1};
    assert(eng_zdepth_as_color(slot,&t));
    int on=getenv("PS3RECOMP_DEPTH_EQUAL_CACHE") && !strcmp(getenv("PS3RECOMP_DEPTH_EQUAL_CACHE"),"1");
    for(u32 cmp=0x0200;cmp<=0x0207;cmp++) {
        unsigned before=conversions; u64 serial=z->depth_serial;
        m(M_DEPTH_FUNC,cmp); draw_triangle();
        assert(z->depth_serial>serial); /* ownership ordering is never skipped */
        assert(eng_zdepth_as_color(slot,&t));
        assert(conversions==before+!(on && (cmp==0x0200 || cmp==0x0202)));
    }
    /* A mutating draw followed by EQUAL must not revive the stale copy. */
    unsigned before=conversions;
    m(M_DEPTH_FUNC,0x0207); draw_triangle();
    m(M_DEPTH_FUNC,0x0202); draw_triangle();
    assert(eng_zdepth_as_color(slot,&t) && conversions==before+1);
    /* Clears also invalidate, including if followed by a no-change draw. */
    before=conversions; m(M_CLEAR_BUFFERS,1); draw_triangle();
    assert(eng_zdepth_as_color(slot,&t) && conversions==before+1);
    /* A different sampling extent still needs its own conversion. */
    before=conversions; t.width=128;
    assert(eng_zdepth_as_color(slot,&t) && conversions==before+1);
    engine_down(); free(vm_base);
    puts("PASS: all depth comparisons, write ordering, stale conversion, clear and size invalidation");
}
