#define NOMINMAX
//#include <windows.h>
typedef struct HINSTANCE__* HINSTANCE;
typedef struct HWND__* HWND;
typedef struct HMONITOR__* HMONITOR;
typedef void* HANDLE;
typedef /*_Null_terminated_*/ const wchar_t *LPCWSTR;
typedef unsigned long DWORD;
typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES;
#include <vulkan/vulkan.hpp>
#include <array>
#include <iostream>
#include "VulkanWindow.h"

using namespace std;


// Vulkan instance
// (must be destructed as the last one, at least on Linux, it must be destroyed after display connection)
static vk::UniqueInstance instance;

// windowing variables
// (this needs to be placed before swapchain, at least on Wayland platform)
static VulkanWindow window;

// Vulkan handles and objects
// (they need to be placed in particular (not arbitrary) order
// because they are destructed from the last one to the first one)
static vk::UniqueSurfaceKHR surface;
static vk::PhysicalDevice physicalDevice;
static uint32_t graphicsQueueFamily;
static uint32_t presentationQueueFamily;
static vk::UniqueDevice device;
static vk::Queue graphicsQueue;
static vk::Queue presentationQueue;
static vk::SurfaceFormatKHR surfaceFormat;
static vk::UniqueRenderPass renderPass;
static vk::UniqueSwapchainKHR swapchain;
static vector<vk::UniqueImageView> swapchainImageViews;
static vector<vk::UniqueFramebuffer> framebuffers;
static vk::UniqueCommandPool commandPool;
static vector<vk::UniqueCommandBuffer> commandBuffers;
static vk::UniqueSemaphore imageAvailableSemaphore;
static vk::UniqueSemaphore renderFinishedSemaphore;



