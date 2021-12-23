#include "VulkanWindow.h"
#if defined(USE_PLATFORM_WIN32)
# define NOMINMAX  // avoid the definition of min and max macros by windows.h
# include <windows.h>
# include <tchar.h>
#elif defined(USE_PLATFORM_XLIB)
# include <X11/Xutil.h>
#elif defined(USE_PLATFORM_WAYLAND)
#elif defined(USE_PLATFORM_QT)
# include <QGuiApplication>
# include <qpa/qplatformnativeinterface.h>  // this requires Qt5 private header development files (package on Ubuntu: qtbase5-private-dev), alternative approach would be to use QX11Info class for xcb but I do not see any alternative for Wayland
#elif defined(USE_PLATFORM_SDL)
# include "SDL.h"
# include "SDL_vulkan.h"
#endif
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <stdexcept>
#include <iostream>  // for debugging

using namespace std;

// global variables
#if defined(USE_PLATFORM_WIN32)
static const _TCHAR* windowClassName = _T("VulkanWindow");
static HINSTANCE hInstance;
static uint32_t hwndCounter = 0;
#endif

#if defined(USE_PLATFORM_QT)
class QtVulkanWindow : public QWindow {
public:
	VulkanWindow* m_vulkanWindow;
	QtVulkanWindow(QWindow* parent, VulkanWindow* vulkanWindow) : QWindow(parent), m_vulkanWindow(vulkanWindow)  {}
	void exposeEvent(QExposeEvent* event) override;
};
#endif

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
#if defined(USE_PLATFORM_WIN32)

	// release resources
	// (do not throw in destructor, so ignore the errors in release builds
	// and assert in debug builds)
# ifdef NDEBUG
	if(m_hwnd) {
		DestroyWindow(m_hwnd);
		m_hwnd = nullptr;
		if(--hwndCounter == 0)
			UnregisterClass(windowClassName, hInstance);
	}
# else
	if(m_hwnd) {
		if(!DestroyWindow(m_hwnd))
			assert(0 && "DestroyWindow(): The function failed.");
		m_hwnd = nullptr;
		if(--hwndCounter == 0)
			if(!UnregisterClass(windowClassName, hInstance))
				assert(0 && "UnregisterClass(): The function failed.");
	}
# endif

#elif defined(USE_PLATFORM_XLIB)

	// release resources
	if(m_window) {
		XDestroyWindow(m_display, m_window);
		m_window = 0;
	}
	if(m_display) {
		XCloseDisplay(m_display);
		m_display = nullptr;
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

#elif defined(USE_PLATFORM_QT)

	if(m_window) {
		delete m_window;
		m_window = nullptr;
	}

#elif defined(USE_PLATFORM_SDL)

	if(m_window) {
		SDL_DestroyWindow(m_window);
		m_window = nullptr;
	}
	if(m_sdlInitialized) {
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		m_sdlInitilized = false;
	}

#endif
}


