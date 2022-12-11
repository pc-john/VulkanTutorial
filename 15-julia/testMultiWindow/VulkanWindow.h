#pragma once

#if defined(USE_PLATFORM_WIN32)
  typedef struct HWND__* HWND;
  typedef struct HINSTANCE__* HINSTANCE;
  typedef unsigned short ATOM;
#elif defined(USE_PLATFORM_XLIB)
#elif defined(USE_PLATFORM_WAYLAND)
  #include "xdg-shell-client-protocol.h"
  #include "xdg-decoration-client-protocol.h"
#elif defined(USE_PLATFORM_SDL)
  struct SDL_Window;
#elif defined(USE_PLATFORM_GLFW)
  struct GLFWwindow;
#elif defined(USE_PLATFORM_QT)
  class QWindow;
#endif
#include <vulkan/vulkan.hpp>
#include <functional>



class VulkanWindow {
public:

	typedef void FrameCallback();
	typedef void RecreateSwapchainCallback(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, vk::Extent2D newSurfaceExtent);
	typedef void CloseCallback();

protected:

#if defined(USE_PLATFORM_WIN32)

	HWND _hwnd = nullptr;
	enum class FramePendingState { NotPending, Pending, TentativePending };
	FramePendingState _framePendingState = FramePendingState::NotPending;
	bool _visible = false;

	static inline HINSTANCE _hInstance = 0;
	static inline ATOM _windowClass = 0;
	static inline const std::vector<const char*> _requiredInstanceExtensions =
		{ "VK_KHR_surface", "VK_KHR_win32_surface" };

#elif defined(USE_PLATFORM_XLIB)

	unsigned long _window = 0;  // unsigned long is used for Window type in Xlib
	bool _framePending = true;
	bool _visible = false;

	static inline struct _XDisplay* _display = nullptr;  // struct _XDisplay is used for Display type in Xlib
	static inline unsigned long _wmDeleteMessage;  // unsigned long is used for Atom type in Xlib
	static inline const std::vector<const char*> _requiredInstanceExtensions =
		{ "VK_KHR_surface", "VK_KHR_xlib_surface" };

#elif defined(USE_PLATFORM_WAYLAND)

	// objects
	wl_surface* _wlSurface = nullptr;
	xdg_surface* _xdgSurface = nullptr;
	xdg_toplevel* _xdgTopLevel = nullptr;
	zxdg_toplevel_decoration_v1* _decoration = nullptr;

	// listeners
	xdg_surface_listener _xdgSurfaceListener;
	xdg_toplevel_listener _xdgToplevelListener;

	// state
	bool _running = true;
	bool _framePending = true;

	// globals
	static inline wl_display* _display = nullptr;
	static inline wl_registry* _registry;
	static inline wl_compositor* _compositor = nullptr;
	static inline xdg_wm_base* _xdgWmBase = nullptr;
	static inline zxdg_decoration_manager_v1* _zxdgDecorationManagerV1 = nullptr;

	// global listeners
	static inline wl_registry_listener _registryListener;
	static inline xdg_wm_base_listener _xdgWmBaseListener;

	static inline const std::vector<const char*> _requiredInstanceExtensions =
		{ "VK_KHR_surface", "VK_KHR_wayland_surface" };

#elif defined(USE_PLATFORM_SDL)

	SDL_Window* _window = nullptr;
	bool _framePending = true;
	bool _visible = false;
	bool _minimized = false;

#elif defined(USE_PLATFORM_GLFW)

	GLFWwindow* _window = nullptr;
	enum class FramePendingState { NotPending, Pending, TentativePending };
	FramePendingState _framePendingState = FramePendingState::NotPending;
	bool _visible = true;
	bool _minimized = false;

#elif defined(USE_PLATFORM_QT)

	QWindow* _window = nullptr;

	void doFrame();
	friend class QtRenderingWindow;

#endif

	std::function<FrameCallback> _frameCallback;
	vk::Instance _instance;
	vk::PhysicalDevice _physicalDevice;
	vk::Device _device;
	vk::SurfaceKHR _surface;

