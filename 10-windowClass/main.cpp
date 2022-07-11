#include "VulkanWindow.h"
#include <algorithm>
#include <iostream>

using namespace std;


// constants
constexpr const char* appName = "10-windowClass";


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

		// set window callbacks
		window.setRecreateSwapchainCallback(
			[](const vk::SurfaceCapabilitiesKHR&, vk::Extent2D){}
		);
		window.setFrameCallback(
			[](){},
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