vk::SurfaceKHR VulkanWindow::init(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title)
{
	// destroy any previous window data
	// if init was called multiple times, for example
	destroy();

#if defined(USE_PLATFORM_WIN32)

	// window's message handling procedure
	auto wndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
		switch(msg)
		{
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				try {
					if(!ValidateRect(hwnd, NULL))
						throw runtime_error("ValidateRect(): The function failed.");
					w->onWmPaint();
				} catch(...) {
					w->m_wndProcException = std::current_exception();
				}
				return 0;
			}
			case WM_SIZE: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				try {
					w->m_surfaceExtent.width  = LOWORD(lParam);
					w->m_surfaceExtent.height = HIWORD(lParam);
					w->scheduleSwapchainResize();
					if(!InvalidateRect(hwnd, NULL, FALSE))
						throw runtime_error("InvalidateRect(): The function failed.");
				} catch(...) {
					w->m_wndProcException = std::current_exception();
				}
				return DefWindowProc(hwnd, msg, wParam, lParam);
			}
			case WM_CLOSE: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				if(!DestroyWindow(hwnd))
					w->m_wndProcException = make_exception_ptr(runtime_error("DestroyWindow(): The function failed."));
				w->m_hwnd = nullptr;
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
	if(hwndCounter == 0)
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
	m_hwnd = CreateWindowEx(
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
	if(m_hwnd == NULL) {
		if(hwndCounter == 0)
			if(!UnregisterClass(windowClassName, hInstance))
				assert(0 && "UnregisterClass(): The function failed.");
		throw runtime_error("CreateWindowEx(): The function failed.");
	}
	hwndCounter++;

	// store this pointer with the window data
	SetWindowLongPtr(m_hwnd, 0, (LONG_PTR)this);

	// create surface
	return
		instance.createWin32SurfaceKHR(
			vk::Win32SurfaceCreateInfoKHR(vk::Win32SurfaceCreateFlagsKHR(), hInstance, m_hwnd)
		);

#elif defined(USE_PLATFORM_XLIB)

	// set surface extent
	m_surfaceExtent = surfaceExtent;

	// open X connection
	m_display = XOpenDisplay(nullptr);
	if(m_display == nullptr)
		throw runtime_error("Can not open display. No X-server running or wrong DISPLAY variable.");

	// create window
	XSetWindowAttributes attr;
	attr.event_mask = ExposureMask | StructureNotifyMask | VisibilityChangeMask;
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

#elif defined(USE_PLATFORM_WAYLAND)

	// no multiple init attempts
	if(m_display)
		throw runtime_error("Multiple VulkanWindow::init() calls are not allowed.");

	// set surface extent
	m_surfaceExtent = surfaceExtent;

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
	return
		instance.createWaylandSurfaceKHR(
			vk::WaylandSurfaceCreateInfoKHR(vk::WaylandSurfaceCreateFlagsKHR(), m_display, m_wlSurface)
		);

#elif defined(USE_PLATFORM_QT)

	m_window = new QtVulkanWindow(nullptr, this);
	//m_window->setSurfaceType(QSurface::VulkanSurface);
	m_window->show();

# if _WIN32

	return
		instance.createWin32SurfaceKHR(
			vk::Win32SurfaceCreateInfoKHR(
				vk::Win32SurfaceCreateFlagsKHR(),  // flags
				GetModuleHandle(NULL),  // hinstance
				m_window.WId())  // hwnd
		);

# else

	QString platform = QGuiApplication::platformName();
	QPlatformNativeInterface* ni = QGuiApplication::platformNativeInterface();
	if(platform == "wayland")
		return
			instance.createWaylandSurfaceKHR(
				vk::WaylandSurfaceCreateInfoKHR(
					vk::WaylandSurfaceCreateFlagsKHR(),  // flags
					reinterpret_cast<wl_display*>(ni->nativeResourceForWindow("display", NULL)),  // display
					reinterpret_cast<wl_surface*>(ni->nativeResourceForWindow("surface", m_window))  // surface
				)
			);
	else if(platform == "xcb")
		return
			instance.createXcbSurfaceKHR(
				vk::XcbSurfaceCreateInfoKHR(
					vk::XcbSurfaceCreateFlagsKHR(),  // flags
					reinterpret_cast<xcb_connection_t*>(ni->nativeResourceForIntegration("connection")),  // connection
					m_window->winId()  // window
				)
			);
	else
		throw runtime_error("VulkanWindow::requiredExtensions(): Unknown Qt platform.");

# endif

#elif defined(USE_PLATFORM_SDL)

	// allow screensaver
	SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");

	// init SDL video subsystem
	if(SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
		runtime_error("VulkanWindow: SDL_InitSubSystem(SDL_INIT_VIDEO) function failed.");
	m_sdlInitialized = true;

	// create Vulkan window
	m_window = SDL_CreateWindow(
		title,  // title
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,  // x,y
		surfaceExtent.width, surfaceExtent.height,  // w,h
		SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE  // flags
	);
	if(m_window == nullptr)
		throw runtime_error("VulkanWindow: SDL_CreateWindow() function failed.");

	// create surface
	VkSurfaceKHR s;
	if(!SDL_Vulkan_CreateSurface(m_window, instance, &s))
		throw runtime_error("VulkanWindow: SDL_Vulkan_CreateSurface() function failed.");

	return s;

#endif
}


#if defined(USE_PLATFORM_WIN32)

void VulkanWindow::mainLoop(vk::PhysicalDevice physicalDevice, vk::Device device, vk::SurfaceKHR surface, FrameCallback frameCallback)
{
	// channel the arguments into onWmPaint()
	m_physicalDevice = physicalDevice;
	m_device = device;
	m_surface = surface;
	m_frameCallback = frameCallback;

	// show window
	ShowWindow(m_hwnd, SW_SHOWDEFAULT);

	// run Win32 event loop
	MSG msg;
	BOOL r;
	m_wndProcException = nullptr;
	while((r = GetMessage(&msg, NULL, 0, 0)) != 0) {
		if(r == -1)
			throw runtime_error("GetMessage(): The function failed.");
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		if(m_wndProcException)  // handle exceptions raised in window procedure
			rethrow_exception(m_wndProcException);
	}
}


void VulkanWindow::onWmPaint()
{
	if(m_swapchainResizePending) {

		// make sure that we finished all the rendering
		// (this is necessary for swapchain re-creation)
		m_device.waitIdle();

		// get surface capabilities
		// On Win32, currentExtent, minImageExtent and maxImageExtent are all equal.
		// It means we can create a new swapchain only with imageExtent being equal
		// to the window size.
		// The currentExtent of 0,0 means the window is minimized with the result
		// we cannot create swapchain for such window. If the currentExtent is not 0,0,
		// both width and height must be greater than 0.
		vk::SurfaceCapabilitiesKHR surfaceCapabilities(m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface));

		// do not allow swapchain creation and rendering when currentExtent is 0,0
		if(surfaceCapabilities.currentExtent == vk::Extent2D(0,0))
			return;

		// recreate swapchain
		m_swapchainResizePending = false;
		m_recreateSwapchainCallback(surfaceCapabilities, m_surfaceExtent);
	}

	// render scene
	m_frameCallback();
}


