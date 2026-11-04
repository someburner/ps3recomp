/* Optional image-to-image Z24 conversion. Immutable descriptor sets survive
 * until the existing submission fence; no per-conversion CPU/GPU wait. */
#include "rsx_vk_depth_direct.spv.h"
static struct {
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkDescriptorPool pool;
    VkImageView depth_view[VK_ENG_MAX_OBJ];
    u32 sets;
    u32 tile_x, tile_y;
    int raster;
    int failed;
} s_dd;

#include "rsx_vk_depth_raster.inc"

static void vk_depth_direct_reset(void)
{
    if (s_dd.pool && s_dd.sets) {
        pvkResetDescriptorPool(s_vk.device,s_dd.pool,0);
        s_dd.sets=0;
    }
}
static void vk_depth_direct_release(u32 handle)
{
    if (handle<VK_ENG_MAX_OBJ && s_dd.depth_view[handle]) {
        if (s_eb_pending) vk_eng_flush();
        pvkDestroyImageView(s_vk.device,s_dd.depth_view[handle],NULL);
        s_dd.depth_view[handle]=VK_NULL_HANDLE;
    }
}
static void vk_depth_direct_shutdown(void)
{
    if (s_eb_pending) vk_eng_flush();
    for(u32 i=1;i<VK_ENG_MAX_OBJ;i++) vk_depth_direct_release(i);
    if(s_dd.pipeline) pvkDestroyPipeline(s_vk.device,s_dd.pipeline,NULL);
    vk_depth_raster_shutdown();
    if(s_dd.layout) pvkDestroyPipelineLayout(s_vk.device,s_dd.layout,NULL);
    if(s_dd.pool) pvkDestroyDescriptorPool(s_vk.device,s_dd.pool,NULL);
    if(s_dd.set_layout) pvkDestroyDescriptorSetLayout(s_vk.device,s_dd.set_layout,NULL);
    memset(&s_dd,0,sizeof s_dd);
}
static int vk_depth_direct_init(void)
{
    if(s_dd.pipeline) return 0;
    if(s_dd.failed || !s_vk.sampled_depth_ok) return -1;
    VkQueueFamilyProperties queues[64];
    u32 nq=64;
    pvkGetPhysicalDeviceQueueFamilyProperties(s_vk.phys,&nq,queues);
    if(s_vk.queue_family>=nq || !(queues[s_vk.queue_family].queueFlags & VK_QUEUE_COMPUTE_BIT)) goto fail;
    VkFormatProperties fp;
    pvkGetPhysicalDeviceFormatProperties(s_vk.phys,VK_FORMAT_R8G8B8A8_UNORM,&fp);
    const char* raster=getenv("PS3RECOMP_VK_DEPTH_RASTER");
    s_dd.raster=raster && !strcmp(raster,"1");
    if(!(fp.optimalTilingFeatures & (s_dd.raster?VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT:VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))) goto fail;
    VkDescriptorSetLayoutBinding bindings[2]={
        {.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
    if(s_dd.raster) bindings[0].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo sci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=s_dd.raster?1:2,.pBindings=bindings};
    if(pvkCreateDescriptorSetLayout(s_vk.device,&sci,NULL,&s_dd.set_layout)) goto fail;
    VkPushConstantRange push={.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.size=8};
    VkPipelineLayoutCreateInfo lci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,
        .pSetLayouts=&s_dd.set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
    if(pvkCreatePipelineLayout(s_vk.device,&lci,NULL,&s_dd.layout)) goto fail;
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1024},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1024}};
    VkDescriptorPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1024,.poolSizeCount=2,.pPoolSizes=sizes};
    if(pvkCreateDescriptorPool(s_vk.device,&pci,NULL,&s_dd.pool)) goto fail;
    if(s_dd.raster) { if(vk_depth_raster_init()) goto fail; return 0; }
    VkShaderModule shader=VK_NULL_HANDLE;
    if(vk_create_shader(rsx_vk_depth_direct_spv,sizeof rsx_vk_depth_direct_spv,&shader)) goto fail;
    /* Tune occupancy without changing pixel coverage or arithmetic. */
    const char* tile=getenv("PS3RECOMP_VK_DEPTH_TILE");
    s_dd.tile_x=8; s_dd.tile_y=8;
    if(tile && !strcmp(tile,"16")) s_dd.tile_x=s_dd.tile_y=16;
    else if(tile && !strcmp(tile,"32")) s_dd.tile_x=32;
    u32 dimensions[2]={s_dd.tile_x,s_dd.tile_y};
    VkSpecializationMapEntry entries[2]={{0,0,sizeof(u32)},{1,sizeof(u32),sizeof(u32)}};
    VkSpecializationInfo specialization={.mapEntryCount=2,.pMapEntries=entries,
        .dataSize=sizeof dimensions,.pData=dimensions};
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main",.pSpecializationInfo=&specialization},.layout=s_dd.layout};
    VkResult rc=pvkCreateComputePipelines(s_vk.device,VK_NULL_HANDLE,1,&ci,NULL,&s_dd.pipeline);
    pvkDestroyShaderModule(s_vk.device,shader,NULL);
    if(rc) goto fail;
    return 0;
