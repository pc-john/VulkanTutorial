#include "VulkanWindow.h"
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
# include <X11/Xutil.h>
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
# define NOMINMAX  // avoid the definition of min and max macros by windows.h
# include <windows.h>
# include <tchar.h>
#endif
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <stdexcept>
#include <iostream>  // for debugging

using namespace std;

#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
static const _TCHAR* windowClassName = _T("VulkanWindow");
static HINSTANCE hInstance;
static uint32_t numVulkanWindows = 0;
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
#endif

#if defined(VK_USE_PLATFORM_WIN32_KHR) && defined(_UNICODE)
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



VulkanWindow::~VulkanWindow()
{
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)

	// release resources
	if(xdgTopLevel)
		xdg_toplevel_destroy(xdgTopLevel);
	if(xdgSurface)
		xdg_surface_destroy(xdgSurface);
	if(wlSurface)
		wl_surface_destroy(wlSurface);
	if(xdg)
		xdg_wm_base_destroy(xdg);
	if(display)
		wl_display_disconnect(display);

#elif defined(VK_USE_PLATFORM_XLIB_KHR)

	// release resources
	if(m_window)  XDestroyWindow(m_display, m_window);
	if(m_display)  XCloseDisplay(m_display);

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

	// release resources
	// (do not throw in destructor, so ignore the errors in release builds
	// and assert in debug builds)
#ifdef NDEBUG
	if(hwnd)
		DestroyWindow(hwnd);
	if(--numVulkanWindows == 0)
		UnregisterClass(windowClassName, hInstance);
#else
	if(hwnd)
		if(!DestroyWindow(hwnd))
			assert(0 && "DestroyWindow(): The function failed.");
	if(--numVulkanWindows == 0)
		if(!UnregisterClass(windowClassName, hInstance))
			assert(0 && "UnregisterClass(): The function failed.");
#endif

#endif
}


