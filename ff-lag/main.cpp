#include "VulkanWindow.h"
#include "Timestamps.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>

using namespace std;


// constants
constexpr const char* appName = "ff-lag";


// shader code in SPIR-V binary
static const uint32_t vsSpirv[] = {
#include "shader.vert.spv"
};
static const uint32_t fsSpirv[] = {
#include "shader.frag.spv"
};


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
static vk::UniqueSemaphore renderingFinishedSemaphore;
static vk::UniqueShaderModule vsModule;
static vk::UniqueShaderModule fsModule;
static vk::UniquePipelineLayout pipelineLayout;
static vk::UniquePipeline pipeline;

enum class FrameUpdateMode { OnDemand, Continuous, MaxFrameRate };
static FrameUpdateMode frameUpdateMode = FrameUpdateMode::Continuous;
static size_t frameID = 0;
static size_t fpsNumFrames = ~size_t(0);
static chrono::high_resolution_clock::time_point fpsStartTime;
static TimestampGenerator tsg;
static uint64_t frameStartTS;
static vector<uint64_t> frameTimes;
static vector<uint64_t> frameRenderingTimes;
static uint64_t presentFinishedTime;
static vector<uint64_t> presentBlockingTimes;
static vector<uint64_t> presentationLagTimes;
static vector<uint64_t> waitTimes;
static vector<uint64_t> frameStartTimes;
static float gpuTimestampPeriod;
static uint64_t gpuTimestampMask;
#if _WIN32
static constexpr vk::TimeDomainEXT timestampHostTimeDomain = vk::TimeDomainEXT::eQueryPerformanceCounter;
#else
static constexpr vk::TimeDomainEXT timestampHostTimeDomain = vk::TimeDomainEXT::eClockMonotonicRaw;
#endif
static struct VkFuncs : vk::DispatchLoaderBase {
	PFN_vkWaitForPresentKHR vkWaitForPresentKHR;
} vkFuncs;


