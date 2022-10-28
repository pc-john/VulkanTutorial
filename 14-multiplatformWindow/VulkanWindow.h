#pragma once

#if defined(USE_PLATFORM_WIN32)
  typedef struct HWND__* HWND;
  typedef struct HINSTANCE__* HINSTANCE;
  typedef unsigned short ATOM;
#elif defined(USE_PLATFORM_XLIB)
  typedef struct _XDisplay Display;
  typedef unsigned long Window;  // Window is XID type (X11/X.h) that is defined as unsigned long (or CARD32 but that does not apply to client applications; CARD32 might be used only when compiling X server sources)
  typedef unsigned long Atom;  // in X11/X.h and X11/Xdefs.h
#elif defined(USE_PLATFORM_WAYLAND)
  #include "xdg-shell-client-protocol.h"
  #include "xdg-decoration-client-protocol.h"
#elif defined(USE_PLATFORM_SDL)
  struct SDL_Window;
#elif defined(USE_PLATFORM_GLFW)
  struct GLFWwindow;
#elif defined(USE_PLATFORM_QT)
  class QWindow;
  class QVulkanInstance;
#endif
#include <vulkan/vulkan.hpp>
#include <functional>



class VulkanWindow {
public:

	typedef void FrameCallback();
	typedef void RecreateSwapchainCallback(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, vk::Extent2D newSurfaceExtent);

protected:

#if defined(USE_PLATFORM_WIN32)

	HWND _hwnd = nullptr;
	std::exception_ptr _wndProcException;
	HINSTANCE _hInstance;
	ATOM _windowClass = 0;

	static inline const std::vector<const char*> _requiredInstanceExtensions =
		{ "VK_KHR_surface", "VK_KHR_win32_surface" };

#elif defined(USE_PLATFORM_XLIB)

	Display* _display = nullptr;
	Window _window = 0;
	Atom _wmDeleteMessage;
	bool _visible = false;

	static inline const std::vector<const char*> _requiredInstanceExtensions =
		{ "VK_KHR_surface", "VK_KHR_xlib_surface" };

#elif defined(USE_PLATFORM_WAYLAND)

	// globals
	wl_display* _display = nullptr;
	wl_registry* _registry;
	wl_compositor* _compositor;
	xdg_wm_base* _xdgWmBase = nullptr;
	zxdg_decoration_manager_v1* _zxdgDecorationManagerV1;

	// objects
	wl_surface* _wlSurface = nullptr;
	xdg_surface* _xdgSurface = nullptr;
	xdg_toplevel* _xdgTopLevel = nullptr;
	zxdg_toplevel_decoration_v1* _decoration = nullptr;

	// state
	bool _running = true;

	// listeners
	wl_registry_listener _registryListener;
	xdg_wm_base_listener _xdgWmBaseListener;
	xdg_surface_listener _xdgSurfaceListener;
	xdg_toplevel_listener _xdgToplevelListener;

	static inline const std::vector<const char*> _requiredInstanceExtensions =
		{ "VK_KHR_surface", "VK_KHR_wayland_surface" };

#elif defined(USE_PLATFORM_SDL)

	SDL_Window* _window = nullptr;
	bool _visible = false;

#elif defined(USE_PLATFORM_GLFW)

	GLFWwindow* _window = nullptr;
	bool _visible = true;

#elif defined(USE_PLATFORM_QT)
#endif

	bool _framePending = true;
	std::function<FrameCallback> _frameCallback;
	vk::Instance _instance;
	vk::PhysicalDevice _physicalDevice;
	vk::Device _device;
	vk::SurfaceKHR _surface;

	vk::Extent2D _surfaceExtent = vk::Extent2D(0,0);
	bool _swapchainResizePending = true;
	std::function<RecreateSwapchainCallback> _recreateSwapchainCallback;

public:

	VulkanWindow() = default;
	~VulkanWindow();
	void destroy() noexcept;

	vk::SurfaceKHR init(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title = "Vulkan window");
	void setRecreateSwapchainCallback(std::function<RecreateSwapchainCallback>&& cb);
	void setRecreateSwapchainCallback(const std::function<RecreateSwapchainCallback>& cb);
	void setFrameCallback(std::function<FrameCallback>&& cb, vk::PhysicalDevice physicalDevice, vk::Device device);
	void setFrameCallback(const std::function<FrameCallback>& cb, vk::PhysicalDevice physicalDevice, vk::Device device);
	void mainLoop();

	vk::SurfaceKHR surface() const;
	vk::Extent2D surfaceExtent() const;

	void scheduleFrame();
	void scheduleSwapchainResize();

	// required Vulkan Instance extensions
	static const std::vector<const char*>& requiredExtensions();
	static std::vector<const char*>& appendRequiredExtensions(std::vector<const char*>& v);
	static uint32_t requiredExtensionCount();
	static const char* const* requiredExtensionNames();

};


// inline methods
inline VulkanWindow::~VulkanWindow()  { destroy(); }
inline void VulkanWindow::setRecreateSwapchainCallback(std::function<RecreateSwapchainCallback>&& cb)  { _recreateSwapchainCallback = move(cb); }
inline void VulkanWindow::setRecreateSwapchainCallback(const std::function<RecreateSwapchainCallback>& cb)  { _recreateSwapchainCallback = cb; }
inline void VulkanWindow::setFrameCallback(std::function<FrameCallback>&& cb, vk::PhysicalDevice physicalDevice, vk::Device device)  { _frameCallback = std::move(cb); _physicalDevice = physicalDevice; _device = device; }
inline void VulkanWindow::setFrameCallback(const std::function<FrameCallback>& cb, vk::PhysicalDevice physicalDevice, vk::Device device)  { _frameCallback = cb; _physicalDevice = physicalDevice; _device = device; }
inline vk::SurfaceKHR VulkanWindow::surface() const  { return _surface; }
inline vk::Extent2D VulkanWindow::surfaceExtent() const  { return _surfaceExtent; }
inline void VulkanWindow::scheduleFrame()  { _framePending = true; }
inline void VulkanWindow::scheduleSwapchainResize()  { _swapchainResizePending = true; _framePending = true; }
#if defined(USE_PLATFORM_WIN32) || defined(USE_PLATFORM_XLIB) || defined(USE_PLATFORM_WAYLAND)
inline const std::vector<const char*>& VulkanWindow::requiredExtensions()  { return _requiredInstanceExtensions; }
inline std::vector<const char*>& VulkanWindow::appendRequiredExtensions(std::vector<const char*>& v)  { v.insert(v.end(), _requiredInstanceExtensions.begin(), _requiredInstanceExtensions.end()); return v; }
inline uint32_t VulkanWindow::requiredExtensionCount()  { return uint32_t(_requiredInstanceExtensions.size()); }
inline const char* const* VulkanWindow::requiredExtensionNames()  { return _requiredInstanceExtensions.data(); }
#endif
