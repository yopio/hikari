#include <fstream>
#include <iostream>
#include <sstream>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include "vulkan.hpp"
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

constexpr uint32_t kWidth  = 512;
constexpr uint32_t kHeight = 512;
GLFWwindow* window = nullptr;

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessageCallback(VkDebugReportFlagsEXT      flags,
                                                           VkDebugReportObjectTypeEXT objectType,
                                                           uint64_t object, size_t location,
                                                           int32_t     messageCode,
                                                           const char *pLayerPrefix,
                                                           const char *pMessage, void *pUserData)
{
  VkBool32 ret = VK_FALSE;
  if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT ||
    flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
  {
    ret = VK_TRUE;
  }
  std::stringstream ss;
  if (pLayerPrefix)
  {
    ss << "[" << pLayerPrefix << "] ";
  }
  ss << pMessage << std::endl;
  std::cerr << ss.str().c_str() << std::endl;

  return ret;
}

class VkRenderer
{
public:
  VkRenderer()
  {
    /*
      Instance creation
    */
    {
      const auto appinfo = vk::ApplicationInfo(nullptr, 1, nullptr, 1, VK_API_VERSION_1_1);

      // Enable all extensions
      std::vector<const char*> extension_names;
      auto extensions = vk::enumerateInstanceExtensionProperties();

      std::cout << "Instance Extensions:" << std::endl;
      for (auto const &ep : extensions)
      {
        std::cout << ep.extensionName << ":" << std::endl;
        std::cout << "\tVersion: " << ep.specVersion << std::endl;
        std::cout << std::endl;
        extension_names.push_back(ep.extensionName);
      }

      uint32_t layer_count = 1;
      const char* validationLayers[] = { "VK_LAYER_LUNARG_standard_validation" };

      std::vector<const char*> layers;
#ifdef DEBUG
      layers.push_back("VK_LAYER_LUNARG_standard_validation");
#endif // DEBUG

      auto instance_create_info = vk::InstanceCreateInfo({},
                                                         &appinfo,
                                                         static_cast<uint32_t>(layers.size()),
                                                         layers.data(),
                                                         static_cast<uint32_t>(extensions.size()),
                                                         extension_names.data());
      instance_ = vk::createInstanceUnique(instance_create_info);
      VULKAN_HPP_DEFAULT_DISPATCHER.init(instance_.get());
    }

    /*
      Select physical device
    */
    {
      // Use first device.
      physical_device_ = instance_->enumeratePhysicalDevices()[0];
      physical_device_memory_properties_ = physical_device_.getMemoryProperties();
    }

    /*
      Find graphics queue index
    */
    {
      const auto queue_family_properties = physical_device_.getQueueFamilyProperties();
      for(int i = 0; i < queue_family_properties.size(); ++i)
      {
        if(queue_family_properties[i].queueFlags & vk::QueueFlagBits::eGraphics)
        {
          graphics_queue_index_ = i;
          break;
        }
      }
      // TODO : Error process here
    }

#ifdef DEBUG
    {
      const auto ci = vk::DebugReportCallbackCreateInfoEXT({}, &DebugMessageCallback,  nullptr);
      instance_->createDebugReportCallbackEXT(ci);
    }
#endif

    /*
      Create logical device
    */
    {
      const float default_queue_priority(1.0f);
      auto queue_ci = vk::DeviceQueueCreateInfo({}, graphics_queue_index_, 1, &default_queue_priority);

      std::vector<const char*> extensions;
      {
        // Get all device extensions
        const auto ext_properties = physical_device_.enumerateDeviceExtensionProperties();
        for(const auto& p : ext_properties)
        {
          extensions.push_back(p.extensionName);
        }
      }

      auto device_ci = vk::DeviceCreateInfo();
      device_ci.setPQueueCreateInfos(&queue_ci);
      device_ci.setQueueCreateInfoCount(1);
      device_ci.setPpEnabledExtensionNames(extensions.data());
      device_ci.setEnabledExtensionCount(extensions.size());

      device_ = physical_device_.createDeviceUnique(device_ci);
      VULKAN_HPP_DEFAULT_DISPATCHER.init(device_.get());
    }

    /*
      Get device queue
    */
    {
      device_->getQueue(graphics_queue_index_, 0, &device_queue_);
    }

    /*
      Command Pool creation
      Command Buffer creation
      Fence for command buffer
      GPU に命令を送るコマンドバッファを保持するもの
    */
    {
      auto pool_ci = vk::CommandPoolCreateInfo({}, graphics_queue_index_);
      pool_ci.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
      command_pool_ = device_->createCommandPoolUnique(pool_ci);

      // Allocate command buffer from the command pool.
      const auto buffer_ci = vk::CommandBufferAllocateInfo(command_pool_.get(),
                                                           vk::CommandBufferLevel::ePrimary,
                                                           1);
      command_buffers_ = device_->allocateCommandBuffersUnique(buffer_ci);

      // Create fence
      const auto fence_ci = vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled);
      fences_.resize(command_buffers_.size());
      for(auto& f : fences_)
      {
        f = device_->createFenceUnique(fence_ci);
      }
    }

