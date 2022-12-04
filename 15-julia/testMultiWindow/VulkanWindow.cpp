#include "VulkanWindow.h"
#if defined(USE_PLATFORM_WIN32)
# define NOMINMAX  // avoid the definition of min and max macros by windows.h
# include <windows.h>
# include <algorithm>
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
# include <filesystem>
# include <fstream>
#endif
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <stdexcept>
#include <iostream>  // for debugging

using namespace std;


#if defined(USE_PLATFORM_WIN32)

// Win32 utf8 string to wstring conversion
# if defined(_UNICODE)
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
# endif

// list of windows waiting for frame rendering
// (the windows have _framePendingState set to FramePendingState::Pending or TentativePending)
static vector<VulkanWindow*> framePendingWindows;
#endif


// Xlib global variables
#if defined(USE_PLATFORM_XLIB)
static bool externalDisplayHandle;
#endif


// Wayland global variables
#if defined(USE_PLATFORM_WAYLAND)
static bool externalDisplayHandle;
#endif


// SDL global variables
#if defined(USE_PLATFORM_SDL)
static bool sdlInitialized = false;
static bool running;
static constexpr const char* windowPointerName = "VulkanWindow";
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

// bool indicating that application is running and it shall not leave main loop
static bool running;

// list of windows waiting for frame rendering
// (the windows have _framePendingState set to FramePendingState::Pending or TentativePending)
static vector<VulkanWindow*> framePendingWindows;
#endif


#if defined(USE_PLATFORM_QT)

// QtRenderingWindow is customized QWindow class for Vulkan rendering
class QtRenderingWindow : public QWindow {
public:
	VulkanWindow* vulkanWindow;
	int timer = 0;
	QtRenderingWindow(QWindow* parent, VulkanWindow* vulkanWindow_) : QWindow(parent), vulkanWindow(vulkanWindow_)  {}
	bool event(QEvent* event) override;
};

// Qt global variables
static std::aligned_storage<sizeof(QGuiApplication), alignof(QGuiApplication)>::type qGuiApplicationMemory;
static QGuiApplication* qGuiApplication = nullptr;
static std::aligned_storage<sizeof(QVulkanInstance), alignof(QVulkanInstance)>::type qVulkanInstanceMemory;
static QVulkanInstance* qVulkanInstance = nullptr;

# if !defined(_WIN32)
// alternative command line arguments
// (if the user does not use VulkanWindow::init(argc, argv),
// we get command line arguments by various API functions)
static vector<char> altArgBuffer;
static vector<char*> altArgv;
static int altArgc;
# endif

#endif



