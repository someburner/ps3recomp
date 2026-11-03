/* Exercise the actual Vulkan compute conversion against the CPU definition. */
#include "../rsx_vulkan_backend.c"
#include <assert.h>
uint8_t* vm_base;

static unsigned checks;
static void check(unsigned w, unsigned h)
{
    unsigned n = w*h;
    float *values = malloc(n*sizeof(float));
    unsigned char *got = malloc(n*4);
    assert(values && got);
    unsigned rng = 1234;
    for (unsigned i=0; i<n; ++i) {
        rng = rng*1664525u+1013904223u;
        values[i] = (float)(rng>>8)/16777216.0f;
    }
    const float edges[] = {0,1,0.5f,0x1p-149f,0x1.fffffep-1f,0x1p-25f,
                          0x1p-24f,0x1p-17f,0x1p-16f};
    for (unsigned i=0; i<sizeof(edges)/sizeof(*edges) && i<n; ++i) values[i]=edges[i];
    unsigned z = vk_eng_depth_target_create(NULL,w,h);
    assert(z);
    vk_buffer upload = {0};
    assert(!vk_create_host_buffer(&upload,n*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    assert(upload.coherent);
    memcpy(upload.ptr,values,n*4);
    assert(!vk_begin());
    vk_barrier_image(s_eobj[z].im.img,s_vk.depth_aspect,VK_IMAGE_LAYOUT_GENERAL);
    VkBufferImageCopy copy = { .imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},
                              .imageExtent={w,h,1} };
    pvkCmdCopyBufferToImage(s_vk.cmd,upload.buf,s_eobj[z].im.img,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
    assert(!vk_submit_and_wait());
    vk_destroy_buffer(&upload);
    assert(!vk_eng_depth_as_color(NULL,z,w+1,h,RSX_TEX_FMT_A8R8G8B8));
    assert(!vk_eng_depth_as_color(NULL,z,w,h,0));
    /* Queue both conversions together to catch reuse/order bugs in batching. */
    for (unsigned repeat=0; repeat<3; ++repeat) {
        unsigned handles[2] = {
            vk_eng_depth_as_color(NULL,z,w,h,RSX_TEX_FMT_A8R8G8B8),
            vk_eng_depth_as_color(NULL,z,w,h,RSX_TEX_FMT_DEPTH16)
        };
        for (unsigned mode=0; mode<2; ++mode) {
            assert(handles[mode]);
            unsigned bpp=mode?2:4, scale=mode?65535:16777215;
            memset(got,0xcc,n*4);
            vk_eng_readback(NULL,handles[mode],0,0,w,h,got,w*bpp);
            for (unsigned i=0; i<n; ++i) {
                unsigned v=(unsigned)((double)values[i]*scale+0.5);
                unsigned char want[4];
                if(mode) { want[0]=v; want[1]=v>>8; }
                else { want[0]=v>>8; want[1]=v; want[2]=0; want[3]=v>>16; }
                if(memcmp(got+i*bpp,want,bpp)) {
                    fprintf(stderr,"FAIL %ux%u mode=%u i=%u depth=%.9g got=%02x,%02x expected=%02x,%02x\n",
                            w,h,mode,i,values[i],got[i*bpp],got[i*bpp+1],want[0],want[1]);
                    abort();
                }
                ++checks;
            }
            vk_eng_release(NULL,handles[mode]);
        }
    }
    vk_eng_release(NULL,z);
    free(values); free(got);
}
int main(void)
{
    setenv("PS3RECOMP_RSX_ENGINE","dispatch",1);
    setenv("PS3RECOMP_VK_WINDOW","0",1);
    assert(!rsx_vulkan_backend_init(64,64,"depth conversion regression"));
    check(3,5); check(257,129); check(128,64); check(1280,720); check(3,5);
    rsx_vulkan_backend_shutdown();
    printf("PASS: %u GPU depth conversions match CPU quantization\n",checks);
    return 0;
}