    /*
      Create swapchain
      描画結果をdisplay 上に表示するためには必要
      描画待ちのバックバッファをタイミングに合わせて切り替えている

      surface 作成 -> forat 確定
                   -> surface size 取得
                   -> present モード確認 -> swapchain をつくる
    */
    {
      /*
        Create surface
      */
      auto surface_c_type = VkSurfaceKHR(surface_.get());
      glfwCreateWindowSurface(instance_.get(), window, nullptr, &surface_c_type);
      surface_ = vk::UniqueSurfaceKHR(surface_c_type);

      /*
        Get the supported format
      */
      const auto formats = physical_device_.getSurfaceFormatsKHR(surface_.get());
      assert(!formats.empty());
      const auto format = (formats[0].format == vk::Format::eUndefined)
                          ? vk::Format::eB8G8R8A8Unorm
                          : formats[0].format;

      // Get surface capabilities
      surface_capabilities_ = physical_device_.getSurfaceCapabilitiesKHR(surface_.get());

      /*
        Get the surface size
        そのまま currentExtent を使うと無効な値が入っていることがある
      */
      auto extent = surface_capabilities_.currentExtent;
      if(extent.width == std::numeric_limits<uint32_t>::max())
      {
        // サーフェスサイズが未定義
        // 自由にサイズを指定していいのでウィンドウのサイズにする
        extent = vk::Extent2D(kWidth, kHeight);
      }

      const auto present_mode = vk::PresentModeKHR::eFifo;
      const auto transform    = (surface_capabilities_.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
                                ? vk::SurfaceTransformFlagBitsKHR::eIdentity
                                : surface_capabilities_.currentTransform;
      const auto composite    = (surface_capabilities_.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
                                  ? vk::CompositeAlphaFlagBitsKHR::ePreMultiplied
                                  : (surface_capabilities_.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
                                      ? vk::CompositeAlphaFlagBitsKHR::ePostMultiplied :
                                        (surface_capabilities_.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
                                        ? vk::CompositeAlphaFlagBitsKHR::eInherit
                                        : vk::CompositeAlphaFlagBitsKHR::eOpaque;
      /*
        Swapchain creation
      */
      auto ci = vk::SwapchainCreateInfoKHR(vk::SwapchainCreateFlagsKHR(),
                                           surface_.get(),
                                           surface_capabilities_.minImageCount,
                                           format,
                                           vk::ColorSpaceKHR::eSrgbNonlinear,
                                           extent,
                                           1,
                                           vk::ImageUsageFlagBits::eColorAttachment,
                                           vk::SharingMode::eExclusive,
                                           0,
                                           nullptr,
                                           transform,
                                           composite,
                                           present_mode,
                                           true,
                                           nullptr);
      swapchain_ = device_->createSwapchainKHRUnique(ci);
      swapchain_extent_ = extent;

      /*
        Create views for swapchain
      */
      const auto component_mapping = vk::ComponentMapping(vk::ComponentSwizzle::eR,
                                                          vk::ComponentSwizzle::eG,
                                                          vk::ComponentSwizzle::eB,
                                                          vk::ComponentSwizzle::eA);
      const auto subresource_range = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);
      const auto swapchain_images = device_->getSwapchainImagesKHR(swapchain_.get());
      for(const auto& image : swapchain_images)
      {
        const auto ci = vk::ImageViewCreateInfo(vk::ImageViewCreateFlags(),
                                                image,
                                                vk::ImageViewType::e2D,
                                                format,
                                                component_mapping,
                                                subresource_range);
        swapchain_image_views_.push_back(device_->createImageViewUnique(ci));
      }
    }

    /*
      depth buffer creation
      スワップチェインには表示用のイメージが含まれているが、デプスバッファはない
    */
    {
      // Create image for depth buffer
      const auto depth_format = vk::Format::eD16Unorm;
      const auto format_properties = physical_device_.getFormatProperties(depth_format);
      vk::ImageTiling tiling;
      if(format_properties.linearTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
      {
        tiling = vk::ImageTiling::eLinear;
      }
      else if(format_properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
      {
        tiling = vk::ImageTiling::eOptimal;
      }
      else
      {
        std::cerr << "Depth stencil attachment is not supported for D16Unorm depth format." << std::endl;
      }

      const auto image_ci = vk::ImageCreateInfo(vk::ImageCreateFlags(),
                                                vk::ImageType::e2D,
                                                depth_format,
                                                vk::Extent3D(swapchain_extent_.width, swapchain_extent_.height, 1),
                                                1,
                                                1,
                                                vk::SampleCountFlagBits::e1,
                                                tiling,
                                                vk::ImageUsageFlagBits::eDepthStencilAttachment);
      depth_image_ = device_->createImageUnique(image_ci);

      // Allocate device memory for depth buffer and bind it
      const auto memory_properties  = physical_device_.getMemoryProperties();
      const auto memory_requirement = device_->getImageMemoryRequirements(depth_image_.get());
      const auto memory_type_index  = MemoryTypeIndex(memory_requirement.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
      // assert(memory_type_index != ~0);
      const auto ai = vk::MemoryAllocateInfo(memory_requirement.size, memory_type_index);
      depth_memory_ = device_->allocateMemoryUnique(ai);

      device_->bindImageMemory(depth_image_.get(), depth_memory_.get(), 0);

      // Create depth image view
      const auto component_mapping = vk::ComponentMapping(vk::ComponentSwizzle::eR,
                                                          vk::ComponentSwizzle::eG,
                                                          vk::ComponentSwizzle::eB,
                                                          vk::ComponentSwizzle::eA);
      const auto subresource_range = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);
      const auto view_ci = vk::ImageViewCreateInfo(vk::ImageViewCreateFlags(),
                                                   depth_image_.get(),
                                                   vk::ImageViewType::e2D,
                                                   depth_format,
                                                   component_mapping,
                                                   subresource_range);
      depth_image_view_ = device_->createImageViewUnique(view_ci);
    }

    /*
      RenderPass creation
    */
    {
      std::array<vk::AttachmentDescription, 2> attachments;
      // color
      attachments[0] = vk::AttachmentDescription(vk::AttachmentDescriptionFlags(),
                                                 surface_format_.format,
                                                 vk::SampleCountFlagBits::e1,
                                                 vk::AttachmentLoadOp::eClear,
                                                 vk::AttachmentStoreOp::eStore,
                                                 vk::AttachmentLoadOp::eDontCare,
                                                 vk::AttachmentStoreOp::eDontCare,
                                                 vk::ImageLayout::eUndefined,
                                                 vk::ImageLayout::eDepthStencilAttachmentOptimal);
      // depth
      attachments[1] = vk::AttachmentDescription(vk::AttachmentDescriptionFlags(),
                                                 vk::Format::eD16Unorm,
                                                 vk::SampleCountFlagBits::e1,
                                                 vk::AttachmentLoadOp::eClear,
                                                 vk::AttachmentStoreOp::eStore,
                                                 vk::AttachmentLoadOp::eDontCare,
                                                 vk::AttachmentStoreOp::eDontCare,
                                                 vk::ImageLayout::eUndefined,
                                                 vk::ImageLayout::eDepthStencilAttachmentOptimal);

      const auto color_reference = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);
      const auto depth_reference = vk::AttachmentReference(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

      auto subpass_description = vk::SubpassDescription(vk::SubpassDescriptionFlags(),
                                                        vk::PipelineBindPoint::eGraphics,
                                                        0,
                                                        nullptr,
                                                        1,
                                                        &color_reference,
                                                        nullptr,
                                                        &depth_reference);
      const auto ci = vk::RenderPassCreateInfo(vk::RenderPassCreateFlags(),
                                               2,
                                               attachments.data(),
                                               1,
                                               &subpass_description);
      render_pass_ = device_->createRenderPassUnique(ci);
    }

    /*
      Create framebuffer
      Swapchain の image view の数だけ用意する
    */
    {
      auto ci = vk::FramebufferCreateInfo();
      ci.setRenderPass(render_pass_.get());
      ci.setWidth(kWidth);
      ci.setHeight(kHeight);
      ci.setLayers(1);

      std::array<vk::ImageView, 2> attachments;
      // attachment[0] は swapchain の image view
      attachments[1] = depth_image_view_.get();

      framebuffers_.reserve(swapchain_image_views_.size());
      for(auto& view : swapchain_image_views_)
      {
        attachments[0] = view.get();
        const auto ci = vk::FramebufferCreateInfo(vk::FramebufferCreateFlags(),
                                                  render_pass_.get(),
                                                  2,
                                                  attachments.data(),
                                                  swapchain_extent_.width,
                                                  swapchain_extent_.height,
                                                  1);

        framebuffers_.push_back(device_->createFramebufferUnique(ci));
      }
    }

    /*
      Create Semaphores
    */
    {
      const auto ci = vk::SemaphoreCreateInfo();
      semaphore_render_complete_  = device_->createSemaphoreUnique(ci);
      semaphore_present_complete_ = device_->createSemaphoreUnique(ci);
    }
  }

  void Render()
  {
    const auto current_buffer = device_->acquireNextImageKHR(swapchain_.get(), UINT64_MAX, semaphore_present_complete_.get(), nullptr);
    assert(current_buffer.result == vk::Result::eSuccess);
    assert(current_buffer.value < framebuffers_.size());

    device_->waitForFences(fences_[current_buffer.value].get(), VK_TRUE, UINT64_MAX);

    // Clear color
    std::array<vk::ClearValue, 2> clear_colors = {vk::ClearValue(std::array<float, 4>{0.5f, 0.25f, 0.25f, 0.0f}), // Color
                                                  vk::ClearValue(std::array<float, 4>{1.0f, 0.0f})};              // Depth

    // Write commands to CommandBuffer
    auto command_buffer_begin_info = vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlags());
    command_buffers_[current_buffer.value]->begin(command_buffer_begin_info);

    // Begin the render pass
    auto render_pass_begin_info = vk::RenderPassBeginInfo(render_pass_.get(),
                                                          framebuffers_[current_buffer.value].get(),
                                                          vk::Rect2D(vk::Offset2D(0, 0), swapchain_extent_),
                                                          clear_colors.size(),
                                                          clear_colors.data());
    command_buffers_[current_buffer.value]->beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

    /*
      write commands here...
    */

    // End the render pass
    command_buffers_[current_buffer.value]->endRenderPass();

    // End of writing to CommandBuffer
    command_buffers_[current_buffer.value]->end();

    /*
      submit commands
    */
    auto wait_stage_mask = vk::PipelineStageFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    auto submit_info     = vk::SubmitInfo(1,
                                          &(semaphore_present_complete_.get()),
                                          &wait_stage_mask,
                                          1,
                                          &command_buffers_[0].get(),
                                          1,
                                          &(semaphore_render_complete_.get()));
    device_->resetFences(fences_[current_buffer.value].get());
    device_queue_.submit(submit_info, fences_[current_buffer.value].get());

    // Present
    auto present_info = vk::PresentInfoKHR(1,
                                           &semaphore_render_complete_.get(),
                                           1,
                                           &swapchain_.get(),
                                           &current_buffer.value);
    device_queue_.presentKHR(present_info);
  }

  ~VkRenderer() {}

  uint32_t MemoryTypeIndex(uint32_t type_bits, vk::MemoryPropertyFlags flags)
  {
    const auto prop = physical_device_.getMemoryProperties();
    for (uint32_t i = 0; i < prop.memoryTypeCount; i++)
    {
      if ((type_bits & 1) == 1)
      {
        if ((prop.memoryTypes[i].propertyFlags & flags) == flags)
        {
          return i;
        }
      }
      type_bits >>= 1;
    }
    return 0;
  }

private:
  vk::UniqueInstance                        instance_;
  vk::PhysicalDevice                        physical_device_;
  vk::PhysicalDeviceMemoryProperties        physical_device_memory_properties_;
  uint32_t                                  graphics_queue_index_;
  vk::UniqueDevice                          device_;
  vk::Queue                                 device_queue_;

  vk::UniqueCommandPool                     command_pool_;
  std::vector<vk::UniqueCommandBuffer>      command_buffers_;
  std::vector<vk::UniqueFence>              fences_;

  vk::UniqueSurfaceKHR                      surface_;
  vk::SurfaceFormatKHR                      surface_format_;
  vk::SurfaceCapabilitiesKHR                surface_capabilities_;
  vk::UniqueSwapchainKHR                    swapchain_;
  vk::Extent2D                              swapchain_extent_;
  std::vector<vk::UniqueImageView>          swapchain_image_views_;

  vk::UniqueImage                           depth_image_;
  vk::UniqueDeviceMemory                    depth_memory_;
  vk::UniqueImageView                       depth_image_view_;

  vk::UniqueRenderPass                      render_pass_;

  std::vector<vk::UniqueFramebuffer>        framebuffers_;

  vk::UniqueSemaphore                       semaphore_render_complete_;
  vk::UniqueSemaphore                       semaphore_present_complete_;
};

int main(int argc, char *argv[])
{
  if (!glfwInit())
  {
    std::cerr << "Failed to initialize GLFW3." << std::endl;
    return -1;
  }
  if (!glfwVulkanSupported())
  {
    std::cerr << "GLFW3 does not support Vulkan." << std::endl;
    return -1;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, 0);
  window = glfwCreateWindow(kWidth, kHeight, "Vulkan", nullptr, nullptr);

  static vk::DynamicLoader dl;
  auto vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

  auto renderer = VkRenderer();
  renderer.Render();

  while (glfwWindowShouldClose(window) == GLFW_FALSE)
  {
    glfwPollEvents();
  }
  glfwTerminate();

  return 0;
}
