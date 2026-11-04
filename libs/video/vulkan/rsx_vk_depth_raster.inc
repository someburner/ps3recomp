/* Optional raster depth conversion: color attachments avoid storage-image
 * restrictions on the driver's color compression. Same per-texel math. */
#include "rsx_vk_depth_raster_vert.spv.h"
#include "rsx_vk_depth_raster_frag.spv.h"
static struct {
    VkRenderPass pass;
    struct { VkImageView view; VkFramebuffer fb; } fb[64];
    u32 count;
} s_dr;
static void vk_depth_raster_forget(VkImageView view)
{
    for(u32 i=0;i<s_dr.count;i++) if(s_dr.fb[i].view==view) {
        if(s_eb_pending) vk_eng_flush();
        pvkDestroyFramebuffer(s_vk.device,s_dr.fb[i].fb,NULL);
        s_dr.fb[i]=s_dr.fb[--s_dr.count];
        return;
    }
}
static void vk_depth_raster_shutdown(void)
{
    if(s_eb_pending) vk_eng_flush();
    while(s_dr.count) vk_depth_raster_forget(s_dr.fb[0].view);
    if(s_dr.pass) pvkDestroyRenderPass(s_vk.device,s_dr.pass,NULL);
    memset(&s_dr,0,sizeof s_dr);
}
static int vk_depth_raster_init(void)
{
    VkAttachmentDescription att={.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,
        .loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout=VK_IMAGE_LAYOUT_GENERAL,.finalLayout=VK_IMAGE_LAYOUT_GENERAL};
    VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount=1,.pColorAttachments=&ref};
    VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&att,.subpassCount=1,.pSubpasses=&sub};
    if(pvkCreateRenderPass(s_vk.device,&ri,NULL,&s_dr.pass)) return -1;
    VkShaderModule vs=VK_NULL_HANDLE,fs=VK_NULL_HANDLE;
    if(vk_create_shader(rsx_vk_depth_raster_vert_spv,sizeof rsx_vk_depth_raster_vert_spv,&vs)) return -1;
    if(vk_create_shader(rsx_vk_depth_raster_frag_spv,sizeof rsx_vk_depth_raster_frag_spv,&fs)) {
        pvkDestroyShaderModule(s_vk.device,vs,NULL); return -1;
    }
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fs,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.lineWidth=1.0f};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState attachment={.colorWriteMask=0xf};
    VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment};
    VkDynamicState states[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=states};
    VkGraphicsPipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,
        .pVertexInputState=&vi,.pInputAssemblyState=&ia,.pViewportState=&vp,.pRasterizationState=&rs,
        .pMultisampleState=&ms,.pColorBlendState=&cb,.pDynamicState=&dyn,.layout=s_dd.layout,.renderPass=s_dr.pass};
    VkResult rc=pvkCreateGraphicsPipelines(s_vk.device,VK_NULL_HANDLE,1,&pi,NULL,&s_dd.pipeline);
    pvkDestroyShaderModule(s_vk.device,vs,NULL); pvkDestroyShaderModule(s_vk.device,fs,NULL);
    return rc==VK_SUCCESS?0:-1;
}
static VkFramebuffer vk_depth_raster_framebuffer(VkImageView view,u32 w,u32 h)
{
    for(u32 i=0;i<s_dr.count;i++) if(s_dr.fb[i].view==view) return s_dr.fb[i].fb;
    if(s_dr.count==64) vk_depth_raster_forget(s_dr.fb[0].view);
    VkFramebufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=s_dr.pass,
        .attachmentCount=1,.pAttachments=&view,.width=w,.height=h,.layers=1};
    VkFramebuffer fb=VK_NULL_HANDLE;
    if(pvkCreateFramebuffer(s_vk.device,&ci,NULL,&fb)) return VK_NULL_HANDLE;
    s_dr.fb[s_dr.count].view=view; s_dr.fb[s_dr.count++].fb=fb;
    return fb;
}
static void vk_depth_raster_draw(VkFramebuffer fb,VkDescriptorSet set,u32 w,u32 h)
{
    VkRenderPassBeginInfo bi={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass=s_dr.pass,.framebuffer=fb,.renderArea={{0,0},{w,h}}};
    VkViewport vp={0,0,(float)w,(float)h,0,1};
    VkRect2D sc={{0,0},{w,h}};
    pvkCmdBeginRenderPass(s_vk.cmd,&bi,VK_SUBPASS_CONTENTS_INLINE);
    pvkCmdBindPipeline(s_vk.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s_dd.pipeline);
    pvkCmdBindDescriptorSets(s_vk.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,s_dd.layout,0,1,&set,0,NULL);
    pvkCmdSetViewport(s_vk.cmd,0,1,&vp); pvkCmdSetScissor(s_vk.cmd,0,1,&sc);
    pvkCmdDraw(s_vk.cmd,3,1,0,0);
    pvkCmdEndRenderPass(s_vk.cmd);
}
