#include "VulkanWindow.h"
#if defined(USE_PLATFORM_WIN32)
# define NOMINMAX  // avoid the definition of min and max macros by windows.h
# include <windows.h>
# include <tchar.h>
#elif defined(USE_PLATFORM_XLIB)
# include <X11/Xutil.h>
#elif defined(USE_PLATFORM_WAYLAND)
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



void VulkanWindow::destroy()
{
	// destroy surface
	if(_instance && _surface) {
		_instance.destroy(_surface);
		_surface = nullptr;
	}

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
	if(m_scheduledFrameCallback) {
		wl_callback_destroy(m_scheduledFrameCallback);
		m_scheduledFrameCallback = nullptr;
	}
	if(m_decoration) {
		zxdg_toplevel_decoration_v1_destroy(m_decoration);
		m_decoration = nullptr;
	}
	if(m_xdgTopLevel) {
		xdg_toplevel_destroy(m_xdgTopLevel);
		m_xdgTopLevel = nullptr;
	}
	if(m_xdgSurface) {
		xdg_surface_destroy(m_xdgSurface);
		m_xdgSurface = nullptr;
	}
	if(m_wlSurface) {
		wl_surface_destroy(m_wlSurface);
		m_wlSurface = nullptr;
	}
	if(m_xdgWmBase) {
		xdg_wm_base_destroy(m_xdgWmBase);
		m_xdgWmBase = nullptr;
	}
	if(m_display) {
		wl_display_disconnect(m_display);
		m_display = nullptr;
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
	auto wndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		switch(msg)
		{
			case WM_ERASEBKGND:
				cout << "WM_ERASEBKGND message" << endl;
				return 1;  // returning non-zero means that background should be considered erased
			case WM_PAINT: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				try {
					cout << "WM_PAINT message" << endl;
					if(!ValidateRect(hwnd, NULL))
						throw runtime_error("ValidateRect(): The function failed.");
					if(w->_frameCallback)
						w->_frameCallback();
				} catch(...) {
					w->_wndProcException = std::current_exception();
				}
				return 0;
			}
			case WM_CLOSE: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				w->_hwnd = nullptr;
				if(!DestroyWindow(hwnd))
					w->_wndProcException = make_exception_ptr(runtime_error("DestroyWindow(): The function failed."));
				PostQuitMessage(0);
				return 0;
			}
			default:
				return DefWindowProc(hwnd, msg, wParam, lParam);
		}
	};

	// register window class with the first window
	_hInstance = GetModuleHandle(NULL);
	WNDCLASSEX wc;
	wc.cbSize        = sizeof(WNDCLASSEX);
	wc.style         = 0;
	wc.lpfnWndProc   = wndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = sizeof(LONG_PTR);
	wc.hInstance     = _hInstance;
	wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wc.lpszMenuName  = NULL;
	wc.lpszClassName = _T("VulkanWindow");
	wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
	_windowClass = RegisterClassEx(&wc);
	if(!_windowClass)
		throw runtime_error("Cannot register window class.");

	// create window
	_hwnd = CreateWindowEx(
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
		NULL, NULL, _hInstance, NULL);  // hWndParent, hMenu, hInstance, lpParam
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
			vk::XlibSurfaceCreateInfoKHR(vk::XlibSurfaceCreateFlagsKHR(), _display, _window)
		);
	return _surface;

