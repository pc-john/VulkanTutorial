#pragma once

#include <vulkan/vulkan.hpp>
#include <functional>
#include <bitset>



class VulkanWindow {
public:

	// general function prototypes
	typedef void FrameCallback(VulkanWindow& window);
	typedef void RecreateSwapchainCallback(VulkanWindow& window,
		const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, vk::Extent2D newSurfaceExtent);
	typedef void CloseCallback(VulkanWindow& window);

	// input structures and enums
	struct MouseButton {
		static constexpr const size_t Left = 0;
		static constexpr const size_t Right = 1;
		static constexpr const size_t Middle = 2;
		static constexpr const size_t X1 = 3;
		static constexpr const size_t X2 = 4;
		static constexpr const size_t Unknown = 31;
	};
	enum class ButtonAction : uint8_t { Down, Up };
	struct Modifier {
		static constexpr const size_t Ctrl = 0;
		static constexpr const size_t Shift = 1;
		static constexpr const size_t Alt = 2;
	};
	struct MouseState {
		int posX, posY;
		int relX, relY;  // relative against the state of previous mouse callback
		std::bitset<32> buttons;
		std::bitset<32> mods;
		int wheelX, wheelY;  // relative against the state of previous wheel callback
	};

	// input function prototypes
	typedef void MouseMoveCallback(VulkanWindow& window, const MouseState& mouseState);
	typedef void MouseButtonCallback(VulkanWindow& window, size_t button, ButtonAction downOrUp, const MouseState& mouseState);
	typedef void MouseWheelCallback(VulkanWindow& window, const MouseState& mouseState);

protected:

#if defined(USE_PLATFORM_WIN32)

	void* _hwnd = nullptr;  // void* is used instead of HWND type to avoid #include <windows.h>
	enum class FramePendingState { NotPending, Pending, TentativePending };
	FramePendingState _framePendingState;
	bool _visible;
	bool _hiddenWindowFramePending;

	static inline void* _hInstance = 0;  // void* is used instead of HINSTANCE type to avoid #include <windows.h>
	static inline uint16_t _windowClass = 0;  // uint16_t is used instead of ATOM type to avoid #include <windows.h>
	static inline const std::vector<const char*> _requiredInstanceExtensions =
		{ "VK_KHR_surface", "VK_KHR_win32_surface" };

#elif defined(USE_PLATFORM_XLIB)

	unsigned long _window = 0;  // unsigned long is used for Window type
	bool _framePending;
	bool _visible;
	bool _fullyObscured;
	bool _iconVisible;
	bool _minimized;

	static inline struct _XDisplay* _display = nullptr;  // struct _XDisplay* is used instead of Display* type
	static inline unsigned long _wmDeleteMessage;  // unsigned long is used for Atom type
	static inline unsigned long _wmStateProperty;  // unsigned long is used for Atom type
	static inline const std::vector<const char*> _requiredInstanceExtensions =
		{ "VK_KHR_surface", "VK_KHR_xlib_surface" };

	void updateMinimized();

#elif defined(USE_PLATFORM_WAYLAND)

	// objects
	struct wl_surface* _wlSurface = nullptr;
	struct xdg_surface* _xdgSurface = nullptr;
	struct xdg_toplevel* _xdgTopLevel = nullptr;
	struct zxdg_toplevel_decoration_v1* _decoration = nullptr;
	struct wl_callback* _scheduledFrameCallback = nullptr;

	// wl and xdg listeners
	struct WaylandListeners* _listeners = nullptr;

	// state
	bool _forcedFrame;
	std::string _title;

	// globals
	static inline struct wl_display* _display = nullptr;
	static inline struct wl_registry* _registry;
	static inline struct wl_compositor* _compositor = nullptr;
	static inline struct xdg_wm_base* _xdgWmBase = nullptr;
	static inline struct zxdg_decoration_manager_v1* _zxdgDecorationManagerV1 = nullptr;

	static inline const std::vector<const char*> _requiredInstanceExtensions =
		{ "VK_KHR_surface", "VK_KHR_wayland_surface" };

#elif defined(USE_PLATFORM_SDL2)

	struct SDL_Window* _window = nullptr;
	bool _framePending;
	bool _hiddenWindowFramePending;
	bool _visible;
	bool _minimized;

#elif defined(USE_PLATFORM_GLFW)

	struct GLFWwindow* _window = nullptr;
	enum class FramePendingState { NotPending, Pending, TentativePending };
	FramePendingState _framePendingState;
	bool _visible;
	bool _minimized;

#elif defined(USE_PLATFORM_QT)

	class QWindow* _window = nullptr;
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

	MouseState _mouseState;
	std::function<MouseMoveCallback> _mouseMoveCallback;
	std::function<MouseButtonCallback> _mouseButtonCallback;
	std::function<MouseWheelCallback> _mouseWheelCallback;

public:

	// initialization and finalization
	static void init();
	static void init(void* data);
	static void init(int& argc, char* argv[]);
	static void finalize() noexcept;

	// construction and destruction
	VulkanWindow() = default;
	VulkanWindow(VulkanWindow&& other);
	~VulkanWindow();
	void destroy() noexcept;
	VulkanWindow& operator=(VulkanWindow&& rhs) noexcept;

	// deleted constructors and operators
	VulkanWindow(const VulkanWindow&) = delete;
	VulkanWindow& operator=(const VulkanWindow&) = delete;

