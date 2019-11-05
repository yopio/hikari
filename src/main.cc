#include <fstream>
#include <iostream>
#include <sstream>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE 1
#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>

constexpr uint32_t kWidth  = 512;
constexpr uint32_t kHeight = 512;
GLFWwindow* kWindow = nullptr;

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
    const auto appinfo = vk::ApplicationInfo(nullptr, 1, nullptr, 1, VK_API_VERSION_1_1);

    /*
      Instance creation
    */
    uint32_t layer_count = 1;
    const char* validationLayers[] = { "VK_LAYER_LUNARG_standard_validation" };

    std::vector<const char*> layers;
#ifdef DEBUG
    layers.push_back("VK_LAYER_LUNARG_standard_validation");
#endif // DEBUG

    // Enable all extensions
    const auto extension_properties = vk::enumerateInstanceExtensionProperties();
    std::vector<const char*> extensions;
    for(const auto& e : extension_properties)
    {
      extensions.push_back(e.extensionName);
    }

    auto instance_create_info = vk::InstanceCreateInfo({},
                                                       &appinfo,
                                                       static_cast<uint32_t>(layers.size()),
                                                       layers.data(),
                                                       static_cast<uint32_t>(extensions.size()),
                                                       extensions.data());
    instance_ = vk::createInstanceUnique(instance_create_info);

#ifdef DEBUG
    auto debug_report_callback_create_info = vk::DebugReportCallbackCreateInfoEXT(vk::DebugReportFlagBitsEXT::eError
                                                                                  | vk::DebugReportFlagBitsEXT::eWarning
                                                                                  | vk::DebugReportFlagBitsEXT::ePerformanceWarning
                                                                                  | vk::DebugReportFlagBitsEXT::eInformation,
                                                                                  (PFN_vkDebugReportCallbackEXT)DebugMessageCallback,
                                                                                  nullptr);

    auto ext = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(instance_->getProcAddr("vkCreateDebugReportCallbackEXT"));
    assert(ext);
    debug_report_callback_ = instance_->createDebugReportCallbackEXT(debug_report_callback_create_info);
