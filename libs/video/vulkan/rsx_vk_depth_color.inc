/* GPU counterpart of eng_depth_row_to_guest + texture decode. The source
 * depth is copied to device-local storage, quantized there, and copied to
 * a sampled colour image. No CPU readback or float64/int64 GPU feature.
 * Included here to use this backend's command ordering and object lifetime. */
#include "rsx_vk_depth_color.spv.h"
static struct {
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    vk_buffer pixels;
    int failed;
} s_dc;

static void vk_depth_color_shutdown(void)
{
    if (s_eb_pending) vk_eng_flush();
    vk_destroy_buffer(&s_dc.pixels);
    if (s_dc.pipeline) pvkDestroyPipeline(s_vk.device, s_dc.pipeline, NULL);
    if (s_dc.layout) pvkDestroyPipelineLayout(s_vk.device, s_dc.layout, NULL);
    if (s_dc.pool) pvkDestroyDescriptorPool(s_vk.device, s_dc.pool, NULL);
    if (s_dc.set_layout) pvkDestroyDescriptorSetLayout(s_vk.device, s_dc.set_layout, NULL);
    memset(&s_dc, 0, sizeof s_dc);
}

static int vk_depth_color_init(void)
{
    if (s_dc.pipeline) return 0;
    if (s_dc.failed) return -1;
    u32 nq = 0;
    VkQueueFamilyProperties q[64];
    pvkGetPhysicalDeviceQueueFamilyProperties(s_vk.phys, &nq, NULL);
    if (nq > 64) goto fail;
    pvkGetPhysicalDeviceQueueFamilyProperties(s_vk.phys, &nq, q);
    if (s_vk.queue_family >= nq || !(q[s_vk.queue_family].queueFlags & VK_QUEUE_COMPUTE_BIT)) goto fail;
    VkDescriptorSetLayoutBinding binding = { .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    VkDescriptorSetLayoutCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding };
    if (pvkCreateDescriptorSetLayout(s_vk.device, &sci, NULL, &s_dc.set_layout)) goto fail;
    VkPushConstantRange push = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .size = 8 };
    VkPipelineLayoutCreateInfo lci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &s_dc.set_layout,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
    if (pvkCreatePipelineLayout(s_vk.device, &lci, NULL, &s_dc.layout)) goto fail;
    VkDescriptorPoolSize size = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
    VkDescriptorPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &size };
    if (pvkCreateDescriptorPool(s_vk.device, &pci, NULL, &s_dc.pool)) goto fail;
    VkDescriptorSetAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = s_dc.pool, .descriptorSetCount = 1, .pSetLayouts = &s_dc.set_layout };
    if (pvkAllocateDescriptorSets(s_vk.device, &ai, &s_dc.set)) goto fail;
    VkShaderModule shader = VK_NULL_HANDLE;
    if (vk_create_shader(rsx_vk_depth_color_spv, sizeof rsx_vk_depth_color_spv, &shader)) goto fail;
    VkComputePipelineCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader, .pName = "main" },
        .layout = s_dc.layout };
    VkResult rc = pvkCreateComputePipelines(s_vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &s_dc.pipeline);
    pvkDestroyShaderModule(s_vk.device, shader, NULL);
    if (rc) goto fail;
    return 0;
fail:
    vk_depth_color_shutdown();
    s_dc.failed = 1;
    VK_LOG("depth colour conversion unavailable; retaining guest-memory path\n");
    return -1;
}

