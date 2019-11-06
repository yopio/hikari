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
    std::cout << "Create surface" << std::endl;
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
    std::cout << "Create swapchain" << std::endl;
    const auto image_count = std::max(2u, surface_capabilities_.maxImageCount);
    auto       extent      = surface_capabilities_.currentExtent;
    if (extent.width == ~0u)
    {
      // 任意サイズが利用可能なのでウィンドウサイズを利用する
      int w, h;
      glfwGetWindowSize(kWindow, &w, &h);
      std::cout << w << " x " << h << std::endl;
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
      Create depth buffer.
    */
    std::cout << "Create depth image as depthbuffer." << std::endl;
    auto depth_image_create_info = vk::ImageCreateInfo({},
                                                       vk::ImageType::e2D,
                                                       vk::Format::eD32Sfloat,
                                                       vk::Extent3D(swapchain_extent_, 1),
                                                       1,
                                                       1,
                                                       vk::SampleCountFlagBits::e1,
                                                       vk::ImageTiling::eOptimal,
                                                       vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                                       vk::SharingMode::eExclusive,
                                                       0,
                                                       nullptr,
                                                       vk::ImageLayout::eUndefined);
    depth_image_ = device_->createImageUnique(depth_image_create_info);

    auto depth_memory_requirement = device_->getImageMemoryRequirements(depth_image_.get());
    auto depth_memory_type_index  = MemoryTypeIndex(depth_memory_requirement.memoryTypeBits,
                                                    vk::MemoryPropertyFlagBits::eDeviceLocal);
    auto depth_memory_allocate_info = vk::MemoryAllocateInfo(depth_memory_requirement.size,
                                                             depth_memory_type_index);
    depth_image_memory_ = device_->allocateMemoryUnique(depth_memory_allocate_info);
    device_->bindImageMemory(depth_image_.get(), depth_image_memory_.get(), 0);

    /*
      View creation
    */
    std::cout << "Create views" << std::endl;
    auto swapchain_images = device_->getSwapchainImagesKHR(swapchain_.get());
    for (auto& image : swapchain_images)
    {
      // スワップチェインが保持するイメージの数だけ View を作成する
      auto view_create_info = vk::ImageViewCreateInfo();
      view_create_info.setViewType(vk::ImageViewType::e2D);
      view_create_info.setFormat(surface_format_.format);
      view_create_info.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eR,
                                                          vk::ComponentSwizzle::eG,
                                                          vk::ComponentSwizzle::eB,
                                                          vk::ComponentSwizzle::eA));
      auto subresourcerange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor,0, 1, 0, 1);
      view_create_info.setSubresourceRange(subresourcerange);
      view_create_info.setImage(image);

      swapchain_image_views_.push_back(std::move(device_->createImageViewUnique(view_create_info)));
    }
    {
      // Depth buffer
      auto view_create_info = vk::ImageViewCreateInfo();
      view_create_info.setViewType(vk::ImageViewType::e2D);
      view_create_info.setFormat(vk::Format::eD32Sfloat);
      view_create_info.setComponents(vk::ComponentMapping(vk::ComponentSwizzle::eR,
                                                          vk::ComponentSwizzle::eG,
                                                          vk::ComponentSwizzle::eB,
                                                          vk::ComponentSwizzle::eA));
      auto subresourcerange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eDepth,0, 1, 0, 1);
      view_create_info.setSubresourceRange(subresourcerange);
      view_create_info.setImage(depth_image_.get());

      depth_image_view_ = device_->createImageViewUnique(view_create_info);
    }

    /*
      Create render pass

      アタッチメントは単一画像と解釈できる。これを１アタッチメントとして
      RenderPass にたして入出力の定義をする
    */
    std::cout << "Create renderpass." << std::endl;
    std::array<vk::AttachmentDescription, 2> attachments;

    // Swapchainn の color 分
    attachments[0].setFormat(surface_format_.format);
    attachments[0].setSamples(vk::SampleCountFlagBits::e1);
    attachments[0].setLoadOp(vk::AttachmentLoadOp::eClear);
    attachments[0].setStoreOp(vk::AttachmentStoreOp::eStore);
    attachments[0].setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
    attachments[0].setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
    attachments[0].setInitialLayout(vk::ImageLayout::eUndefined);
    attachments[0].setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

    // Depth buffer 分
    attachments[1].setFormat(vk::Format::eD32Sfloat);
    attachments[1].setSamples(vk::SampleCountFlagBits::e1);
    attachments[1].setLoadOp(vk::AttachmentLoadOp::eClear);
    attachments[1].setStoreOp(vk::AttachmentStoreOp::eStore);
    attachments[1].setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
    attachments[1].setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
    attachments[1].setInitialLayout(vk::ImageLayout::eUndefined);
    attachments[1].setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    /*
      Attachment Description と対応するようにする必要がある
      ex) attachment[0] は color attachemnt なので color_reference の attachment (第一引数) は 0 になる
    */
    auto color_reference = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);
    auto depth_reference = vk::AttachmentReference(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

    /*
      レンダーパスは複数のサプパスを保持することが可能
      リソースの依存関係を設定する
    */
    auto subpass_description = vk::SubpassDescription();
    subpass_description.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
    subpass_description.setColorAttachmentCount(1);
    subpass_description.setPColorAttachments(&color_reference);
    subpass_description.setPDepthStencilAttachment(&depth_reference);

    auto renderpass_create_info = vk::RenderPassCreateInfo();
    renderpass_create_info.setAttachmentCount(attachments.size());
    renderpass_create_info.setSubpassCount(1);
    renderpass_create_info.setPSubpasses(&subpass_description);

    render_pass_ = device_->createRenderPassUnique(renderpass_create_info);
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

  vk::UniqueFramebuffer    framebuffer_;

  vk::UniqueSwapchainKHR           swapchain_;
  vk::Extent2D                     swapchain_extent_;
  std::vector<vk::UniqueImageView> swapchain_image_views_;

  vk::UniqueImage          depth_image_;
  vk::UniqueDeviceMemory   depth_image_memory_;
  vk::UniqueImageView      depth_image_view_;

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
