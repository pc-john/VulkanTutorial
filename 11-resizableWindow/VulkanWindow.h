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

	typedef void FrameCallback();
	typedef void RecreateSwapchainCallback(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, vk::Extent2D newSurfaceExtent);

protected:

#if defined(VK_USE_PLATFORM_WIN32_KHR)

	HWND m_hwnd = nullptr;
	std::exception_ptr m_wndProcException;
	vk::PhysicalDevice m_physicalDevice;
	vk::Device m_device;
	vk::SurfaceKHR m_surface;
	FrameCallback* m_frameCallback;
	void onWmPaint();

#elif defined(VK_USE_PLATFORM_XLIB_KHR)

	Display* m_display = nullptr;
	Window m_window = 0;
	Atom m_wmDeleteMessage;
	bool m_visible = false;
	bool m_framePending = true;

#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)

	// globals
	wl_display* m_display = nullptr;
	wl_registry* m_registry;
	wl_compositor* m_compositor;
	xdg_wm_base* m_xdgWmBase = nullptr;
	zxdg_decoration_manager_v1* m_zxdgDecorationManagerV1;

	// objects
	wl_surface* m_wlSurface = nullptr;
	xdg_surface* m_xdgSurface = nullptr;
	xdg_toplevel* m_xdgTopLevel = nullptr;
	zxdg_toplevel_decoration_v1* m_decoration = nullptr;

	// state
	bool m_running = true;
	bool m_forceFrame = true;
	wl_callback* m_scheduledFrameCallback = nullptr;

	// listeners
	wl_registry_listener m_registryListener;
	xdg_wm_base_listener m_xdgWmBaseListener;
	xdg_surface_listener m_xdgSurfaceListener;
	xdg_toplevel_listener m_xdgToplevelListener;
	wl_callback_listener m_frameListener;

	// mainLoop parameters
	vk::PhysicalDevice m_physicalDevice;
	vk::Device m_device;
	vk::SurfaceKHR m_surface;
	FrameCallback* m_frameCallback;
	void doFrame();

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
	void mainLoop(vk::PhysicalDevice physicalDevice, vk::Device device, vk::SurfaceKHR surface, FrameCallback cb);

	vk::Extent2D surfaceExtent() const;

	void scheduleNextFrame();
	void scheduleSwapchainResize();

	// Wayland prefers the use of mailbox present mode
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
	static constexpr const inline bool mailboxPresentModePreferred = true;
#else
	static constexpr const inline bool mailboxPresentModePreferred = false;
#endif
};


// inline methods
inline vk::UniqueSurfaceKHR VulkanWindow::initUnique(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title)  { return vk::UniqueSurfaceKHR(init(instance, surfaceExtent, title), {instance}); }
inline void VulkanWindow::setRecreateSwapchainCallback(RecreateSwapchainCallback cb)  { m_recreateSwapchainCallback = cb; }
inline vk::Extent2D VulkanWindow::surfaceExtent() const  { return m_surfaceExtent; }
#if defined(VK_USE_PLATFORM_WIN32_KHR)
inline void VulkanWindow::scheduleSwapchainResize()  { m_swapchainResizePending = true; scheduleNextFrame(); }
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
inline void VulkanWindow::scheduleSwapchainResize()  { m_swapchainResizePending = true; m_framePending = true; }
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
inline void VulkanWindow::scheduleSwapchainResize()  { m_swapchainResizePending = true; m_forceFrame = true; }
#endif