void VulkanWindow::scheduleNextFrame()
{
	InvalidateRect(m_hwnd, NULL, FALSE);
}


#elif defined(USE_PLATFORM_XLIB)


void VulkanWindow::mainLoop(vk::PhysicalDevice physicalDevice, vk::Device device, vk::SurfaceKHR surface, FrameCallback frameCallback)
{
	// run Xlib event loop
	XEvent e;
	while(true) {

		// get number of events in the queue
		int numEvents = XPending(m_display);
		if(numEvents == 0)
			if(m_framePending && m_visible)
				goto skipMessageLoop;  // render frame
			else
				numEvents = 1;  // want for events

		// process events
		do {

			for(int i=0; i<numEvents; i++) {

				// get event
				XNextEvent(m_display, &e);

				if(e.type==Expose) {
					cout<<"e"<<flush;
					m_framePending = true;
					continue;
				}

				if(e.type==ConfigureNotify) {
					if(e.xconfigure.width!=m_surfaceExtent.width || e.xconfigure.height!=m_surfaceExtent.height) {
						cout<<"Configure "<<e.xconfigure.width<<"x"<<e.xconfigure.height<<endl;
						m_framePending = true;
						m_swapchainResizePending = true;
					}
					continue;
				}

				if(e.type==MapNotify || (e.type==VisibilityNotify && e.xvisibility.state!=VisibilityFullyObscured)) {
					m_visible = true;
					m_framePending = true;
					if(e.type==MapNotify) cout<<"m"<<flush;
					else cout<<"v"<<flush;
					continue;
				}
				if(e.type==UnmapNotify || (e.type==VisibilityNotify && e.xvisibility.state==VisibilityFullyObscured)) {
					m_visible = false;
					if(e.type==UnmapNotify) cout<<"u"<<flush;
					else cout<<"o"<<flush;
					continue;
				}

				// handle window close
				if(e.type==ClientMessage && ulong(e.xclient.data.l[0])==m_wmDeleteMessage)
					return;
			}

			// if more events came in the mean time, handle them as well
			numEvents = XPending(m_display);
		} while(numEvents > 0);

	skipMessageLoop:

		// render window
		if(m_framePending && m_visible) {

			if(m_swapchainResizePending) {

				// make sure that we finished all the rendering
				// (this is necessary for swapchain re-creation)
				device.waitIdle();

				// get surface capabilities
				// On Xlib, currentExtent, minImageExtent and maxImageExtent are all equal.
				// It means we can create a new swapchain only with imageExtent being equal
				// to the window size.
				// The currentExtent of 0,0 means the window is minimized with the result
				// we cannot create swapchain for such window. If the currentExtent is not 0,0,
				// both width and height must be greater than 0.
				vk::SurfaceCapabilitiesKHR surfaceCapabilities(physicalDevice.getSurfaceCapabilitiesKHR(surface));

				// do not allow swapchain creation and rendering when currentExtent is 0,0
				// (this never happened on my KDE 5.80.0 (Kubuntu 21.04) and KDE 5.44.0 (Kubuntu 18.04.5);
				// window minimalizing just unmaps the window)
				if(surfaceCapabilities.currentExtent == vk::Extent2D(0,0))
					continue;

				// recreate swapchain
				m_swapchainResizePending = false;
				m_surfaceExtent = surfaceCapabilities.currentExtent;
				m_recreateSwapchainCallback(surfaceCapabilities, m_surfaceExtent);
			}

			// render scene
			cout<<"expose"<<m_surfaceExtent.width<<"x"<<m_surfaceExtent.height<<endl;
			m_framePending = false;
			frameCallback();

		}
	}
}


