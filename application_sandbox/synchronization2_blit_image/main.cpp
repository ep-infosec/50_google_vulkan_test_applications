// Copyright 2022 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <chrono>

#include "application_sandbox/sample_application_framework/sample_application.h"
#include "mathfu/matrix.h"
#include "mathfu/vector.h"
#include "support/entry/entry.h"
#include "vulkan_helpers/buffer_frame_data.h"
#include "vulkan_helpers/helper_functions.h"
#include "vulkan_helpers/vulkan_application.h"
#include "vulkan_helpers/vulkan_model.h"

using Mat44 = mathfu::Matrix<float, 4, 4>;
using Vector4 = mathfu::Vector<float, 4>;

namespace cube_model {
#include "cube.obj.h"
}
const auto& cube_data = cube_model::model;

uint32_t cube_vertex_shader[] =
#include "cube.vert.spv"
    ;

uint32_t cube_fragment_shader[] =
#include "cube.frag.spv"
    ;

struct CubeFrameData {
  containers::unique_ptr<vulkan::VkCommandBuffer> command_buffer_;
  containers::unique_ptr<vulkan::VkFramebuffer> framebuffer_;
  containers::unique_ptr<vulkan::DescriptorSet> cube_descriptor_set_;
  vulkan::ImagePointer blit_src_;
  containers::unique_ptr<vulkan::VkImageView> blit_src_view_;
};

VkPhysicalDeviceSynchronization2FeaturesKHR kSynchronization2Features = {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR, nullptr,
    VK_TRUE};

