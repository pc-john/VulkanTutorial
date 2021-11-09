#pragma once

#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
# include "xdg-shell-client-protocol.h"
# include "xdg-decoration-client-protocol.h"
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
# include <exception>
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
	wl_display* m_display = nullptr;
	wl_registry* m_registry;
	wl_compositor* m_compositor;
	xdg_wm_base* m_xdg = nullptr;
	zxdg_decoration_manager_v1* m_zxdgDecorationManagerV1;

	// objects
	wl_surface* m_wlSurface = nullptr;
	xdg_surface* m_xdgSurface = nullptr;
	xdg_toplevel* m_xdgTopLevel = nullptr;

	// state
	bool m_running;

	// listeners
	wl_registry_listener m_registryListener;
	xdg_wm_base_listener m_xdgWmBaseListener;
	xdg_surface_listener m_xdgSurfaceListener;
	xdg_toplevel_listener m_xdgToplevelListener;

#elif defined(VK_USE_PLATFORM_XLIB_KHR)

	Display* m_display = nullptr;
	Window m_window = 0;
	Atom m_wmDeleteMessage;
	bool m_visible = false;
	bool m_exposePending = true;

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

	HWND m_hwnd = nullptr;
	std::exception_ptr m_wndProcException;
	vk::PhysicalDevice m_physicalDevice;
	vk::Device m_device;
	vk::SurfaceKHR m_surface;
	ExposeCallback* m_exposeCallback;
	void onWmPaint();

#endif

	vk::Extent2D m_surfaceExtent = vk::Extent2D(0,0);
	bool m_swapchainResizePending = true;
	RecreateSwapchainCallback* m_recreateSwapchainCallback = nullptr;

public:

	VulkanWindow() = default;
	~VulkanWindow();

	[[nodiscard]] vk::SurfaceKHR init(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title = "Vulkan window");
	vk::UniqueSurfaceKHR initUnique(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title = "Vulkan window");
	void setRecreateSwapchainCallback(RecreateSwapchainCallback cb);
	void mainLoop(vk::PhysicalDevice physicalDevice, vk::Device device, vk::SurfaceKHR surface, ExposeCallback cb);

	vk::Extent2D surfaceExtent() const;

	void scheduleNextFrame();
	void scheduleSwapchainResize();

};


// inline methods
inline vk::UniqueSurfaceKHR VulkanWindow::initUnique(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title)  { return vk::UniqueSurfaceKHR(init(instance, surfaceExtent, title), {instance}); }
inline void VulkanWindow::setRecreateSwapchainCallback(RecreateSwapchainCallback cb)  { m_recreateSwapchainCallback = cb; }
inline vk::Extent2D VulkanWindow::surfaceExtent() const  { return m_surfaceExtent; }
#if defined(VK_USE_PLATFORM_WIN32_KHR)
inline void VulkanWindow::scheduleSwapchainResize()  { m_swapchainResizePending = true; scheduleNextFrame(); }
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
inline void VulkanWindow::scheduleSwapchainResize()  { m_swapchainResizePending = true; m_exposePending = true; }
#else
inline void VulkanWindow::scheduleSwapchainResize()  { m_swapchainResizePending = true; }
#endif
