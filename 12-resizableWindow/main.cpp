#include "VulkanWindow.h"
#include <algorithm>
#include <iostream>

using namespace std;


// constants
constexpr const char* appName = "12-resizableWindow";


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


int main(int, char**)
{
	// catch exceptions
	// (vulkan.hpp functions throw if they fail)
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
		vector<tuple<vk::PhysicalDevice, uint32_t, uint32_t, vk::PhysicalDeviceProperties>> compatibleDevices;
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
						compatibleDevices.emplace_back(pd, i, i, pd.getProperties());
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
				compatibleDevices.emplace_back(pd, graphicsQueueFamily, presentationQueueFamily, pd.getProperties());
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

		// create device
		device =
			physicalDevice.createDeviceUnique(
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
					1,           // number of enabled extensions
					array<const char*, 1>{ "VK_KHR_swapchain" }.data(),  // enabled extension names
					nullptr,    // enabled features
				}
			);

		// get queues
		graphicsQueue = device->getQueue(graphicsQueueFamily, 0);
		presentationQueue = device->getQueue(presentationQueueFamily, 0);

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

				// wait for previous frame rendering work
				// if still not finished
				graphicsQueue.waitIdle();

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
					0   // firstInstance
				);

				// end render pass and command buffer
				commandBuffer.endRenderPass();
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
					vk::Fence(nullptr)
				);

				// present
				r =
					presentationQueue.presentKHR(
						&(const vk::PresentInfoKHR&)vk::PresentInfoKHR(
							1, &renderingFinishedSemaphore.get(),  // waitSemaphoreCount + pWaitSemaphores
							1, &swapchain.get(), &imageIndex,  // swapchainCount + pSwapchains + pImageIndices
							nullptr  // pResults
						)
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
