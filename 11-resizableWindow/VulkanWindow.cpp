#include "VulkanWindow.h"
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
# define NOMINMAX  // avoid the definition of min and max macros by windows.h
# include <windows.h>
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
#endif
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <stdexcept>
#include <iostream>  // for debugging

using namespace std;

#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
static const char* windowClassName = "VulkanWindow";
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
#endif



VulkanWindow::~VulkanWindow()
{
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)

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

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

	if(window) {
		DestroyWindow(window);
		UnregisterClass(windowClassName, GetModuleHandle(NULL));
	}

#elif defined(VK_USE_PLATFORM_XLIB_KHR)

	if(window)  XDestroyWindow(display,window);
	if(display)  XCloseDisplay(display);

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
		return instance.createWaylandSurfaceKHR(
				vk::WaylandSurfaceCreateInfoKHR(vk::WaylandSurfaceCreateFlagsKHR(), display, wlSurface)
			);

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

	// initial window size
	RECT screenSize;
	if(GetWindowRect(GetDesktopWindow(), &screenSize)==0)
		throw runtime_error("GetWindowRect() failed.");
	windowWidth  = (screenSize.right - screenSize.left) / 2;
	windowHeight = (screenSize.bottom - screenSize.top) / 2;

	// window's message handling procedure
	auto wndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		switch(msg)
		{
			case WM_SIZE:
				windowWidth  = LOWORD(lParam);
				windowHeight = HIWORD(lParam);
				return DefWindowProc(hwnd, msg, wParam, lParam);
			case WM_CLOSE:
				DestroyWindow(hwnd);
				UnregisterClass(windowClassName, GetModuleHandle(NULL));
				window = nullptr;
				return 0;
			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;
			default:
				return DefWindowProc(hwnd, msg, wParam, lParam);
		}
	};

	// register window class
	WNDCLASSEX wc;
	wc.cbSize        = sizeof(WNDCLASSEX);
	wc.style         = 0;
	wc.lpfnWndProc   = wndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = GetModuleHandle(NULL);
	wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wc.lpszMenuName  = NULL;
	wc.lpszClassName = "HelloWindow";
	wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
	if(!RegisterClassEx(&wc))
		throw runtime_error("Can not register window class.");

	// create window
	window = CreateWindowEx(
		WS_EX_CLIENTEDGE,
		"HelloWindow",
		"Hello window!",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
		NULL, NULL, wc. hInstance, NULL);
	if(window == NULL) {
		UnregisterClass(windowClassName, GetModuleHandle(NULL));
		throw runtime_error("Can not create window.");
	}

	// create surface
	surface =
		instance->createWin32SurfaceKHRUnique(
			vk::Win32SurfaceCreateInfoKHR(vk::Win32SurfaceCreateFlagsKHR(), wc.hInstance, window)
		);

#elif defined(VK_USE_PLATFORM_XLIB_KHR)

	// open X connection
	display = XOpenDisplay(nullptr);
	if(display == nullptr)
		throw runtime_error("Can not open display. No X-server running or wrong DISPLAY variable.");

	// create window
	int blackColor = BlackPixel(display, DefaultScreen(display));
	Screen* screen = XDefaultScreenOfDisplay(display);
	uint32_t windowWidth  = XWidthOfScreen(screen) / 2;
	uint32_t windowHeight = XHeightOfScreen(screen) / 2;
	window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0,
	                             windowWidth, windowHeight, 0, blackColor, blackColor);
	XSetStandardProperties(display, window, "Hello window!", "Hello window!", None, NULL, 0, NULL);
	wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, window, &wmDeleteMessage, 1);
	XMapWindow(display, window);

	// create surface
	surface =
		instance->createXlibSurfaceKHRUnique(
			vk::XlibSurfaceCreateInfoKHR(vk::XlibSurfaceCreateFlagsKHR(), display, window)
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

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

	// show window
	ShowWindow(window,SW_SHOWDEFAULT);

	// run Win32 event loop
	MSG msg;
	while(true){

		// process messages
		while(PeekMessage(&msg,NULL,0,0,PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if(msg.message==WM_QUIT)
			goto ExitMainLoop;

#elif defined(VK_USE_PLATFORM_XLIB_KHR)

	// run Xlib event loop
	XEvent e;
	while(true) {

		// process messages
		while(XPending(display)>0) {
			XNextEvent(display,&e);
			if(e.type==ClientMessage&&ulong(e.xclient.data.l[0])==wmDeleteMessage)
				goto ExitMainLoop;
		}

#endif

		// call
		exposeCallback();

	}
}
