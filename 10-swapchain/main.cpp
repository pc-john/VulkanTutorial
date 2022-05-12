#include "VulkanWindow.h"
#include <iostream>

using namespace std;


// constants
constexpr const char* appName = "10-swapchain";


// vk::Instance
// (we destroy it as the last one)
static vk::UniqueInstance instance;

// Vulkan window
// (It needs to be destroyed after the swapchain.
// This is required especially on Wayland.)
static VulkanWindow window;

// Vulkan handles and objects
// (they need to be placed in particular (not arbitrary) order
// because they are destructed from the last one to the first one)
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
static vk::CommandBuffer commandBuffer;
static vk::UniqueSemaphore imageAvailableSemaphore;
static vk::UniqueSemaphore renderFinishedSemaphore;


int main(int,char**)
{
	// catch exceptions
	// (vulkan.hpp functions throws if they fail)
	try {

		// Vulkan instance
		instance =
			vk::createInstanceUnique(
				vk::InstanceCreateInfo{
					vk::InstanceCreateFlags(),  // flags
					&(const vk::ApplicationInfo&)vk::ApplicationInfo{
						appName,                 // application name
						VK_MAKE_VERSION(0,0,0),  // application version
						nullptr,                 // engine name
						VK_MAKE_VERSION(0,0,0),  // engine version
						VK_API_VERSION_1_0,      // api version
					},
					0, nullptr,  // no layers
					VulkanWindow::requiredExtensionCount(),  // enabled extension count
					VulkanWindow::requiredExtensionNames(),  // enabled extension names
				}
			);

		// create surface
		vk::SurfaceKHR surface =
			window.init(instance.get(), {1024, 768}, appName);

		// find compatible devices
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

			// select queues for graphics rendering and for presentation
			uint32_t graphicsQueueFamily = UINT32_MAX;
			uint32_t presentationQueueFamily = UINT32_MAX;
			vector<vk::QueueFamilyProperties> queueFamilyList = pd.getQueueFamilyProperties();
			for(uint32_t i=0, c=uint32_t(queueFamilyList.size()); i<c; i++) {
				if(pd.getSurfaceSupportKHR(i, surface)) {
					if(queueFamilyList[i].queueFlags & vk::QueueFlagBits::eGraphics) {
						// if presentation and graphics operations are supported, we use single queue
						compatibleDevicesSingleQueue.emplace_back(pd, i);
						goto nextDevice;
					}
					else
						// if only presentation is supported, we remember the first such queue
						if(presentationQueueFamily == UINT32_MAX)
							presentationQueueFamily = i;
				}
				else {
					if(queueFamilyList[i].queueFlags & vk::QueueFlagBits::eGraphics)
						// if only graphics operations are supported, we remember the first such queue
						if(graphicsQueueFamily == UINT32_MAX)
							graphicsQueueFamily = i;
				}
			}
			if(graphicsQueueFamily != UINT32_MAX && presentationQueueFamily != UINT32_MAX)
				compatibleDevicesTwoQueues.emplace_back(pd, graphicsQueueFamily, presentationQueueFamily);
			nextDevice:;
		}

		// print compatible devices
		cout << "Compatible devices:" << endl;
		for(auto& t : compatibleDevicesSingleQueue)
			cout << "   " << get<0>(t).getProperties().deviceName << endl;
		for(auto& t : compatibleDevicesTwoQueues)
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
					array<const char*, 1>{ "VK_KHR_swapchain" }.data(),  // enabled extension names
					nullptr,    // enabled features
				}
			);

		// get queues
		graphicsQueue = device->getQueue(graphicsQueueFamily, 0);
		presentationQueue = device->getQueue(presentationQueueFamily, 0);

		// choose surface format
		constexpr array allowedSurfaceFormats{
			vk::SurfaceFormatKHR{ vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear },
			vk::SurfaceFormatKHR{ vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear },
			vk::SurfaceFormatKHR{ vk::Format::eA8B8G8R8SrgbPack32, vk::ColorSpaceKHR::eSrgbNonlinear },
		};
		cout << "Surface formats:" << endl;
		vector<vk::SurfaceFormatKHR> availableSurfaceFormats = physicalDevice.getSurfaceFormatsKHR(surface);
			for(vk::SurfaceFormatKHR sf : availableSurfaceFormats)
				cout << "   " << vk::to_string(sf.format) << " " << vk::to_string(sf.colorSpace) << endl;
		if(availableSurfaceFormats.size()==1 && availableSurfaceFormats[0].format==vk::Format::eUndefined)
			// Vulkan spec allowed single eUndefined value until 1.1.111 (2019-06-10)
			// with the meaning you can use any valid vk::Format value.
			// Now, it is forbidden, but let's handle any old driver.
			surfaceFormat = allowedSurfaceFormats[0];
		else {
			for(vk::SurfaceFormatKHR sf : availableSurfaceFormats) {
				auto it = std::find(allowedSurfaceFormats.begin(), allowedSurfaceFormats.end(), sf);
				if(it != allowedSurfaceFormats.end()) {
					surfaceFormat = *it;
					goto surfaceFormatFound;
				}
			}
			if(availableSurfaceFormats.size() == 0)  // Vulkan must return at least one format (this is mandated since Vulkan 1.0.37 (2016-10-10), but was missing in the spec before probably because of omission)
				throw std::runtime_error("Vulkan error: getSurfaceFormatsKHR() returned empty list.");
			surfaceFormat = availableSurfaceFormats[0];
		surfaceFormatFound:;
		}

		// render pass
		renderPass =
			device->createRenderPassUnique(
				vk::RenderPassCreateInfo(
					vk::RenderPassCreateFlags(),  // flags
					1,      // attachmentCount
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
					1,      // subpassCount
					array{  // pSubpasses
						vk::SubpassDescription(
							vk::SubpassDescriptionFlags(),     // flags
							vk::PipelineBindPoint::eGraphics,  // pipelineBindPoint
							0,        // inputAttachmentCount
							nullptr,  // pInputAttachments
							1,        // colorAttachmentCount
							array{    // pColorAttachments
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
					1,      // dependencyCount
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

		window.setRecreateSwapchainCallback(
			[](const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, vk::Extent2D newSurfaceExtent) {

				// clear resources
				swapchainImageViews.clear();
				framebuffers.clear();

				// create new swapchain
				constexpr uint32_t requestedImageCount = 2;
				cout << "Recreating swapchain (extent: " << newSurfaceExtent.width << "x" << newSurfaceExtent.height
				     << ", extent by surfaceCapabilities: " << surfaceCapabilities.currentExtent.width << "x"
					  << surfaceCapabilities.currentExtent.height << ", minImageCount: " << surfaceCapabilities.minImageCount
					  << ", maxImageCount: "<<surfaceCapabilities.maxImageCount << ")" << endl;
		// create swapchain
		/*vk::SurfaceCapabilitiesKHR surfaceCapabilities=physicalDevice.getSurfaceCapabilitiesKHR(surface.get());
		vk::Extent2D currentSurfaceExtent=(surfaceCapabilities.currentExtent.width!=std::numeric_limits<uint32_t>::max())
				?surfaceCapabilities.currentExtent
				:vk::Extent2D{max(min(windowWidth,surfaceCapabilities.maxImageExtent.width),surfaceCapabilities.minImageExtent.width),
				              max(min(windowHeight,surfaceCapabilities.maxImageExtent.height),surfaceCapabilities.minImageExtent.height)};*/
				vk::UniqueSwapchainKHR newSwapchain =
					device->createSwapchainKHRUnique(
						vk::SwapchainCreateInfoKHR(
							vk::SwapchainCreateFlagsKHR(),  // flags
							window.surface(),               // surface
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
							VK_TRUE,  // clipped
							swapchain.get()  // oldSwapchain
						)
					);
				swapchain = move(newSwapchain);

				// swapchain image views
				vector<vk::Image> swapchainImages = device->getSwapchainImagesKHR(swapchain.get());
				swapchainImageViews.reserve(swapchainImages.size());
				for(vk::Image image : swapchainImages)
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
				for(size_t i=0, c=swapchainImages.size(); i<c; i++)
					framebuffers.emplace_back(
						device->createFramebufferUnique(
							vk::FramebufferCreateInfo(
								vk::FramebufferCreateFlags(),  // flags
								renderPass.get(),  // renderPass
								1,  // attachmentCount
								&swapchainImageViews[i].get(),  // pAttachments
								newSurfaceExtent.width,  // width
								newSurfaceExtent.height,  // height
								1  // layers
							)
						)
					);

			});

		// commandPool and commandBuffer
		commandPool =
			device->createCommandPoolUnique(
				vk::CommandPoolCreateInfo(
					vk::CommandPoolCreateFlagBits::eTransient |  // flags
						vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
					graphicsQueueFamily  // queueFamilyIndex
				)
			);
		commandBuffer =
			device->allocateCommandBuffers(
				vk::CommandBufferAllocateInfo(
					commandPool.get(),  // commandPool
					vk::CommandBufferLevel::ePrimary,  // level
					1  // commandBufferCount
				)
			)[0];

		// semaphores
		imageAvailableSemaphore =
			device->createSemaphoreUnique(
				vk::SemaphoreCreateInfo(
					vk::SemaphoreCreateFlags()  // flags
				)
			);
		renderFinishedSemaphore =
			device->createSemaphoreUnique(
				vk::SemaphoreCreateInfo(
					vk::SemaphoreCreateFlags()  // flags
				)
			);

		window.setFrameCallback(
			[]() {

				// acquire image
				uint32_t imageIndex =
					device->acquireNextImageKHR(
						swapchain.get(),                  // swapchain
						numeric_limits<uint64_t>::max(),  // timeout
						imageAvailableSemaphore.get(),    // semaphore to signal
						vk::Fence(nullptr)                // fence to signal
					).value;

				// record command buffer
				commandBuffer.begin(
					vk::CommandBufferBeginInfo(
						vk::CommandBufferUsageFlagBits::eSimultaneousUse,  // flags
						nullptr  // pInheritanceInfo
					)
				);
				commandBuffer.beginRenderPass(
					vk::RenderPassBeginInfo(
						renderPass.get(),  // renderPass
						framebuffers[imageIndex].get(),  // framebuffer
						vk::Rect2D(vk::Offset2D(0,0), window.surfaceExtent()),  // renderArea
						1,  // clearValueCount
						&(const vk::ClearValue&)vk::ClearValue(  // pClearValues
							vk::ClearColorValue(array<float,4>{0.5f,0.5f,1.f,1.f})
						)
					),
					vk::SubpassContents::eInline
				);
				commandBuffer.endRenderPass();
				commandBuffer.end();

				// submit frame
				graphicsQueue.submit(
					vk::ArrayProxy<const vk::SubmitInfo>(
						1,
						&(const vk::SubmitInfo&)vk::SubmitInfo(
							1,                         // waitSemaphoreCount
							&imageAvailableSemaphore.get(),  // pWaitSemaphores
							&(const vk::PipelineStageFlags&)vk::PipelineStageFlags(  // pWaitDstStageMask
								vk::PipelineStageFlagBits::eColorAttachmentOutput),
							1,                         // commandBufferCount
							&commandBuffer,            // pCommandBuffers
							1,                         // signalSemaphoreCount
							&renderFinishedSemaphore.get()  // pSignalSemaphores
						)
					),
					vk::Fence(nullptr)
				);

				// present
				ignore =
					presentationQueue.presentKHR(
						vk::PresentInfoKHR(
							1, &renderFinishedSemaphore.get(),  // waitSemaphoreCount+pWaitSemaphores
							1, &swapchain.get(), &imageIndex,  // swapchainCount+pSwapchains+pImageIndices
							nullptr  // pResults
						)
					);
				presentationQueue.waitIdle();

			},
			physicalDevice,
			device.get()
		);

/*#ifdef _WIN32

#elif USE_WAYLAND

		cout<<"Before showWindow..."<<endl;
		if(wl_display_roundtrip(wl.display) == -1)
			throw runtime_error("wl_display_roundtrip() failed.");

		// run event loop
		cout<<"Entering main loop..."<<endl;
		while(running) {
			if(wl_display_dispatch_pending(wl.display) == -1)
				throw std::runtime_error("wl_display_dispatch_pending() failed.");

#endif*/

		// run main loop
		window.mainLoop();

	// catch exceptions
	} catch(vk::Error &e) {
		cout << "Failed because of Vulkan exception: " << e.what() << endl;
	} catch(exception &e) {
		cout << "Failed because of exception: " << e.what() << endl;
	} catch(...) {
		cout << "Failed because of unspecified exception." << endl;
	}

	// wait for device idle state
	// (to prevent errors during destruction of Vulkan resources)
	try {
		device->waitIdle();
	} catch(vk::Error &e) {
		cout << "Failed because of Vulkan exception: " << e.what() << endl;
	}

	return 0;
}