void VulkanWindow::scheduleNextFrame()
{
	if(m_framePending)
		return;

	m_framePending = true;
	/*if(m_visible) {
		XSendEvent(
			m_display,
			m_window,
			False,
			ExposureMask,
			(XEvent*)&(const XExposeEvent&)
				XExposeEvent{
					Expose,
					0,
					True,
					m_display,
					m_window,
					0,0,
					0,0,
					0
				}
		);
		cout<<"s"<<flush;
	}*/
}


#elif defined(USE_PLATFORM_WAYLAND)


void VulkanWindow::mainLoop(vk::PhysicalDevice physicalDevice, vk::Device device, vk::SurfaceKHR surface, FrameCallback frameCallback)
{
	// channel the arguments into frame callback
	m_physicalDevice = physicalDevice;
	m_device = device;
	m_surface = surface;
	m_frameCallback = frameCallback;

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
			else
				throw runtime_error("VulkanWindow::requiredExtensions(): Unknown Qt platform.");
		}();

	return l;
}

void VulkanWindow::appendRequiredExtensions(vector<const char*>& v)  { auto& l=requiredExtensions(); v.reserve(v.size()+l.size()); for(auto s : l) v.push_back(s); }
uint32_t VulkanWindow::requiredExtensionCount()  { return requiredExtensions().size(); }
const char* const* VulkanWindow::requiredExtensionNames()  { return requiredExtensions().data(); }


void VulkanWindow::mainLoop(vk::PhysicalDevice physicalDevice, vk::Device device, vk::SurfaceKHR surface,
                            const function<FrameCallback>& frameCallback)
{
	// channel the arguments into onFrame()
	m_physicalDevice = physicalDevice;
	m_device = device;
	m_surface = surface;
	m_frameCallback = &frameCallback;

	QGuiApplication::exec();
}

void VulkanWindow::doFrame()
{
	if(m_swapchainResizePending) {

		// make sure that we finished all the rendering
		// (this is necessary for swapchain re-creation)
		m_device.waitIdle();

		// get surface capabilities
		vk::SurfaceCapabilitiesKHR surfaceCapabilities(m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface));

		// do not allow swapchain creation and rendering when currentExtent is 0,0
		if(surfaceCapabilities.currentExtent == vk::Extent2D(0,0))
			return;

		// recreate swapchain
		m_swapchainResizePending = false;
		QSize s = m_window->size();
		m_surfaceExtent = vk::Extent2D(s.width(), s.height());
		m_recreateSwapchainCallback(surfaceCapabilities, m_surfaceExtent);
	}

	// render scene
	cout<<"expose"<<m_surfaceExtent.width<<"x"<<m_surfaceExtent.height<<endl;
	(*m_frameCallback)();
}

void QtVulkanWindow::exposeEvent(QExposeEvent* event)
{
	if(isExposed())
		m_vulkanWindow->doFrame();
}

void VulkanWindow::scheduleSwapchainResize()
{
	m_swapchainResizePending = true;
	m_window->requestUpdate();
}