fail:
    vk_depth_direct_shutdown();
    s_dd.failed=1;
    return -1;
}
static u32 vk_eng_depth_direct(void* user,u32 depth,u32 w,u32 h)
{
    static int on=-1;
    if(on<0) { const char* e=getenv("PS3RECOMP_VK_DEPTH_DIRECT"); on=e && !strcmp(e,"1"); }
    if(!on || vk_depth_direct_init()) return 0;
    vk_eng_obj* z=vk_eng_get(depth,VK_ENG_DEPTH);
    if(!z || !(z->im.usage & VK_IMAGE_USAGE_SAMPLED_BIT)) return 0;
    if(!s_dd.depth_view[depth]) {
        VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=z->im.img,
            .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=z->fmt,.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
        if(pvkCreateImageView(s_vk.device,&vi,NULL,&s_dd.depth_view[depth])) return 0;
    }
    if(s_dd.sets>=1024) { if(s_eb_pending) vk_eng_flush(); else vk_depth_direct_reset(); }
    const u32 hd=vk_eng_alloc();
    if(!hd) return 0;
    vk_eng_obj* o=&s_eobj[hd];
    const VkImageUsageFlags output_usage=s_dd.raster?VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:VK_IMAGE_USAGE_STORAGE_BIT;
    int reused=0;
    for(u32 i=0;i<s_snap_pool_n;i++)
        if(s_snap_pool[i].fmt==VK_FORMAT_R8G8B8A8_UNORM && s_snap_pool[i].w==w && s_snap_pool[i].h==h &&
           (s_snap_pool[i].im.usage & output_usage)) {
            o->im=s_snap_pool[i].im; s_snap_pool[i]=s_snap_pool[--s_snap_pool_n]; reused=1; break;
        }
    if(!reused && vk_create_image(&o->im,VK_FORMAT_R8G8B8A8_UNORM,w,h,
        output_usage|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT)) {
        vk_destroy_image(&o->im); memset(o,0,sizeof *o); return 0;
    }
    o->kind=VK_ENG_COLOR; o->snapshot=1; o->fmt=VK_FORMAT_R8G8B8A8_UNORM; o->bpp=4; o->w=w; o->h=h;
    /* Cache creation/eviction can flush; do it before allocating a set. */
    VkFramebuffer fb=s_dd.raster?vk_depth_raster_framebuffer(o->im.view,w,h):VK_NULL_HANDLE;
    if(s_dd.raster && !fb) { vk_eng_release(user,hd); return 0; }
    static int split=-1;
    if(split<0) { const char* e=getenv("PS3RECOMP_VK_DEPTH_SPLIT"); split=e && !strcmp(e,"1"); }
    const int batch=vk_eb_active() && !split;
    /* Begin can flush an existing batch, which also resets the set pool.
     * Allocate this conversion's set only after that potential reset. */
    if(!(batch && s_eb_pending) && vk_begin()) { vk_eng_release(user,hd); return 0; }
    VkDescriptorSet set;
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=s_dd.pool,.descriptorSetCount=1,.pSetLayouts=&s_dd.set_layout};
    if(pvkAllocateDescriptorSets(s_vk.device,&ai,&set)) { vk_eng_release(user,hd); return 0; }
    s_dd.sets++;
    VkDescriptorImageInfo ii[2]={{s_vk.sampler,s_dd.depth_view[depth],VK_IMAGE_LAYOUT_GENERAL},
                               {VK_NULL_HANDLE,o->im.view,VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet writes[2]={
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&ii[0]},
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,.pImageInfo=&ii[1]}};
    pvkUpdateDescriptorSets(s_vk.device,s_dd.raster?1:2,writes,0,NULL);
    vk_cmd_after_everything(s_vk.cmd);
    /* The global barrier covers both images. Only a new output needs an
     * actual layout transition; reused images already remain GENERAL. */
    if(!vk_compact_barriers())
        vk_barrier_image(z->im.img,s_vk.depth_aspect,VK_IMAGE_LAYOUT_GENERAL);
    if(!reused || !vk_compact_barriers())
        vk_barrier_image(o->im.img,VK_IMAGE_ASPECT_COLOR_BIT,reused?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED);
    u32 params[2]={w,h};
    if(s_dd.raster) vk_depth_raster_draw(fb,set,w,h);
    else {
    pvkCmdBindPipeline(s_vk.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s_dd.pipeline);
    pvkCmdBindDescriptorSets(s_vk.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,s_dd.layout,0,1,&set,0,NULL);
    pvkCmdPushConstants(s_vk.cmd,s_dd.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof params,params);
    pvkCmdDispatch(s_vk.cmd,(w+s_dd.tile_x-1)/s_dd.tile_x,(h+s_dd.tile_y-1)/s_dd.tile_y,1);
    }
    vk_barrier_image(o->im.img,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_GENERAL);
    if(batch) { if(++s_eb_pending>=vk_eb_max()) vk_eng_flush(); }
    else if(vk_submit_and_wait()) { vk_eng_release(user,hd); return 0; }
    return hd;
}