int main(int argc, char** argv)
{
	// catch exceptions
	// (vulkan.hpp functions throw if they fail)
	try {

		// process command-line arguments
		for(int i=1; i<argc; i++)
			if(strcmp(argv[i], "--on-demand") == 0)
				frameUpdateMode = FrameUpdateMode::OnDemand;
			else if(strcmp(argv[i], "--continuous") == 0)
				frameUpdateMode = FrameUpdateMode::Continuous;
			else if(strcmp(argv[i], "--max-frame-rate") == 0)
				frameUpdateMode = FrameUpdateMode::MaxFrameRate;
			else {
				if(strcmp(argv[i], "--help") != 0 && strcmp(argv[i], "-h") != 0)
					cout << "Unrecognized option: " << argv[i] << endl;
				cout << appName << " usage:\n"
						"   --help or -h:  usage information\n"
						"   --on-demand:   on demand window content refresh,\n"
						"                  this conserves computing resources\n"
						"   --continuous:  constantly update window content using\n"
						"                  screen refresh rate, this is the default\n"
						"   --max-frame-rate:  ignore screen refresh rate, update\n"
						"                      window content as often as possible\n" << endl;
				exit(99);
			}
		// instance extensions
		vector<const char*> extensionsToEnable = VulkanWindow::requiredExtensions();
		extensionsToEnable.push_back("VK_KHR_get_physical_device_properties2");

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
					uint32_t(extensionsToEnable.size()),  // enabled extension count
					extensionsToEnable.data(),  // enabled extension names
				}
			);

		// create surface
		vk::SurfaceKHR surface =
			window.init(instance.get(), {1024, 768}, appName);

		// find compatible devices
		vector<vk::PhysicalDevice> deviceList = instance->enumeratePhysicalDevices();
		vector<tuple<vk::PhysicalDevice, uint32_t, uint32_t, vk::PhysicalDeviceProperties,
		       uint32_t, vector<vk::ExtensionProperties>>> compatibleDevices;
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

				// test for presentation support
				if(pd.getSurfaceSupportKHR(i, surface)) {

					// test for graphics operations support
					if(queueFamilyList[i].queueFlags & vk::QueueFlagBits::eGraphics) {
						// if presentation and graphics operations are supported on the same queue,
						// we will use single queue
						compatibleDevices.emplace_back(pd, i, i, pd.getProperties(),
						                               queueFamilyList[i].timestampValidBits,
						                               move(extensionList));
						goto nextDevice;
					}
					else
						// if only presentation is supported, we store the first such queue
						if(presentationQueueFamily == UINT32_MAX)
							presentationQueueFamily = i;
				}
				else {
					if(queueFamilyList[i].queueFlags & vk::QueueFlagBits::eGraphics)
						// if only graphics operations are supported, we store the first such queue
						if(graphicsQueueFamily == UINT32_MAX)
							graphicsQueueFamily = i;
				}
			}

			if(graphicsQueueFamily != UINT32_MAX && presentationQueueFamily != UINT32_MAX)
				// presentation and graphics operations are supported on the different queues
				compatibleDevices.emplace_back(pd, graphicsQueueFamily, presentationQueueFamily,
				                               pd.getProperties(),
				                               queueFamilyList[graphicsQueueFamily].timestampValidBits,
				                               move(extensionList));
			nextDevice:;
		}

		// print compatible devices
		cout << "Compatible devices:" << endl;
		for(auto& t : compatibleDevices)
			cout << "   " << get<3>(t).deviceName << " (graphics queue: " << get<1>(t)
			     << ", presentation queue: " << get<2>(t)
			     << ", type: " << to_string(get<3>(t).deviceType) << ")" << endl;

		// choose the best device
		auto bestDevice = compatibleDevices.begin();
		if(bestDevice == compatibleDevices.end())
			throw runtime_error("No compatible devices.");
		constexpr const array deviceTypeScore = {
			10, // vk::PhysicalDeviceType::eOther         - lowest score
			40, // vk::PhysicalDeviceType::eIntegratedGpu - high score
			50, // vk::PhysicalDeviceType::eDiscreteGpu   - highest score
			30, // vk::PhysicalDeviceType::eVirtualGpu    - normal score
			20, // vk::PhysicalDeviceType::eCpu           - low score
			10, // unknown vk::PhysicalDeviceType
		};
		int bestScore = deviceTypeScore[clamp(int(get<3>(*bestDevice).deviceType), 0, int(deviceTypeScore.size())-1)];
		if(get<1>(*bestDevice) == get<2>(*bestDevice))
			bestScore++;
		for(auto it=compatibleDevices.begin()+1; it!=compatibleDevices.end(); it++) {
			int score = deviceTypeScore[clamp(int(get<3>(*it).deviceType), 0, int(deviceTypeScore.size())-1)];
			if(get<1>(*it) == get<2>(*it))
				score++;
			if(score > bestScore) {
				bestDevice = it;
				bestScore = score;
			}
		}
		cout << "Using device:\n"
		        "   " << get<3>(*bestDevice).deviceName << endl;
		physicalDevice = get<0>(*bestDevice);
		graphicsQueueFamily = get<1>(*bestDevice);
		presentationQueueFamily = get<2>(*bestDevice);
		gpuTimestampPeriod = get<3>(*bestDevice).limits.timestampPeriod;
		uint32_t gpuTimestampValidBits = get<4>(*bestDevice);
		gpuTimestampMask = (uint64_t(1) << (gpuTimestampValidBits&0x3f)) - 1;
		vector<vk::ExtensionProperties> extensionList = move(get<5>(*bestDevice));
		cout << "PresentId support: " << physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDevicePresentIdFeaturesKHR>().get<vk::PhysicalDevicePresentIdFeaturesKHR>().presentId << endl;
		cout << "PresentWait support: " << physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDevicePresentWaitFeaturesKHR>().get<vk::PhysicalDevicePresentWaitFeaturesKHR>().presentWait << endl;
		compatibleDevices.clear();  // release memory

		// extensions to enable
		bool useCalibratedTimestamps = false;
		bool presentWaitSupported = false;
		extensionsToEnable.clear();
		extensionsToEnable.reserve(4);
		extensionsToEnable.push_back("VK_KHR_swapchain");
		for(vk::ExtensionProperties& e : extensionList) {
			if(strcmp(e.extensionName, "VK_EXT_calibrated_timestamps") == 0) {
				extensionsToEnable.push_back("VK_EXT_calibrated_timestamps");
				useCalibratedTimestamps = true;
			}
			if(strcmp(e.extensionName, "VK_KHR_present_wait") == 0) {
				extensionsToEnable.push_back("VK_KHR_present_id");
				extensionsToEnable.push_back("VK_KHR_present_wait");
				presentWaitSupported = true;
			}
		}

		// create device
		device =
			physicalDevice.createDeviceUnique(
				vk::StructureChain<vk::DeviceCreateInfo,
				                   vk::PhysicalDevicePresentIdFeaturesKHR,
				                   vk::PhysicalDevicePresentWaitFeaturesKHR>
				{
					vk::DeviceCreateInfo{
						vk::DeviceCreateFlags(),  // flags
						graphicsQueueFamily==presentationQueueFamily ? uint32_t(1) : uint32_t(2),  // queueCreateInfoCount
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
						uint32_t(extensionsToEnable.size()),  // number of enabled extensions
						extensionsToEnable.data(),  // enabled extension names
						nullptr,    // enabled features
					},
					vk::PhysicalDevicePresentIdFeaturesKHR(1),
					vk::PhysicalDevicePresentWaitFeaturesKHR(1)
				}.get()
#if 0
				vk::DeviceCreateInfo{
					vk::DeviceCreateFlags(),  // flags
					graphicsQueueFamily==presentationQueueFamily ? uint32_t(1) : uint32_t(2),  // queueCreateInfoCount
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
					uint32_t(extensionsToEnable.size()),  // number of enabled extensions
					extensionsToEnable.data(),  // enabled extension names
					nullptr,    // enabled features
				}.setPNext(
					&vk::PhysicalDevicePresentIdFeaturesKHR(1)
				)
#endif
			);

		// get queues
		graphicsQueue = device->getQueue(graphicsQueueFamily, 0);
		presentationQueue = device->getQueue(presentationQueueFamily, 0);

		// function pointers
		vkFuncs.vkWaitForPresentKHR = PFN_vkWaitForPresentKHR(device->getProcAddr("vkWaitForPresentKHR"));

		// init TimestampGenerator
		tsg.init(device.get(), graphicsQueue, graphicsQueueFamily,
		        useCalibratedTimestamps, timestampHostTimeDomain, 2);

		// print surface formats
		cout << "Surface formats:" << endl;
		vector<vk::SurfaceFormatKHR> availableSurfaceFormats = physicalDevice.getSurfaceFormatsKHR(surface);
		for(vk::SurfaceFormatKHR sf : availableSurfaceFormats)
			cout << "   " << vk::to_string(sf.format) << ", color space: " << vk::to_string(sf.colorSpace) << endl;

		// choose surface format
		constexpr const array allowedSurfaceFormats{
			vk::SurfaceFormatKHR{ vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear },
			vk::SurfaceFormatKHR{ vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear },
			vk::SurfaceFormatKHR{ vk::Format::eA8B8G8R8SrgbPack32, vk::ColorSpaceKHR::eSrgbNonlinear },
		};
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
		cout << "Using format:\n"
		     << "   " << to_string(surfaceFormat.format) << ", color space: " << to_string(surfaceFormat.colorSpace) << endl;

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

				// print info
				cout << "Recreating swapchain (extent: " << newSurfaceExtent.width << "x" << newSurfaceExtent.height
				     << ", extent by surfaceCapabilities: " << surfaceCapabilities.currentExtent.width << "x"
					  << surfaceCapabilities.currentExtent.height << ", minImageCount: " << surfaceCapabilities.minImageCount
					  << ", maxImageCount: " << surfaceCapabilities.maxImageCount << ")" << endl;

				// create new swapchain
				constexpr const uint32_t requestedImageCount = 2;
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
							uint32_t(2),  // queueFamilyIndexCount
							array<uint32_t, 2>{graphicsQueueFamily, presentationQueueFamily}.data(),  // pQueueFamilyIndices
							surfaceCapabilities.currentTransform,    // preTransform
							vk::CompositeAlphaFlagBitsKHR::eOpaque,  // compositeAlpha
							[]()  // presentMode
								{
									// for MaxFrameRate, try Mailbox and Immediate if they are available
									if(frameUpdateMode == FrameUpdateMode::MaxFrameRate) {
										vector<vk::PresentModeKHR> modes =
											physicalDevice.getSurfacePresentModesKHR(window.surface());
										if(find(modes.begin(), modes.end(), vk::PresentModeKHR::eMailbox) != modes.end())
											return vk::PresentModeKHR::eMailbox;
										if(find(modes.begin(), modes.end(), vk::PresentModeKHR::eImmediate) != modes.end())
											return vk::PresentModeKHR::eImmediate;
									}

									// return Fifo that is always supported
									return vk::PresentModeKHR::eFifo;
								}(),
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

				// pipeline
				pipeline =
					device->createGraphicsPipelineUnique(
						nullptr,  // pipelineCache
						vk::GraphicsPipelineCreateInfo(
							vk::PipelineCreateFlags(),  // flags

							// shader stages
							2,  // stageCount
							array{  // pStages
								vk::PipelineShaderStageCreateInfo{
									vk::PipelineShaderStageCreateFlags(),  // flags
									vk::ShaderStageFlagBits::eVertex,  // stage
									vsModule.get(),  // module
									"main",  // pName
									nullptr  // pSpecializationInfo
								},
								vk::PipelineShaderStageCreateInfo{
									vk::PipelineShaderStageCreateFlags(),  // flags
									vk::ShaderStageFlagBits::eFragment,  // stage
									fsModule.get(),  // module
									"main",  // pName
									nullptr  // pSpecializationInfo
								},
							}.data(),

							// vertex input
							&(const vk::PipelineVertexInputStateCreateInfo&)vk::PipelineVertexInputStateCreateInfo{  // pVertexInputState
								vk::PipelineVertexInputStateCreateFlags(),  // flags
								0,        // vertexBindingDescriptionCount
								nullptr,  // pVertexBindingDescriptions
								0,        // vertexAttributeDescriptionCount
								nullptr   // pVertexAttributeDescriptions
							},

							// input assembly
							&(const vk::PipelineInputAssemblyStateCreateInfo&)vk::PipelineInputAssemblyStateCreateInfo{  // pInputAssemblyState
								vk::PipelineInputAssemblyStateCreateFlags(),  // flags
								vk::PrimitiveTopology::eTriangleList,  // topology
								VK_FALSE  // primitiveRestartEnable
							},

							// tessellation
							nullptr, // pTessellationState

							// viewport
							&(const vk::PipelineViewportStateCreateInfo&)vk::PipelineViewportStateCreateInfo{  // pViewportState
								vk::PipelineViewportStateCreateFlags(),  // flags
								1,  // viewportCount
								array{  // pViewports
									vk::Viewport(0.f, 0.f, float(newSurfaceExtent.width), float(newSurfaceExtent.height), 0.f, 1.f),
								}.data(),
								1,  // scissorCount
								array{  // pScissors
									vk::Rect2D(vk::Offset2D(0,0), newSurfaceExtent)
								}.data(),
							},

							// rasterization
							&(const vk::PipelineRasterizationStateCreateInfo&)vk::PipelineRasterizationStateCreateInfo{  // pRasterizationState
								vk::PipelineRasterizationStateCreateFlags(),  // flags
								VK_FALSE,  // depthClampEnable
								VK_FALSE,  // rasterizerDiscardEnable
								vk::PolygonMode::eFill,  // polygonMode
								vk::CullModeFlagBits::eNone,  // cullMode
								vk::FrontFace::eCounterClockwise,  // frontFace
								VK_FALSE,  // depthBiasEnable
								0.f,  // depthBiasConstantFactor
								0.f,  // depthBiasClamp
								0.f,  // depthBiasSlopeFactor
								1.f   // lineWidth
							},

							// multisampling
							&(const vk::PipelineMultisampleStateCreateInfo&)vk::PipelineMultisampleStateCreateInfo{  // pMultisampleState
								vk::PipelineMultisampleStateCreateFlags(),  // flags
								vk::SampleCountFlagBits::e1,  // rasterizationSamples
								VK_FALSE,  // sampleShadingEnable
								0.f,       // minSampleShading
								nullptr,   // pSampleMask
								VK_FALSE,  // alphaToCoverageEnable
								VK_FALSE   // alphaToOneEnable
							},

							// depth and stencil
							nullptr,  // pDepthStencilState

							// blending
							&(const vk::PipelineColorBlendStateCreateInfo&)vk::PipelineColorBlendStateCreateInfo{  // pColorBlendState
								vk::PipelineColorBlendStateCreateFlags(),  // flags
								VK_FALSE,  // logicOpEnable
								vk::LogicOp::eClear,  // logicOp
								1,  // attachmentCount
								array{  // pAttachments
									vk::PipelineColorBlendAttachmentState{
										VK_FALSE,  // blendEnable
										vk::BlendFactor::eZero,  // srcColorBlendFactor
										vk::BlendFactor::eZero,  // dstColorBlendFactor
										vk::BlendOp::eAdd,       // colorBlendOp
										vk::BlendFactor::eZero,  // srcAlphaBlendFactor
										vk::BlendFactor::eZero,  // dstAlphaBlendFactor
										vk::BlendOp::eAdd,       // alphaBlendOp
										vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
											vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA  // colorWriteMask
									},
								}.data(),
								array<float,4>{0.f,0.f,0.f,0.f}  // blendConstants
							},

							nullptr,  // pDynamicState
							pipelineLayout.get(),  // layout
							renderPass.get(),  // renderPass
							0,  // subpass
							vk::Pipeline(nullptr),  // basePipelineHandle
							-1 // basePipelineIndex
						)
					).value;

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
		renderingFinishedSemaphore =
			device->createSemaphoreUnique(
				vk::SemaphoreCreateInfo(
					vk::SemaphoreCreateFlags()  // flags
				)
			);

		// create shader modules
		vsModule =
			device->createShaderModuleUnique(
				vk::ShaderModuleCreateInfo(
					vk::ShaderModuleCreateFlags(),  // flags
					sizeof(vsSpirv),  // codeSize
					vsSpirv  // pCode
				)
			);
		fsModule =
			device->createShaderModuleUnique(
				vk::ShaderModuleCreateInfo(
					vk::ShaderModuleCreateFlags(),  // flags
					sizeof(fsSpirv),  // codeSize
					fsSpirv  // pCode
				)
			);

		// pipeline layout
		pipelineLayout =
			device->createPipelineLayoutUnique(
				vk::PipelineLayoutCreateInfo{
					vk::PipelineLayoutCreateFlags(),  // flags
					0,       // setLayoutCount
					nullptr, // pSetLayouts
					0,       // pushConstantRangeCount
					nullptr  // pPushConstantRanges
				}
			);

		window.setFrameCallback(
			[]() {

				// wait for previous frame
				// if still not finished
				//uint64_t waitStartTime = ts.getGpuTimestamp();
				uint64_t waitStartTime = tsg.getCpuTimestamp();
#if 0
				graphicsQueue.waitIdle();
#else
				if(frameID != 0)
					if(device->waitForPresentKHR(swapchain.get(), frameID, uint64_t(3e9), vkFuncs) == vk::Result::eTimeout)
						throw runtime_error("Vulkan error: vkWaitForPresentKHR timed out.");
#endif
				uint64_t waitStopTime = tsg.getCpuTimestamp();
				uint64_t frameFinishTS = tsg.getGpuTimestamp();
				if(frameStartTimes.size() < 10)
					frameStartTimes.push_back(frameFinishTS);
				graphicsQueue.waitIdle();

				// increment frame counter
				frameID++;

				// measure FPS
				fpsNumFrames++;
				if(frameID == 1) {
					fpsStartTime = chrono::high_resolution_clock::now();
					frameStartTS = frameFinishTS;
				}
				else {
					array<uint64_t, 2> renderingTS;
					tsg.readGpuTimestamps(2, renderingTS.data());
					frameTimes.push_back(frameFinishTS-frameStartTS);
					frameRenderingTimes.push_back(renderingTS[1]-renderingTS[0]);
					presentBlockingTimes.push_back(presentFinishedTime > renderingTS[1] ? presentFinishedTime-renderingTS[1] : 0);
					presentationLagTimes.push_back(frameFinishTS-renderingTS[1]);
					//waitTimes.push_back(frameFinishTS-waitStartTime);
					waitTimes.push_back(waitStopTime-waitStartTime);
					frameStartTS = frameFinishTS;

					auto t = chrono::high_resolution_clock::now();
					auto dt = t - fpsStartTime;
					if(dt >= chrono::seconds(2)) {
						cout << "FPS: " << fpsNumFrames/chrono::duration<double>(dt).count() << endl;
						fpsNumFrames = 0;
						fpsStartTime = t;

						float avgValue = std::accumulate(frameTimes.begin(), frameTimes.end(), uint64_t(0)) /
							frameTimes.size() * gpuTimestampPeriod * 1e-6f;
						float minValue = *std::min_element(frameTimes.begin(), frameTimes.end()) * gpuTimestampPeriod * 1e-6f;
						float maxValue = *std::max_element(frameTimes.begin(), frameTimes.end()) * gpuTimestampPeriod * 1e-6f;
						cout << "FrameTimes - avg: " << avgValue << "ms "
						        "(min: " << minValue << "ms, max: " << maxValue << "ms)" << endl;
						avgValue = std::accumulate(frameRenderingTimes.begin(), frameRenderingTimes.end(), uint64_t(0)) /
							(frameRenderingTimes.size() * gpuTimestampPeriod * 1e3f);
						minValue = *std::min_element(frameRenderingTimes.begin(), frameRenderingTimes.end()) * gpuTimestampPeriod * 1e-3f;
						maxValue = *std::max_element(frameRenderingTimes.begin(), frameRenderingTimes.end()) * gpuTimestampPeriod * 1e-3f;
						cout << "FrameRenderingTimes - avg: " << avgValue << "us "
						        "(min: " << minValue << "us, max: " << maxValue << "us)" << endl;
						avgValue = std::accumulate(presentationLagTimes.begin(), presentationLagTimes.end(), uint64_t(0)) /
							(presentationLagTimes.size() * gpuTimestampPeriod * 1e6f);
						minValue = *std::min_element(presentationLagTimes.begin(), presentationLagTimes.end()) * gpuTimestampPeriod * 1e-6f;
						maxValue = *std::max_element(presentationLagTimes.begin(), presentationLagTimes.end()) * gpuTimestampPeriod * 1e-6f;
						cout << "frameWaitingTimes - avg: " << avgValue << "ms "
						        "(min: " << minValue << "ms, max: " << maxValue << "ms)" << endl;
						if(frameID-1==frameTimes.size()) {
							cout << "FrameTimes: ";
							for(size_t i=0; i<10; i++)
								cout << frameTimes[i] * gpuTimestampPeriod * 1e-6f << ", ";
							cout << endl;
							cout << "PresentBlockingTimes: ";
							for(size_t i=0; i<10; i++)
								cout << presentBlockingTimes[i] * gpuTimestampPeriod * 1e-6f << ", ";
							cout << endl;
							cout << "PresentationLagTimes: ";
							for(size_t i=0; i<10; i++)
								cout << presentationLagTimes[i] * gpuTimestampPeriod * 1e-6f << ", ";
							cout << endl;
							cout << "WaitTimes: ";
							for(size_t i=0; i<10; i++)
								//cout << waitTimes[i] * gpuTimestampPeriod * 1e-6f << ", ";
								cout << waitTimes[i] * float(tsg.getCpuTimestampPeriod()) * 1e3f << ", ";
							cout << endl;
							cout << "FrameStartTimes: ";
							for(size_t i=1; i<10; i++)
								cout << (frameStartTimes[i]-frameStartTimes[0]) * gpuTimestampPeriod * 1e-6f << ", ";
							cout << endl;
						}
						frameTimes.clear();
						frameRenderingTimes.clear();
						presentBlockingTimes.clear();
						presentationLagTimes.clear();
						waitTimes.clear();
					}
				}

				// acquire image
				uint32_t imageIndex;
				vk::Result r =
					device->acquireNextImageKHR(
						swapchain.get(),                // swapchain
						uint64_t(3e9),                  // timeout (3s)
						imageAvailableSemaphore.get(),  // semaphore to signal
						vk::Fence(nullptr),             // fence to signal
						&imageIndex                     // pImageIndex
					);
				if(r != vk::Result::eSuccess) {
					if(r == vk::Result::eSuboptimalKHR) {
						window.scheduleSwapchainResize();
						cout << "acquire result: Suboptimal" << endl;
						return;
					} else if(r == vk::Result::eErrorOutOfDateKHR) {
						window.scheduleSwapchainResize();
						cout << "acquire error: OutOfDate" << endl;
						return;
					} else
						throw runtime_error("Vulkan error: vkAcquireNextImageKHR failed with error " + to_string(r) + ".");
				}

				// record command buffer
				commandBuffer.begin(
					vk::CommandBufferBeginInfo(
						vk::CommandBufferUsageFlagBits::eOneTimeSubmit,  // flags
						nullptr  // pInheritanceInfo
					)
				);
				tsg.writeGpuTimestamp(commandBuffer, vk::PipelineStageFlagBits::eTopOfPipe, 0);
				commandBuffer.beginRenderPass(
					vk::RenderPassBeginInfo(
						renderPass.get(),  // renderPass
						framebuffers[imageIndex].get(),  // framebuffer
						vk::Rect2D(vk::Offset2D(0, 0), window.surfaceExtent()),  // renderArea
						1,  // clearValueCount
						&(const vk::ClearValue&)vk::ClearValue(  // pClearValues
							vk::ClearColorValue(array<float, 4>{0.0f, 0.0f, 0.0f, 1.f})
						)
					),
					vk::SubpassContents::eInline
				);

				// rendering commands
				commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.get());  // bind pipeline
				commandBuffer.draw(  // draw single triangle
					3,  // vertexCount
					1,  // instanceCount
					0,  // firstVertex
					uint32_t(frameID)  // firstInstance
				);

				// end render pass and command buffer
				commandBuffer.endRenderPass();
				tsg.writeGpuTimestamp(commandBuffer, vk::PipelineStageFlagBits::eBottomOfPipe, 1);
				commandBuffer.end();

				// submit frame
				graphicsQueue.submit(
					vk::ArrayProxy<const vk::SubmitInfo>(
						1,
						&(const vk::SubmitInfo&)vk::SubmitInfo(
							1, &imageAvailableSemaphore.get(),  // waitSemaphoreCount + pWaitSemaphores +
							&(const vk::PipelineStageFlags&)vk::PipelineStageFlags(  // pWaitDstStageMask
								vk::PipelineStageFlagBits::eColorAttachmentOutput),
							1, &commandBuffer,  // commandBufferCount + pCommandBuffers
							1, &renderingFinishedSemaphore.get()  // signalSemaphoreCount + pSignalSemaphores
						)
					),
					nullptr  // fence
				);

				// present
				r =
					presentationQueue.presentKHR(
						vk::StructureChain<vk::PresentInfoKHR, vk::PresentIdKHR>{
							{
								1, &renderingFinishedSemaphore.get(),  // waitSemaphoreCount + pWaitSemaphores
								1, &swapchain.get(), &imageIndex,  // swapchainCount + pSwapchains + pImageIndices
								nullptr  // pResults
							},
							{
								1,  // swapchainCount
								&frameID  // pPresentIds
							}
						}.get()
#if 0
						&(const vk::PresentInfoKHR&)vk::PresentInfoKHR(
							1, &renderingFinishedSemaphore.get(),  // waitSemaphoreCount + pWaitSemaphores
							1, &swapchain.get(), &imageIndex,  // swapchainCount + pSwapchains + pImageIndices
							nullptr  // pResults
						).setPNext(
//							(false)
//							? static_cast<vk::PresentIdKHR*>(nullptr)
							&vk::PresentIdKHR(
								1,  // swapchainCount
								&framePresentID  // pPresentIds
							)
						)
#endif
					);
				if(r != vk::Result::eSuccess) {
					if(r == vk::Result::eSuboptimalKHR) {
						window.scheduleSwapchainResize();
						cout << "present result: Suboptimal" << endl;
					} else if(r == vk::Result::eErrorOutOfDateKHR) {
						window.scheduleSwapchainResize();
						cout << "present error: OutOfDate" << endl;
					} else
						throw runtime_error("Vulkan error: vkQueuePresentKHR() failed with error " + to_string(r) + ".");
				}
				presentFinishedTime = tsg.getGpuTimestamp();

				// schedule next frame
				if(frameUpdateMode != FrameUpdateMode::OnDemand)
					window.scheduleFrame();

			},
			physicalDevice,
			device.get()
		);

		// run main loop
		window.mainLoop();

	// catch exceptions
	} catch(vk::Error& e) {
		cout << "Failed because of Vulkan exception: " << e.what() << endl;
	} catch(exception& e) {
		cout << "Failed because of exception: " << e.what() << endl;
	} catch(...) {
		cout << "Failed because of unspecified exception." << endl;
	}

	// wait for device idle state
	// (to prevent errors during destruction of Vulkan resources)
	try {
		if(device)
			device->waitIdle();
	} catch(vk::Error& e) {
		cout << "Failed because of Vulkan exception: " << e.what() << endl;
	}

	return 0;
}