// This creates an application with 16MB of image memory, and defaults
// for host, and device buffer sizes.
class BlitImageSample : public sample_application::Sample<CubeFrameData> {
 public:
  BlitImageSample(const entry::EntryData* data)
      : data_(data),
        Sample<CubeFrameData>(
            data->allocator(), data, 1, 512, 1, 1,
            sample_application::SampleOptions()
                .SetVulkanApiVersion(VK_API_VERSION_1_1)
                .AddDeviceExtensionStructure(&kSynchronization2Features),
            {}, {}, {VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME}),
        cube_(data->allocator(), data->logger(), cube_data) {}
  virtual void InitializeApplicationData(
      vulkan::VkCommandBuffer* initialization_buffer,
      size_t num_swapchain_images) override {
    cube_.InitializeData(app(), initialization_buffer);

    cube_descriptor_set_layouts_[0] = {
        0,                                  // binding
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  // descriptorType
        1,                                  // descriptorCount
        VK_SHADER_STAGE_VERTEX_BIT,         // stageFlags
        nullptr                             // pImmutableSamplers
    };
    cube_descriptor_set_layouts_[1] = {
        1,                                  // binding
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  // descriptorType
        1,                                  // descriptorCount
        VK_SHADER_STAGE_VERTEX_BIT,         // stageFlags
        nullptr                             // pImmutableSamplers
    };

    pipeline_layout_ = containers::make_unique<vulkan::PipelineLayout>(
        data_->allocator(),
        app()->CreatePipelineLayout({{cube_descriptor_set_layouts_[0],
                                      cube_descriptor_set_layouts_[1]}}));

    VkAttachmentReference color_attachment = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    render_pass_ = containers::make_unique<vulkan::VkRenderPass>(
        data_->allocator(),
        app()->CreateRenderPass(
            {{
                0,                                         // flags
                render_format(),                           // format
                num_samples(),                             // samples
                VK_ATTACHMENT_LOAD_OP_CLEAR,               // loadOp
                VK_ATTACHMENT_STORE_OP_STORE,              // storeOp
                VK_ATTACHMENT_LOAD_OP_DONT_CARE,           // stencilLoadOp
                VK_ATTACHMENT_STORE_OP_DONT_CARE,          // stencilStoreOp
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,  // initialLayout
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL   // finalLayout
            }},  // AttachmentDescriptions
            {{
                0,                                // flags
                VK_PIPELINE_BIND_POINT_GRAPHICS,  // pipelineBindPoint
                0,                                // inputAttachmentCount
                nullptr,                          // pInputAttachments
                1,                                // colorAttachmentCount
                &color_attachment,                // colorAttachment
                nullptr,                          // pResolveAttachments
                nullptr,                          // pDepthStencilAttachment
                0,                                // preserveAttachmentCount
                nullptr                           // pPreserveAttachments
            }},                                   // SubpassDescriptions
            {}                                    // SubpassDependencies
            ));

    cube_pipeline_ = containers::make_unique<vulkan::VulkanGraphicsPipeline>(
        data_->allocator(), app()->CreateGraphicsPipeline(
                                pipeline_layout_.get(), render_pass_.get(), 0));
    cube_pipeline_->AddShader(VK_SHADER_STAGE_VERTEX_BIT, "main",
                              cube_vertex_shader);
    cube_pipeline_->AddShader(VK_SHADER_STAGE_FRAGMENT_BIT, "main",
                              cube_fragment_shader);
    cube_pipeline_->SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    cube_pipeline_->SetInputStreams(&cube_);
    cube_pipeline_->SetViewport(viewport());
    cube_pipeline_->SetScissor(scissor());
    cube_pipeline_->SetSamples(num_samples());
    cube_pipeline_->AddAttachment();
    cube_pipeline_->Commit();

    camera_data =
        containers::make_unique<vulkan::BufferFrameData<camera_data_>>(
            data_->allocator(), app(), num_swapchain_images,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    model_data = containers::make_unique<vulkan::BufferFrameData<model_data_>>(
        data_->allocator(), app(), num_swapchain_images,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    float aspect =
        (float)app()->swapchain().width() / (float)app()->swapchain().height();
    camera_data->data().projection_matrix =
        Mat44::FromScaleVector(mathfu::Vector<float, 3>{1.0f, -1.0f, 1.0f}) *
        Mat44::Perspective(1.5708f, aspect, 0.1f, 100.0f);

    model_data->data().transform = Mat44::FromTranslationVector(
        mathfu::Vector<float, 3>{0.0f, 0.0f, -3.0f});
  }

  virtual void InitializeFrameData(
      CubeFrameData* frame_data, vulkan::VkCommandBuffer* initialization_buffer,
      size_t frame_index) override {
    VkImageCreateInfo blit_src_image_create_info{
        /* sType = */
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        /* pNext = */ nullptr,
        /* flags = */ 0,
        /* imageType = */ VK_IMAGE_TYPE_2D,
        /* format = */ app()->swapchain().format(),
        /* extent = */
        {
            /* width = */ app()->swapchain().width(),
            /* height = */ app()->swapchain().height(),
            /* depth = */ app()->swapchain().depth(),
        },
        /* mipLevels = */ 1,
        /* arrayLayers = */ 1,
        /* samples = */ VK_SAMPLE_COUNT_1_BIT,
        /* tiling = */
        VK_IMAGE_TILING_OPTIMAL,
        /* usage = */
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        /* sharingMode = */
        VK_SHARING_MODE_EXCLUSIVE,
        /* queueFamilyIndexCount = */ 0,
        /* pQueueFamilyIndices = */ nullptr,
        /* initialLayout = */
        VK_IMAGE_LAYOUT_UNDEFINED,
    };
    frame_data->blit_src_ =
        app()->CreateAndBindImage(&blit_src_image_create_info);
    frame_data->blit_src_view_ = app()->CreateImageView(
        frame_data->blit_src_.get(), VK_IMAGE_VIEW_TYPE_2D,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});

    frame_data->command_buffer_ =
        containers::make_unique<vulkan::VkCommandBuffer>(
            data_->allocator(), app()->GetCommandBuffer());

    frame_data->cube_descriptor_set_ =
        containers::make_unique<vulkan::DescriptorSet>(
            data_->allocator(),
            app()->AllocateDescriptorSet({cube_descriptor_set_layouts_[0],
                                          cube_descriptor_set_layouts_[1]}));

    VkDescriptorBufferInfo buffer_infos[2] = {
        {
            camera_data->get_buffer(),                       // buffer
            camera_data->get_offset_for_frame(frame_index),  // offset
            camera_data->size(),                             // range
        },
        {
            model_data->get_buffer(),                       // buffer
            model_data->get_offset_for_frame(frame_index),  // offset
            model_data->size(),                             // range
        }};

    VkWriteDescriptorSet write{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,  // sType
        nullptr,                                 // pNext
        *frame_data->cube_descriptor_set_,       // dstSet
        0,                                       // dstbinding
        0,                                       // dstArrayElement
        2,                                       // descriptorCount
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,       // descriptorType
        nullptr,                                 // pImageInfo
        buffer_infos,                            // pBufferInfo
        nullptr,                                 // pTexelBufferView
    };

    app()->device()->vkUpdateDescriptorSets(app()->device(), 1, &write, 0,
                                            nullptr);

    // Create a framebuffer with the blit source image as attachment
    VkFramebufferCreateInfo framebuffer_create_info{
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,      // sType
        nullptr,                                        // pNext
        0,                                              // flags
        *render_pass_,                                  // renderPass
        1,                                              // attachmentCount
        &frame_data->blit_src_view_->get_raw_object(),  // attachments
        app()->swapchain().width(),                     // width
        app()->swapchain().height(),                    // height
        1                                               // layers
    };

    ::VkFramebuffer raw_framebuffer;
    app()->device()->vkCreateFramebuffer(
        app()->device(), &framebuffer_create_info, nullptr, &raw_framebuffer);
    frame_data->framebuffer_ = containers::make_unique<vulkan::VkFramebuffer>(
        data_->allocator(),
        vulkan::VkFramebuffer(raw_framebuffer, nullptr, &app()->device()));

    vulkan::VkCommandBuffer& cmdBuffer = (*frame_data->command_buffer_);
    cmdBuffer->vkBeginCommandBuffer(cmdBuffer,
                                    &sample_application::kBeginCommandBuffer);

    VkClearValue clear;
    vulkan::MemoryClear(&clear);

    // Image barriers for the blit source image, UNDEFINED ->
    // COLOR_ATTACHMENT_OPTIMAL COLOR_ATTACHMENT_OPTIMAL ->
    // TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier2KHR undef_to_attach_barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,         // sType
        nullptr,                                              // pNext
        VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,                 // srcStageMask
        0,                                                    // srcAccessMask
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,  // dstStageMask
        0,                                                    // dstAccessMask
        VK_IMAGE_LAYOUT_UNDEFINED,                            // oldLayout
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,             // newLayout
        VK_QUEUE_FAMILY_IGNORED,                 // srcQueueFamilyIndex
        VK_QUEUE_FAMILY_IGNORED,                 // dstQueueFamilyIndex
        *frame_data->blit_src_,                  // image
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}  // subresourceRange
    };
    VkDependencyInfoKHR undef_to_attach = {
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,  // sType
        nullptr,                                // pNext
        0,                                      // dependencyFlags
        0,                                      // memoryBarrierCount
        nullptr,                                // pMemoryBarriers
        0,                                      // bufferMemoryBarrierCount
        nullptr,                                // pBufferMemoryBarriers
        1,                                      // imageMemoryBarrierCount
        &undef_to_attach_barrier                // pImageMemoryBarriers
    };