#endif // DEBUG

    /*
      Device creation
    */
    physical_device_ = instance_->enumeratePhysicalDevices()[0]; // 見つけた一番最初のデバイスを使う

    // Request a single graphics queue
    const float default_queue_priority = 1.0f;
    const auto queue_family_properties = physical_device_.getQueueFamilyProperties();

    // Get the first index into queueFamilyProperties which support graphics.
    graphics_queue_family_index_
      = std::distance(queue_family_properties.begin(),
                      std::find_if(queue_family_properties.begin(),
                                   queue_family_properties.end(),
                                   [](const vk::QueueFamilyProperties& qfp)
                                   {
                                     return qfp.queueFlags & vk::QueueFlagBits::eGraphics;
                                   }));
    assert(graphics_queue_family_index_ < queue_family_properties.size());

    // Create logical device
    auto device_queue_create_info = vk::DeviceQueueCreateInfo(vk::DeviceQueueCreateFlags(),
                                                              static_cast<uint32_t>(graphics_queue_family_index_),
                                                              1,
                                                              &default_queue_priority);
    std::vector<const char*> device_extensions;
    const auto device_extension_properties = physical_device_.enumerateDeviceExtensionProperties();
    device_extensions.resize(device_extensions.size());

    for(const auto& ext : device_extension_properties)
    {
      device_extensions.push_back(ext.extensionName);
    }

    // Enable all extensions
    auto device_create_info = vk::DeviceCreateInfo(vk::DeviceCreateFlags(), 1, &device_queue_create_info);
    device_create_info.setPpEnabledExtensionNames(device_extensions.data());
    device_create_info.setEnabledExtensionCount(device_extensions.size());
    device_ = physical_device_.createDeviceUnique(device_create_info);

    /*
      Get a graphics queue
    */
    device_->getQueue(graphics_queue_family_index_, 0, &queue_);

    /*
      Create command pool
    */
    auto command_pool_create_info = vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                                              graphics_queue_family_index_);
    command_pool_ = device_->createCommandPoolUnique(command_pool_create_info);

    /*
      Create surface
    */
    auto psurface = VkSurfaceKHR(surface_.get());
    const auto err = glfwCreateWindowSurface(VkInstance(instance_.get()), kWindow, nullptr, &psurface);
    if (err)
    {
      std::cout << "Failed to create glfw3 surface." << std::endl;
    }
    surface_ = vk::UniqueSurfaceKHR(psurface);

    // Select surface format
    const auto formats = physical_device_.getSurfaceFormatsKHR(surface_.get());
    for (const auto& f : formats)
    {
      if (f.format == vk::Format::eR8G8B8A8Unorm)
      {
        surface_format_ = f;
      }
    }
    // capabilities
    surface_capabilities_ = physical_device_.getSurfaceCapabilitiesKHR(surface_.get());

    /*
      Create Swapchain
    */
    std::cout << "Create Swapchain" << std::endl;
    const auto image_count = std::max(2u, surface_capabilities_.maxImageCount);
    auto       extent      = surface_capabilities_.currentExtent;
    if (extent.width == ~0u)
    {
      // 任意サイズが利用可能なのでウィンドウサイズを利用する
      int w, h;
      glfwGetWindowSize(kWindow, &w, &h);
      extent = vk::Extent2D(w, h);
    }

    auto swapchain_create_info = vk::SwapchainCreateInfoKHR({},
                                                            surface_.get(),
                                                            image_count,
                                                            surface_format_.format,
                                                            surface_format_.colorSpace,
                                                            extent,
                                                            1,
                                                            vk::ImageUsageFlagBits::eColorAttachment,
                                                            vk::SharingMode::eExclusive,
                                                            0,
                                                            nullptr,
                                                            surface_capabilities_.currentTransform,
                                                            vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                                            vk::PresentModeKHR::eFifo,
                                                            VK_TRUE,
                                                            nullptr);
    swapchain_ = device_->createSwapchainKHRUnique(swapchain_create_info);
    swapchain_extent_ = extent;

    /*
      Create view image
    */
    auto color_image_create_info = vk::ImageCreateInfo(
        {}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Unorm, vk::Extent3D(kWidth, kHeight, 1), 1, 1,
        vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
        vk::SharingMode::eExclusive, 0, 0, vk::ImageLayout::eUndefined);
    color_image_ = device_->createImageUnique(color_image_create_info);

    // Allocate memory for color image, and bind it.
    auto memory_requirement   = device_->getImageMemoryRequirements(color_image_.get());
    auto memory_allocate_info = vk::MemoryAllocateInfo(memory_requirement.size,
                                                       MemoryTypeIndex(memory_requirement.memoryTypeBits,
                                                                       vk::MemoryPropertyFlagBits::eDeviceLocal));
    color_image_memory_ = device_->allocateMemoryUnique(memory_allocate_info);
    device_->bindImageMemory(color_image_.get(), color_image_memory_.get(), 0);

    // 描画先として ImageView を作成する
    auto view_create_info = vk::ImageViewCreateInfo({},
                                                    color_image_.get(),
                                                    vk::ImageViewType::e2D,
                                                    vk::Format::eR8G8B8A8Unorm,
                                                    vk::ComponentMapping(vk::ComponentSwizzle::eR,
                                                                         vk::ComponentSwizzle::eG,
                                                                         vk::ComponentSwizzle::eB,
                                                                         vk::ComponentSwizzle::eA),
                                                    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
    color_image_view_ = device_->createImageViewUnique(view_create_info);

    /*
      Create render pass
    */
    std::array<vk::AttachmentDescription, 1> attachment_descriptions {};
    auto& color = attachment_descriptions[0];

    color = vk::AttachmentDescription(vk::AttachmentDescriptionFlags(),
                                      vk::Format::eR8G8B8A8Unorm,
                                      vk::SampleCountFlagBits::e1,
                                      vk::AttachmentLoadOp::eClear,
                                      vk::AttachmentStoreOp::eStore,
                                      vk::AttachmentLoadOp::eDontCare,
                                      vk::AttachmentStoreOp::eDontCare,
                                      vk::ImageLayout::eUndefined,
                                      vk::ImageLayout::eTransferSrcOptimal);

    const auto color_reference
      = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);

    auto subpass_description = vk::SubpassDescription();
    subpass_description.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
    subpass_description.setColorAttachmentCount(1);
    subpass_description.setPColorAttachments(&color_reference);

    auto render_pass_create_info
      = vk::RenderPassCreateInfo(vk::RenderPassCreateFlags(),
                                 attachment_descriptions.size(),
                                 attachment_descriptions.data(),
                                 1,
                                 &subpass_description);

    render_pass_ = device_->createRenderPassUnique(render_pass_create_info);

    /*
      Framebuffer creation
    */
    auto framebuffer_create_info = vk::FramebufferCreateInfo(vk::FramebufferCreateFlags(),
                                                             render_pass_.get(),
                                                             1, // Color attachment only
                                                             &(color_image_view_.get()),
                                                             kWidth,
                                                             kHeight,
                                                             1);
    framebuffer_ = device_->createFramebufferUnique(framebuffer_create_info);

    /*
      Command buffer creation
    */
    auto command_buffer_allocate_info = vk::CommandBufferAllocateInfo(command_pool_.get(),
                                                                      vk::CommandBufferLevel::ePrimary,
                                                                      1);
    command_buffer_ = std::move(device_->allocateCommandBuffersUnique(command_buffer_allocate_info)[0]);

    /*
      Write commands to Command buffer
    */
    auto clear_value = vk::ClearValue(vk::ClearColorValue(std::array<uint32_t, 4>{128, 64, 64, 255}));

    auto command_begin_info = vk::CommandBufferBeginInfo();
    command_buffer_->begin(command_begin_info);
    command_buffer_->end();

    /*
      Begin render pass
    */
    auto render_begin_info = vk::RenderPassBeginInfo(render_pass_.get(),
                                                     framebuffer_.get(),
                                                     vk::Rect2D(kWidth, kHeight),
                                                     1,
                                                     &clear_value);
    command_buffer_->beginRenderPass(render_begin_info, vk::SubpassContents::eInline);
    command_buffer_->endRenderPass();

    /*
      Submit commands to device queue
    */
    auto submit_info = vk::SubmitInfo();
    submit_info.setCommandBufferCount(1);
    submit_info.setPCommandBuffers(&(command_buffer_.get()));

    auto fence_create_info = vk::FenceCreateInfo();
    auto fence             = device_->createFenceUnique(fence_create_info);

    queue_.submit(submit_info, fence.get());

    device_->waitForFences(1, &(fence.get()), VK_TRUE, UINT64_MAX);

    /*
      Copy framebuffer data to host.
    */
    const char *imagedata;
    {
      // Create the linear tiled destination image to copy to and to read the memory from
      auto imgCreateInfo = vk::ImageCreateInfo();
      imgCreateInfo.imageType     = vk::ImageType::e2D;
      imgCreateInfo.format        = vk::Format::eR8G8B8A8Unorm;
      imgCreateInfo.extent.width  = kWidth;
      imgCreateInfo.extent.height = kHeight;
      imgCreateInfo.extent.depth  = 1;
      imgCreateInfo.arrayLayers   = 1;
      imgCreateInfo.mipLevels     = 1;
      imgCreateInfo.initialLayout = vk::ImageLayout::eUndefined;
      imgCreateInfo.samples       = vk::SampleCountFlagBits::e1;
      imgCreateInfo.tiling        = vk::ImageTiling::eLinear;
      imgCreateInfo.usage         = vk::ImageUsageFlagBits::eTransferDst;

      // Create the image
      auto dstImage = device_->createImageUnique(imgCreateInfo);

      // Create memory to back up the image
      auto memAllocInfo    = vk::MemoryAllocateInfo();
      auto memRequirements = device_->getImageMemoryRequirements(dstImage.get());
      memAllocInfo.allocationSize = memRequirements.size;
      // Memory must be host visible to copy from
      memAllocInfo.memoryTypeIndex = MemoryTypeIndex(memRequirements.memoryTypeBits,
                                                     vk::MemoryPropertyFlagBits::eHostVisible
                                                     | vk::MemoryPropertyFlagBits::eHostCoherent);
      auto dstImageMemory = device_->allocateMemoryUnique(memAllocInfo);
      device_->bindImageMemory(dstImage.get(), dstImageMemory.get(), 0);

      // Do the actual blit from the offscreen image to our host visible destination image
      auto cmdBufAllocateInfo = vk::CommandBufferAllocateInfo(command_pool_.get(), vk::CommandBufferLevel::ePrimary, 1);
      auto copyCmd = device_->allocateCommandBuffers(cmdBufAllocateInfo)[0];
      auto cmdBufInfo = vk::CommandBufferBeginInfo();
      copyCmd.begin(cmdBufInfo);

      // Transition destination image to transfer destination layout
      auto image_barrier          = vk::ImageMemoryBarrier();
      image_barrier.srcAccessMask = {};
      image_barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
      image_barrier.oldLayout     = vk::ImageLayout::eUndefined;
      image_barrier.newLayout     = vk::ImageLayout::eTransferDstOptimal;
      image_barrier.image         = dstImage.get();
      image_barrier.subresourceRange
        = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

      copyCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                              vk::PipelineStageFlagBits::eTransfer,
                              {},
                              0,
                              nullptr,
                              0,
                              nullptr,
                              1,
                              &image_barrier);
      // colorAttachment.image is already in VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, and does not need
      // to be transitioned
      auto imageCopyRegion = vk::ImageCopy();
      imageCopyRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
      imageCopyRegion.srcSubresource.layerCount = 1;
      imageCopyRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
      imageCopyRegion.dstSubresource.layerCount = 1;
      imageCopyRegion.extent.width              = kWidth;
      imageCopyRegion.extent.height             = kHeight;
      imageCopyRegion.extent.depth              = 1;

      copyCmd.copyImage(color_image_.get(),
                        vk::ImageLayout::eTransferSrcOptimal,
                        dstImage.get(),
                        vk::ImageLayout::eTransferDstOptimal,
                        imageCopyRegion);

      // Transition destination image to general layout, which is the required layout for mapping
      // the image memory later on
      auto image_barrier2          = vk::ImageMemoryBarrier();
      image_barrier2.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
      image_barrier2.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
      image_barrier2.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
      image_barrier2.newLayout     = vk::ImageLayout::eGeneral;
      image_barrier2.image         = dstImage.get();
      image_barrier2.subresourceRange
        = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

      copyCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                              vk::PipelineStageFlagBits::eTransfer,
                              {},
                              0,
                              nullptr,
                              0,
                              nullptr,
                              1,
                              &image_barrier2);

      copyCmd.end();

      auto submitInfo               = vk::SubmitInfo();
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers    = &copyCmd;
      auto fence = device_->createFenceUnique(vk::FenceCreateInfo());
      queue_.submit(1, &submitInfo, fence.get());
      device_->waitForFences(1, &(fence.get()), VK_TRUE, UINT64_MAX);

      // Get layout of the image (including row pitch)
      auto subResource       = vk::ImageSubresource();
      subResource.aspectMask = vk::ImageAspectFlagBits::eColor;
      auto subResourceLayout = vk::SubresourceLayout();

      device_->getImageSubresourceLayout(dstImage.get(), &subResource, &subResourceLayout);

      // Map image memory so we can start copying from it
      imagedata = reinterpret_cast<const char*>(device_->mapMemory(dstImageMemory.get(), 0, vk::DeviceSize()));
      imagedata += subResourceLayout.offset;

      /*
        Save host visible framebuffer image to disk (ppm format)
      */
      std::ofstream file("headless.ppm", std::ios::out | std::ios::binary);

      // ppm header
      file << "P6\n" << kWidth << "\n" << kHeight << "\n" << 255 << "\n";

      // If source is BGR (destination is always RGB) and we can't use blit (which does automatic
      // conversion), we'll have to manually swizzle color components Check if source is BGR and
      // needs swizzle
      std::vector<VkFormat> formatsBGR   = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM,
                                          VK_FORMAT_B8G8R8A8_SNORM};
      const bool            colorSwizzle = (std::find(formatsBGR.begin(), formatsBGR.end(),
                                           VK_FORMAT_R8G8B8A8_UNORM) != formatsBGR.end());

      // ppm binary pixel data
      for (int32_t y = 0; y < kHeight; y++)
      {
        unsigned int *row = (unsigned int *)imagedata;
        for (int32_t x = 0; x < kWidth; x++)
        {
          if (colorSwizzle)
          {
            file.write((char *)row + 2, 1);
            file.write((char *)row + 1, 1);
            file.write((char *)row, 1);
          }
          else
          {
            file.write((char *)row, 3);
          }
          row++;
        }
        imagedata += subResourceLayout.rowPitch;
      }
      file.close();
    }
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
  vk::UniqueInstance       instance_;
  vk::PhysicalDevice       physical_device_;
  vk::UniqueDevice         device_;
  vk::Queue                queue_;
  std::size_t              graphics_queue_family_index_;

  vk::UniqueCommandPool   command_pool_;
  vk::UniqueCommandBuffer command_buffer_;
  vk::UniqueRenderPass    render_pass_;

  vk::UniqueImage          color_image_;
  vk::UniqueDeviceMemory   color_image_memory_;
  vk::UniqueImageView      color_image_view_;

  vk::UniqueFramebuffer    framebuffer_;

  vk::UniqueSwapchainKHR     swapchain_;
  vk::Extent2D               swapchain_extent_;

  vk::UniqueSurfaceKHR       surface_;
  vk::SurfaceFormatKHR       surface_format_;
  vk::SurfaceCapabilitiesKHR surface_capabilities_;

  vk::DebugReportCallbackEXT debug_report_callback_;
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
  kWindow = glfwCreateWindow(kWidth, kHeight, "Vulkan", nullptr, nullptr);

  auto renderer = VkRenderer();

  while (glfwWindowShouldClose(kWindow) == GLFW_FALSE)
  {
    glfwPollEvents();
  }
  glfwTerminate();

  return 0;
}
