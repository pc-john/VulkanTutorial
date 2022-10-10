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



void VulkanWindow::destroy() noexcept
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
		static vector<tuple<UINT,WPARAM>> msgList;
		static vector<DWORD> tList;
		static DWORD t = GetTickCount();
		msgList.emplace_back(msg,wParam);
		tList.push_back(GetTickCount());
		switch(msg)
		{
			case WM_ERASEBKGND:
				cout << "WM_ERASEBKGND message" << endl;
				return 1;  // returning non-zero means that background should be considered erased

			case WM_PAINT: {

				//cout << "WM_PAINT message" << endl;

				DWORD newT = GetTickCount();
				if(newT-t > 50) {
					cout << "Delta: " << newT-t << " ms" << endl;
					for(size_t i=0, c=msgList.size(); i<c; i++)
						if(get<0>(msgList[i])==0x112)
							cout << "   " << tList[i] << ": " << hex << get<0>(msgList[i]) << " - "
							     << get<1>(msgList[i]) << dec << endl;
						else
							cout << "   " << tList[i] << ": " << hex << get<0>(msgList[i]) << dec << endl;
				}
				msgList.clear();
				tList.clear();
				t = newT;

				// validate window area
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				if(!ValidateRect(hwnd, NULL))
					w->_wndProcException = make_exception_ptr(runtime_error("ValidateRect(): The function failed."));

				// render frame
				w->doFrame();

				return 0;
			}

			case WM_SIZE: {

				cout << "WM_SIZE message (" << LOWORD(lParam) << "x" << HIWORD(lParam) << ")" << endl;

				// schedule swapchain resize
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				w->scheduleSwapchainResize();

				return DefWindowProc(hwnd, msg, wParam, lParam);
			}

			case WM_NCLBUTTONDOWN: {
				//if(wParam == HTCAPTION) {
					/*VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
					w->_ncLMouseButton = true;
					w->_ncMousePos = lParam;
					return 0;*/
					POINT point;
					GetCursorPos(&point);
					ScreenToClient(hwnd, &point);
					PostMessage(hwnd, WM_MOUSEMOVE, 0, point.x | point.y<<16);
				//}
				return DefWindowProc(hwnd, msg, wParam, lParam);
			}

			case WM_NCRBUTTONDOWN: {
				//if(wParam == HTCAPTION) {
					POINT point;
					GetCursorPos(&point);
					ScreenToClient(hwnd, &point);
					PostMessage(hwnd, WM_MOUSEMOVE, 0, point.x | point.y<<16);
				//}
				return DefWindowProc(hwnd, msg, wParam, lParam);
			}

			/*case WM_NCMOUSEMOVE:
			case WM_MOUSEMOVE: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				if(w->_ncLMouseButton && w->_ncMousePos != lParam) {
					w->_ncLMouseButton = false;
					DefWindowProc(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, w->_ncMousePos);
				}
				return DefWindowProc(hwnd, msg, wParam, lParam);
			}*/

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

	// frame listener
	m_frameListener = {
		.done =
			[](void *data, wl_callback* cb, uint32_t time) {

				cout<<"callback"<<endl;
				reinterpret_cast<VulkanWindow*>(data)->doFrame();

			}
	};

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

#else

	// unknown platform
	throw runtime_error("VulkanWindow: Unknown platform.");

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
	BOOL r;
	_wndProcException = nullptr;
	while((r = GetMessage(&msg, NULL, 0, 0)) != 0) {

		// handle errors
		if(r == -1)
			throw runtime_error("GetMessage(): The function failed.");

		// handle message
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		// handle exceptions raised in window procedure
		if(_wndProcException)
			rethrow_exception(_wndProcException);

	}
}


void VulkanWindow::scheduleFrame()
{
	if(_framePending)
		return;

	// invalidate window content (this will cause WM_PAINT message to be sent) 
	if(!InvalidateRect(_hwnd, NULL, FALSE))
		throw runtime_error("InvalidateRect(): The function failed.");
}

void VulkanWindow::doFrame()
{
	// recreate swapchain if requested
	if(_swapchainResizePending) {

		// make sure that we finished all the rendering
		// (this is necessary for swapchain re-creation)
		_device.waitIdle();

		// get surface capabilities
		// On Win32, currentExtent, minImageExtent and maxImageExtent are all equal.
		// It means we can create a new swapchain only with imageExtent being equal to the window size.
		// The currentExtent of 0,0 means the window is minimized with the result
		// we cannot create swapchain for such window. If the currentExtent is not 0,0,
		// both width and height must be greater than 0.
		vk::SurfaceCapabilitiesKHR surfaceCapabilities(_physicalDevice.getSurfaceCapabilitiesKHR(_surface));

		// do not allow swapchain creation and rendering when currentExtent is 0,0
		if(surfaceCapabilities.currentExtent == vk::Extent2D(0,0)) {
			return;
		}

		// recreate swapchain
		_swapchainResizePending = false;
		_surfaceExtent = surfaceCapabilities.currentExtent;
		_recreateSwapchainCallback(surfaceCapabilities, _surfaceExtent);
	}

	// render scene
	//cout<<"x"<<flush;
	_framePending = false;
	_frameCallback();
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
			// On Xlib, currentExtent, minImageExtent and maxImageExtent are all equal.
			// It means we can create a new swapchain only with imageExtent being equal to the window size.
			// The currentExtent of 0,0 means the window is minimized with the result
			// we cannot create swapchain for such window. If the currentExtent is not 0,0,
			// both width and height must be greater than 0.
			vk::SurfaceCapabilitiesKHR surfaceCapabilities(_physicalDevice.getSurfaceCapabilitiesKHR(_surface));

			// do not allow swapchain creation and rendering when currentExtent is 0,0
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
		if(_framePending) {

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
		else
			// dispatch events with blocking
			if(wl_display_dispatch(_display) == -1)  // it blocks if there are no events
				throw runtime_error("wl_display_dispatch() failed.");

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


void VulkanWindow::scheduleFrame()
{
	if(m_scheduledFrameCallback)
		return;

	m_scheduledFrameCallback = wl_surface_frame(_wlSurface);
	wl_callback_add_listener(m_scheduledFrameCallback, &m_frameListener, this);
	wl_surface_commit(_wlSurface);
}


void VulkanWindow::doFrame()
{
	// destroy Wayland frame callback
	if(m_scheduledFrameCallback) {
		wl_callback_destroy(m_scheduledFrameCallback);
		m_scheduledFrameCallback = nullptr;
	}

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


#else


static const vector<const char*> requiredInstanceExtensions;

const vector<const char*>& VulkanWindow::requiredExtensions()  { return requiredInstanceExtensions; }
vector<const char*>& VulkanWindow::appendRequiredExtensions(vector<const char*>& v)  { return v; }
uint32_t VulkanWindow::requiredExtensionCount()  { return 0; }
const char* const* VulkanWindow::requiredExtensionNames()  { return nullptr; }
void VulkanWindow::mainLoop()  {}
void VulkanWindow::scheduleFrame()  {}


#endif
