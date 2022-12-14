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

#include "application_sandbox/sample_application_framework/sample_application.h"
#include "support/entry/entry.h"
#include "vulkan_helpers/buffer_frame_data.h"
#include "vulkan_helpers/helper_functions.h"
#include "vulkan_helpers/vulkan_application.h"
#include "vulkan_helpers/vulkan_model.h"

#include <chrono>
#include "mathfu/matrix.h"
#include "mathfu/vector.h"

using Mat44 = mathfu::Matrix<float, 4, 4>;

// red cube
const struct {
  size_t num_vertices;
  float positions[24];
  float uv[16];
  float normals[24];
  size_t num_indices;
  uint32_t indices[36];
} red_cube_data = {8,
                   {0.5f,  0.5f,  0.5f,  0.5f,  -0.5f, 0.5f,  0.5f,  0.5f,
                    -0.5f, 0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f,
                    -0.5f, -0.5f, -0.5f, 0.5f,  0.5f,  -0.5f, -0.5f, 0.5f},
                   {1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
                    1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
                   {0.5f,  0.5f,  0.5f,  0.5f,  -0.5f, 0.5f,  0.5f,  0.5f,
                    -0.5f, 0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f,
                    -0.5f, -0.5f, -0.5f, 0.5f,  0.5f,  -0.5f, -0.5f, 0.5f},
                   36,
                   {0, 1, 3, 3, 2, 0, 1, 0, 6, 6, 7, 1, 4, 5, 7, 7, 6, 4,
                    2, 3, 5, 5, 4, 2, 0, 2, 4, 4, 6, 0, 3, 1, 7, 7, 5, 3}};

// green cube
const struct {
  size_t num_vertices;
  float positions[24];
  float uv[16];
  float normals[24];
  size_t num_indices;
  uint32_t indices[36];
} green_cube_data = {8,
                     {1.0f,  0.5f,  0.5f,  1.0f,  -0.5f, 0.5f,  1.0f,  0.5f,
                      -0.5f, 1.0f,  -0.5f, -0.5f, -1.0f, 0.5f,  -0.5f, -1.0f,
                      -0.5f, -0.5f, -1.0f, 0.5f,  0.5f,  -1.0f, -0.5f, 0.5f},
                     {0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
                      1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f},
                     {0.5f,  0.5f,  0.5f,  0.5f,  -0.5f, 0.5f,  0.5f,  0.5f,
                      -0.5f, 0.5f,  -0.5f, -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f,
                      -0.5f, -0.5f, -0.5f, 0.5f,  0.5f,  -0.5f, -0.5f, 0.5f},
                     36,
                     {0, 1, 3, 3, 2, 0, 1, 0, 6, 6, 7, 1, 4, 5, 7, 7, 6, 4,
                      2, 3, 5, 5, 4, 2, 0, 2, 4, 4, 6, 0, 3, 1, 7, 7, 5, 3}};

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
};

// for now only enabling basic tended_dynamic_state2_support to use depth bias
// element
VkPhysicalDeviceExtendedDynamicState2FeaturesEXT kFeat{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT,
    nullptr,
    VK_TRUE,
    VK_FALSE,
    VK_FALSE,
};

// This creates an application with 16MB of image memory, and defaults
// for host, and device buffer sizes.
class CubeSample : public sample_application::Sample<CubeFrameData> {
 public:
  CubeSample(const entry::EntryData* data)
      : data_(data),
        cubes_{vulkan::VulkanModel(data->allocator(), data->logger(),
                                   red_cube_data),
               vulkan::VulkanModel(data->allocator(), data->logger(),
                                   green_cube_data)},
        Sample<CubeFrameData>(
            data->allocator(), data, 1, 512, 1, 1,
            sample_application::SampleOptions()
                .EnableMultisampling()
                .EnableDepthBuffer()
                .UseHighPrecisionPrecisionDepth()
                .SetVulkanApiVersion(VK_API_VERSION_1_1)
                .AddDeviceExtensionStructure(&kFeat),
            {}, {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME},
            {VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME}) {}

  virtual void InitializeApplicationData(
      vulkan::VkCommandBuffer* initialization_buffer,
      size_t num_swapchain_images) override {
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT ext_features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT,
        nullptr,
        VK_FALSE,
        VK_FALSE,
        VK_FALSE,
    };