/// main function of the application
int main(int, char**)
{
	// catch exceptions
	// (vulkan.hpp fuctions throw if they fail)
	try {

		// Vulkan instance
		instance =
			vk::createInstanceUnique(
				vk::InstanceCreateInfo{
					vk::InstanceCreateFlags(),  // flags
					&(const vk::ApplicationInfo&)vk::ApplicationInfo{
						"11-resizableWindow",    // application name
						VK_MAKE_VERSION(0,0,0),  // application version
						nullptr,                 // engine name
						VK_MAKE_VERSION(0,0,0),  // engine version
						VK_API_VERSION_1_0,      // api version
					},
					0, nullptr,  // no layers
					2,           // enabled extension count
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
					array<const char*,2>{"VK_KHR_surface","VK_KHR_wayland_surface"}.data(),  // enabled extension names
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
					array<const char*,2>{"VK_KHR_surface","VK_KHR_win32_surface"}.data(),  // enabled extension names
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
					array<const char*,2>{"VK_KHR_surface","VK_KHR_xlib_surface"}.data(),  // enabled extension names
#endif
				});

		// create surface
		surface = window.initUnique(instance.get(), {1024, 768}, "11-resizableWindow");

		// find compatible devices
		// (On Windows, all graphics adapters capable of monitor output are usually compatible devices.
		// On Linux X11 platform, only one graphics adapter is compatible device (the one that
		// renders the window).
		vector<vk::PhysicalDevice> deviceList = instance->enumeratePhysicalDevices();
		vector<tuple<vk::PhysicalDevice, uint32_t>> compatibleDevicesSingleQueue;
		vector<tuple<vk::PhysicalDevice, uint32_t, uint32_t>> compatibleDevicesTwoQueues;
		for(vk::PhysicalDevice pd : deviceList) {

			// skip devices without VK_KHR_swapchain
			auto extensionList = pd.enumerateDeviceExtensionProperties();
			for(vk::ExtensionProperties& e : extensionList)
				if(strcmp(e.extensionName, "VK_KHR_swapchain") == 0)
					goto swapchainSupported;
			continue;
			swapchainSupported:

			// select queues (for graphics rendering and for presentation)
			uint32_t graphicsQueueFamily = UINT32_MAX;
			uint32_t presentationQueueFamily = UINT32_MAX;
			vector<vk::QueueFamilyProperties> queueFamilyList = pd.getQueueFamilyProperties();
			uint32_t i = 0;
			for(uint32_t i=0, c=uint32_t(queueFamilyList.size()); i<c; i++) {
				bool p = pd.getSurfaceSupportKHR(i, surface.get()) != 0;
				if(queueFamilyList[i].queueFlags & vk::QueueFlagBits::eGraphics) {
					if(p) {
						compatibleDevicesSingleQueue.emplace_back(pd, i);
						goto nextDevice;
					}
					if(graphicsQueueFamily == UINT32_MAX)
						graphicsQueueFamily = i;
				}
				else {
					if(p)
						if(presentationQueueFamily == UINT32_MAX)
							presentationQueueFamily = i;
				}
			}
			compatibleDevicesTwoQueues.emplace_back(pd, graphicsQueueFamily, presentationQueueFamily);
			nextDevice:;
		}
		cout << "Compatible devices:" << endl;
		for(auto& t : compatibleDevicesSingleQueue)
			cout << "   " << get<0>(t).getProperties().deviceName << endl;
		for(auto& t:compatibleDevicesTwoQueues)
			cout << "   " << get<0>(t).getProperties().deviceName << endl;

		// choose device
		if(compatibleDevicesSingleQueue.size() > 0) {
			auto t = compatibleDevicesSingleQueue.front();
			physicalDevice = get<0>(t);
			graphicsQueueFamily = get<1>(t);
			presentationQueueFamily = graphicsQueueFamily;
		}
		else if(compatibleDevicesTwoQueues.size() > 0) {
			auto t = compatibleDevicesTwoQueues.front();
			physicalDevice = get<0>(t);
			graphicsQueueFamily = get<1>(t);
			presentationQueueFamily = get<2>(t);
		}
		else
			throw runtime_error("No compatible devices.");
		cout << "Using device:\n"
				"   " << physicalDevice.getProperties().deviceName << endl;

		// create device
		device =
			physicalDevice.createDeviceUnique(
				vk::DeviceCreateInfo{
					vk::DeviceCreateFlags(),  // flags
					compatibleDevicesSingleQueue.size()>0 ? uint32_t(1) : uint32_t(2),  // queueCreateInfoCount
					array{  // pQueueCreateInfos
						vk::DeviceQueueCreateInfo{
							vk::DeviceQueueCreateFlags(),
							graphicsQueueFamily,
							1,
							&(const float&)1.f,
						},
						vk::DeviceQueueCreateInfo{
							vk::DeviceQueueCreateFlags(),
							presentationQueueFamily,
							1,
							&(const float&)1.f,
						},
					}.data(),
					0, nullptr,  // no layers
					1,           // number of enabled extensions
					array<const char*,1>{"VK_KHR_swapchain"}.data(),  // enabled extension names
					nullptr,    // enabled features
				}
			);

		// get queues
		graphicsQueue = device->getQueue(graphicsQueueFamily, 0);
		presentationQueue = device->getQueue(presentationQueueFamily, 0);

		// choose surface format
		constexpr array wantedSurfaceFormats{
			vk::SurfaceFormatKHR{ vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear },
			vk::SurfaceFormatKHR{ vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear },
		};
		vector<vk::SurfaceFormatKHR> supportedSurfaceFormats = physicalDevice.getSurfaceFormatsKHR(surface.get());
		if(supportedSurfaceFormats.size()==1 && supportedSurfaceFormats[0].format==vk::Format::eUndefined)
			// Vulkan spec allowed single eUndefined value until 1.1.111 (2019-06-10) with the meaning you can use any valid vk::Format value
			surfaceFormat = wantedSurfaceFormats[0];
		else {
			for(vk::SurfaceFormatKHR sf : supportedSurfaceFormats) {
				auto it = std::find(wantedSurfaceFormats.begin(), wantedSurfaceFormats.end(), sf);
				if(it != wantedSurfaceFormats.end()) {
					surfaceFormat = *it;
					goto surfaceFormatFound;
				}
			}
			if(supportedSurfaceFormats.size() == 0)  // Vulkan must return at least one format (this is mandated since Vulkan 1.0.37 (2016-10-10), but was missing before probably because of omission)
				throw std::runtime_error("Vulkan error: getSurfaceFormatsKHR() returned empty list.");
			surfaceFormat = supportedSurfaceFormats[0];
		surfaceFormatFound:;
		}

		// render pass
		renderPass =
			device->createRenderPassUnique(
				vk::RenderPassCreateInfo(
					vk::RenderPassCreateFlags(),  // flags
					1,                            // attachmentCount
					array{  // pAttachments
						vk::AttachmentDescription(
							vk::AttachmentDescriptionFlags(),  // flags
							surfaceFormat.format,              // format
							vk::SampleCountFlagBits::e1,       // samples
							vk::AttachmentLoadOp::eClear,      // loadOp
							vk::AttachmentStoreOp::eStore,     // storeOp
							vk::AttachmentLoadOp::eDontCare,   // stencilLoadOp
							vk::AttachmentStoreOp::eDontCare,  // stencilStoreOp
							vk::ImageLayout::eUndefined,       // initialLayout
							vk::ImageLayout::ePresentSrcKHR    // finalLayout
						),
					}.data(),
					1,  // subpassCount
					array{  // pSubpasses
						vk::SubpassDescription(
							vk::SubpassDescriptionFlags(),     // flags
							vk::PipelineBindPoint::eGraphics,  // pipelineBindPoint
							0,        // inputAttachmentCount
							nullptr,  // pInputAttachments
							1,        // colorAttachmentCount
							array{  // pColorAttachments
								vk::AttachmentReference(
									0,  // attachment
									vk::ImageLayout::eColorAttachmentOptimal  // layout
								),
							}.data(),
							nullptr,  // pResolveAttachments
							nullptr,  // pDepthStencilAttachment
							0,        // preserveAttachmentCount
							nullptr   // pPreserveAttachments
						),
					}.data(),
					1,  // dependencyCount
					array{  // pDependencies
						vk::SubpassDependency(
							VK_SUBPASS_EXTERNAL,   // srcSubpass
							0,                     // dstSubpass
							vk::PipelineStageFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput),  // srcStageMask
							vk::PipelineStageFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput),  // dstStageMask
							vk::AccessFlags(),     // srcAccessMask
							vk::AccessFlags(vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite),  // dstAccessMask
							vk::DependencyFlags()  // dependencyFlags
						),
					}.data()
				)
			);

		// command pool
		commandPool=
			device->createCommandPoolUnique(
				vk::CommandPoolCreateInfo(
					vk::CommandPoolCreateFlags(),  // flags
					graphicsQueueFamily  // queueFamilyIndex
				)
			);

		// semaphores
		imageAvailableSemaphore=
			device->createSemaphoreUnique(
				vk::SemaphoreCreateInfo(
					vk::SemaphoreCreateFlags()  // flags
				)
			);
		renderFinishedSemaphore=
			device->createSemaphoreUnique(
				vk::SemaphoreCreateInfo(
					vk::SemaphoreCreateFlags()  // flags
				)
			);

		// Recreate swapchain and pipeline callback.
		// The function is usually called after the window resize and on the application start.
		window.setRecreateSwapchainCallback(
			[](const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, vk::Extent2D newSurfaceExtent){

				// clear resources
				commandBuffers.clear();
				framebuffers.clear();
				swapchainImageViews.clear();

				// create new swapchain
				constexpr uint32_t requestedImageCount = 2;
				cout<<"Recreating swapchain "<<newSurfaceExtent.width<<"x"<<newSurfaceExtent.height<<", surfaceCapabilities: "
				    <<surfaceCapabilities.currentExtent.width<<"x"<<surfaceCapabilities.currentExtent.height
				    <<" and min: "<<surfaceCapabilities.minImageCount<<" max: "<<surfaceCapabilities.maxImageCount<<endl;
				swapchain =
					device->createSwapchainKHRUnique(
						vk::SwapchainCreateInfoKHR(
							vk::SwapchainCreateFlagsKHR(),  // flags
							surface.get(),                  // surface
							surfaceCapabilities.maxImageCount==0  // minImageCount
								? max(requestedImageCount, surfaceCapabilities.minImageCount)
								: clamp(requestedImageCount, surfaceCapabilities.minImageCount, surfaceCapabilities.maxImageCount),
							surfaceFormat.format,           // imageFormat
							surfaceFormat.colorSpace,       // imageColorSpace
							newSurfaceExtent,               // imageExtent
							1,                              // imageArrayLayers
							vk::ImageUsageFlagBits::eColorAttachment,  // imageUsage
							(graphicsQueueFamily==presentationQueueFamily) ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent, // imageSharingMode
							(graphicsQueueFamily==presentationQueueFamily) ? uint32_t(0) : uint32_t(2),  // queueFamilyIndexCount
							(graphicsQueueFamily==presentationQueueFamily) ? nullptr : array<uint32_t,2>{graphicsQueueFamily,presentationQueueFamily}.data(),  // pQueueFamilyIndices
							surfaceCapabilities.currentTransform,    // preTransform
							vk::CompositeAlphaFlagBitsKHR::eOpaque,  // compositeAlpha
							vk::PresentModeKHR::eFifo,  // presentMode
							VK_TRUE,         // clipped
							swapchain.get()  // oldSwapchain
						)
					);

				// swapchain images and image views
				vector<vk::Image> swapchainImages=device->getSwapchainImagesKHR(swapchain.get());
				swapchainImageViews.reserve(swapchainImages.size());
				for(vk::Image image:swapchainImages)
					swapchainImageViews.emplace_back(
						device->createImageViewUnique(
							vk::ImageViewCreateInfo(
								vk::ImageViewCreateFlags(),  // flags
								image,                       // image
								vk::ImageViewType::e2D,      // viewType
								surfaceFormat.format,        // format
								vk::ComponentMapping(),      // components
								vk::ImageSubresourceRange(   // subresourceRange
									vk::ImageAspectFlagBits::eColor,  // aspectMask
									0,  // baseMipLevel
									1,  // levelCount
									0,  // baseArrayLayer
									1   // layerCount
								)
							)
						)
					);

				// framebuffers
				framebuffers.reserve(swapchainImages.size());
				for(size_t i=0,c=swapchainImages.size(); i<c; i++)
					framebuffers.emplace_back(
						device->createFramebufferUnique(
							vk::FramebufferCreateInfo(
								vk::FramebufferCreateFlags(),   // flags
								renderPass.get(),               // renderPass
								1,  // attachmentCount
								&swapchainImageViews[i].get(),  // pAttachments
								newSurfaceExtent.width,         // width
								newSurfaceExtent.height,        // height
								1  // layers
							)
						)
					);

				// reallocate command buffers
				if(commandBuffers.size()!=swapchainImages.size()) {
					commandBuffers=
						device->allocateCommandBuffersUnique(
							vk::CommandBufferAllocateInfo(
								commandPool.get(),                 // commandPool
								vk::CommandBufferLevel::ePrimary,  // level
								uint32_t(swapchainImages.size())   // commandBufferCount
							)
						);
				}

				// record command buffers
				for(size_t i=0,c=swapchainImages.size(); i<c; i++) {
					vk::CommandBuffer& cb=commandBuffers[i].get();
					cb.begin(
						vk::CommandBufferBeginInfo(
							vk::CommandBufferUsageFlagBits::eSimultaneousUse,  // flags
							nullptr  // pInheritanceInfo
						)
					);
					cb.beginRenderPass(
						vk::RenderPassBeginInfo(
							renderPass.get(),       // renderPass
							framebuffers[i].get(),  // framebuffer
							vk::Rect2D(vk::Offset2D(0,0), newSurfaceExtent),  // renderArea
							1,                      // clearValueCount
							&(const vk::ClearValue&)vk::ClearValue(vk::ClearColorValue(array<float,4>{0.f,1.f,0.f,1.f}))  // pClearValues
						),
						vk::SubpassContents::eInline
					);
					cb.endRenderPass();
					cb.end();
				}
			});

		window.mainLoop(
			physicalDevice,
			device.get(),
			surface.get(),
			[](){

				// acquire image
				uint32_t imageIndex;
				vk::Result r =
					device->acquireNextImageKHR(
						swapchain.get(),                  // swapchain
						numeric_limits<uint64_t>::max(),  // timeout
						imageAvailableSemaphore.get(),    // semaphore to signal
						vk::Fence(nullptr),               // fence to signal
						&imageIndex                       // pImageIndex
					);
				if(r != vk::Result::eSuccess) {
					if(r == vk::Result::eSuboptimalKHR)
						window.scheduleSwapchainResize();
					else if(r == vk::Result::eErrorOutOfDateKHR) {
						window.scheduleSwapchainResize();
						return;
					} else
						throw runtime_error("Vulkan function vkAcquireNextImageKHR failed.");
				}

				// submit frame
				graphicsQueue.submit(
					vk::ArrayProxy<const vk::SubmitInfo>(
						1,
						&(const vk::SubmitInfo&)vk::SubmitInfo(
							1,                               // waitSemaphoreCount
							&imageAvailableSemaphore.get(),  // pWaitSemaphores
							&(const vk::PipelineStageFlags&)vk::PipelineStageFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput),  // pWaitDstStageMask
							1,                               // commandBufferCount
							&commandBuffers[imageIndex].get(),  // pCommandBuffers
							1,                               // signalSemaphoreCount
							&renderFinishedSemaphore.get()   // pSignalSemaphores
						)
					),
					vk::Fence(nullptr)
				);

				// present
				r =
					presentationQueue.presentKHR(
						&(const vk::PresentInfoKHR&)vk::PresentInfoKHR(
							1, &renderFinishedSemaphore.get(),  // waitSemaphoreCount+pWaitSemaphores
							1, &swapchain.get(), &imageIndex,   // swapchainCount+pSwapchains+pImageIndices
							nullptr                             // pResults
						)
					);
				if(r != vk::Result::eSuccess) {
					if(r == vk::Result::eSuboptimalKHR)
						window.scheduleSwapchainResize();
					else if(r == vk::Result::eErrorOutOfDateKHR) {
						window.scheduleSwapchainResize();
						return;
					} else
						throw runtime_error("Vulkan function vkQueuePresentKHR failed.");
				}

				// wait for completion
				presentationQueue.waitIdle();

			});

	// catch exceptions
	} catch(vk::Error &e) {
		cout<<"Failed because of Vulkan exception: "<<e.what()<<endl;
	} catch(exception &e) {
		cout<<"Failed because of exception: "<<e.what()<<endl;
	} catch(...) {
		cout<<"Failed because of unspecified exception."<<endl;
	}

	return 0;
}
