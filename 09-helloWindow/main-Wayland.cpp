#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.hpp>
#include <iostream>
#include "xdg-shell-client-protocol.h"

using namespace std;

// vk::Instance
// (we destroy it as the last one)
static vk::UniqueInstance instance;

static wl_display* display = nullptr;
static wl_registry* registry = nullptr;
static wl_compositor* compositor = nullptr;
static xdg_wm_base* xdg = nullptr;
static wl_surface* wlSurface = nullptr;
static xdg_surface* xdgSurface = nullptr;
static xdg_toplevel* xdgTopLevel = nullptr;
static bool xdgSurfaceConfigured = false;
static bool running = true;


static void registryHandleGlobal(void*, wl_registry* registry, uint32_t name,
                                 const char* interface, uint32_t version)
{
	if(strcmp(interface, wl_compositor_interface.name) == 0)
		compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 1));
	else if(strcmp(interface, xdg_wm_base_interface.name) == 0)
		xdg = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
}


static void registryRemoveGlobal(void*, wl_registry*, uint32_t)
{
}


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
						"09-helloWindow-Wayland",  // application name
						VK_MAKE_VERSION(0,0,0),  // application version
						nullptr,                 // engine name
						VK_MAKE_VERSION(0,0,0),  // engine version
						VK_API_VERSION_1_0,      // api version
					},
					0, nullptr,  // no layers
					2,           // enabled extension count
					array<const char*, 2>{  // enabled extension names
						"VK_KHR_surface",
						"VK_KHR_wayland_surface",
					}.data(),
				});

		// open Wayland connection
		display = wl_display_connect(nullptr);
		if(display == nullptr)
			throw runtime_error("Cannot connect to Wayland display. No Wayland server is running or invalid WAYLAND_DISPLAY variable.");

		// initialize global objects
		registry = wl_display_get_registry(display);
		wl_registry_add_listener(
			registry,
			&(const wl_registry_listener&)wl_registry_listener{
				.global = registryHandleGlobal,
				.global_remove = registryRemoveGlobal,
			},
			nullptr
		);
		wl_display_roundtrip(display);
		if(compositor == nullptr)
			throw runtime_error("Cannot get Wayland compositor object.");
		if(xdg == nullptr)
			throw runtime_error("Cannot get Wayland xdg_wm_base object.");

		xdg_wm_base_add_listener(
			xdg,
			&(const xdg_wm_base_listener&)xdg_wm_base_listener{
				.ping =
					[](void*, xdg_wm_base* xdg, uint32_t serial) {
						xdg_wm_base_pong(xdg, serial);
					}
			},
			nullptr
		);

		// create window
		wlSurface = wl_compositor_create_surface(compositor);
		xdgSurface = xdg_wm_base_get_xdg_surface(xdg, wlSurface);

		xdg_surface_add_listener(
			xdgSurface,
			&(const xdg_surface_listener&)xdg_surface_listener{
				.configure =
					[](void* data, xdg_surface* xdgSurface, uint32_t serial) {
						xdg_surface_ack_configure(xdgSurface, serial);
						wl_surface_commit(wlSurface);
						xdgSurfaceConfigured = true;
						cout<<"surface configure"<<endl;
					},
			},
			nullptr
		);
		wl_region* region = wl_compositor_create_region(compositor);
		wl_region_add(region, 0, 0, 128, 128);
		wl_surface_set_opaque_region(wlSurface, region);
		wl_region_destroy(region);
		wl_surface_commit(wlSurface);
		wl_display_flush(display);
		wl_surface_commit(wlSurface);
		wl_display_flush(display);

		xdgTopLevel = xdg_surface_get_toplevel(xdgSurface);
		cout<<"TopLevel: "<<xdgTopLevel<<endl;
		xdg_toplevel_set_title(xdgTopLevel, "Example");
		xdg_toplevel_add_listener(
			xdgTopLevel,
			&(const xdg_toplevel_listener&)xdg_toplevel_listener{
				.configure = [](void*, xdg_toplevel* toplevel, int32_t w, int32_t h, wl_array*)->void{ cout<<"toplevel "<<toplevel<<" configure "<<w<<"x"<<h<<endl; },
				.close =
					[](void* data, xdg_toplevel* xdgTopLevel) {
						running = false;
					},
			},
			nullptr
		);
		cout<<"init done"<<endl;
		wl_surface_commit(wlSurface);
		wl_display_roundtrip(display);
		//wl_surface_frame(wlSurface);
		wl_display_flush(display);
		while(!xdgSurfaceConfigured) {
			wl_display_flush(display);
			wl_display_dispatch(display);
		}
		cout<<"configured"<<endl;


		// create surface
		/*vk::UniqueSurfaceKHR surface=
			instance->createWaylandSurfaceKHRUnique(
				vk::WaylandSurfaceCreateInfoKHR(vk::WaylandSurfaceCreateFlagsKHR(), display, wlSurface)
			);
*/
		// get VisualID

		// find compatible devices
		// (On Windows, all graphics adapters capable of monitor output are usually compatible devices.
		// On Linux when using Xlib, only one graphics adapter is compatible device (the one that
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
		while(wl_display_dispatch(display) != -1 && running) {
			wl_display_flush(display);
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
	if(xdgTopLevel)
		xdg_toplevel_destroy(xdgTopLevel);
	if(wlSurface)
		wl_surface_destroy(wlSurface);
	if(xdg)
		xdg_wm_base_destroy(xdg);
	if(compositor)
		wl_compositor_destroy(compositor);
	if(registry)
		wl_registry_destroy(registry);
	if(display)
		wl_display_disconnect(display);

	return 0;
}
