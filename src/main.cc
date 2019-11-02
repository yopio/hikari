
#include <iostream>
#include <sstream>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

static const std::string kApplicationName = "Renderer";

#ifndef NDEBUG
#define VK_CHECK( call )                                                                \
  do                                                                                    \
  {                                                                                     \
    const VkResult res = call;                                                          \
    if ( res != VK_SUCCESS )                                                            \
    {                                                                                   \
      std::stringstream ss;                                                             \
      ss << "Vulkan call '" << #call << "' failed: " __FILE__ ":" << __LINE__ << ")\n"; \
      throw std::runtime_error( ss.str().c_str() );                                     \
    }                                                                                   \
  } while (0);
#else
#define VK_CHECK( call )
#endif

constexpr uint32_t kWindowWidth  = 640;
constexpr uint32_t kWindowHeight = 480;

VkInstance                       instance_;
VkPhysicalDevice                 physical_device_;
VkPhysicalDeviceMemoryProperties physical_device_memory_properties_;
uint32_t                         graphics_queue_index_;
VkDevice                         device_;
VkQueue                          device_queue_;
VkCommandPool                    command_pool_;
VkSurfaceKHR                     surface_;
VkSurfaceFormatKHR               surface_format_;
VkSurfaceCapabilitiesKHR         surface_capabilities_;
VkPresentModeKHR                 present_mode_;
VkSwapchainKHR                   swapchain_;
VkExtent2D                       swapchain_extent_;

GLFWwindow* window_;

void CreateInstance()
{
  /*
    Vulkan API を利用するために必須なオブジェクト
  */

  std::vector<const char *> extensions;

  // 全ての拡張情報名を取得する
  std::vector<VkExtensionProperties> properties;
  {
    /*
      全ての拡張機能を有効にして初期化する、デフォルトは無効
      vkEnumerateinstanceextensionproperties１回目は個数を取得、２回目は個数分の領域を確保してから情報を取得する
    */
    uint32_t property_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &property_count, nullptr);
    properties.resize(property_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &property_count, properties.data());

    for (const auto &p : properties)
    {
      extensions.push_back(p.extensionName);
    }
  }

  VkApplicationInfo app_info{};
  app_info.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = kApplicationName.c_str();
  app_info.pEngineName      = kApplicationName.c_str();
  app_info.apiVersion       = VK_API_VERSION_1_1;
  app_info.engineVersion    = VK_MAKE_VERSION(1, 0, 0);

  VkInstanceCreateInfo create_info{};
  create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
  create_info.ppEnabledExtensionNames = extensions.data();
  create_info.pApplicationInfo        = &app_info;
#ifndef NDEBUG
  const char *layers[]            = {"VK_LAYER_LUNARG_standard_validation"};
  create_info.enabledLayerCount   = 1;
  create_info.ppEnabledLayerNames = layers;
#endif

  const auto result = vkCreateInstance(&create_info, nullptr, &instance_);
  VK_CHECK(result);
}

void SelectPhysicalDevice()
{
  /*
    物理的に接続されている GPU を検出する
  */
  std::vector<VkPhysicalDevice> physical_devices;
  {
    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr);

    physical_devices.resize(physical_device_count);
    vkEnumeratePhysicalDevices(instance_, &physical_device_count, physical_devices.data());
  }

  physical_device_ = physical_devices[0];
  vkGetPhysicalDeviceMemoryProperties(physical_device_, &physical_device_memory_properties_);
}

void FindGraphicsQueueIndex()
{
  /*
    CPU から GPU へ書き込んだコマンドを流すパイプ
    デバイスキューのは処理ができる内容によって区分けがされている
  */

  std::vector<VkQueueFamilyProperties> properties;
  {
    uint32_t propertiy_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &propertiy_count, nullptr);

    properties.resize(propertiy_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &propertiy_count, properties.data());
  }

  for (int i = 0; i < properties.size(); ++i)
  {
    if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
    {
      graphics_queue_index_ = i;
      break;
    }
  }
}

