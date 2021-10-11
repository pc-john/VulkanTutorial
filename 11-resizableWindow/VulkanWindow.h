#pragma once

#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
# include "xdg-shell-client-protocol.h"
# include "xdg-decoration-client-protocol.h"
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
#endif
# include <vulkan/vulkan.hpp>

namespace vk {
	class Instance;
}



class VulkanWindow {
public:

	typedef void ExposeCallback();
	typedef void RecreateSwapchainCallback(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, vk::Extent2D newSurfaceExtent);

protected:

#if defined(VK_USE_PLATFORM_WAYLAND_KHR)

	// globals
	wl_display* display = nullptr;
	wl_registry* registry;
	wl_compositor* compositor;
	xdg_wm_base* xdg = nullptr;
	zxdg_decoration_manager_v1* zxdgDecorationManagerV1;

	// objects
	wl_surface* wlSurface = nullptr;
	xdg_surface* xdgSurface = nullptr;
	xdg_toplevel* xdgTopLevel = nullptr;

	// state
	bool running;

	// listeners
	wl_registry_listener registryListener;
	xdg_wm_base_listener xdgWmBaseListener;
	xdg_surface_listener xdgSurfaceListener;
	xdg_toplevel_listener xdgToplevelListener;

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

	HWND hwnd = nullptr;

#elif defined(VK_USE_PLATFORM_XLIB_KHR)

	Display* display = nullptr;
	Window window = 0;
	Atom wmDeleteMessage;

#endif

	vk::Extent2D m_surfaceExtent = vk::Extent2D(0,0);
	bool swapchainResizePending = true;
	RecreateSwapchainCallback *recreateSwapchainCallback = nullptr;

public:

	VulkanWindow() = default;
	~VulkanWindow();

	[[nodiscard]] vk::SurfaceKHR init(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title = "Vulkan window");
	vk::UniqueSurfaceKHR initUnique(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title = "Vulkan window");
	void setRecreateSwapchainCallback(RecreateSwapchainCallback cb);
	void mainLoop(vk::PhysicalDevice physicalDevice, vk::Device device, vk::SurfaceKHR surface, ExposeCallback cb);

	vk::Extent2D surfaceExtent() const;

	void scheduleSwapchainResize();

};


// inline methods
inline vk::UniqueSurfaceKHR VulkanWindow::initUnique(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title)  { return vk::UniqueSurfaceKHR(init(instance, surfaceExtent, title), {instance}); }
inline void VulkanWindow::setRecreateSwapchainCallback(RecreateSwapchainCallback cb)  { recreateSwapchainCallback = cb; }
inline vk::Extent2D VulkanWindow::surfaceExtent() const  { return m_surfaceExtent; }
inline void VulkanWindow::scheduleSwapchainResize()  { swapchainResizePending = true; }