	// general methods
	vk::SurfaceKHR create(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title = "Vulkan window");
	void show();
	void hide();
	void setVisible(bool value);
	void renderFrame();
	static void mainLoop();
	static void exitMainLoop();

	// callbacks
	void setRecreateSwapchainCallback(std::function<RecreateSwapchainCallback>&& cb);
	void setRecreateSwapchainCallback(const std::function<RecreateSwapchainCallback>& cb);
	void setFrameCallback(std::function<FrameCallback>&& cb, vk::PhysicalDevice physicalDevice, vk::Device device);
	void setFrameCallback(const std::function<FrameCallback>& cb, vk::PhysicalDevice physicalDevice, vk::Device device);
	void setCloseCallback(std::function<CloseCallback>&& cb);
	void setCloseCallback(const std::function<CloseCallback>& cb);
	void setMouseMoveCallback(std::function<MouseMoveCallback>&& cb);
	void setMouseMoveCallback(const std::function<MouseMoveCallback>& cb);
	void setMouseButtonCallback(std::function<MouseButtonCallback>&& cb);
	void setMouseButtonCallback(const std::function<MouseButtonCallback>& cb);
	void setMouseWheelCallback(std::function<MouseWheelCallback>&& cb);
	void setMouseWheelCallback(const std::function<MouseWheelCallback>& cb);
	const std::function<RecreateSwapchainCallback>& recreateSwapchainCallback() const;
	const std::function<FrameCallback>& frameCallback() const;
	const std::function<CloseCallback>& closeCallback() const;
	const std::function<MouseMoveCallback>& mouseMoveCallback() const;
	const std::function<MouseButtonCallback>& mouseButtonCallback() const;
	const std::function<MouseWheelCallback>& mouseWheelCallback() const;

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
inline void VulkanWindow::setVisible(bool value)  { if(value) show(); else hide(); }
inline void VulkanWindow::setRecreateSwapchainCallback(std::function<RecreateSwapchainCallback>&& cb)  { _recreateSwapchainCallback = move(cb); }
inline void VulkanWindow::setRecreateSwapchainCallback(const std::function<RecreateSwapchainCallback>& cb)  { _recreateSwapchainCallback = cb; }
inline void VulkanWindow::setFrameCallback(std::function<FrameCallback>&& cb, vk::PhysicalDevice physicalDevice, vk::Device device)  { _frameCallback = std::move(cb); _physicalDevice = physicalDevice; _device = device; }
inline void VulkanWindow::setFrameCallback(const std::function<FrameCallback>& cb, vk::PhysicalDevice physicalDevice, vk::Device device)  { _frameCallback = cb; _physicalDevice = physicalDevice; _device = device; }
inline void VulkanWindow::setCloseCallback(std::function<CloseCallback>&& cb)  { _closeCallback = move(cb); }
inline void VulkanWindow::setCloseCallback(const std::function<CloseCallback>& cb)  { _closeCallback = cb; }
inline void VulkanWindow::setMouseMoveCallback(std::function<MouseMoveCallback>&& cb)  { _mouseMoveCallback = move(cb); }
inline void VulkanWindow::setMouseMoveCallback(const std::function<MouseMoveCallback>& cb)  { _mouseMoveCallback = cb; }
inline void VulkanWindow::setMouseButtonCallback(std::function<MouseButtonCallback>&& cb)  { _mouseButtonCallback = move(cb); }
inline void VulkanWindow::setMouseButtonCallback(const std::function<MouseButtonCallback>& cb)  { _mouseButtonCallback = cb; }
inline void VulkanWindow::setMouseWheelCallback(std::function<MouseWheelCallback>&& cb)  { _mouseWheelCallback = move(cb); }
inline void VulkanWindow::setMouseWheelCallback(const std::function<MouseWheelCallback>& cb)  { _mouseWheelCallback = cb; }
inline const std::function<VulkanWindow::RecreateSwapchainCallback>& VulkanWindow::recreateSwapchainCallback() const  { return _recreateSwapchainCallback; }
inline const std::function<VulkanWindow::FrameCallback>& VulkanWindow::frameCallback() const  { return _frameCallback; }
inline const std::function<VulkanWindow::CloseCallback>& VulkanWindow::closeCallback() const  { return _closeCallback; }
inline const std::function<VulkanWindow::MouseMoveCallback>& VulkanWindow::mouseMoveCallback() const  { return _mouseMoveCallback; }
inline const std::function<VulkanWindow::MouseButtonCallback>& VulkanWindow::mouseButtonCallback() const  { return _mouseButtonCallback; }
inline const std::function<VulkanWindow::MouseWheelCallback>& VulkanWindow::mouseWheelCallback() const  { return _mouseWheelCallback; }
inline vk::SurfaceKHR VulkanWindow::surface() const  { return _surface; }
inline vk::Extent2D VulkanWindow::surfaceExtent() const  { return _surfaceExtent; }
#if defined(USE_PLATFORM_WIN32) || defined(USE_PLATFORM_XLIB) || defined(USE_PLATFORM_SDL2) || defined(USE_PLATFORM_GLFW)
inline bool VulkanWindow::isVisible() const  { return _visible; }
#elif defined(USE_PLATFORM_WAYLAND)
inline bool VulkanWindow::isVisible() const  { return _xdgSurface != nullptr; }
#endif
inline void VulkanWindow::scheduleSwapchainResize()  { _swapchainResizePending = true; scheduleFrame(); }
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