static u32 vk_eng_depth_as_color(void* user, u32 depth, u32 w, u32 h, u32 rsx_fmt)
{
    const vk_eng_obj* z = vk_eng_get(depth, VK_ENG_DEPTH);
    const u32 bits = rsx_fmt == RSX_TEX_FMT_A8R8G8B8 ? 24u :
                     rsx_fmt == RSX_TEX_FMT_DEPTH16 ? 16u : 0u;
    if (!bits || !z || !w || !h || w > z->w || h > z->h ||
        (s_vk.depth_format != VK_FORMAT_D32_SFLOAT &&
         s_vk.depth_format != VK_FORMAT_D32_SFLOAT_S8_UINT)) return 0;
    if (vk_depth_color_init()) return 0;
    const u64 count64 = (u64)w * h;
    VkPhysicalDeviceProperties props;
    pvkGetPhysicalDeviceProperties(s_vk.phys, &props);
    if (count64 * 8u > props.limits.maxStorageBufferRange ||
        (count64 + 63u) / 64u > props.limits.maxComputeWorkGroupCount[0]) return 0;
    const u32 count = (u32)count64;
    const VkDeviceSize need = count64 * 8u;
    if (s_dc.pixels.size < need) {
        vk_destroy_buffer(&s_dc.pixels); /* flushes before replacing its descriptor */
        if (vk_create_device_buffer(&s_dc.pixels, need,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return 0;
        VkDescriptorBufferInfo bi = { .buffer = s_dc.pixels.buf, .range = need };
        VkWriteDescriptorSet wr = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = s_dc.set, .dstBinding = 0, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi };
        pvkUpdateDescriptorSets(s_vk.device, 1, &wr, 0, NULL);
    }
    const VkFormat fmt = bits == 24 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16_UNORM;
    const u32 hd = vk_eng_alloc();
    if (!hd) return 0;
    vk_eng_obj* o = &s_eobj[hd];
    int reused = 0;
    for (u32 i = 0; i < s_snap_pool_n; i++)
        if (s_snap_pool[i].fmt == fmt && s_snap_pool[i].w == w && s_snap_pool[i].h == h) {
            o->im = s_snap_pool[i].im;
            s_snap_pool[i] = s_snap_pool[--s_snap_pool_n];
            reused = 1; break;
        }
    if (!reused && vk_create_image(&o->im, fmt, w, h,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT)) {
        vk_destroy_image(&o->im); memset(o, 0, sizeof *o); return 0;
    }
    o->kind = VK_ENG_COLOR; o->snapshot = 1;
    o->fmt = fmt; o->bpp = bits == 24 ? 4u : 2u; o->w = w; o->h = h;
    const int batch = vk_eb_active();
    if (!(batch && s_eb_pending) && vk_begin()) { vk_eng_release(user, hd); return 0; }
    vk_cmd_after_everything(s_vk.cmd); /* previous reads of the reusable buffer/image */
    vk_barrier_image(z->im.img, s_vk.depth_aspect, VK_IMAGE_LAYOUT_GENERAL);
    vk_barrier_image(o->im.img, VK_IMAGE_ASPECT_COLOR_BIT,
                     reused ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED);
    VkBufferImageCopy src = { .imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 },
        .imageExtent = { w, h, 1 } };
    pvkCmdCopyImageToBuffer(s_vk.cmd, z->im.img, VK_IMAGE_LAYOUT_GENERAL, s_dc.pixels.buf, 1, &src);
    VkBufferMemoryBarrier bb = { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = s_dc.pixels.buf, .size = VK_WHOLE_SIZE };
    pvkCmdPipelineBarrier(s_vk.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          0, 0, NULL, 1, &bb, 0, NULL);
    const u32 params[2] = {count, bits};
    pvkCmdBindPipeline(s_vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_dc.pipeline);
    pvkCmdBindDescriptorSets(s_vk.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_dc.layout, 0, 1, &s_dc.set, 0, NULL);
    pvkCmdPushConstants(s_vk.cmd, s_dc.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof params, params);
    const u32 items = bits == 24 ? count : (count + 1u) / 2u;
    pvkCmdDispatch(s_vk.cmd, (items + 63u) / 64u, 1, 1);
    bb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; bb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    pvkCmdPipelineBarrier(s_vk.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          0, 0, NULL, 1, &bb, 0, NULL);
    VkBufferImageCopy dst = { .bufferOffset = count64 * 4u,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { w, h, 1 } };
    pvkCmdCopyBufferToImage(s_vk.cmd, s_dc.pixels.buf, o->im.img, VK_IMAGE_LAYOUT_GENERAL, 1, &dst);
    vk_barrier_image(o->im.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL);
    if (batch) {
        if (++s_eb_pending >= vk_eb_max()) vk_eng_flush();
    } else if (vk_submit_and_wait()) { vk_eng_release(user, hd); return 0; }
    return hd;
}
