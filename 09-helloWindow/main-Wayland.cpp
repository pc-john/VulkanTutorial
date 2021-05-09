#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.hpp>
#include <iostream>

using namespace std;

static wl_display* wlDisplay = nullptr;
static wl_registry* wlRegistry = nullptr;
static wl_compositor* wlCompositor = nullptr;
static wl_shell* wlShell = nullptr;
static wl_surface* wlSurface = nullptr;


static void registryHandleGlobal(void*, wl_registry* registry, uint32_t id, const char* interface, uint32_t)
{
}


static void registryHandleGlobalRemove(void*, wl_registry*, uint32_t)
{
}


int main(int,char**)
{
	// catch exceptions
	// (vulkan.hpp functions throws if they fail)
	try {

		// Vulkan instance
		/*vk::UniqueInstance instance(
			vk::createInstanceUnique(
				vk::InstanceCreateInfo{
					vk::InstanceCreateFlags(),  // flags
					&(const vk::ApplicationInfo&)vk::ApplicationInfo{
						"09-helloWindow-Wayland",  // application name
						VK_MAKE_VERSION(0,0,0),  // application version
						nullptr,                 // engine name
						VK_MAKE_VERSION(0,0,0),  // engine version
						VK_API_VERSION_1_0,      // api version
					},
					0,nullptr,  // no layers
					2,          // enabled extension count
					array<const char*,2>{"VK_KHR_surface","VK_KHR_wayland_surface"}.data(),  // enabled extension names
				}));*/

		// open Wayland connection
		wlDisplay = wl_display_connect(nullptr);
		if(wlDisplay==nullptr)
			throw runtime_error("Cannot connect to Wayland display. No Wayland server is running or invalid WAYLAND_DISPLAY variable.");

		// initialize global objects
		wlRegistry = wl_display_get_registry(wlDisplay);
		wl_registry_add_listener(
			wlRegistry,
			array<wl_registry_listener,2>{ registryHandleGlobal, registryHandleGlobalRemove }.data(),
			nullptr);
		wl_display_roundtrip(wlDisplay);
		if(wlCompositor==nullptr)
			throw runtime_error("Cannot get Wayland compositor object.");
		if(wlShell==nullptr)
			throw runtime_error("Cannot get Wayland shell object.");

		// create window

		// create surface
		/*vk::UniqueSurfaceKHR surface=
			instance->createWaylandSurfaceKHRUnique(
				vk::WaylandSurfaceCreateInfoKHR(vk::WaylandSurfaceCreateFlagsKHR(), wlDisplay, wlSurface)
			);*/

		// get VisualID

		// find compatible devices
		// (On Windows, all graphics adapters capable of monitor output are usually compatible devices.
		// On Linux X11 platform, only one graphics adapter is compatible device (the one that
		// renders the window).
		/*vector<vk::PhysicalDevice> deviceList=instance->enumeratePhysicalDevices();
		vector<string> compatibleDevices;
		for(vk::PhysicalDevice pd:deviceList) {
			uint32_t c;
			pd.getQueueFamilyProperties(&c,nullptr,vk::DispatchLoaderStatic());
			for(uint32_t i=0; i<c; i++)
				if(pd.getWaylandPresentationSupportKHR(i, wlDisplay, wlSurface)) {
					compatibleDevices.push_back(pd.getProperties().deviceName);
					break;
				}
		}
		cout<<"Compatible devices:"<<endl;
		for(string& name:compatibleDevices)
			cout<<"   "<<name<<endl;*/

		// run event loop
		while(true) {
		}

	// catch exceptions
	} catch(vk::Error &e) {
		cout<<"Failed because of Vulkan exception: "<<e.what()<<endl;
	} catch(exception &e) {
		cout<<"Failed because of exception: "<<e.what()<<endl;
	} catch(...) {
		cout<<"Failed because of unspecified exception."<<endl;
	}

	// clean up
	if(wlSurface)
		wl_surface_destroy(wlSurface);
	if(wlDisplay)
		wl_display_disconnect(wlDisplay);

	return 0;
}