    VkImageMemoryBarrier2KHR attach_to_src_barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,         // sType
        nullptr,                                              // pNext
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,  // srcStageMask
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR,           // srcAccessMask
        VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,                 // dstStageMask
        VK_ACCESS_2_TRANSFER_READ_BIT_KHR,                    // dstAccessMask
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,             // oldLayout
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,                 // newLayout
        VK_QUEUE_FAMILY_IGNORED,  // srcQueueFamilyIndex
        VK_QUEUE_FAMILY_IGNORED,  // dstQueueFamilyIndex
        *frame_data->blit_src_,   // image
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfoKHR attach_to_src = {
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,  // sType
        nullptr,                                // pNext
        0,                                      // dependencyFlags
        0,                                      // memoryBarrierCount
        nullptr,                                // pMemoryBarriers
        0,                                      // bufferMemoryBarrierCount
        nullptr,                                // pBufferMemoryBarriers
        1,                                      // imageMemoryBarrierCount
        &attach_to_src_barrier                  // pImageMemoryBarriers
    };

    // Change the layout of blit source image to COLOR_ATTACHMENT_OPTIMAL
    cmdBuffer->vkCmdPipelineBarrier2KHR(cmdBuffer, &undef_to_attach);

    VkRenderPassBeginInfo pass_begin = {
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,  // sType
        nullptr,                                   // pNext
        *render_pass_,                             // renderPass
        *frame_data->framebuffer_,                 // framebuffer
        {{0, 0},
         {app()->swapchain().width(),
          app()->swapchain().height()}},  // renderArea
        1,                                // clearValueCount
        &clear                            // clears
    };