    VkPhysicalDeviceFeatures2 features = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &ext_features};

    app()->instance()->vkGetPhysicalDeviceFeatures2KHR(
        app()->device().physical_device(), &features);

    if (ext_features.extendedDynamicState2 == VK_FALSE) {
      data_->logger()->LogError(
          "VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_"
          "EXT not supported");
      exit(1);
    }
    cubes_[0].InitializeData(app(), initialization_buffer);
    cubes_[1].InitializeData(app(), initialization_buffer);

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
        1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_attachment = {
        0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    render_pass_ = containers::make_unique<vulkan::VkRenderPass>(
        data_->allocator(),
        app()->CreateRenderPass(
            {
                {
                    0,                                 // flags
                    depth_format(),                    // format
                    num_samples(),                     // samples
                    VK_ATTACHMENT_LOAD_OP_CLEAR,       // loadOp
                    VK_ATTACHMENT_STORE_OP_STORE,      // storeOp
                    VK_ATTACHMENT_LOAD_OP_DONT_CARE,   // stencilLoadOp
                    VK_ATTACHMENT_STORE_OP_DONT_CARE,  // stencilStoreOp
                    VK_IMAGE_LAYOUT_UNDEFINED,         // initialLayout
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL  // finalLayout
                },  // Depth Attachment
                {
                    0,                                         // flags
                    render_format(),                           // format
                    num_samples(),                             // samples
                    VK_ATTACHMENT_LOAD_OP_CLEAR,               // loadOp
                    VK_ATTACHMENT_STORE_OP_STORE,              // storeOp
                    VK_ATTACHMENT_LOAD_OP_DONT_CARE,           // stencilLoadOp
                    VK_ATTACHMENT_STORE_OP_DONT_CARE,          // stencilStoreOp
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,  // initialLayout
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL   // finalLayout
                },  // Color Attachment
            },      // AttachmentDescriptions
            {{
                0,                                // flags
                VK_PIPELINE_BIND_POINT_GRAPHICS,  // pipelineBindPoint
                0,                                // inputAttachmentCount
                nullptr,                          // pInputAttachments
                1,                                // colorAttachmentCount
                &color_attachment,                // colorAttachment
                nullptr,                          // pResolveAttachments
                &depth_attachment,                // pDepthStencilAttachment
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
    cube_pipeline_->SetInputStreams(cubes_);
    cube_pipeline_->SetViewport(viewport());
    cube_pipeline_->SetScissor(scissor());
    cube_pipeline_->SetSamples(num_samples());
    cube_pipeline_->DepthStencilState().depthTestEnable = VK_TRUE;
    cube_pipeline_->DepthStencilState().depthWriteEnable = VK_TRUE;
    cube_pipeline_->DepthStencilState().depthCompareOp = VK_COMPARE_OP_LESS;
    cube_pipeline_->DepthStencilState().depthBoundsTestEnable = VK_FALSE;
    cube_pipeline_->DepthStencilState().stencilTestEnable = VK_FALSE;
    cube_pipeline_->EnableDepthBias(1.0f, 1.0f, 1.0f);
    cube_pipeline_->AddAttachment();
    cube_pipeline_->Commit();

    camera_data_ = containers::make_unique<vulkan::BufferFrameData<CameraData>>(
        data_->allocator(), app(), num_swapchain_images,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    model_data_ = containers::make_unique<vulkan::BufferFrameData<ModelData>>(
        data_->allocator(), app(), num_swapchain_images,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    float aspect =
        (float)app()->swapchain().width() / (float)app()->swapchain().height();
    camera_data_->data().projection_matrix =
        Mat44::FromScaleVector(mathfu::Vector<float, 3>{1.0f, -1.0f, 1.0f}) *
        Mat44::Perspective(1.5708f, aspect, 0.1f, 100.0f);

    model_data_->data().transform = Mat44::FromTranslationVector(
        mathfu::Vector<float, 3>{0.0f, 0.0f, -3.0f});
  }

  virtual void InitializeFrameData(
      CubeFrameData* frame_data, vulkan::VkCommandBuffer* initialization_buffer,
      size_t frame_index) override {
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
            camera_data_->get_buffer(),                       // buffer
            camera_data_->get_offset_for_frame(frame_index),  // offset
            camera_data_->size(),                             // range
        },
        {
            model_data_->get_buffer(),                       // buffer
            model_data_->get_offset_for_frame(frame_index),  // offset
            model_data_->size(),                             // range
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

    ::VkImageView raw_view[2] = {depth_view(frame_data),
                                 color_view(frame_data)};

    // Create a framebuffer with depth and image attachments
    VkFramebufferCreateInfo framebuffer_create_info{
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,  // sType
        nullptr,                                    // pNext
        0,                                          // flags
        *render_pass_,                              // renderPass
        2,                                          // attachmentCount
        raw_view,                                   // attachments
        app()->swapchain().width(),                 // width
        app()->swapchain().height(),                // height
        1                                           // layers
    };

    ::VkFramebuffer raw_framebuffer;
    app()->device()->vkCreateFramebuffer(
        app()->device(), &framebuffer_create_info, nullptr, &raw_framebuffer);
    frame_data->framebuffer_ = containers::make_unique<vulkan::VkFramebuffer>(
        data_->allocator(),
        vulkan::VkFramebuffer(raw_framebuffer, nullptr, &app()->device()));

    (*frame_data->command_buffer_)
        ->vkBeginCommandBuffer((*frame_data->command_buffer_),
                               &sample_application::kBeginCommandBuffer);
    vulkan::VkCommandBuffer& cmdBuffer = (*frame_data->command_buffer_);

    VkClearValue clear[2] = {{1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}};

    VkRenderPassBeginInfo pass_begin = {
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,  // sType
        nullptr,                                   // pNext
        *render_pass_,                             // renderPass
        *frame_data->framebuffer_,                 // framebuffer
        {{0, 0},
         {app()->swapchain().width(),
          app()->swapchain().height()}},  // renderArea
        2,                                // clearValueCount
        clear                             // clears
    };

    cmdBuffer->vkCmdBeginRenderPass(cmdBuffer, &pass_begin,
                                    VK_SUBPASS_CONTENTS_INLINE);

    cmdBuffer->vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 *cube_pipeline_);
    cmdBuffer->vkCmdBindDescriptorSets(
        cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        ::VkPipelineLayout(*pipeline_layout_), 0, 1,
        &frame_data->cube_descriptor_set_->raw_set(), 0, nullptr);
    cmdBuffer->vkCmdSetDepthBiasEnableEXT(cmdBuffer, VK_TRUE);
    cubes_[1].Draw(&cmdBuffer);
    cmdBuffer->vkCmdSetDepthBiasEnableEXT(cmdBuffer, VK_FALSE);
    cubes_[0].Draw(&cmdBuffer);
    cmdBuffer->vkCmdEndRenderPass(cmdBuffer);

    (*frame_data->command_buffer_)
        ->vkEndCommandBuffer(*frame_data->command_buffer_);
  }

  virtual void Update(float time_since_last_render) override {
    model_data_->data().transform =
        model_data_->data().transform *
        Mat44::FromRotationMatrix(
            Mat44::RotationX(3.14f * time_since_last_render) *
            Mat44::RotationY(3.14f * time_since_last_render * 0.5f));
  }
  virtual void Render(vulkan::VkQueue* queue, size_t frame_index,
                      CubeFrameData* frame_data) override {
    // Update our uniform buffers.
    camera_data_->UpdateBuffer(queue, frame_index);
    model_data_->UpdateBuffer(queue, frame_index);

    VkSubmitInfo init_submit_info{
        VK_STRUCTURE_TYPE_SUBMIT_INFO,  // sType
        nullptr,                        // pNext
        0,                              // waitSemaphoreCount
        nullptr,                        // pWaitSemaphores
        nullptr,                        // pWaitDstStageMask,
        1,                              // commandBufferCount
        &(frame_data->command_buffer_->get_command_buffer()),
        0,       // signalSemaphoreCount
        nullptr  // pSignalSemaphores
    };

    app()->render_queue()->vkQueueSubmit(app()->render_queue(), 1,
                                         &init_submit_info,
                                         static_cast<VkFence>(VK_NULL_HANDLE));
  }

 private:
  struct CameraData {
    Mat44 projection_matrix;
  };

  struct ModelData {
    Mat44 transform;
  };

  const entry::EntryData* data_;
  containers::unique_ptr<vulkan::PipelineLayout> pipeline_layout_;
  containers::unique_ptr<vulkan::VulkanGraphicsPipeline> cube_pipeline_;
  containers::unique_ptr<vulkan::VkRenderPass> render_pass_;
  VkDescriptorSetLayoutBinding cube_descriptor_set_layouts_[2];
  vulkan::VulkanModel cubes_[2];

  containers::unique_ptr<vulkan::BufferFrameData<CameraData>> camera_data_;
  containers::unique_ptr<vulkan::BufferFrameData<ModelData>> model_data_;
};

int main_entry(const entry::EntryData* data) {
  data->logger()->LogInfo("Application Startup");
  CubeSample sample(data);
  sample.Initialize();

  while (!sample.should_exit() && !data->WindowClosing()) {
    sample.ProcessFrame();
  }
  sample.WaitIdle();

  data->logger()->LogInfo("Application Shutdown");
  return 0;
}