void VulkanWindow::init()
{
#if defined(USE_PLATFORM_WIN32)

	// handle multiple init attempts
	if(_windowClass)
		return;

	// window's message handling procedure
	auto wndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept -> LRESULT {
		switch(msg)
		{
			// erase background message
			// (we ignore the message)
			case WM_ERASEBKGND:
				return 1;  // returning non-zero means that background should be considered erased

			// paint the window message
			// (we render the window content here)
			case WM_PAINT: {

				// render all windows in framePendingWindows list
				// (if a window keeps its window area invalidated,
				// WM_PAINT might be constantly received by this single window only,
				// starving all other windows => we render all windows in framePendingWindows list)
				VulkanWindow* window = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				bool windowProcessed = false;
				for(size_t i=0; i<framePendingWindows.size(); ) {
					
					VulkanWindow* w = framePendingWindows[i];
					if(w == window)
						windowProcessed = true;

					// render frame
					w->_framePendingState = FramePendingState::TentativePending;
					w->renderFrame();

					// was frame scheduled again?
					// (it might be rescheduled again in renderFrame())
					if(w->_framePendingState == FramePendingState::TentativePending) {

						// validate window area
						if(!ValidateRect(w->_hwnd, NULL))
							thrownException = make_exception_ptr(runtime_error("ValidateRect(): The function failed."));

						// update state to no-frame-pending
						w->_framePendingState = FramePendingState::NotPending;
						if(framePendingWindows.size() == 1) {
							framePendingWindows.clear();  // all iterators are invalidated
							break;
						}
						else {
							framePendingWindows[i] = framePendingWindows.back();
							framePendingWindows.pop_back();  // end() iterator is invalidated
							continue;
						}
					}
					i++;

				}

				// if window was not in framePendingWindows, process it
				if(!windowProcessed) {

					// validate window area
					if(!ValidateRect(hwnd, NULL))
						thrownException = make_exception_ptr(runtime_error("ValidateRect(): The function failed."));

					// render frame
					// (_framePendingState is No, but scheduleFrame() might be called inside renderFrame())
					window->renderFrame();

				}

				return 0;
			}

			// left mouse button down on non-client window area message
			// (application freeze for several hundred of milliseconds is workarounded here)
			case WM_NCLBUTTONDOWN: {
				// This is a workaround for window freeze for several hundred of milliseconds
				// if you click on its title bar. The clicking on the title bar
				// makes execution of DefWindowProc to not return for several hundreds of milliseconds,
				// making the application frozen for this time period.
				// Sending extra WM_MOUSEMOVE workarounds the problem.
				POINT point;
				GetCursorPos(&point);
				ScreenToClient(hwnd, &point);
				PostMessage(hwnd, WM_MOUSEMOVE, 0, point.x | point.y<<16);
				return DefWindowProc(hwnd, msg, wParam, lParam);
			}

			// window resize message
			// (we schedule swapchain resize here)
			case WM_SIZE: {
				cout << "WM_SIZE message (" << LOWORD(lParam) << "x" << HIWORD(lParam) << ")" << endl;
				if(LOWORD(lParam) != 0 && HIWORD(lParam) != 0) {
					VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
					w->scheduleSwapchainResize();
				}
				return DefWindowProc(hwnd, msg, wParam, lParam);
			}

			// window show and hide message
			// (we set _visible variable here)
			case WM_SHOWWINDOW: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				w->_visible = wParam==TRUE;
				return DefWindowProc(hwnd, msg, wParam, lParam);
			}

			// close window message
			// (we call _closeCallback here if registered,
			// otherwise we hide the window and schedule main loop exit)
			case WM_CLOSE: {
				cout << "WM_CLOSE message" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				if(w->_closeCallback)
					w->_closeCallback();
				else {
					w->hide();
					VulkanWindow::exitMainLoop();
				}
				return 0;
			}

			// destroy window message
			// (we make sure that the window is not in framePendingWindows list)
			case WM_DESTROY: {

				cout << "WM_DESTROY message" << endl;

				// remove frame pending state
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				if(w->_framePendingState != FramePendingState::NotPending) {
					w->_framePendingState = FramePendingState::NotPending;
					if(framePendingWindows.size() != 1) {
						for(size_t i=0; i<framePendingWindows.size(); i++)
							if(framePendingWindows[i] == w) {
								framePendingWindows[i] = framePendingWindows.back();
								break;
							}
					}
					framePendingWindows.pop_back();
				}

				return 0;
			}

			// all other messages are handled by standard DefWindowProc()
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

#elif defined(USE_PLATFORM_XLIB)

	if(_display)
		return;

	// open X connection
	_display = XOpenDisplay(nullptr);
	if(_display == nullptr)
		throw runtime_error("Can not open display. No X-server running or wrong DISPLAY variable.");
	externalDisplayHandle = false;

#elif defined(USE_PLATFORM_WAYLAND)

	init(nullptr);

#elif defined(USE_PLATFORM_SDL)

	// handle multiple init attempts
	if(sdlInitialized)
		return;

	// initialize SDL
	if(SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
		throw runtime_error(string("SDL_InitSubSystem(SDL_INIT_VIDEO) function failed. Error details: ") + SDL_GetError());
	sdlInitialized = true;

	// set hints
	SDL_SetHint("SDL_QUIT_ON_LAST_WINDOW_CLOSE", "0");   // do not send SDL_QUIT event when the last window closes; we shall leave main loop
	                                                     // after VulkanWindow::exitMainLoop() is called; hint supported since SDL 2.0.22
	SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");  // allow screensaver

#elif defined(USE_PLATFORM_GLFW)

	// initialize GLFW
	// (it is safe to call glfwInit() multiple times)
	if(!glfwInit())
		throwError("glfwInit");

#elif defined(USE_PLATFORM_QT)

	if(qGuiApplication)
		return;

# if defined(_WIN32)

	// construct QGuiApplication
	qGuiApplication = reinterpret_cast<QGuiApplication*>(&qGuiApplicationMemory);
	new(qGuiApplication) QGuiApplication(__argc, __argv);

# else

	// get command line arguments
	ifstream f("/proc/self/cmdline", ios::binary);
	altArgBuffer.clear();
	int c = f.get();
	while(f) {
		altArgBuffer.push_back(char(c));
		c = f.get();
	}
	if(altArgBuffer.size()==0 || altArgBuffer.back()!='\0')
		altArgBuffer.push_back('\0');
	altArgv.clear();
	altArgv.push_back(&altArgBuffer[0]);
	for(int i=0,c=int(altArgBuffer.size())-1; i<c; i++)
		if(altArgBuffer[i] == '\0')
			altArgv.push_back(&altArgBuffer[i+1]);
	altArgc = int(altArgv.size());
	altArgv.push_back(nullptr);  // argv[argc] must be nullptr

	// construct QGuiApplication
	qGuiApplication = reinterpret_cast<QGuiApplication*>(&qGuiApplicationMemory);
	new(qGuiApplication) QGuiApplication(altArgc, altArgv.data());

# endif

#endif
}


void VulkanWindow::init(void* data)
{
#if defined(USE_PLATFORM_XLIB)

	// use data as Display* handle

	if(_display)
		return;

	if(data) {

		// use data as Display* handle
		_display = reinterpret_cast<Display*>(data);
		externalDisplayHandle = true;

	}
	else {

		// open X connection
		_display = XOpenDisplay(nullptr);
		if(_display == nullptr)
			throw runtime_error("Can not open display. No X-server running or wrong DISPLAY variable.");
		externalDisplayHandle = false;

	}

#elif defined(USE_PLATFORM_WAYLAND)

	// use data as wl_display* handle

	if(_display)
		return;

	if(data) {
		_display = reinterpret_cast<wl_display*>(data);
		externalDisplayHandle = true;
	}
	else {

		// open Wayland connection
		_display = wl_display_connect(nullptr);
		if(_display == nullptr)
			throw runtime_error("Cannot connect to Wayland display. No Wayland server is running or invalid WAYLAND_DISPLAY variable.");
		externalDisplayHandle = false;

	}

	// registry listener
	_registry = wl_display_get_registry(_display);
	if(_registry == nullptr)
		throw runtime_error("Cannot get Wayland registry object.");
	_registryListener = {
		.global =
			[](void*, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
				if(strcmp(interface, wl_compositor_interface.name) == 0)
					_compositor = static_cast<wl_compositor*>(
						wl_registry_bind(registry, name, &wl_compositor_interface, 1));
				else if(strcmp(interface, xdg_wm_base_interface.name) == 0)
					_xdgWmBase = static_cast<xdg_wm_base*>(
						wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
				else if(strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
					_zxdgDecorationManagerV1 = static_cast<zxdg_decoration_manager_v1*>(
						wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
			},
		.global_remove =
			[](void*, wl_registry*, uint32_t) {
			},
	};
	if(wl_registry_add_listener(_registry, &_registryListener, nullptr))
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

#elif defined(USE_PLATFORM_QT)

	// use data as pointer to
	// tuple<QGuiApplication*,QVulkanInstance*>

	if(qGuiApplication)
		return;

	// get objects from data parameter
	if(data) {
		auto& d = *reinterpret_cast<tuple<QGuiApplication*,QVulkanInstance*>*>(data);
		qGuiApplication = get<0>(d);
		qVulkanInstance = get<1>(d);
	}

	// construct QGuiApplication
	// using VulkanWindow::init()
	if(qGuiApplication == nullptr)
		init();

#else

	// on all other platforms,
	// just perform init()
	init();

#endif
}


void VulkanWindow::init(int& argc, char* argv[])
{
	// use argc and argv
	// for QGuiApplication initialization
#if defined(USE_PLATFORM_QT)

	if(qGuiApplication)
		return;

	// construct QGuiApplication
	qGuiApplication = reinterpret_cast<QGuiApplication*>(&qGuiApplicationMemory);
	new(qGuiApplication) QGuiApplication(argc, argv);

#else

	// on all other platforms,
	// just perform init()
	init();

#endif
}


void VulkanWindow::finalize() noexcept
{
#if defined(USE_PLATFORM_WIN32)

	// release resources
	// (do not throw in the finalization code,
	// so ignore the errors in release builds and assert in debug builds)
# ifdef NDEBUG
	if(_windowClass) {
		UnregisterClass(MAKEINTATOM(_windowClass), _hInstance);
		_windowClass = 0;
	}
# else
	if(_windowClass) {
		if(!UnregisterClass(MAKEINTATOM(_windowClass), _hInstance))
			assert(0 && "UnregisterClass(): The function failed.");
		_windowClass = 0;
	}
# endif

#elif defined(USE_PLATFORM_XLIB)

	if(_display) {
		if(!externalDisplayHandle)
			XCloseDisplay(_display);
		_display = nullptr;
	}

#elif defined(USE_PLATFORM_WAYLAND)

	if(_xdgWmBase) {
		xdg_wm_base_destroy(_xdgWmBase);
		_xdgWmBase = nullptr;
	}
	if(_display) {
		if(!externalDisplayHandle)
			wl_display_disconnect(_display);
		_display = nullptr;
	}
	_registry = nullptr;
	_compositor = nullptr;
	_xdgWmBase = nullptr;
	_zxdgDecorationManagerV1 = nullptr;

#elif defined(USE_PLATFORM_SDL)

	// finalize SDL
	if(sdlInitialized) {
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		SDL_Quit();
		sdlInitialized = false;
	}

#elif defined(USE_PLATFORM_GLFW)

	// finalize GLFW
	// (it is safe to call glfwTerminate() even if GLFW was not initialized)
	glfwTerminate();
# if 1 // test error by assert
	assert(glfwGetError(nullptr) == GLFW_NO_ERROR && "VulkanWindow: glfwTerminate() function failed.");
# else // print error if any
	const char* errorString;
	int errorCode = glfwGetError(&errorString);
	if(errorCode != GLFW_NO_ERROR) {
		cout <<  && "VulkanWindow: glfwTerminate() function failed. Error code: 0x"
		     << hex << errorCode << ". Error string: " << errorString << endl;
		assert(0 && "VulkanWindow: glfwTerminate() function failed.");
	}
# endif

#elif defined(USE_PLATFORM_QT)

	// delete QVulkanInstance object
	// but only if we own it
	if(qVulkanInstance == reinterpret_cast<QVulkanInstance*>(&qVulkanInstanceMemory))
		qVulkanInstance->~QVulkanInstance();
	qVulkanInstance = nullptr;

	// destroy QGuiApplication object
	// but only if we own it
	if(qGuiApplication == reinterpret_cast<QGuiApplication*>(&qGuiApplicationMemory))
		qGuiApplication->~QGuiApplication();
	qGuiApplication = nullptr;

#endif
}



void VulkanWindow::destroy() noexcept
{
	// destroy surface
	// except Qt platform
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
# else
	assert(_windowClass && "VulkanWindow::destroy(): Window class does not exist. "
		                   "Did you called VulkanWindow::finalize() prematurely?");
	if(_hwnd) {
		if(!DestroyWindow(_hwnd))
			assert(0 && "DestroyWindow(): The function failed.");
		_hwnd = nullptr;
	}
# endif

#elif defined(USE_PLATFORM_XLIB)

	// release resources
	if(_window) {
		XDestroyWindow(_display, _window);
		_window = 0;
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

#elif defined(USE_PLATFORM_SDL)

	if(_window) {

		// get windowID
		auto windowID = SDL_GetWindowID(_window);
		assert(windowID != 0 && "SDL_GetWindowID(): The function failed.");

		// destroy window
		SDL_DestroyWindow(_window);
		_window = nullptr;

		// get all events in the queue
		SDL_PumpEvents();
		vector<SDL_Event> buf(16);
		while(true) {
			int num = SDL_PeepEvents(&buf[buf.size()-16], 16, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
			if(num < 0) {
				assert(0 && "SDL_PeepEvents(): The function failed.");
				break;
			}
			if(num == 16) {
				buf.resize(buf.size() + 16);
				continue;
			}
			buf.resize(buf.size() - 16 + num);
			break;
		};

		// fill the queue again
		// while skipping all events of deleted window
		// (we need to skip those that are processed in VulkanWindow::mainLoop() because they might cause SIGSEGV)
		for(SDL_Event& event : buf) {
			if(event.type == SDL_WINDOWEVENT)
				if(event.window.windowID == windowID)
					continue;
			int num = SDL_PeepEvents(&event, 1, SDL_ADDEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT);
			if(num != 1)
				assert(0 && "SDL_PeepEvents(): The function failed.");
		}
	}

#elif defined(USE_PLATFORM_GLFW)

	if(_window) {

		// destroy window
		glfwDestroyWindow(_window);
		_window = nullptr;

		// cancel pending frame, if any
		if(_framePendingState != FramePendingState::NotPending) {
			_framePendingState = FramePendingState::NotPending;
			for(size_t i=0; i<framePendingWindows.size(); i++)
				if(framePendingWindows[i] == this) {
					framePendingWindows[i] = framePendingWindows.back();
					framePendingWindows.pop_back();
					break;
				}
		}
	}

#elif defined(USE_PLATFORM_QT)

	// delete QtRenderingWindow
	if(_window) {
		delete _window;
		_window = nullptr;
	}

#endif
}


vk::SurfaceKHR VulkanWindow::create(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title)
{
	// asserts
#if defined(USE_PLATFORM_WIN32)
	assert(_windowClass && "VulkanWindow was not initialized. Call VulkanWindow::init() before VulkanWindow::create()."); 
#elif defined(USE_PLATFORM_SDL)
	assert(sdlInitialized && "VulkanWindow was not initialized. Call VulkanWindow::init() before VulkanWindow::create()."); 
#endif

	// destroy any previous window data
	// (this makes calling init() multiple times safe operation)
	destroy();

	// set Vulkan instance
	_instance = instance;

	// set surface extent
	_surfaceExtent = surfaceExtent;

#if defined(USE_PLATFORM_WIN32)

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

	// create Vulkan window
	_window = SDL_CreateWindow(
		title,  // title
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,  // x,y
		surfaceExtent.width, surfaceExtent.height,  // w,h
		SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN  // flags
	);
	if(_window == nullptr)
		throw runtime_error(string("VulkanWindow: SDL_CreateWindow() function failed. Error details: ") + SDL_GetError());

	// set pointer to this
	SDL_SetWindowData(_window, windowPointerName, this);

	// create surface
	if(!SDL_Vulkan_CreateSurface(_window, instance, reinterpret_cast<VkSurfaceKHR*>(&_surface)))
		throw runtime_error(string("VulkanWindow: SDL_Vulkan_CreateSurface() function failed. Error details: ") + SDL_GetError());

	return _surface;

#elif defined(USE_PLATFORM_GLFW)

	// create window
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);  // this prevents fail inside glfwShowWindow(): "Wayland: Focusing a window requires user interaction"; seen on glfw 3.3.8 and Kubuntu 22.10
	_window = glfwCreateWindow(surfaceExtent.width, surfaceExtent.height, title, nullptr, nullptr);
	if(_window == nullptr)
		throwError("glfwCreateWindow");

	// setup window
	glfwSetWindowUserPointer(_window, this);
	glfwSetWindowRefreshCallback(
		_window,
		[](GLFWwindow* window) {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(glfwGetWindowUserPointer(window));
			w->scheduleFrame();
		}
	);
	glfwSetFramebufferSizeCallback(
		_window,
		[](GLFWwindow* window, int width, int height) {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(glfwGetWindowUserPointer(window));
			w->scheduleSwapchainResize();
		}
	);
	glfwSetWindowIconifyCallback(
		_window,
		[](GLFWwindow* window, int iconified) {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(glfwGetWindowUserPointer(window));

			w->_minimized = iconified;

			if(w->_minimized)
				// cancel pending frame, if any, on window minimalization
				if(w->_framePendingState != FramePendingState::NotPending) {
					w->_framePendingState = FramePendingState::NotPending;
					for(size_t i=0; i<framePendingWindows.size(); i++)
						if(framePendingWindows[i] == w) {
							framePendingWindows[i] = framePendingWindows.back();
							framePendingWindows.pop_back();
							break;
						}
				}
			else
				// schedule frame on window un-minimalization
				w->scheduleFrame();
		}
	);
	glfwSetWindowCloseCallback(
		_window,
		[](GLFWwindow* window) {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(glfwGetWindowUserPointer(window));
			if(w->_closeCallback)
				w->_closeCallback();  // VulkanWindow object might be already destroyed when returning from the callback
			else {
				w->hide();
				VulkanWindow::exitMainLoop();
			}
		}
	);

	// create surface
	if(glfwCreateWindowSurface(instance, _window, nullptr, reinterpret_cast<VkSurfaceKHR*>(&_surface)) != VK_SUCCESS)
		throwError("glfwCreateWindowSurface");

	return _surface;

#elif defined(USE_PLATFORM_QT)

	// create QVulkanInstance
	if(qVulkanInstance == nullptr) {
		qVulkanInstance = reinterpret_cast<QVulkanInstance*>(&qVulkanInstanceMemory);
		new(qVulkanInstance) QVulkanInstance;
		qVulkanInstance->setVkInstance(instance);
		qVulkanInstance->create();
	}

	// setup QtRenderingWindow
	_window = new QtRenderingWindow(nullptr, this);
	_window->setSurfaceType(QSurface::VulkanSurface);
	_window->setVulkanInstance(qVulkanInstance);
	_window->resize(surfaceExtent.width, surfaceExtent.height);
	_window->create();

	// return Vulkan surface
	_surface = QVulkanInstance::surfaceForWindow(_window);
	if(!_surface)
		throw runtime_error("VulkanWindow::init(): Failed to create surface.");
	return _surface;

#endif
}


void VulkanWindow::renderFrame()
{
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

#if defined(USE_PLATFORM_SDL)
		// get surface size on SDL
		SDL_Vulkan_GetDrawableSize(_window, reinterpret_cast<int*>(&surfaceCapabilities.currentExtent.width), reinterpret_cast<int*>(&surfaceCapabilities.currentExtent.height));
		surfaceCapabilities.currentExtent.width = clamp(surfaceCapabilities.currentExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
		surfaceCapabilities.currentExtent.height = clamp(surfaceCapabilities.currentExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
#elif defined(USE_PLATFORM_GLFW)
		// get surface size on GLFW
		// (0xffffffff values might be returned on Wayland)
		if(surfaceCapabilities.currentExtent.width == 0xffffffff || surfaceCapabilities.currentExtent.height == 0xffffffff) {
			glfwGetFramebufferSize(_window, reinterpret_cast<int*>(&surfaceCapabilities.currentExtent.width), reinterpret_cast<int*>(&surfaceCapabilities.currentExtent.height));
			checkError("glfwGetFramebufferSize");
			surfaceCapabilities.currentExtent.width = clamp(surfaceCapabilities.currentExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
			surfaceCapabilities.currentExtent.height = clamp(surfaceCapabilities.currentExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
		}
#endif

		// zero size swapchain is not allowed,
		// so we will repeat the resize and rendering attempt after the next window resize
		// (this may happen on Win32-based and Xlib-based systems, for instance)
		if(surfaceCapabilities.currentExtent == vk::Extent2D(0,0))
			return;  // new frame will be scheduled on the next window resize

		// recreate swapchain
		_swapchainResizePending = false;
		_surfaceExtent = surfaceCapabilities.currentExtent;
		_recreateSwapchainCallback(surfaceCapabilities, _surfaceExtent);
	}

	// render scene
	_frameCallback();
}


#if defined(USE_PLATFORM_WIN32)


void VulkanWindow::show()
{
	// callbacks need to be assigned
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	// show window
	ShowWindow(_hwnd, SW_SHOW);
}


void VulkanWindow::hide()
{
	ShowWindow(_hwnd, SW_HIDE);
}


void VulkanWindow::mainLoop()
{
	// run Win32 event loop
	MSG msg;
	BOOL r;
	thrownException = nullptr;
	while((r = GetMessage(&msg, NULL, 0, 0)) != 0) {

		// handle errors
		if(r == -1)
			throw runtime_error("GetMessage(): The function failed.");

		// handle message
		TranslateMessage(&msg);
		DispatchMessage(&msg);

		// handle exceptions raised in window procedure
		if(thrownException)
			rethrow_exception(thrownException);

	}
}


void VulkanWindow::exitMainLoop()
{
	PostQuitMessage(0);
}


void VulkanWindow::scheduleFrame()
{
	if(_framePendingState == FramePendingState::Pending)
		return;

	if(_framePendingState == FramePendingState::NotPending) {

		// invalidate window content (this will cause WM_PAINT message to be sent) 
		if(!InvalidateRect(_hwnd, NULL, FALSE))
			throw runtime_error("InvalidateRect(): The function failed.");

		framePendingWindows.push_back(this);
	}

	_framePendingState = FramePendingState::Pending;
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


void VulkanWindow::show()
{
	// callbacks need to be assigned
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	// show window
	// and set _visible flag immediately
	SDL_ShowWindow(_window);
	_visible = true;
}


void VulkanWindow::hide()
{
	// hide window
	// and set _visible flag immediately
	SDL_HideWindow(_window);
	_visible = false;
}


void VulkanWindow::mainLoop()
{
	// main loop
	SDL_Event event;
	running = true;
	do {

		// get event
		// (wait for one if no events are in the queue yet)
		if(SDL_WaitEvent(&event) == 0)  // want for events
			throw runtime_error(string("VulkanWindow: SDL_WaitEvent() function failed. Error details: ") + SDL_GetError());

		// handle event
		// (Make sure that all event types (event.type) handled here, such as SDL_WINDOWEVENT,
		// are removed from the queue in VulkanWindow::destroy().
		// Otherwise we might receive events of already non-existing window.)
		switch(event.type) {
		case SDL_WINDOWEVENT:
			switch(event.window.event) {

			case SDL_WINDOWEVENT_EXPOSED: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
					SDL_GetWindowData(SDL_GetWindowFromID(event.window.windowID), windowPointerName));
				w->_framePending = false;
				if(w->_visible && !w->_minimized)
					w->renderFrame();
				break;
			}

			case SDL_WINDOWEVENT_SIZE_CHANGED: {
				cout << "Size changed event" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
					SDL_GetWindowData(SDL_GetWindowFromID(event.window.windowID), windowPointerName));
				w->scheduleSwapchainResize();
				break;
			}

			case SDL_WINDOWEVENT_SHOWN: {
				cout << "Shown event" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
					SDL_GetWindowData(SDL_GetWindowFromID(event.window.windowID), windowPointerName));
				w->_visible = true;
				w->scheduleFrame();
				break;
			}

			case SDL_WINDOWEVENT_HIDDEN: {
				cout << "Hidden event" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
					SDL_GetWindowData(SDL_GetWindowFromID(event.window.windowID), windowPointerName));
				w->_visible = false;
				break;
			}

			case SDL_WINDOWEVENT_MINIMIZED: {
				cout << "Minimized event" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
					SDL_GetWindowData(SDL_GetWindowFromID(event.window.windowID), windowPointerName));
				w->_minimized = true;
				break;
			}

			case SDL_WINDOWEVENT_RESTORED: {
				cout << "Restored event" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
					SDL_GetWindowData(SDL_GetWindowFromID(event.window.windowID), windowPointerName));
				w->_minimized = false;
				w->scheduleFrame();
				break;
			}

			case SDL_WINDOWEVENT_CLOSE: {
				cout << "Close event" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
					SDL_GetWindowData(SDL_GetWindowFromID(event.window.windowID), windowPointerName));
				if(w->_closeCallback)
					w->_closeCallback();  // VulkanWindow object might be already destroyed when returning from the callback
				else {
					w->hide();
					VulkanWindow::exitMainLoop();
				}
				break;
			}

			}
			break;

		case SDL_QUIT:  // SDL_QUIT is generated on variety of reasons, including SIGINT and SIGTERM, or pressing Command-Q on Mac OS X.
		                // By default, it would also be generated by SDL on last window close. This would interfere with VulkanWindow expected behaviour
		                // to leave main loop after the call of exitMainLoop(). So, we disabled it by SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE hint available since SDL 2.0.22.
			return;
		}

	} while(running);
}


void VulkanWindow::exitMainLoop()
{
	running = false;
}


void VulkanWindow::scheduleFrame()
{
	if(_framePending || !_visible || _minimized)
		return;

	_framePending = true;

	SDL_Event e;
	e.window.type = SDL_WINDOWEVENT;
	e.window.timestamp = SDL_GetTicks();
	e.window.windowID = SDL_GetWindowID(_window);
	e.window.event = SDL_WINDOWEVENT_EXPOSED;
	e.window.data1 = 0;
	e.window.data2 = 0;
	SDL_PushEvent(&e);
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


void VulkanWindow::show()
{
	// callbacks need to be assigned
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	// show window
	_visible = true;
	glfwShowWindow(_window);
	checkError("glfwShowWindow");
	scheduleFrame();
}


void VulkanWindow::hide()
{
	// hide window
	_visible = false;
	glfwHideWindow(_window);
	checkError("glfwHideWindow");

	// cancel pending frame, if any, on window hide
	if(_framePendingState != FramePendingState::NotPending) {
		_framePendingState = FramePendingState::NotPending;
		for(size_t i=0; i<framePendingWindows.size(); i++)
			if(framePendingWindows[i] == this) {
				framePendingWindows[i] = framePendingWindows.back();
				framePendingWindows.pop_back();
				break;
			}
	}
}


void VulkanWindow::mainLoop()
{
	// main loop
	running = true;
	do {

		if(framePendingWindows.empty())
		{
			glfwWaitEvents();
			checkError("glfwWaitEvents");
		}
		else
		{
			glfwPollEvents();
			checkError("glfwPollEvents");
		}

		// render all windows with _framePendingState set to Pending
		for(size_t i=0; i<framePendingWindows.size(); ) {

			// render frame
			VulkanWindow* w = framePendingWindows[i];
			w->_framePendingState = FramePendingState::TentativePending;
			w->renderFrame();

			// was frame scheduled again?
			// (it might be rescheduled again in renderFrame())
			if(w->_framePendingState == FramePendingState::TentativePending) {

				// update state to no-frame-pending
				w->_framePendingState = FramePendingState::NotPending;
				if(framePendingWindows.size() == 1) {
					framePendingWindows.clear();  // all iterators are invalidated
					break;
				}
				else {
					framePendingWindows[i] = framePendingWindows.back();
					framePendingWindows.pop_back();  // end() iterator is invalidated
					continue;
				}
			}
			i++;

		}

	} while(running);
}


void VulkanWindow::exitMainLoop()
{
	running = false;
}


void VulkanWindow::scheduleFrame()
{
	if(_framePendingState == FramePendingState::Pending)
		return;

	if(_framePendingState == FramePendingState::NotPending)
		framePendingWindows.push_back(this);

	_framePendingState = FramePendingState::Pending;
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


void VulkanWindow::show()
{
	// callbacks need to be assigned
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::show() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::show().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::show() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::show().");

	_window->show();
}


void VulkanWindow::hide()
{
	_window->hide();
}


bool VulkanWindow::isVisible() const
{
	return _window->isVisible();
}


void VulkanWindow::mainLoop()
{
	cout << "DisplayName: " << qGuiApplication->applicationDisplayName().toStdString() << endl;
	cout << "ApplicationName: " << qGuiApplication->applicationName().toStdString() << endl;
	cout << "ApplicationVersion: " << qGuiApplication->applicationVersion().toStdString() << endl;
	cout << "OrganizationDomain: " << qGuiApplication->organizationDomain().toStdString() << endl;
	cout << "OrganizationName: " << qGuiApplication->organizationName().toStdString() << endl;

	// call exec()
	thrownException = nullptr;
	QGuiApplication::exec();
	if(thrownException)  // rethrow the exception that we caught in QtRenderingWindow::event()
		rethrow_exception(thrownException);
}


void VulkanWindow::exitMainLoop()
{
	QGuiApplication::exit();
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
	qVulkanInstance->presentAboutToBeQueued(_window);
#endif
	_frameCallback();
	qVulkanInstance->presentQueued(_window);
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
			if(vulkanWindow->_closeCallback)
				vulkanWindow->_closeCallback();
			else {
				vulkanWindow->hide();
				VulkanWindow::exitMainLoop();
			}
			return true;

		default:
			return QWindow::event(event);
		}
	}
	catch(...) {
		VulkanWindow::thrownException = std::current_exception();
		QGuiApplication::exit();
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



static int niftyCounter = 0;


VulkanWindowInitAndFinalizer::VulkanWindowInitAndFinalizer()
{
	niftyCounter++;
}


VulkanWindowInitAndFinalizer::~VulkanWindowInitAndFinalizer()
{
	if(--niftyCounter == 0)
		VulkanWindow::finalize();
}