vk::SurfaceKHR VulkanWindow::init(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title)
{
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)

	// set surface extent
	m_surfaceExtent = surfaceExtent;

	// open Wayland connection
	display = wl_display_connect(nullptr);
	if(display == nullptr)
		throw runtime_error("Cannot connect to Wayland display. No Wayland server is running or invalid WAYLAND_DISPLAY variable.");

	// registry listener
	registry = wl_display_get_registry(display);
	if(registry == nullptr)
		throw runtime_error("Cannot get Wayland registry object.");
	compositor = nullptr;
	xdg = nullptr;
	registryListener = {
		.global =
			[](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(data);
				if(strcmp(interface, wl_compositor_interface.name) == 0)
					w->compositor = static_cast<wl_compositor*>(
						wl_registry_bind(registry, name, &wl_compositor_interface, 1));
				else if(strcmp(interface, xdg_wm_base_interface.name) == 0)
					w->xdg = static_cast<xdg_wm_base*>(
						wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
				else if(strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
					w->zxdgDecorationManagerV1 = static_cast<zxdg_decoration_manager_v1*>(
						wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
			},
		.global_remove =
			[](void*, wl_registry*, uint32_t) {
			},
	};
	if(wl_registry_add_listener(registry, &registryListener, this))
		throw runtime_error("wl_registry_add_listener() failed.");

	// get and init global objects
	if(wl_display_roundtrip(display) == -1)
		throw runtime_error("wl_display_roundtrip() failed.");
	if(compositor == nullptr)
		throw runtime_error("Cannot get Wayland compositor object.");
	if(xdg == nullptr)
		throw runtime_error("Cannot get Wayland xdg_wm_base object.");
	xdgWmBaseListener = {
		.ping =
			[](void*, xdg_wm_base* xdg, uint32_t serial) {
				xdg_wm_base_pong(xdg, serial);
			}
	};
	if(xdg_wm_base_add_listener(xdg, &xdgWmBaseListener, nullptr))
		throw runtime_error("xdg_wm_base_add_listener() failed.");

	// create window
	wlSurface = wl_compositor_create_surface(compositor);
	if(wlSurface == nullptr)
		throw runtime_error("wl_compositor_create_surface() failed.");
	xdgSurface = xdg_wm_base_get_xdg_surface(xdg, wlSurface);
	if(xdgSurface == nullptr)
		throw runtime_error("xdg_wm_base_get_xdg_surface() failed.");
	xdgSurfaceListener = {
		.configure =
			[](void* data, xdg_surface* xdgSurface, uint32_t serial) {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(data);
				cout<<"surface configure "<<serial<<endl;
				xdg_surface_ack_configure(xdgSurface, serial);
				wl_surface_commit(w->wlSurface);
			},
	};
	if(xdg_surface_add_listener(xdgSurface, &xdgSurfaceListener, this))
		throw runtime_error("xdg_surface_add_listener() failed.");

	// init xdg toplevel
	xdgTopLevel = xdg_surface_get_toplevel(xdgSurface);
	if(xdgTopLevel == nullptr)
		throw runtime_error("xdg_surface_get_toplevel() failed.");
	if(zxdgDecorationManagerV1) {
		zxdg_toplevel_decoration_v1* decoration =
			zxdg_decoration_manager_v1_get_toplevel_decoration(zxdgDecorationManagerV1, xdgTopLevel);
		zxdg_toplevel_decoration_v1_set_mode(decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	xdg_toplevel_set_title(xdgTopLevel, title);
	running = true;
	xdgToplevelListener = {
		.configure =
			[](void* data, xdg_toplevel* toplevel, int32_t width, int32_t height, wl_array*) -> void {
				cout<<"toplevel configure "<<width<<"x"<<height<<endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(data);
				if(width != 0) {
					w->m_surfaceExtent.width = width;
					if(height != 0)
						w->m_surfaceExtent.height = height;
					w->swapchainResizePending = true;
				}
				else if(height != 0) {
					w->m_surfaceExtent.height = height;
					w->swapchainResizePending = true;
				}
			},
		.close =
			[](void* data, xdg_toplevel* xdgTopLevel) {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(data);
				w->running = false;
			},
	};
	if(xdg_toplevel_add_listener(xdgTopLevel, &xdgToplevelListener, this))
		throw runtime_error("xdg_toplevel_add_listener() failed.");
	wl_surface_commit(wlSurface);
	if(wl_display_flush(display) == -1)
		throw runtime_error("wl_display_flush() failed.");

	// create surface
	return
		instance.createWaylandSurfaceKHR(
			vk::WaylandSurfaceCreateInfoKHR(vk::WaylandSurfaceCreateFlagsKHR(), display, wlSurface)
		);

#elif defined(VK_USE_PLATFORM_XLIB_KHR)

	// set surface extent
	m_surfaceExtent = surfaceExtent;

	// open X connection
	m_display = XOpenDisplay(nullptr);
	if(m_display == nullptr)
		throw runtime_error("Can not open display. No X-server running or wrong DISPLAY variable.");

	// create window
	XSetWindowAttributes attr;
	attr.event_mask = StructureNotifyMask | VisibilityChangeMask;
	m_window =
		XCreateWindow(
			m_display,  // display
			DefaultRootWindow(m_display),  // parent
			0, 0,  // x,y
			m_surfaceExtent.width, m_surfaceExtent.height,  // width, height
			0,  // border_width
			CopyFromParent,  // depth
			InputOutput,  // class
			CopyFromParent,  // visual
			CWEventMask,  // valuemask
			&attr  // attributes
		);
	XSetStandardProperties(m_display, m_window, title, title, None, NULL, 0, NULL);
	m_wmDeleteMessage = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(m_display, m_window, &m_wmDeleteMessage, 1);
	XMapWindow(m_display, m_window);

	// create surface
	return
		instance.createXlibSurfaceKHR(
			vk::XlibSurfaceCreateInfoKHR(vk::XlibSurfaceCreateFlagsKHR(), m_display, m_window)
		);

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

	// window's message handling procedure
	auto wndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		switch(msg)
		{
			case WM_SIZE: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				w->m_surfaceExtent.width  = LOWORD(lParam);
				w->m_surfaceExtent.height = HIWORD(lParam);
				w->scheduleSwapchainResize();
				return DefWindowProc(hwnd, msg, wParam, lParam);
			}
			case WM_CLOSE: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				if(!DestroyWindow(hwnd))
					throw runtime_error("DestroyWindow(): The function failed.");
				w->hwnd = nullptr;
				return 0;
			}
			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;
			default:
				return DefWindowProc(hwnd, msg, wParam, lParam);
		}
	};

	// register window class with the first window
	if(++numVulkanWindows == 1)
	{
		hInstance = GetModuleHandle(NULL);

		WNDCLASSEX wc;
		wc.cbSize        = sizeof(WNDCLASSEX);
		wc.style         = 0;
		wc.lpfnWndProc   = wndProc;
		wc.cbClsExtra    = 0;
		wc.cbWndExtra    = sizeof(LONG_PTR);
		wc.hInstance     = hInstance;
		wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
		wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
		wc.lpszMenuName  = NULL;
		wc.lpszClassName = windowClassName;
		wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
		if(!RegisterClassEx(&wc))
			throw runtime_error("RegisterClassEx(): The function failed.");
	}

	// create window
	hwnd = CreateWindowEx(
		WS_EX_CLIENTEDGE,  // dwExStyle
		windowClassName,  // lpClassName
	#if _UNICODE
		utf8toWString(title).c_str(),  // lpWindowName
	#else
		title,  // lpWindowName
	#endif
		WS_OVERLAPPEDWINDOW,  // dwStyle
		CW_USEDEFAULT, CW_USEDEFAULT,  // x,y
		surfaceExtent.width, surfaceExtent.height,  // width, height
		NULL, NULL, hInstance, NULL);  // hWndParent, hMenu, hInstance, lpParam
	if(hwnd == NULL)
		throw runtime_error("CreateWindowEx(): The function failed.");

	// store this pointer with the window data
	SetWindowLongPtr(hwnd, 0, (LONG_PTR)this);

	// create surface
	return 
		instance.createWin32SurfaceKHR(
			vk::Win32SurfaceCreateInfoKHR(vk::Win32SurfaceCreateFlagsKHR(), hInstance, hwnd)
		);

#endif
}


void VulkanWindow::mainLoop(vk::PhysicalDevice physicalDevice, vk::Device device, vk::SurfaceKHR surface, ExposeCallback exposeCallback)
{
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)

	if(wl_display_roundtrip(display) == -1)
		throw runtime_error("wl_display_roundtrip() failed.");

	while(running) {
		if(wl_display_dispatch_pending(display) == -1)
			throw std::runtime_error("wl_display_dispatch_pending() failed.");

		if(swapchainResizePending) {

			swapchainResizePending = false;

			// get surface capabilities
			// On Wayland, currentExtent is 0xffffffff, 0xffffffff with the meaning that the extent
			// will be determined by the extent of a swapchain,
			// minImageExtent is 1,1 and maxImageExtent is the maximum supported surface size.
			vk::SurfaceCapabilitiesKHR surfaceCapabilities(physicalDevice.getSurfaceCapabilitiesKHR(surface));

			// recreate swapchain
			device.waitIdle();
			recreateSwapchainCallback(surfaceCapabilities, m_surfaceExtent);
		}

#elif defined(VK_USE_PLATFORM_XLIB_KHR)

	// run Xlib event loop
	XEvent e;
	while(true) {

		// process messages
		while(!m_visible ||  // keep spinning the loop if window is hidden (XNextEvent will block whenever no more events)
		      XPending(m_display)>0)  // if window is visible, process all events and then leave the loop for rendering
		{
			// process event
		waitNextEvent:
			XNextEvent(m_display, &e);

			// handle window close
			if(e.type==ClientMessage && ulong(e.xclient.data.l[0])==m_wmDeleteMessage)
				return;

			// handle visibility and mapping changes
			if(e.type==MapNotify || (e.type==VisibilityNotify && e.xvisibility.state!=VisibilityFullyObscured))
				m_visible = true;
			if(e.type==UnmapNotify || (e.type==VisibilityNotify && e.xvisibility.state==VisibilityFullyObscured))
				m_visible = false;
		}

		if(m_swapchainResizePending) {

			// get surface capabilities
			// On Xlib, currentExtent, minImageExtent and maxImageExtent are all equal.
			// It means we can create a new swapchain only with imageExtent being equal
			// to the window size.
			// The currentExtent of 0,0 means the window is minimized with the result
			// we cannot create swapchain for such window. If the currentExtent is not 0,0,
			// both width and height must be greater than 0.
			vk::SurfaceCapabilitiesKHR surfaceCapabilities(physicalDevice.getSurfaceCapabilitiesKHR(surface));

			// do not allow swapchain creation and rendering when currentExtent is 0,0
			// (this never happened on my KDE 5.80.0 (Kubuntu 21.04) and KDE 5.44.0 (Kubuntu 18.04.5),
			// window minimalizing just unmaps the window)
			if(surfaceCapabilities.currentExtent == vk::Extent2D(0,0))
				goto waitNextEvent;

			// recreate swapchain
			device.waitIdle();
			m_swapchainResizePending = false;
			m_surfaceExtent = surfaceCapabilities.currentExtent;
			m_recreateSwapchainCallback(surfaceCapabilities, m_surfaceExtent);
		}

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

	// show window
	ShowWindow(hwnd, SW_SHOWDEFAULT);

	// run Win32 event loop
	MSG msg;
	while(true){

		// process messages
		while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if(msg.message == WM_QUIT)
				return;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if(swapchainResizePending) {

			// get surface capabilities
			// On Win32, currentExtent, minImageExtent and maxImageExtent are all equal.
			// It means we can create a new swapchain only with imageExtent being equal
			// to the window size.
			// The currentExtent of 0,0 means the window is minimized with the result
			// we cannot create swapchain for such window. If the currentExtent is not 0,0,
			// both width and height must be greater than 0.
			vk::SurfaceCapabilitiesKHR surfaceCapabilities(physicalDevice.getSurfaceCapabilitiesKHR(surface));
			
			// do not allow swapchain creation and rendering when currentExtent is 0,0
			if(surfaceCapabilities.currentExtent == vk::Extent2D(0,0)) {
				if(!WaitMessage())  // make application sleep until there is a message to process
					throw runtime_error("WaitMessage(): The function failed.");
				continue;
			}

			// recreate swapchain
			swapchainResizePending = false;
			device.waitIdle();
			recreateSwapchainCallback(surfaceCapabilities, m_surfaceExtent);
		}

#endif

		// call
		exposeCallback();

	}
}