void CreateDevice()
{
  /*
    論理デバイス作成
  */

  const float kDefaultQueuePriority(1.0f);

  VkDeviceQueueCreateInfo device_queue_create_info{};
  device_queue_create_info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  device_queue_create_info.queueFamilyIndex = graphics_queue_index_;
  device_queue_create_info.queueCount       = 1;
  device_queue_create_info.pQueuePriorities = &kDefaultQueuePriority;

  // 論理デバイス拡張を全て取得する
  std::vector<VkExtensionProperties> extension_properties;
  {
    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &extension_count, nullptr);

    extension_properties.resize(extension_count);
    vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &extension_count,
                                         extension_properties.data());
  }

  std::vector<const char *> extensions;
  for (const auto &p : extension_properties)
  {
    extensions.push_back(p.extensionName);
  }

  VkDeviceCreateInfo device_create_info{};
  device_create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_create_info.pQueueCreateInfos       = &device_queue_create_info;
  device_create_info.queueCreateInfoCount    = 1;
  device_create_info.ppEnabledExtensionNames = extensions.data();
  device_create_info.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());

  // 論理デバイス作成
  auto result = vkCreateDevice(physical_device_, &device_create_info, nullptr, &device_);
  VK_CHECK(result);

  // デバイスキュー取得
  vkGetDeviceQueue(device_, graphics_queue_index_, 0, &device_queue_);
}

void CreateCommandPool()
{
  /*
    Comamnd buffer は GPU に命令を送るためのコマンドバッファを保持する
  */
  VkCommandPoolCreateInfo command_pool_create_info{};
  command_pool_create_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_create_info.queueFamilyIndex = graphics_queue_index_;
  command_pool_create_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  const auto result =
      vkCreateCommandPool(device_, &command_pool_create_info, nullptr, &command_pool_);
  VK_CHECK(result);
}

void SelectSurfaceFormat(VkFormat format)
{
  std::vector<VkSurfaceFormatKHR> surface_formats;
  {
    uint32_t surface_format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &surface_format_count,
                                         nullptr);

    surface_formats.resize(surface_format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &surface_format_count,
                                         surface_formats.data());
  }

  // 引数と同じフォーマットを探す
  for (const auto &f : surface_formats)
  {
    if (f.format == format)
    {
      surface_format_ = f;
    }
  }
}

void CreateSwapChain()
{
  /*
    描画された結果をディスプレイ上に表示するためのもの
    事前に Surface の生成
      -> 表示フォーマット決定
      -> サーフェスサイズ取得
      -> Preset モード確認
    をしてから swapchain を生成する
  */
  auto extent = surface_capabilities_.currentExtent;
  if (extent.width == ~0u)
  {
    // 任意サイズが可能なので window のサイズと同じにする
    int w, h;
    glfwGetWindowSize(window_, &w, &h);
    extent.width  = static_cast<uint32_t>(w);
    extent.height = static_cast<uint32_t>(h);
  }

  uint32_t queue_family_indices[] = {graphics_queue_index_};
  VkSwapchainCreateInfoKHR swapchain_create_info{};
  swapchain_create_info.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchain_create_info.surface               = surface_;
  swapchain_create_info.minImageCount         = std::max(2u, surface_capabilities_.minImageCount);
  swapchain_create_info.imageFormat           = surface_format_.format;
  swapchain_create_info.imageColorSpace       = surface_format_.colorSpace;
  swapchain_create_info.imageExtent           = extent;
  swapchain_create_info.imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  swapchain_create_info.preTransform          = surface_capabilities_.currentTransform;
  swapchain_create_info.imageArrayLayers      = 1;
  swapchain_create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
  swapchain_create_info.queueFamilyIndexCount = 0;
  swapchain_create_info.presentMode           = present_mode_;
  swapchain_create_info.oldSwapchain          = VK_NULL_HANDLE;
  swapchain_create_info.clipped               = VK_TRUE;
  swapchain_create_info.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

  const auto result = vkCreateSwapchainKHR(device_, &swapchain_create_info, nullptr, &swapchain_);
  VK_CHECK(result);

  swapchain_extent_ = extent;
}

int main(int argc, char *argv[])
{
  if (!glfwInit())
  {
    std::cerr << "Failed to initialize GLFW3." << std::endl;
    return -1;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, 0);
  window_ =
      glfwCreateWindow(kWindowWidth, kWindowHeight, kApplicationName.c_str(), nullptr, nullptr);

  CreateInstance();
  SelectPhysicalDevice();
  FindGraphicsQueueIndex();
  CreateDevice();
  CreateCommandPool();

  VkBool32 is_support;
  glfwCreateWindowSurface(instance_, window_, nullptr, &surface_);
  SelectSurfaceFormat(VK_FORMAT_R8G8B8A8_UNORM);
  present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &surface_capabilities_);
  vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, graphics_queue_index_, surface_, &is_support);
  CreateSwapChain();

  return 0;
}
