#include "VulkanWindow.h"
#if defined(USE_PLATFORM_WIN32)
# define NOMINMAX  // avoid the definition of min and max macros by windows.h
# include <windows.h>
# include <tchar.h>
#elif defined(USE_PLATFORM_XLIB)
# include <X11/Xutil.h>
#elif defined(USE_PLATFORM_WAYLAND)
#elif defined(USE_PLATFORM_SDL)
# include "SDL.h"
# include "SDL_vulkan.h"
# include <algorithm>
# include <memory>
#elif defined(USE_PLATFORM_GLFW)
# define GLFW_INCLUDE_NONE  // do not include OpenGL headers
# include <GLFW/glfw3.h>
#elif defined(USE_PLATFORM_QT)
# include <QGuiApplication>
# include <QWindow>
# include <QVulkanInstance>
# include <QResizeEvent>
#endif
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <stdexcept>
#include <iostream>  // for debugging

using namespace std;


// string utf8 to wstring conversion
#if defined(USE_PLATFORM_WIN32) && defined(_UNICODE)
static wstring utf8toWString(const char* s)
{
	// get string lengths
	if(s == nullptr)  return {};
	int l1 = int(strlen(s));  // strlen() might return bigger length of s becase s is not normal string but multibyte string
	if(l1 == 0)  return {};
	l1++;  // include null terminating character
	int l2 = MultiByteToWideChar(CP_UTF8, 0, s, l1, NULL, 0);
	if(l2 == 0)
		throw runtime_error("MultiByteToWideChar(): The function failed.");

	// perform the conversion
	wstring r(l2, '\0'); // resize the string and initialize it with zeros because we have no simple way to leave it unitialized
	if(MultiByteToWideChar(CP_UTF8, 0, s, l1, r.data(), l2) == 0)
		throw runtime_error("MultiByteToWideChar(): The function failed.");
	return r;
}
#endif


// GLFW error handling
#if defined(USE_PLATFORM_GLFW)
static void throwError(const string& funcName)
{
	const char* errorString;
	int errorCode = glfwGetError(&errorString);
	throw runtime_error(string("VulkanWindow: ") + funcName + "() function failed. Error code: " +
	                    to_string(errorCode) + ". Error string: " + errorString);
}
static void checkError(const string& funcName)
{
	const char* errorString;
	int errorCode = glfwGetError(&errorString);
	if(errorCode != GLFW_NO_ERROR)
		throw runtime_error(string("VulkanWindow: ") + funcName + "() function failed. Error code: " +
		                    to_string(errorCode) + ". Error string: " + errorString);
}
#endif


// QtRenderingWindow is customized QWindow class for Vulkan rendering
#if defined(USE_PLATFORM_QT)
class QtRenderingWindow : public QWindow {
public:
	VulkanWindow* vulkanWindow;
	int timer = 0;
	QtRenderingWindow(QWindow* parent, VulkanWindow* vulkanWindow_) : QWindow(parent), vulkanWindow(vulkanWindow_)  {}
	bool event(QEvent* event) override;
};
#endif



void VulkanWindow::destroy() noexcept
{
	// destroy surface
#if !defined(USE_PLATFORM_QT)
	if(_instance && _surface) {
		_instance.destroy(_surface);
		_surface = nullptr;
	}
#else
	// do not destroy surface on Qt platform
	// because QtRenderingWindow owns the surface object and it will destroy the surface by itself
	_surface = nullptr;
#endif

#if defined(USE_PLATFORM_WIN32)

	// release resources
	// (do not throw in destructor, so ignore the errors in release builds
	// and assert in debug builds)
# ifdef NDEBUG
	if(_hwnd) {
		DestroyWindow(_hwnd);
		_hwnd = nullptr;
	}
	if(_windowClass) {
		UnregisterClass(MAKEINTATOM(_windowClass), _hInstance);
		_windowClass = 0;
	}
# else
	if(_hwnd) {
		if(!DestroyWindow(_hwnd))
			assert(0 && "DestroyWindow(): The function failed.");
		_hwnd = nullptr;
	}
	if(_windowClass) {
		if(!UnregisterClass(MAKEINTATOM(_windowClass), _hInstance))
			assert(0 && "UnregisterClass(): The function failed.");
		_windowClass = 0;
	}
# endif

#elif defined(USE_PLATFORM_XLIB)

	// release resources
	if(_window) {
		XDestroyWindow(_display, _window);
		_window = 0;
	}
	if(_display) {
		XCloseDisplay(_display);
		_display = nullptr;
	}

#elif defined(USE_PLATFORM_WAYLAND)

	// release resources
	if(_decoration) {
		zxdg_toplevel_decoration_v1_destroy(_decoration);
		_decoration = nullptr;
	}
	if(_xdgTopLevel) {
		xdg_toplevel_destroy(_xdgTopLevel);
		_xdgTopLevel = nullptr;
	}
	if(_xdgSurface) {
		xdg_surface_destroy(_xdgSurface);
		_xdgSurface = nullptr;
	}
	if(_wlSurface) {
		wl_surface_destroy(_wlSurface);
		_wlSurface = nullptr;
	}
	if(_xdgWmBase) {
		xdg_wm_base_destroy(_xdgWmBase);
		_xdgWmBase = nullptr;
	}
	if(_display) {
		wl_display_disconnect(_display);
		_display = nullptr;
	}

#elif defined(USE_PLATFORM_SDL)

	if(_window) {
		SDL_DestroyWindow(_window);
		_window = nullptr;
	}

#elif defined(USE_PLATFORM_GLFW)

	if(_window) {
		glfwDestroyWindow(_window);
		_window = nullptr;
	}

#elif defined(USE_PLATFORM_QT)

	// delete QtRenderingWindow
	if(_window) {
		delete _window;
		_window = nullptr;
	}

	// delete QVulkanInstance
	if(_vulkanInstance) {
		delete _vulkanInstance;
		_vulkanInstance = nullptr;
	}

#endif
}