	vk::Extent2D _surfaceExtent = vk::Extent2D(0,0);
	bool _swapchainResizePending = true;
	std::function<RecreateSwapchainCallback> _recreateSwapchainCallback;
	std::function<CloseCallback> _closeCallback;

public:

	// initialization and finalization
	static void init();
	static void init(void* data);
	static void init(int& argc, char* argv[]);
	static void finalize() noexcept;

	// construction and destruction
	VulkanWindow() = default;
	~VulkanWindow();
	void destroy() noexcept;

	// deleted constructors and operators
	VulkanWindow(const VulkanWindow&) = delete;
	VulkanWindow& operator=(const VulkanWindow&) = delete;

	// general methods
	vk::SurfaceKHR create(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title = "Vulkan window");
	void setRecreateSwapchainCallback(std::function<RecreateSwapchainCallback>&& cb);
	void setRecreateSwapchainCallback(const std::function<RecreateSwapchainCallback>& cb);
	void setFrameCallback(std::function<FrameCallback>&& cb, vk::PhysicalDevice physicalDevice, vk::Device device);
	void setFrameCallback(const std::function<FrameCallback>& cb, vk::PhysicalDevice physicalDevice, vk::Device device);
	void setCloseCallback(std::function<CloseCallback>&& cb);
	void setCloseCallback(const std::function<CloseCallback>& cb);
	void show();
	void hide();
	void setVisible(bool value);
	void renderFrame();
	static void mainLoop();
	static void exitMainLoop();

	// getters
	vk::SurfaceKHR surface() const;
	vk::Extent2D surfaceExtent() const;
	bool isVisible() const;

	// schedule methods
	void scheduleFrame();
	void scheduleSwapchainResize();

	// exception handling
	static inline std::exception_ptr thrownException;

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
inline void VulkanWindow::setCloseCallback(std::function<CloseCallback>&& cb)  { _closeCallback = move(cb); }
inline void VulkanWindow::setCloseCallback(const std::function<CloseCallback>& cb)  { _closeCallback = cb; }
inline void VulkanWindow::setVisible(bool value)  { if(value) show(); else hide(); }
inline vk::SurfaceKHR VulkanWindow::surface() const  { return _surface; }
inline vk::Extent2D VulkanWindow::surfaceExtent() const  { return _surfaceExtent; }
#if defined(USE_PLATFORM_WIN32) || defined(USE_PLATFORM_SDL) || defined(USE_PLATFORM_GLFW)
inline bool VulkanWindow::isVisible() const  { return _visible; }
#endif
#if defined(USE_PLATFORM_WAYLAND)
inline void VulkanWindow::scheduleFrame()  { _framePending = true; }
inline void VulkanWindow::scheduleSwapchainResize()  { _swapchainResizePending = true; _framePending = true; }
#else
inline void VulkanWindow::scheduleSwapchainResize()  { _swapchainResizePending = true; scheduleFrame(); }
#endif
#if defined(USE_PLATFORM_WIN32) || defined(USE_PLATFORM_XLIB) || defined(USE_PLATFORM_WAYLAND)
inline const std::vector<const char*>& VulkanWindow::requiredExtensions()  { return _requiredInstanceExtensions; }
inline std::vector<const char*>& VulkanWindow::appendRequiredExtensions(std::vector<const char*>& v)  { v.insert(v.end(), _requiredInstanceExtensions.begin(), _requiredInstanceExtensions.end()); return v; }
inline uint32_t VulkanWindow::requiredExtensionCount()  { return uint32_t(_requiredInstanceExtensions.size()); }
inline const char* const* VulkanWindow::requiredExtensionNames()  { return _requiredInstanceExtensions.data(); }
#endif


// nifty counter / Schwarz counter
// (VulkanWindow::finalize() must be called after all VulkanWindow objects were destroyed.
// We are doing it using Nifty counter programming idiom.)
struct VulkanWindowInitAndFinalizer
{
	VulkanWindowInitAndFinalizer();
	~VulkanWindowInitAndFinalizer();
};
static VulkanWindowInitAndFinalizer vulkanWindowInitAndFinalizer;
