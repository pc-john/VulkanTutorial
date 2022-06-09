#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.hpp>
#include <iostream>
#include "xdg-shell-client-protocol.h"

using namespace std;

// vk::Instance
// (we destroy it as the last one)
static vk::UniqueInstance instance;

// deleters
struct DisplayDeleter { void operator()(wl_display* d) { wl_display_disconnect(d); } };
struct RegistryDeleter { void operator()(wl_registry* r) { wl_registry_destroy(r); } };
struct CompositorDeleter { void operator()(wl_compositor* c) { wl_compositor_destroy(c); } };
struct XdgWmBaseDeleter { void operator()(xdg_wm_base* b) { xdg_wm_base_destroy(b); } };
struct WlSurfaceDeleter { void operator()(wl_surface* s) { wl_surface_destroy(s); } };
struct XdgSurfaceDeleter { void operator()(xdg_surface* s) { xdg_surface_destroy(s); } };
struct XdgToplevelDeleter { void operator()(xdg_toplevel* t) { xdg_toplevel_destroy(t); } };

// globals
static unique_ptr<wl_display, DisplayDeleter> display;
static unique_ptr<wl_registry, RegistryDeleter> registry;
static unique_ptr<wl_compositor, CompositorDeleter> compositor;
static unique_ptr<xdg_wm_base, XdgWmBaseDeleter> xdgWmBase;

// objects
static unique_ptr<wl_surface, WlSurfaceDeleter> wlSurface;
static unique_ptr<xdg_surface, XdgSurfaceDeleter> xdgSurface;
static unique_ptr<xdg_toplevel, XdgToplevelDeleter> xdgToplevel;

// state
static bool running = true;

// listeners
static wl_registry_listener registryListener;
static xdg_wm_base_listener xdgWmBaseListener;
static xdg_surface_listener xdgSurfaceListener;
static xdg_toplevel_listener xdgToplevelListener;

// Vulkan window surface
static vk::UniqueSurfaceKHR surface;


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
		display.reset(wl_display_connect(nullptr));
		if(display == nullptr)
			throw runtime_error("Cannot connect to Wayland display. No Wayland server is running or invalid WAYLAND_DISPLAY variable.");

		// registry listener
		registry.reset(wl_display_get_registry(display.get()));
		if(registry == nullptr)
			throw runtime_error("Cannot get Wayland registry object.");
		compositor = nullptr;
		registryListener = {
			.global =
				[](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
					cout << "   " << interface << endl;
					if(strcmp(interface, wl_compositor_interface.name) == 0)
						compositor.reset(
							static_cast<wl_compositor*>(
								wl_registry_bind(registry, name, &wl_compositor_interface, 1)));
					else if(strcmp(interface, xdg_wm_base_interface.name) == 0)
						xdgWmBase.reset(
							static_cast<xdg_wm_base*>(
								wl_registry_bind(registry, name, &xdg_wm_base_interface, 1)));
				},
			.global_remove =
				[](void*, wl_registry*, uint32_t) {
				},
		};
		cout << "Wayland global registry objects:" << endl;
		if(wl_registry_add_listener(registry.get(), &registryListener, nullptr))
			throw runtime_error("wl_registry_add_listener() failed.");

		// get and init global objects
		if(wl_display_roundtrip(display.get()) == -1)
			throw runtime_error("wl_display_roundtrip() failed.");
		if(compositor == nullptr)
			throw runtime_error("Cannot get Wayland compositor object.");
		if(xdgWmBase == nullptr)
			throw runtime_error("Cannot get Wayland xdg_wm_base object.");
		xdgWmBaseListener = {
			.ping =
				[](void*, xdg_wm_base* xdgWmBase, uint32_t serial) {
					xdg_wm_base_pong(xdgWmBase, serial);
				}
		};
		if(xdg_wm_base_add_listener(xdgWmBase.get(), &xdgWmBaseListener, nullptr))
			throw runtime_error("xdg_wm_base_add_listener() failed.");

		// create Wayland surface
		wlSurface.reset(wl_compositor_create_surface(compositor.get()));
		if(wlSurface == nullptr)
			throw runtime_error("wl_compositor_create_surface() failed.");
		xdgSurface.reset(xdg_wm_base_get_xdg_surface(xdgWmBase.get(), wlSurface.get()));
		if(xdgSurface == nullptr)
			throw runtime_error("xdg_wm_base_get_xdg_surface() failed.");
		xdgSurfaceListener = {
			.configure =
				[](void* data, xdg_surface* xdgSurface, uint32_t serial) {
					cout << "surface configure" << endl;
					xdg_surface_ack_configure(xdgSurface, serial);
					wl_surface_commit(wlSurface.get());
				},
		};
		if(xdg_surface_add_listener(xdgSurface.get(), &xdgSurfaceListener, nullptr))
			throw runtime_error("xdg_surface_add_listener() failed.");

		// init xdg toplevel
		xdgToplevel.reset(xdg_surface_get_toplevel(xdgSurface.get()));
		if(xdgToplevel == nullptr)
			throw runtime_error("xdg_surface_get_toplevel() failed.");
		xdg_toplevel_set_title(xdgToplevel.get(), "Hello window!");
		xdgToplevelListener = {
			.configure =
				[](void* data, xdg_toplevel* toplevel, int32_t width, int32_t height, wl_array*) -> void {
					cout << "toplevel configure (width=" << width << ", height=" << height << ")" << endl;
				},
			.close =
				[](void* data, xdg_toplevel* xdgToplevel) {
					running = false;
				},
		};
		if(xdg_toplevel_add_listener(xdgToplevel.get(), &xdgToplevelListener, nullptr))
			throw runtime_error("xdg_toplevel_add_listener() failed.");
		wl_surface_commit(wlSurface.get());
		if(wl_display_flush(display.get()) == -1)
			throw runtime_error("wl_display_flush() failed.");

		// create surface
		surface =
			instance->createWaylandSurfaceKHRUnique(
				vk::WaylandSurfaceCreateInfoKHR(
					vk::WaylandSurfaceCreateFlagsKHR(),  // flags
					display.get(),  // display
					wlSurface.get()  // surface
				)
			);

		// find compatible devices
		vector<vk::PhysicalDevice> deviceList = instance->enumeratePhysicalDevices();
		vector<string> compatibleDevices;
		for(vk::PhysicalDevice pd : deviceList) {
			uint32_t numQueues;
			pd.getQueueFamilyProperties(&numQueues, nullptr);
			for(uint32_t i=0; i<numQueues; i++)
				if(pd.getSurfaceSupportKHR(i, surface.get())) {
					compatibleDevices.push_back(pd.getProperties().deviceName);
					break;
				}
		}
		cout << "Compatible devices:" << endl;
		for(string& name : compatibleDevices)
			cout << "   " << name << endl;

		// run event loop
		cout << "Entering main loop." << endl;
		if(wl_display_flush(display.get()) == -1)
			throw runtime_error("wl_display_flush() failed.");
		while(running) {
			if(wl_display_dispatch(display.get()) == -1)
				throw runtime_error("wl_display_dispatch() failed.");
			if(wl_display_flush(display.get()) == -1)
				throw runtime_error("wl_display_flush() failed.");
		}
		cout << "Main loop left." << endl;

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
