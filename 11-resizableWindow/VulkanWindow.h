#pragma once

#if defined(USE_PLATFORM_WIN32)
  #include <exception>
  typedef struct HWND__* HWND;
#elif defined(USE_PLATFORM_XLIB)
  typedef struct _XDisplay Display;
  #if 1 //sizeof(unsigned long) == 8, FIXME: test on some 32-bit system
    typedef unsigned long Window;  // Window is XID type (X11/X.h) that is defined as unsigned long or CARD32 (in X11/X.h based on _XSERVER64 define) while CARD32 is defined as unsigned int or unsigned long in X11/Xmd.h based on LONG64 define
    typedef unsigned long Atom;  // in X11/X.h and X11/Xdefs.h
  #else
    typedef unsigned int Window;
    typedef unsigned int Atom;
  #endif
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
  #include "xdg-shell-client-protocol.h"
  #include "xdg-decoration-client-protocol.h"
#endif
#include <vulkan/vulkan.hpp>



class VulkanWindow {
public:

	typedef void FrameCallback();
	typedef void RecreateSwapchainCallback(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, vk::Extent2D newSurfaceExtent);

protected:

#if defined(USE_PLATFORM_WIN32)

	HWND m_hwnd = nullptr;
	std::exception_ptr m_wndProcException;
	vk::PhysicalDevice m_physicalDevice;
	vk::Device m_device;
	vk::SurfaceKHR m_surface;
	FrameCallback* m_frameCallback;
	void onWmPaint();

#elif defined(USE_PLATFORM_XLIB)

	Display* m_display = nullptr;
	Window m_window = 0;
	Atom m_wmDeleteMessage;
	bool m_visible = false;
	bool m_framePending = true;

#elif defined(USE_PLATFORM_WAYLAND)

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

	static std::vector<const char*> s_requiredInstanceExtensions;

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

	// required Vulkan Instance extensions
	static std::vector<const char*>& requiredExtensions();
	static void appendRequiredExtensions(std::vector<const char*>& v);
	static uint32_t requiredExtensionCount();
	static const char* const* requiredExtensionNames();

	// Wayland prefers the use of mailbox present mode
#if defined(USE_PLATFORM_WAYLAND)
	static constexpr const inline bool mailboxPresentModePreferred = true;
#else
	static constexpr const inline bool mailboxPresentModePreferred = false;
#endif

};


// inline methods
inline vk::UniqueSurfaceKHR VulkanWindow::initUnique(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title)  { return vk::UniqueSurfaceKHR(init(instance, surfaceExtent, title), {instance}); }
inline void VulkanWindow::setRecreateSwapchainCallback(RecreateSwapchainCallback cb)  { m_recreateSwapchainCallback = cb; }
inline vk::Extent2D VulkanWindow::surfaceExtent() const  { return m_surfaceExtent; }
#if defined(USE_PLATFORM_WIN32)
inline void VulkanWindow::scheduleSwapchainResize()  { m_swapchainResizePending = true; scheduleNextFrame(); }
#elif defined(USE_PLATFORM_XLIB)
inline void VulkanWindow::scheduleSwapchainResize()  { m_swapchainResizePending = true; m_framePending = true; }
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
inline void VulkanWindow::scheduleSwapchainResize()  { m_swapchainResizePending = true; m_forceFrame = true; }
#endif
#if defined(USE_PLATFORM_WIN32) || defined(USE_PLATFORM_XLIB) || defined(VK_USE_PLATFORM_WAYLAND_KHR)
inline std::vector<const char*>& VulkanWindow::requiredExtensions()  { return s_requiredInstanceExtensions; }
inline void VulkanWindow::appendRequiredExtensions(std::vector<const char*>& v)  { v.emplace_back(s_requiredInstanceExtensions[0]); v.emplace_back(s_requiredInstanceExtensions[1]); }
inline uint32_t VulkanWindow::requiredExtensionCount()  { return 2; }
inline const char* const* VulkanWindow::requiredExtensionNames()  { return s_requiredInstanceExtensions.data(); }
#endif