vk::SurfaceKHR VulkanWindow::init(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title)
{
	// destroy any previous window data
	// (this makes calling init() multiple times safe operation)
	destroy();

	// set Vulkan instance
	_instance = instance;

	// set surface extent
	_surfaceExtent = surfaceExtent;

#if defined(USE_PLATFORM_WIN32)

	// window's message handling procedure
	auto wndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept -> LRESULT {
		switch(msg)
		{
			case WM_ERASEBKGND:
				cout << "WM_ERASEBKGND message" << endl;
				return 1;  // returning non-zero means that background should be considered erased

			case WM_PAINT: {

				cout << "WM_PAINT message" << endl;

				// set _framePending flag
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				w->_framePending = true;

				// validate window area
				if(!ValidateRect(hwnd, NULL))
					w->_wndProcException = make_exception_ptr(runtime_error("ValidateRect(): The function failed."));

				return 0;
			}

			case WM_SIZE: {
				cout << "WM_SIZE message (" << LOWORD(lParam) << "x" << HIWORD(lParam) << ")" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				w->_framePending = true;
				w->_swapchainResizePending = true;
				return DefWindowProc(hwnd, msg, wParam, lParam);
			}

			case WM_CLOSE: {
				cout << "WM_CLOSE message" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				w->_hwnd = nullptr;
				if(!DestroyWindow(hwnd))
					w->_wndProcException = make_exception_ptr(runtime_error("DestroyWindow(): The function failed."));
				return 0;
			}

			case WM_DESTROY:
				cout << "WM_DESTROY message" << endl;
				PostQuitMessage(0);
				return 0;

			default:
				return DefWindowProc(hwnd, msg, wParam, lParam);
		}
	};

	// register window class with the first window
	_hInstance = GetModuleHandle(NULL);
	_windowClass =
		RegisterClassEx(
			&(const WNDCLASSEX&)WNDCLASSEX{
				sizeof(WNDCLASSEX),  // cbSize
				0,                   // style
				wndProc,             // lpfnWndProc
				0,                   // cbClsExtra
				sizeof(LONG_PTR),    // cbWndExtra
				_hInstance,          // hInstance
				LoadIcon(NULL, IDI_APPLICATION),  // hIcon
				LoadCursor(NULL, IDC_ARROW),  // hCursor
				NULL,                // hbrBackground
				NULL,                // lpszMenuName
				_T("VulkanWindow"),  // lpszClassName
				LoadIcon(NULL, IDI_APPLICATION)  // hIconSm
			}
		);
	if(!_windowClass)
		throw runtime_error("Cannot register window class.");

	// create window
	_hwnd =
		CreateWindowEx(
			WS_EX_CLIENTEDGE,  // dwExStyle
			MAKEINTATOM(_windowClass),  // lpClassName
		#if _UNICODE
			utf8toWString(title).c_str(),  // lpWindowName
		#else
			title,  // lpWindowName
		#endif
			WS_OVERLAPPEDWINDOW,  // dwStyle
			CW_USEDEFAULT, CW_USEDEFAULT,  // x,y
			surfaceExtent.width, surfaceExtent.height,  // width, height
			NULL, NULL, _hInstance, NULL  // hWndParent, hMenu, hInstance, lpParam
		);
	if(_hwnd == NULL)
		throw runtime_error("Cannot create window.");

	// store this pointer with the window data
	SetWindowLongPtr(_hwnd, 0, (LONG_PTR)this);

	// show window
	ShowWindow(_hwnd, SW_SHOWDEFAULT);

	// create surface
	_surface =
		instance.createWin32SurfaceKHR(
			vk::Win32SurfaceCreateInfoKHR(
				vk::Win32SurfaceCreateFlagsKHR(),  // flags
				_hInstance,  // hinstance
				_hwnd  // hwnd
			)
		);
	return _surface;

#elif defined(USE_PLATFORM_XLIB)

	// open X connection
	_display = XOpenDisplay(nullptr);
	if(_display == nullptr)
		throw runtime_error("Can not open display. No X-server running or wrong DISPLAY variable.");

	// create window
	XSetWindowAttributes attr;
	attr.event_mask = ExposureMask | StructureNotifyMask | VisibilityChangeMask;
	_window =
		XCreateWindow(
			_display,  // display
			DefaultRootWindow(_display),  // parent
			0, 0,  // x,y
			surfaceExtent.width, surfaceExtent.height,  // width, height
			0,  // border_width
			CopyFromParent,  // depth
			InputOutput,  // class
			CopyFromParent,  // visual
			CWEventMask,  // valuemask
			&attr  // attributes
		);
	XSetStandardProperties(_display, _window, title, title, None, NULL, 0, NULL);
	_wmDeleteMessage = XInternAtom(_display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(_display, _window, &_wmDeleteMessage, 1);

	// show window
	XMapWindow(_display, _window);

	// create surface
	_surface =
		instance.createXlibSurfaceKHR(
			vk::XlibSurfaceCreateInfoKHR(
				vk::XlibSurfaceCreateFlagsKHR(),  // flags
				_display,  // dpy
				_window  // window
			)
		);
	return _surface;

#elif defined(USE_PLATFORM_WAYLAND)

	// open Wayland connection
	_display = wl_display_connect(nullptr);
	if(_display == nullptr)
		throw runtime_error("Cannot connect to Wayland display. No Wayland server is running or invalid WAYLAND_DISPLAY variable.");

	// registry listener
	_registry = wl_display_get_registry(_display);
	if(_registry == nullptr)
		throw runtime_error("Cannot get Wayland registry object.");
	_compositor = nullptr;
	_registryListener = {
		.global =
			[](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(data);
				if(strcmp(interface, wl_compositor_interface.name) == 0)
					w->_compositor = static_cast<wl_compositor*>(
						wl_registry_bind(registry, name, &wl_compositor_interface, 1));
				else if(strcmp(interface, xdg_wm_base_interface.name) == 0)
					w->_xdgWmBase = static_cast<xdg_wm_base*>(
						wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
				else if(strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
					w->_zxdgDecorationManagerV1 = static_cast<zxdg_decoration_manager_v1*>(
						wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
			},
		.global_remove =
			[](void*, wl_registry*, uint32_t) {
			},
	};
	if(wl_registry_add_listener(_registry, &_registryListener, this))
		throw runtime_error("wl_registry_add_listener() failed.");

	// get and init global objects
	if(wl_display_roundtrip(_display) == -1)
		throw runtime_error("wl_display_roundtrip() failed.");
	if(_compositor == nullptr)
		throw runtime_error("Cannot get Wayland compositor object.");
	if(_xdgWmBase == nullptr)
		throw runtime_error("Cannot get Wayland xdg_wm_base object.");
	_xdgWmBaseListener = {
		.ping =
			[](void*, xdg_wm_base* xdg, uint32_t serial) {
				xdg_wm_base_pong(xdg, serial);
			}
	};
	if(xdg_wm_base_add_listener(_xdgWmBase, &_xdgWmBaseListener, nullptr))
		throw runtime_error("xdg_wm_base_add_listener() failed.");

	// create window
	_wlSurface = wl_compositor_create_surface(_compositor);
	if(_wlSurface == nullptr)
		throw runtime_error("wl_compositor_create_surface() failed.");
	_xdgSurface = xdg_wm_base_get_xdg_surface(_xdgWmBase, _wlSurface);
	if(_xdgSurface == nullptr)
		throw runtime_error("xdg_wm_base_get_xdg_surface() failed.");
	_xdgSurfaceListener = {
		.configure =
			[](void* data, xdg_surface* xdgSurface, uint32_t serial) {
				cout << "surface configure" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(data);
				xdg_surface_ack_configure(xdgSurface, serial);
				wl_surface_commit(w->_wlSurface);
			},
	};
	if(xdg_surface_add_listener(_xdgSurface, &_xdgSurfaceListener, this))
		throw runtime_error("xdg_surface_add_listener() failed.");

	// init xdg toplevel
	_xdgTopLevel = xdg_surface_get_toplevel(_xdgSurface);
	if(_xdgTopLevel == nullptr)
		throw runtime_error("xdg_surface_get_toplevel() failed.");
	if(_zxdgDecorationManagerV1) {
		_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(_zxdgDecorationManagerV1, _xdgTopLevel);
		zxdg_toplevel_decoration_v1_set_mode(_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	xdg_toplevel_set_title(_xdgTopLevel, title);
	_xdgToplevelListener = {
		.configure =
			[](void* data, xdg_toplevel* toplevel, int32_t width, int32_t height, wl_array*) -> void
			{
				cout << "toplevel configure (width=" << width << ", height=" << height << ")" << endl;

				// if width or height of the window changed,
				// schedule swapchain resize and force new frame rendering
				// (width and height of zero means that the compositor does not know the window dimension)
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(data);
				if(width != w->_surfaceExtent.width && width != 0) {
					w->_surfaceExtent.width = width;
					if(height != w->_surfaceExtent.height && height != 0)
						w->_surfaceExtent.height = height;
					w->_framePending = true;
					w->_swapchainResizePending = true;
				}
				else if(height != w->_surfaceExtent.height && height != 0) {
					w->_surfaceExtent.height = height;
					w->_framePending = true;
					w->_swapchainResizePending = true;
				}
			},
		.close =
			[](void* data, xdg_toplevel* xdgTopLevel) {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(data);
				w->_running = false;
			},
	};
	if(xdg_toplevel_add_listener(_xdgTopLevel, &_xdgToplevelListener, this))
		throw runtime_error("xdg_toplevel_add_listener() failed.");
	wl_surface_commit(_wlSurface);

	// create surface
	_surface =
		instance.createWaylandSurfaceKHR(
			vk::WaylandSurfaceCreateInfoKHR(
				vk::WaylandSurfaceCreateFlagsKHR(),  // flags
				_display,  // display
				_wlSurface  // surface
			)
		);
	if(wl_display_flush(_display) == -1)
		throw runtime_error("wl_display_flush() failed.");
	return _surface;

#elif defined(USE_PLATFORM_SDL)

	// allow screensaver
	SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");

	// create Vulkan window
	_window = SDL_CreateWindow(
		title,  // title
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,  // x,y
		surfaceExtent.width, surfaceExtent.height,  // w,h
		SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE  // flags
	);
	if(_window == nullptr)
		throw runtime_error(string("VulkanWindow: SDL_CreateWindow() function failed. Error details: ") + SDL_GetError());

	// create surface
	if(!SDL_Vulkan_CreateSurface(_window, VkInstance(instance), reinterpret_cast<VkSurfaceKHR*>(&_surface)))
		throw runtime_error(string("VulkanWindow: SDL_Vulkan_CreateSurface() function failed. Error details: ") + SDL_GetError());

	return _surface;

#elif defined(USE_PLATFORM_GLFW)

	// create window
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	_window = glfwCreateWindow(surfaceExtent.width, surfaceExtent.height, title, nullptr, nullptr);
	if(_window == nullptr)
		throwError("glfwCreateWindow");

	// setup window
	glfwSetWindowUserPointer(_window, this);
	glfwSetWindowRefreshCallback(
		_window,
		[](GLFWwindow* window) {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
				glfwGetWindowUserPointer(window));
			w->_framePending = true;
		}
	);
	glfwSetFramebufferSizeCallback(
		_window,
		[](GLFWwindow* window, int width, int height) {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
				glfwGetWindowUserPointer(window));
			w->_framePending = true;
			w->_swapchainResizePending = true;
		}
	);
	glfwSetWindowIconifyCallback(
		_window,
		[](GLFWwindow* window, int iconified) {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
				glfwGetWindowUserPointer(window));
			w->_visible = iconified==GLFW_FALSE;
			if(iconified == GLFW_FALSE)
				w->_framePending = true;
		}
	);

	// create surface
	if(glfwCreateWindowSurface(instance, _window, nullptr, reinterpret_cast<VkSurfaceKHR*>(&_surface)) != VK_SUCCESS)
		throwError("glfwCreateWindowSurface");
	
	return _surface;

#elif defined(USE_PLATFORM_QT)

	// init QVulkanInstance
	_vulkanInstance = new QVulkanInstance;
	_vulkanInstance->setVkInstance(instance);
	_vulkanInstance->create();

	// setup QtRenderingWindow
	_window = new QtRenderingWindow(nullptr, this);
	_window->setSurfaceType(QSurface::VulkanSurface);
	_window->setVulkanInstance(_vulkanInstance);
	_window->resize(surfaceExtent.width, surfaceExtent.height);
	_window->show();

	// return Vulkan surface
	_surface = QVulkanInstance::surfaceForWindow(_window);
	if(!_surface)
		throw runtime_error("VulkanWindow::init(): Failed to create surface.");
	return _surface;

#endif
}


#if defined(USE_PLATFORM_WIN32)


void VulkanWindow::mainLoop()
{
	// callbacks need to be assigned
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	// run Win32 event loop
	MSG msg;
	_wndProcException = nullptr;
	while(true) {

		// handle all messages
		while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) != 0) {

			// handle WM_QUIT
			if(msg.message == WM_QUIT)
				return;

			// handle message
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			// handle exceptions raised in window procedure
			if(_wndProcException)
				rethrow_exception(_wndProcException);
		}

		// no frame pending?
		if(!_framePending) {

			// wait messages
			if(WaitMessage() == 0)
				throw runtime_error("WaitMessage() failed.");
			
			continue;
		}

		// recreate swapchain if requested
		if(_swapchainResizePending) {

			// make sure that we finished all the rendering
			// (this is necessary for swapchain re-creation)
			_device.waitIdle();

			// get surface capabilities
			// On Win32, currentExtent, minImageExtent and maxImageExtent of returned surfaceCapabilites are all equal.
			// It means that we can create a new swapchain only with imageExtent being equal to the window size.
			// The currentExtent might become 0,0 on this platform, for example, when the window is minimized.
			// If the currentExtent is not 0,0, both width and height must be greater than 0.
			vk::SurfaceCapabilitiesKHR surfaceCapabilities(_physicalDevice.getSurfaceCapabilitiesKHR(_surface));

			// zero size swapchain is not allowed,
			// so we will repeat the resize attempt after the next window resize
			if(surfaceCapabilities.currentExtent == vk::Extent2D(0,0)) {
				_framePending = false;  // this will be rescheduled on the first window resize
				continue;
			}

			// recreate swapchain
			_swapchainResizePending = false;
			_surfaceExtent = surfaceCapabilities.currentExtent;
			_recreateSwapchainCallback(surfaceCapabilities, _surfaceExtent);
		}

		// render scene
		_framePending = false;
		_frameCallback();
	}
}


#elif defined(USE_PLATFORM_XLIB)


void VulkanWindow::mainLoop()
{
	// callbacks need to be assigned
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	// run Xlib event loop
	XEvent e;
	while(true) {

		// get number of pending events
		int numEvents = XPending(_display);

		// handle zero events
		if(numEvents == 0)
			if(_framePending && _visible)
				goto renderFrame;  // frame request -> render frame
			else
				numEvents = 1;  // no frame request -> wait for events in XNextEvent()

		// process events
		do {

			for(int i=0; i<numEvents; i++) {

				// get event
				XNextEvent(_display, &e);

				// expose event
				if(e.type == Expose) {
					cout << "Expose event" << endl;
					_framePending = true;
					continue;
				}

				// configure event
				if(e.type == ConfigureNotify) {
					if(e.xconfigure.width != _surfaceExtent.width || e.xconfigure.height != _surfaceExtent.height) {
						cout << "Configure event " << e.xconfigure.width << "x" << e.xconfigure.height << endl;
						_framePending = true;
						_swapchainResizePending = true;
					}
					continue;
				}

				// map, unmap, obscured, unobscured
				if(e.type==MapNotify || (e.type==VisibilityNotify && e.xvisibility.state!=VisibilityFullyObscured)) {
					cout << "Window visible" << endl;
					_visible = true;
					_framePending = true;
					continue;
				}
				if(e.type==UnmapNotify || (e.type==VisibilityNotify && e.xvisibility.state==VisibilityFullyObscured)) {
					cout << "Window not visible" << endl;
					_visible = false;
					continue;
				}

				// handle window close
				if(e.type==ClientMessage && ulong(e.xclient.data.l[0])==_wmDeleteMessage)
					return;
			}

			// if more events came in the mean time, handle them as well
			numEvents = XPending(_display);

		} while(numEvents > 0);


		// frame pending?
		if(!_framePending || !_visible)
			continue;

		// render frame code starts with swapchain re-creation
		renderFrame:

		// recreate swapchain if requested
		if(_swapchainResizePending) {

			// make sure that we finished all the rendering
			// (this is necessary for swapchain re-creation)
			_device.waitIdle();

			// get surface capabilities
			// On Xlib, currentExtent, minImageExtent and maxImageExtent of returned surfaceCapabilites are all equal.
			// It means that we can create a new swapchain only with imageExtent being equal to the window size.
			// The currentExtent might become 0,0 on this platform, for example, when the window is minimized.
			// If the currentExtent is not 0,0, both width and height must be greater than 0.
			vk::SurfaceCapabilitiesKHR surfaceCapabilities(_physicalDevice.getSurfaceCapabilitiesKHR(_surface));

			// zero size swapchain is not allowed,
			// so we will repeat the resize attempt after the next window resize
			// (this never happened on my KDE 5.80.0 (Kubuntu 21.04) and KDE 5.44.0 (Kubuntu 18.04.5);
			// window minimalizing just unmaps the window)
			if(surfaceCapabilities.currentExtent == vk::Extent2D(0,0)) {
				_framePending = false;  // this will be rescheduled on the first window resize
				continue;
			}

			// recreate swapchain
			_swapchainResizePending = false;
			_surfaceExtent = surfaceCapabilities.currentExtent;
			_recreateSwapchainCallback(surfaceCapabilities, _surfaceExtent);
		}

		// render frame
		_framePending = false;
		_frameCallback();

	}
}


#elif defined(USE_PLATFORM_WAYLAND)


void VulkanWindow::mainLoop()
{
	// callbacks need to be assigned
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	// flush outgoing buffers
	cout << "Entering main loop." << endl;
	if(wl_display_flush(_display) == -1)
		throw runtime_error("wl_display_flush() failed.");

	// main loop
	while(_running) {

		// dispatch events
		if(!_framePending)
		{
			// dispatch events with blocking
			if(wl_display_dispatch(_display) == -1)  // it blocks if there are no events
				throw runtime_error("wl_display_dispatch() failed.");
		}
		else
		{
			// dispatch events without blocking
			while(wl_display_prepare_read(_display) != 0)
				if(wl_display_dispatch_pending(_display) == -1)
					throw runtime_error("wl_display_dispatch_pending() failed.");
			if(wl_display_flush(_display) == -1)
				throw runtime_error("wl_display_flush() failed.");
			if(wl_display_read_events(_display) == -1)
				throw runtime_error("wl_display_read_events() failed.");
			if(wl_display_dispatch_pending(_display) == -1)
				throw runtime_error("wl_display_dispatch_pending() failed.");
		}

		// flush outgoing buffers
		if(wl_display_flush(_display) == -1)
			throw runtime_error("wl_display_flush() failed.");

		if(!_framePending)
			continue;

		// recreate swapchain if requested
		if(_swapchainResizePending) {

			// make sure that we finished all the rendering
			// (this is necessary for swapchain re-creation)
			_device.waitIdle();

			// get surface capabilities
			// On Wayland, currentExtent is 0xffffffff, 0xffffffff with the meaning that the window extent
			// will be determined by the extent of the swapchain,
			// minImageExtent is 1,1 and maxImageExtent is the maximum supported surface size.
			vk::SurfaceCapabilitiesKHR surfaceCapabilities(_physicalDevice.getSurfaceCapabilitiesKHR(_surface));

			// recreate swapchain
			_swapchainResizePending = false;
			_recreateSwapchainCallback(surfaceCapabilities, _surfaceExtent);
		}

		// render frame
		_framePending = false;
		_frameCallback();

	}
	cout << "Main loop left." << endl;
}


#elif defined(USE_PLATFORM_SDL)


const vector<const char*>& VulkanWindow::requiredExtensions()
{
	// cache the result in static local variable
	// so extension list is constructed only once
	static const vector<const char*> l =
		[]() {

			// init SDL video subsystem
			struct SDL {
				SDL() {
					if(SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
						throw runtime_error(string("VulkanWindow: SDL_InitSubSystem(SDL_INIT_VIDEO) function failed. Error details: ") + SDL_GetError());
				}
				~SDL() {
					SDL_QuitSubSystem(SDL_INIT_VIDEO);
				}
			} sdl;

			// create temporary Vulkan window
			unique_ptr<SDL_Window,void(*)(SDL_Window*)> tmpWindow(
				SDL_CreateWindow("Temporary", 0,0, 1,1, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
				[](SDL_Window* w){ SDL_DestroyWindow(w); }
			);
			if(tmpWindow.get() == nullptr)
				throw runtime_error(string("VulkanWindow: SDL_CreateWindow() function failed. Error details: ") + SDL_GetError());

			// get number of required instance extensions
			unsigned int count;
			if(!SDL_Vulkan_GetInstanceExtensions(tmpWindow.get(), &count, nullptr))
				throw runtime_error("VulkanWindow: SDL_Vulkan_GetInstanceExtensions() function failed.");

			// get required instance extensions
			vector<const char*> l;
			l.resize(count);
			if(!SDL_Vulkan_GetInstanceExtensions(tmpWindow.get(), &count, l.data()))
				throw runtime_error("VulkanWindow: SDL_Vulkan_GetInstanceExtensions() function failed.");
			l.resize(count);
			return l;

		}();

	return l;
}

vector<const char*>& VulkanWindow::appendRequiredExtensions(vector<const char*>& v)  { auto& l=requiredExtensions(); v.insert(v.end(), l.begin(), l.end()); return v; }
uint32_t VulkanWindow::requiredExtensionCount()  { return uint32_t(requiredExtensions().size()); }
const char* const* VulkanWindow::requiredExtensionNames()  { return requiredExtensions().data(); }


void VulkanWindow::mainLoop()
{
	// callbacks need to be assigned
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	// main loop
	SDL_Event event;
	while(true) {

		int haveEvents = SDL_PollEvent(&event);
		if(!haveEvents)
			if(_framePending && _visible)
				goto skipMessageLoop;  // render frame
			else
				if(SDL_WaitEvent(&event) == 0)  // want for events
					throw runtime_error(string("VulkanWindow: SDL_WaitEvent() function failed. Error details: ") + SDL_GetError());

		do {
			switch(event.type) {
			case SDL_WINDOWEVENT:
				switch(event.window.event) {

				case SDL_WINDOWEVENT_EXPOSED:
					cout << "Exposed event" << endl;
					_framePending = true;
					break;

				case SDL_WINDOWEVENT_SIZE_CHANGED:
					cout << "Size changed event" << endl;
					_framePending = true;
					_swapchainResizePending = true;
					break;

				case SDL_WINDOWEVENT_SHOWN:
					cout << "Shown event" << endl;
					_visible = true;
					break;

				case SDL_WINDOWEVENT_HIDDEN:
					cout << "Hidden event" << endl;
					_visible = false;
					break;

				case SDL_WINDOWEVENT_MINIMIZED:
					cout << "Minimized event" << endl;
					_visible = false;
					break;

				case SDL_WINDOWEVENT_RESTORED:
					cout << "Restored event" << endl;
					_visible = true;
					break;

				}
				break;

			case SDL_QUIT:
				return;
			}
		} while(SDL_PollEvent(&event));

	skipMessageLoop:

		// render window
		if(_framePending && _visible) {

			if(_swapchainResizePending) {

				// make sure that we finished all the rendering
				// (this is necessary for swapchain re-creation)
				_device.waitIdle();

				// get surface capabilities
				vk::SurfaceCapabilitiesKHR surfaceCapabilities(_physicalDevice.getSurfaceCapabilitiesKHR(_surface));

				// get surface size
				SDL_Vulkan_GetDrawableSize(_window, reinterpret_cast<int*>(&_surfaceExtent.width), reinterpret_cast<int*>(&_surfaceExtent.height));
				_surfaceExtent.width = clamp(_surfaceExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
				_surfaceExtent.height = clamp(_surfaceExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);

				// do not allow swapchain creation and rendering when surface extent is 0,0;
				// we will repeat the resize attempt after the next window resize
				// (this happens on Win32 systems and may happen also on systems that use Xlib)
				if(_surfaceExtent == vk::Extent2D(0,0)) {
					_framePending = false;  // this will be rescheduled on the first window resize
					continue;
				}

				// recreate swapchain
				_swapchainResizePending = false;
				_recreateSwapchainCallback(surfaceCapabilities, _surfaceExtent);
			}

			// render scene
			_framePending = false;
			_frameCallback();

		}
	}
}


#elif defined(USE_PLATFORM_GLFW)


const vector<const char*>& VulkanWindow::requiredExtensions()
{
	// cache the result in static local variable
	// so extension list is constructed only once
	static const vector<const char*> l =
		[]() {

			// get required extensions
			uint32_t count;
			const char** a = glfwGetRequiredInstanceExtensions(&count);
			if(a == nullptr) {
				const char* errorString;
				int errorCode = glfwGetError(&errorString);
				throw runtime_error(string("VulkanWindow: glfwGetRequiredInstanceExtensions() function failed. Error code: ") +
				                    to_string(errorCode) + ". Error string: " + errorString);
			}
			
			// fill std::vector
			vector<const char*> l;
			l.reserve(count);
			for(uint32_t i=0; i<count; i++)
				l.emplace_back(a[i]);
			return l;
		}();

	return l;
}


vector<const char*>& VulkanWindow::appendRequiredExtensions(vector<const char*>& v)  { auto& l=requiredExtensions(); v.insert(v.end(), l.begin(), l.end()); return v; }
uint32_t VulkanWindow::requiredExtensionCount()  { return uint32_t(requiredExtensions().size()); }
const char* const* VulkanWindow::requiredExtensionNames()  { return requiredExtensions().data(); }


void VulkanWindow::mainLoop()
{
	// callbacks need to be assigned
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	// main loop
	while(!glfwWindowShouldClose(_window)) {

		if(_framePending)
		{
			glfwPollEvents();
			checkError("glfwPollEvents");
		}
		else
		{
			glfwWaitEvents();
			checkError("glfwWaitEvents");
		}

		// render window
		if(_framePending) {

			// do not render invisible window
			// (rendering will be re-triggered when window is made visible again)
			if(!_visible) {
				_framePending = false;
				continue;
			}

			if(_swapchainResizePending) {

				// make sure that we finished all the rendering
				// (this is necessary for swapchain re-creation)
				_device.waitIdle();

				// get surface capabilities
				vk::SurfaceCapabilitiesKHR surfaceCapabilities(_physicalDevice.getSurfaceCapabilitiesKHR(_surface));

				// get surface size
				// (0xffffffff values might be returned on Wayland)
				if(surfaceCapabilities.currentExtent.width != 0xffffffff && surfaceCapabilities.currentExtent.height != 0xffffffff)
					_surfaceExtent = surfaceCapabilities.currentExtent;
				else {
					glfwGetFramebufferSize(_window, reinterpret_cast<int*>(&_surfaceExtent.width), reinterpret_cast<int*>(&_surfaceExtent.height));
					_surfaceExtent.width = clamp(_surfaceExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
					_surfaceExtent.height = clamp(_surfaceExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
				}

				// do not allow swapchain creation and rendering when surface extent is 0,0;
				// we will repeat the resize attempt after the next window resize
				// (this happens on Win32 systems and may happen also on systems that use Xlib)
				if(_surfaceExtent == vk::Extent2D(0,0)) {
					_framePending = false;  // this will be rescheduled on the first window resize
					continue;
				}

				// recreate swapchain
				_swapchainResizePending = false;
				_recreateSwapchainCallback(surfaceCapabilities, _surfaceExtent);
			}

			// render scene
			_framePending = false;
			_frameCallback();

		}

	}
}


#elif defined(USE_PLATFORM_QT)


const vector<const char*>& VulkanWindow::requiredExtensions()
{
	static vector<const char*> l =
		[]() {
			QString platform = QGuiApplication::platformName();
			if(platform == "wayland")
				return vector<const char*>{ "VK_KHR_surface", "VK_KHR_wayland_surface" };
			else if(platform == "windows")
				return vector<const char*>{ "VK_KHR_surface", "VK_KHR_win32_surface" };
			else if(platform == "xcb")
				return vector<const char*>{ "VK_KHR_surface", "VK_KHR_xcb_surface" };
			else if(platform == "")
				throw runtime_error("VulkanWindow::requiredExtensions(): QGuiApplication not initialized or unknown Qt platform.");
			else
				throw runtime_error("VulkanWindow::requiredExtensions(): Unknown Qt platform \"" + platform.toStdString() + "\".");
		}();

	return l;
}

vector<const char*>& VulkanWindow::appendRequiredExtensions(vector<const char*>& v)  { auto& l=requiredExtensions(); v.insert(v.end(), l.begin(), l.end()); return v; }
uint32_t VulkanWindow::requiredExtensionCount()  { return uint32_t(requiredExtensions().size()); }
const char* const* VulkanWindow::requiredExtensionNames()  { return requiredExtensions().data(); }


void VulkanWindow::mainLoop()
{
	// callbacks need to be assigned
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	// call exec()
	_thrownException = nullptr;
	QGuiApplication::exec();
	if(_thrownException)  // rethrow the exception that we caught in QtRenderingWindow::event()
		rethrow_exception(_thrownException);
}


void VulkanWindow::doFrame()
{
	if(_swapchainResizePending) {

		// make sure that we finished all the rendering
		// (this is necessary for swapchain re-creation)
		_device.waitIdle();

		// get surface capabilities
		vk::SurfaceCapabilitiesKHR surfaceCapabilities(_physicalDevice.getSurfaceCapabilitiesKHR(_surface));

		// get surface size
		// (0xffffffff values might be returned on Wayland)
		if(surfaceCapabilities.currentExtent.width != 0xffffffff && surfaceCapabilities.currentExtent.height != 0xffffffff)
			_surfaceExtent = surfaceCapabilities.currentExtent;
		else {
			QSize size = _window->size();
			auto ratio = _window->devicePixelRatio();
			_surfaceExtent = vk::Extent2D(uint32_t(float(size.width()) * ratio + 0.5f), uint32_t(float(size.height()) * ratio + 0.5f));
			_surfaceExtent.width = clamp(_surfaceExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
			_surfaceExtent.height = clamp(_surfaceExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
		}
		cout << "New Qt window size in device independent pixels: " << _window->width() << "x" << _window->height()
		     << ", in physical pixels: " << uint32_t(float(_window->width()) * _window->devicePixelRatio() + 0.5f)
		     << "x" << uint32_t(float(_window->height()) * _window->devicePixelRatio() + 0.5f) << endl;

		// do not allow swapchain creation and rendering when surface extent is 0,0;
		// we will repeat the resize attempt after the next window resize
		// (this happens on Win32 systems and may happen also on systems that use Xlib)
		if(_surfaceExtent == vk::Extent2D(0,0))
			return;  // skip frame rendering (new frame will be scheduled on the next window resize)

		// recreate swapchain
		_swapchainResizePending = false;
		_recreateSwapchainCallback(surfaceCapabilities, _surfaceExtent);
	}

	// render scene
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
	_vulkanInstance->presentAboutToBeQueued(_window);
#endif
	_frameCallback();
	_vulkanInstance->presentQueued(_window);
}


bool QtRenderingWindow::event(QEvent* event)
{
	try {

		// handle verious events
		switch(event->type()) {

		case QEvent::Type::Timer:
			cout<<"t";
			killTimer(timer);
			timer = 0;
			if(isExposed())
				vulkanWindow->doFrame();
			return true;

		case QEvent::Type::Expose: {
			cout<<"e";
			bool r = QWindow::event(event);
			if(isExposed())
				vulkanWindow->scheduleFrame();
			return r;
		}

		case QEvent::Type::UpdateRequest:
			if(isExposed())
				vulkanWindow->scheduleFrame();
			return true;

		case QEvent::Type::Resize: {
			vulkanWindow->scheduleSwapchainResize();
			return QWindow::event(event);
		}

		// hide window on close
		// (we must not really close it as Vulkan surface would be destroyed
		// and this would make a problem as swapchain still exists and Vulkan
		// requires the swapchain to be destroyed first)
		case QEvent::Type::Close:
			if(isVisible())
				hide();
			QGuiApplication::quit();
			return true;

		default:
			return QWindow::event(event);
		}
	}
	catch(...) {
		vulkanWindow->_thrownException = std::current_exception();
		QGuiApplication::quit();
		return true;
	}
}


void VulkanWindow::scheduleFrame()
{
	QtRenderingWindow* w = static_cast<QtRenderingWindow*>(_window);
	if(w->timer == 0) {
		w->timer = _window->startTimer(0);
		if(w->timer == 0)
			throw runtime_error("VulkanWindow::scheduleNextFrame(): Cannot allocate timer.");
	}
}


#endif