#elif defined(USE_PLATFORM_WAYLAND)

	// no multiple init attempts
	if(m_display)
		throw runtime_error("Multiple VulkanWindow::init() calls are not allowed.");

	// open Wayland connection
	m_display = wl_display_connect(nullptr);
	if(m_display == nullptr)
		throw runtime_error("Cannot connect to Wayland display. No Wayland server is running or invalid WAYLAND_DISPLAY variable.");

	// registry listener
	m_registry = wl_display_get_registry(m_display);
	if(m_registry == nullptr)
		throw runtime_error("Cannot get Wayland registry object.");
	m_compositor = nullptr;
	m_registryListener = {
		.global =
			[](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(data);
				if(strcmp(interface, wl_compositor_interface.name) == 0)
					w->m_compositor = static_cast<wl_compositor*>(
						wl_registry_bind(registry, name, &wl_compositor_interface, 1));
				else if(strcmp(interface, xdg_wm_base_interface.name) == 0)
					w->m_xdgWmBase = static_cast<xdg_wm_base*>(
						wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
				else if(strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
					w->m_zxdgDecorationManagerV1 = static_cast<zxdg_decoration_manager_v1*>(
						wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
			},
		.global_remove =
			[](void*, wl_registry*, uint32_t) {
			},
	};
	if(wl_registry_add_listener(m_registry, &m_registryListener, this))
		throw runtime_error("wl_registry_add_listener() failed.");

	// get and init global objects
	if(wl_display_roundtrip(m_display) == -1)
		throw runtime_error("wl_display_roundtrip() failed.");
	if(m_compositor == nullptr)
		throw runtime_error("Cannot get Wayland compositor object.");
	if(m_xdgWmBase == nullptr)
		throw runtime_error("Cannot get Wayland xdg_wm_base object.");
	m_xdgWmBaseListener = {
		.ping =
			[](void*, xdg_wm_base* xdg, uint32_t serial) {
				xdg_wm_base_pong(xdg, serial);
			}
	};
	if(xdg_wm_base_add_listener(m_xdgWmBase, &m_xdgWmBaseListener, nullptr))
		throw runtime_error("xdg_wm_base_add_listener() failed.");

	// create window
	m_wlSurface = wl_compositor_create_surface(m_compositor);
	if(m_wlSurface == nullptr)
		throw runtime_error("wl_compositor_create_surface() failed.");
	m_xdgSurface = xdg_wm_base_get_xdg_surface(m_xdgWmBase, m_wlSurface);
	if(m_xdgSurface == nullptr)
		throw runtime_error("xdg_wm_base_get_xdg_surface() failed.");
	m_xdgSurfaceListener = {
		.configure =
			[](void* data, xdg_surface* xdgSurface, uint32_t serial) {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(data);
				cout<<"surface configure "<<serial<<endl;
				xdg_surface_ack_configure(xdgSurface, serial);
				wl_surface_commit(w->m_wlSurface);
			},
	};
	if(xdg_surface_add_listener(m_xdgSurface, &m_xdgSurfaceListener, this))
		throw runtime_error("xdg_surface_add_listener() failed.");

	// init xdg toplevel
	m_xdgTopLevel = xdg_surface_get_toplevel(m_xdgSurface);
	if(m_xdgTopLevel == nullptr)
		throw runtime_error("xdg_surface_get_toplevel() failed.");
	if(m_zxdgDecorationManagerV1) {
		m_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(m_zxdgDecorationManagerV1, m_xdgTopLevel);
		zxdg_toplevel_decoration_v1_set_mode(m_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	xdg_toplevel_set_title(m_xdgTopLevel, title);
	m_xdgToplevelListener = {
		.configure =
			[](void* data, xdg_toplevel* toplevel, int32_t width, int32_t height, wl_array*) -> void {
				cout<<"toplevel configure "<<width<<"x"<<height<<endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(data);

				// if width or height of the window changed,
				// schedule swapchain resize and force new frame rendering
				// (width and height of zero means that the compositor does not know the window dimension)
				if(width != w->m_surfaceExtent.width && width != 0) {
					w->m_surfaceExtent.width = width;
					if(height != w->m_surfaceExtent.height && height != 0)
						w->m_surfaceExtent.height = height;
					w->scheduleSwapchainResize();
				}
				else if(height != w->m_surfaceExtent.height && height != 0) {
					w->m_surfaceExtent.height = height;
					w->scheduleSwapchainResize();
				}

			},
		.close =
			[](void* data, xdg_toplevel* xdgTopLevel) {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(data);
				w->m_running = false;
			},
	};
	if(xdg_toplevel_add_listener(m_xdgTopLevel, &m_xdgToplevelListener, this))
		throw runtime_error("xdg_toplevel_add_listener() failed.");
	wl_surface_commit(m_wlSurface);
	if(wl_display_flush(m_display) == -1)
		throw runtime_error("wl_display_flush() failed.");

	// frame listener
	m_frameListener = {
		.done =
			[](void *data, wl_callback* cb, uint32_t time) {

				cout<<"callback"<<endl;
				reinterpret_cast<VulkanWindow*>(data)->doFrame();

			}
	};

	// create surface
	m_surface =
		instance.createWaylandSurfaceKHR(
			vk::WaylandSurfaceCreateInfoKHR(vk::WaylandSurfaceCreateFlagsKHR(), m_display, m_wlSurface)
		);
	return m_surface;

#endif
}


#if defined(USE_PLATFORM_WIN32)


void VulkanWindow::mainLoop()
{
	// update surface extent by the real window size
	vk::SurfaceCapabilitiesKHR surfaceCapabilities(_physicalDevice.getSurfaceCapabilitiesKHR(_surface));
	_surfaceExtent = surfaceCapabilities.currentExtent;

	// create swapchain
	_recreateSwapchainCallback(surfaceCapabilities, _surfaceExtent);

	// run Win32 event loop
	MSG msg;
	BOOL r;
	_wndProcException = nullptr;
	while((r = GetMessage(&msg, NULL, 0, 0)) != 0) {
		if(r == -1)
			throw runtime_error("GetMessage(): The function failed.");
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		if(_wndProcException)  // handle exceptions raised in window procedure
			rethrow_exception(_wndProcException);
	}
}


#elif defined(USE_PLATFORM_XLIB)


void VulkanWindow::mainLoop()
{
	// create swapchain
	_recreateSwapchainCallback(_physicalDevice.getSurfaceCapabilitiesKHR(_surface), _surfaceExtent);

	// run Xlib event loop
	XEvent e;
	while(true) {

		XNextEvent(_display, &e);

		// handle expose event
		if(e.type == Expose) {
			cout << "Expose event" << endl;
			if(_frameCallback)
				_frameCallback();
			continue;
		}

		// handle window close
		if(e.type==ClientMessage && ulong(e.xclient.data.l[0])==_wmDeleteMessage)
			break;

	}
}


#elif defined(USE_PLATFORM_WAYLAND)


void VulkanWindow::mainLoop()
{
	if(wl_display_roundtrip(m_display) == -1)
		throw runtime_error("wl_display_roundtrip() failed.");

	if(m_forceFrame)
		doFrame();

	while(m_running) {

		// dispatch events
		if(wl_display_dispatch(m_display) == -1)  // it blocks if there are no events
			throw std::runtime_error("wl_display_dispatch() failed.");

		if(m_forceFrame) {
			cout<<"force expose is set"<<endl;
			doFrame();
		}
	}
}


void VulkanWindow::doFrame()
{
	// destroy Wayland frame callback
	if(m_scheduledFrameCallback) {
		wl_callback_destroy(m_scheduledFrameCallback);
		m_scheduledFrameCallback = nullptr;
	}

	if(m_swapchainResizePending) {

		// make sure that we finished all the rendering
		// (this is necessary for swapchain re-creation)
		m_device.waitIdle();

		// get surface capabilities
		// On Wayland, currentExtent is 0xffffffff, 0xffffffff with the meaning that the extent
		// will be determined by the extent of a swapchain,
		// minImageExtent is 1,1 and maxImageExtent is the maximum supported surface size.
		vk::SurfaceCapabilitiesKHR surfaceCapabilities(m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface));

		// recreate swapchain
		m_swapchainResizePending = false;
		m_recreateSwapchainCallback(surfaceCapabilities, m_surfaceExtent);
	}

	// render scene
	cout<<"expose"<<m_surfaceExtent.width<<"x"<<m_surfaceExtent.height<<endl;
	m_forceFrame = false;
	m_frameCallback();
}


void VulkanWindow::scheduleNextFrame()
{
	if(m_scheduledFrameCallback)
		return;

	m_scheduledFrameCallback = wl_surface_frame(m_wlSurface);
	wl_callback_add_listener(m_scheduledFrameCallback, &m_frameListener, this);
	wl_surface_commit(m_wlSurface);
}

#endif