    cmdBuffer->vkCmdBeginRenderPass(cmdBuffer, &pass_begin,
                                    VK_SUBPASS_CONTENTS_INLINE);

    cmdBuffer->vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 *cube_pipeline_);
    cmdBuffer->vkCmdBindDescriptorSets(
        cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        ::VkPipelineLayout(*pipeline_layout_), 0, 1,
        &frame_data->cube_descriptor_set_->raw_set(), 0, nullptr);
    cube_.Draw(&cmdBuffer);
    cmdBuffer->vkCmdEndRenderPass(cmdBuffer);

    // Change the layout of blit source image to TRANSFER_SRC_OPTIMAL
    cmdBuffer->vkCmdPipelineBarrier2KHR(cmdBuffer, &attach_to_src);

    // Image barriers for swapchain image, COLOR_ATTACHMENT_OPTIMAL ->
    // TRANSFER_DST_OPTIMAL and TRANSFER_DST_OPTIMAL ->
    // COLOR_ATTACHMENT_OPTIMAL
    VkImageMemoryBarrier2KHR attach_to_dst_barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,         // sType
        nullptr,                                              // pNext
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,  // srcStageMask
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR,           // srcAccessMask
        VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,                 // dstStageMask
        VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,                   // dstAccessMask
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,             // oldLayout
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,                 // newLayout
        VK_QUEUE_FAMILY_IGNORED,      // srcQueueFamilyIndex
        VK_QUEUE_FAMILY_IGNORED,      // dstQueueFamilyIndex
        swapchain_image(frame_data),  // image
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfoKHR attach_to_dst = {
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,  // sType
        nullptr,                                // pNext
        0,                                      // dependencyFlags
        0,                                      // memoryBarrierCount
        nullptr,                                // pMemoryBarriers
        0,                                      // bufferMemoryBarrierCount
        nullptr,                                // pBufferMemoryBarriers
        1,                                      // imageMemoryBarrierCount
        &attach_to_dst_barrier                  // pImageMemoryBarriers
    };

    VkImageMemoryBarrier2KHR dst_to_attach_barrier{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,         // sType
        nullptr,                                              // pNext
        VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,                 // srcStageMask
        VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,                   // srcAccessMask
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,  // dstStageMask
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR,           // dstAccessMask
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,                 // oldLayout
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,             // newLayout
        VK_QUEUE_FAMILY_IGNORED,      // srcQueueFamilyIndex
        VK_QUEUE_FAMILY_IGNORED,      // dstQueueFamilyIndex
        swapchain_image(frame_data),  // image
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfoKHR dst_to_attach = {
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,  // sType
        nullptr,                                // pNext
        0,                                      // dependencyFlags
        0,                                      // memoryBarrierCount
        nullptr,                                // pMemoryBarriers
        0,                                      // bufferMemoryBarrierCount
        nullptr,                                // pBufferMemoryBarriers
        1,                                      // imageMemoryBarrierCount
        &dst_to_attach_barrier                  // pImageMemoryBarriers
    };

    // Change the layout of swapchain image to TRANSFER_DST_OPTIMAL
    cmdBuffer->vkCmdPipelineBarrier2KHR(cmdBuffer, &attach_to_dst);

    // Blit the blit source image to the swapchain image
    VkImageBlit blit_region{
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        {{0, 0, 0},
         {int32_t(app()->swapchain().width()),
          int32_t(app()->swapchain().height()), 1}},
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        {{0, 0, 0},
         {int32_t(app()->swapchain().width()),
          int32_t(app()->swapchain().height()), 1}},
    };
    cmdBuffer->vkCmdBlitImage(
        cmdBuffer, *frame_data->blit_src_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapchain_image(frame_data), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
        &blit_region, VK_FILTER_NEAREST);

    // Change the layout of swapchain image to COLOR_ATTACHMENT_OPTIMAL
    cmdBuffer->vkCmdPipelineBarrier2KHR(cmdBuffer, &dst_to_attach);

    (*frame_data->command_buffer_)
        ->vkEndCommandBuffer(*frame_data->command_buffer_);
  }

  virtual void Update(float time_since_last_render) override {
    model_data->data().transform =
        model_data->data().transform *
        Mat44::FromRotationMatrix(
            Mat44::RotationX(3.14f * time_since_last_render) *
            Mat44::RotationY(3.14f * time_since_last_render * 0.5f));
  }
  virtual void Render(vulkan::VkQueue* queue, size_t frame_index,
                      CubeFrameData* frame_data) override {
    // Update our uniform buffers.
    camera_data->UpdateBuffer(queue, frame_index);
    model_data->UpdateBuffer(queue, frame_index);

    VkCommandBufferSubmitInfoKHR cb_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR,   // sType
        nullptr,                                            // pNext
        frame_data->command_buffer_->get_command_buffer(),  // commandBuffer
        0                                                   // deviceMask
    };
    VkSubmitInfo2KHR init_submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,  // sType
        nullptr,                              // pNext
        0,                                    // flags
        0,                                    // waitSemaphoreInfoCount
        nullptr,                              // pWaitSemaphoreInfos
        1,                                    // commandBufferInfoCount
        &cb_info,                             // pCommandBufferInfos
        0,                                    // signalSemaphoreInfoCount
        nullptr                               // pSignalSemaphoreInfos
    };

    app()->render_queue()->vkQueueSubmit2KHR(
        app()->render_queue(), 1, &init_submit_info,
        static_cast<VkFence>(VK_NULL_HANDLE));
  }

 private:
  struct camera_data_ {
    Mat44 projection_matrix;
  };

  struct model_data_ {
    Mat44 transform;
  };

  const entry::EntryData* data_;
  containers::unique_ptr<vulkan::PipelineLayout> pipeline_layout_;
  containers::unique_ptr<vulkan::VulkanGraphicsPipeline> cube_pipeline_;
  containers::unique_ptr<vulkan::VkRenderPass> render_pass_;
  VkDescriptorSetLayoutBinding cube_descriptor_set_layouts_[2];
  vulkan::VulkanModel cube_;

  containers::unique_ptr<vulkan::BufferFrameData<camera_data_>> camera_data;
  containers::unique_ptr<vulkan::BufferFrameData<model_data_>> model_data;
};

int main_entry(const entry::EntryData* data) {
  data->logger()->LogInfo("Application Startup");
  BlitImageSample sample(data);
  sample.Initialize();

  while (!sample.should_exit() && !data->WindowClosing()) {
    sample.ProcessFrame();
  }
  sample.WaitIdle();

  data->logger()->LogInfo("Application Shutdown");
  return 0;
}