void VulkanWindow::scheduleNextFrame()
{
	m_window->requestUpdate();
}

#elif defined(USE_PLATFORM_SDL)


const vector<const char*>& VulkanWindow::requiredExtensions()
{
	static vector<const char*> l =
		[]() {

			// init SDL video subsystem
			struct SDL {
				SDL() {
					if(SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
						runtime_error("VulkanWindow: SDL_InitSubSystem(SDL_INIT_VIDEO) function failed.");
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
				throw runtime_error("VulkanWindow: SDL_CreateWindow() function failed.");

			// get number of required instance extensions
			unsigned int count;
			if(!SDL_Vulkan_GetInstanceExtensions(tmpWindow.get(), &count, nullptr))
				throw runtime_error("VulkanWindow: SDL_Vulkan_GetInstanceExtensions() function failed.");

			// get required instance extensions
			vector<const char*> l;
			l.reserve(count);
			l.resize(count);
			while(!SDL_Vulkan_GetInstanceExtensions(tmpWindow.get(), &count, l.data())) {}

			return l;

		}();

	return l;
}

void VulkanWindow::appendRequiredExtensions(vector<const char*>& v)  { auto& l=requiredExtensions(); v.reserve(v.size()+l.size()); for(auto s : l) v.push_back(s); }
uint32_t VulkanWindow::requiredExtensionCount()  { return requiredExtensions().size(); }
const char* const* VulkanWindow::requiredExtensionNames()  { return requiredExtensions().data(); }


void VulkanWindow::mainLoop(vk::PhysicalDevice physicalDevice, vk::Device device, vk::SurfaceKHR surface, FrameCallback frameCallback)
{
	SDL_Event event;
	while(true) {

		int haveEvents = SDL_PollEvent(&event);
		if(!haveEvents)
			if(m_framePending && m_visible)
				goto skipMessageLoop;  // render frame
			else
				SDL_WaitEvent(&event);  // want for events

		do {
			switch(event.type) {
			case SDL_WINDOWEVENT:
				switch(event.window.event) {

				case SDL_WINDOWEVENT_EXPOSED:
					cout<<"Exposed."<<endl;
					m_framePending = true;
					break;

				case SDL_WINDOWEVENT_SIZE_CHANGED:
					cout<<"Resize."<<endl;
					m_framePending = true;
					m_swapchainResizePending = true;
					break;

				case SDL_WINDOWEVENT_SHOWN:
					cout<<"Show."<<endl;
					m_visible = true;
					break;

				case SDL_WINDOWEVENT_HIDDEN:
					cout<<"Hide."<<endl;
					m_visible = false;
					break;

				}
				break;

			case SDL_QUIT:
				return;
			}
		} while(SDL_PollEvent(&event));

	skipMessageLoop:

		// render window
		if(m_framePending && m_visible) {

			if(m_swapchainResizePending) {

				// make sure that we finished all the rendering
				// (this is necessary for swapchain re-creation)
				device.waitIdle();

				// get surface capabilities
				vk::SurfaceCapabilitiesKHR surfaceCapabilities(physicalDevice.getSurfaceCapabilitiesKHR(surface));

				// do not allow swapchain creation and rendering when currentExtent is 0,0
				if(surfaceCapabilities.currentExtent == vk::Extent2D(0,0))
					continue;

				// recreate swapchain
				m_swapchainResizePending = false;
				m_surfaceExtent = surfaceCapabilities.currentExtent;
				m_recreateSwapchainCallback(surfaceCapabilities, m_surfaceExtent);
			}

			// render scene
			cout<<"expose"<<m_surfaceExtent.width<<"x"<<m_surfaceExtent.height<<endl;
			m_framePending = false;
			frameCallback();

		}
	}

}


void VulkanWindow::scheduleSwapchainResize()  { m_swapchainResizePending = true; m_framePending = true; }
void VulkanWindow::scheduleNextFrame()
{
	if(m_framePending)
		return;

	m_framePending = true;
	/*if(m_visible) {
		SDL_Event event;
		event.type = SDL_WINDOWEVENT;
		event.window.event = SDL_WINDOWEVENT_EXPOSED;
		SDL_PushEvent(&event);
	}*/
}

#endif
