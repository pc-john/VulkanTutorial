#include "VulkanWindow.h"
#if defined(USE_PLATFORM_WIN32)
# define NOMINMAX  // avoid the definition of min and max macros by windows.h
# include <windows.h>
# include <windowsx.h>
# include <tchar.h>
# include <map>
#elif defined(USE_PLATFORM_XLIB)
# include <X11/Xutil.h>
# include <map>
extern "C" int xkb_keysym_to_utf8(uint32_t keysym, char *buffer, size_t size);  // originally defined in xkbcommon/xkbcommon.h; this definition avoids the need to have xkbcommon headers installed
#elif defined(USE_PLATFORM_WAYLAND)
# include "xdg-shell-client-protocol.h"
# include "xdg-decoration-client-protocol.h"
# include <xkbcommon/xkbcommon.h>
# include <sys/mman.h>
# include <unistd.h>
# include <map>
#elif defined(USE_PLATFORM_SDL2)
# include "SDL.h"
# include "SDL_vulkan.h"
# include <algorithm>
# include <cmath>
# include <memory>
#elif defined(USE_PLATFORM_GLFW)
# define GLFW_INCLUDE_NONE  // do not include OpenGL headers
# include <GLFW/glfw3.h>
# include <cmath>
#elif defined(USE_PLATFORM_QT)
# include <QGuiApplication>
# include <QWindow>
# include <QVulkanInstance>
# include <QMouseEvent>
# include <QWheelEvent>
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

static std::map<uint8_t, std::string> pressedKeys;

// list of windows waiting for frame rendering
// (the windows have _framePendingState set to FramePendingState::Pending or TentativePending)
static vector<VulkanWindow*> framePendingWindows;

// remove VulkanWindow from framePendingWindows; VulkanWindow MUST be in framePendingWindows
static void removeFromFramePendingWindows(VulkanWindow* w)
{
	if(framePendingWindows.size() != 1) {
		for(size_t i=0; i<framePendingWindows.size(); i++)
			if(framePendingWindows[i] == w) {
				framePendingWindows[i] = framePendingWindows.back();
				break;
			}
	}
	framePendingWindows.pop_back();
}
#endif


// Xlib global variables
#if defined(USE_PLATFORM_XLIB)
static bool externalDisplayHandle;
static map<Window, VulkanWindow*> vulkanWindowMap;
static bool running;  // bool indicating that application is running and it shall not leave main loop
#endif


// Wayland global variables
#if defined(USE_PLATFORM_WAYLAND)
static bool externalDisplayHandle;
static bool running;  // bool indicating that application is running and it shall not leave main loop
static map<wl_surface*, VulkanWindow*> surface2windowMap;
static VulkanWindow* windowUnderPointer = nullptr;
static VulkanWindow* windowWithKbFocus = nullptr;

// listeners
struct WaylandListeners {
	VulkanWindow* vulkanWindow;
	xdg_surface_listener xdgSurfaceListener;
	xdg_toplevel_listener xdgToplevelListener;
	wl_callback_listener frameListener;
};

// global listeners
static wl_registry_listener registryListener;
static xdg_wm_base_listener xdgWmBaseListener;
static wl_seat_listener seatListener;
static wl_pointer_listener pointerListener;
static wl_keyboard_listener keyboardListener;
#endif


// SDL global variables
#if defined(USE_PLATFORM_SDL2)
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
	void scheduleFrameTimer();
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
	auto wndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept -> LRESULT
	{
		// mouse functions
		auto handleModifiers =
			[](VulkanWindow* w, WPARAM wParam) -> void
			{
				w->_mouseState.mods.set(Modifier::Ctrl,  wParam & MK_CONTROL);
				w->_mouseState.mods.set(Modifier::Shift, wParam & MK_SHIFT);
				w->_mouseState.mods.set(Modifier::Alt,   GetKeyState(VK_MENU) < 0);
				w->_mouseState.mods.set(Modifier::Meta,  GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN));
			};
		auto handleMouseMove =
			[](VulkanWindow* w, int x, int y)
			{
				if(x != w->_mouseState.posX || y != w->_mouseState.posY) {
					w->_mouseState.relX = x - w->_mouseState.posX;
					w->_mouseState.relY = y - w->_mouseState.posY;
					w->_mouseState.posX = x;
					w->_mouseState.posY = y;
					if(w->_mouseMoveCallback)
						w->_mouseMoveCallback(*w, w->_mouseState);
				}
			};
		auto handleMouseButton =
			[](HWND hwnd, MouseButton::EnumType mouseButton, ButtonAction downOrUp, WPARAM wParam, LPARAM lParam) -> LRESULT
			{
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));

				// handle mods
				w->_mouseState.mods.set(Modifier::Ctrl,  wParam & MK_CONTROL);
				w->_mouseState.mods.set(Modifier::Shift, wParam & MK_SHIFT);
				w->_mouseState.mods.set(Modifier::Alt,   GetKeyState(VK_MENU) < 0);
				w->_mouseState.mods.set(Modifier::Meta,  GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN));

				// handle mouse move, if any
				int x = GET_X_LPARAM(lParam);
				int y = GET_Y_LPARAM(lParam);
				if(x != w->_mouseState.posX || y != w->_mouseState.posY) {
					w->_mouseState.relX = x - w->_mouseState.posX;
					w->_mouseState.relY = y - w->_mouseState.posY;
					w->_mouseState.posX = x;
					w->_mouseState.posY = y;
					if(w->_mouseMoveCallback)
						w->_mouseMoveCallback(*w, w->_mouseState);
				}

				// set new state and capture mouse
				bool prevAny = w->_mouseState.buttons.any();
				w->_mouseState.buttons.set(mouseButton, downOrUp==ButtonAction::Down);
				bool newAny = w->_mouseState.buttons.any();
				if(prevAny != newAny) {
					if(newAny)
						SetCapture(hwnd);
					else
						ReleaseCapture();
				}

				// callback
				if(w->_mouseButtonCallback)
					w->_mouseButtonCallback(*w, mouseButton, downOrUp, w->_mouseState);

				return 0;
			};
		auto getMouseXButton =
			[](WPARAM wParam) -> MouseButton::EnumType
			{
				switch(GET_XBUTTON_WPARAM(wParam)) {
				case XBUTTON1: return MouseButton::X1;
				case XBUTTON2: return MouseButton::X2;
				default: return MouseButton::Unknown;
				}
			};

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
						if(!ValidateRect(HWND(w->_hwnd), NULL))
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

			// mouse move
			case WM_MOUSEMOVE: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				handleModifiers(w, wParam);
				handleMouseMove(w, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
				return 0;
			}

			// mouse buttons
			case WM_LBUTTONDOWN: return handleMouseButton(hwnd, MouseButton::Left,   ButtonAction::Down, wParam, lParam);
			case WM_LBUTTONUP:   return handleMouseButton(hwnd, MouseButton::Left,   ButtonAction::Up,   wParam, lParam);
			case WM_RBUTTONDOWN: return handleMouseButton(hwnd, MouseButton::Right,  ButtonAction::Down, wParam, lParam);
			case WM_RBUTTONUP:   return handleMouseButton(hwnd, MouseButton::Right,  ButtonAction::Up,   wParam, lParam);
			case WM_MBUTTONDOWN: return handleMouseButton(hwnd, MouseButton::Middle, ButtonAction::Down, wParam, lParam);
			case WM_MBUTTONUP:   return handleMouseButton(hwnd, MouseButton::Middle, ButtonAction::Up,   wParam, lParam);
			case WM_XBUTTONDOWN: return handleMouseButton(hwnd, getMouseXButton(wParam), ButtonAction::Down, wParam, lParam);
			case WM_XBUTTONUP:   return handleMouseButton(hwnd, getMouseXButton(wParam), ButtonAction::Up,   wParam, lParam);

			// vertical and horizontal mouse wheel
			// (amount of wheel rotation since the last wheel message)
			case WM_MOUSEWHEEL: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				handleModifiers(w, wParam);
				POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				if(ScreenToClient(hwnd, &p) == 0)
					thrownException = make_exception_ptr(runtime_error("ScreenToClient(): The function failed."));
				handleMouseMove(w, p.x, p.y);
				if(w->_mouseWheelCallback)
					w->_mouseWheelCallback(*w, 0, GET_WHEEL_DELTA_WPARAM(wParam), w->_mouseState);
				return 0;
			}
			case WM_MOUSEHWHEEL: {
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				handleModifiers(w, wParam);
				POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				if(ScreenToClient(hwnd, &p) == 0)
					thrownException = make_exception_ptr(runtime_error("ScreenToClient(): The function failed."));
				handleMouseMove(w, p.x, p.y);
				if(w->_mouseWheelCallback)
					w->_mouseWheelCallback(*w, GET_WHEEL_DELTA_WPARAM(wParam), 0, w->_mouseState);
				return 0;
			}

			// keyboard events
			case WM_KEYDOWN: {

				// skip auto-repeat messages
				if(lParam & (1 << 30))
					return 0;

				auto getUtf8 =
					[](HWND hwnd) -> std::string
					{
						// get WM_CHAR message
						MSG msg;
						if(!PeekMessage(&msg, hwnd, WM_CHAR, WM_CHAR, PM_REMOVE))
							return "";

						// convert from current code page to utf-16
						WPARAM c;
					#if _UNICODE
						c = msg.wParam;
					#else
						int l1 = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<char*>(&msg.wParam), 1, reinterpret_cast<wchar_t*>(&c), sizeof(c));
						if(l1 == 0)
							return "";
					#endif

						// convert from utf-16 to utf-8
						constexpr int maxLen = 12;
						char s[maxLen+1];
						int l2 = WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<wchar_t*>(&c), l1, s, maxLen, nullptr, nullptr);
						if(l2 == 0)
							return "";
						s[l2] = 0;
						return s;
					};

				uint8_t keyCode = wParam & 0xff;
				auto& utf8character = pressedKeys[keyCode];
				utf8character = getUtf8(hwnd);
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				if(w->_keyCallback)
					w->_keyCallback(*w, KeyAction::Down, (lParam >> 16) & 0xff, keyCode, utf8character);
				return 0;
			}
			case WM_KEYUP: {
				uint8_t keyCode = wParam & 0xff;
				auto it = pressedKeys.find(keyCode);
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				if(w->_keyCallback)
					w->_keyCallback(*w, KeyAction::Up, (lParam >> 16) & 0xff, wParam & 0xff, (it != pressedKeys.end()) ? it->second : "");
				if(it != pressedKeys.end())
					pressedKeys.erase(it);
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
				if(w->_visible == false) {

					// store frame pending state
					w->_hiddenWindowFramePending = w->_framePendingState == FramePendingState::Pending;

					// cancel frame pending state
					if(w->_framePendingState == FramePendingState::Pending) {
						w->_framePendingState = FramePendingState::NotPending;
						removeFromFramePendingWindows(w);
					}

				}
				else {

					// restore frame pending state
					if(w->_hiddenWindowFramePending) {
						w->_framePendingState = FramePendingState::Pending;
						framePendingWindows.push_back(w);
					}

				}
				return DefWindowProc(hwnd, msg, wParam, lParam);
			}

			// close window message
			// (we call _closeCallback here if registered,
			// otherwise we hide the window and schedule main loop exit)
			case WM_CLOSE: {
				cout << "WM_CLOSE message" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(GetWindowLongPtr(hwnd, 0));
				if(w->_closeCallback)
					w->_closeCallback(*w);  // VulkanWindow object might be already destroyed when returning from the callback
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
					removeFromFramePendingWindows(w);
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
				HINSTANCE(_hInstance),  // hInstance
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

	// get atoms
	_wmDeleteMessage = XInternAtom(_display, "WM_DELETE_WINDOW", False);
	_wmStateProperty = XInternAtom(_display, "WM_STATE", False);

#elif defined(USE_PLATFORM_WAYLAND)

	init(nullptr);

#elif defined(USE_PLATFORM_SDL2)

	// handle multiple init attempts
	if(sdlInitialized)
		return;

	// initialize SDL
	if(SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
		throw runtime_error(string("SDL_InitSubSystem(SDL_INIT_VIDEO) function failed. Error details: ") + SDL_GetError());
	sdlInitialized = true;

	// set hints
	SDL_SetHint("SDL_QUIT_ON_LAST_WINDOW_CLOSE", "0");   // do not send SDL_QUIT event when the last window closes; we shall exit main loop
	                                                     // after VulkanWindow::exitMainLoop() is called; the hint is supported since SDL 2.0.22
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

	// get atoms
	_wmDeleteMessage = XInternAtom(_display, "WM_DELETE_WINDOW", False);
	_wmStateProperty = XInternAtom(_display, "WM_STATE", False);

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
	registryListener = {
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
				else if(strcmp(interface, "wl_seat") == 0)
					_seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
			},
		.global_remove =
			[](void*, wl_registry*, uint32_t) {
			},
	};
	if(wl_registry_add_listener(_registry, &registryListener, nullptr))
		throw runtime_error("wl_registry_add_listener() failed.");

	// get and init global objects
	if(wl_display_roundtrip(_display) == -1)
		throw runtime_error("wl_display_roundtrip() failed.");
	if(_compositor == nullptr)
		throw runtime_error("Cannot get Wayland wl_compositor object.");
	if(_xdgWmBase == nullptr)
		throw runtime_error("Cannot get Wayland xdg_wm_base object.");
	xdgWmBaseListener = {
		.ping =
			[](void*, xdg_wm_base* xdg, uint32_t serial) {
				xdg_wm_base_pong(xdg, serial);
			}
	};
	if(xdg_wm_base_add_listener(_xdgWmBase, &xdgWmBaseListener, nullptr))
		throw runtime_error("xdg_wm_base_add_listener() failed.");
	if(_seat == nullptr)
		throw runtime_error("Cannot get Wayland wl_seat object.");

	// xkb_context
	_xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if(_xkbContext == NULL)
		throw runtime_error("VulkanWindow::init(): Cannot create XKB context.");

	// pointer
	pointerListener = {
		.enter =
			[](void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface,
			   wl_fixed_t surface_x, wl_fixed_t surface_y)
			{
				auto it = surface2windowMap.find(surface);
				if(it == surface2windowMap.end()) {
					// unknown window
					windowUnderPointer = nullptr;
					return;
				}
				windowUnderPointer = it->second;

				int x = surface_x >> 8;
				int y = surface_y >> 8;
				if(windowUnderPointer->_mouseState.posX != x ||
					windowUnderPointer->_mouseState.posY != y)
				{
					windowUnderPointer->_mouseState.relX = 0;
					windowUnderPointer->_mouseState.relY = 0;
					windowUnderPointer->_mouseState.posX = x;
					windowUnderPointer->_mouseState.posY = y;
					if(windowUnderPointer->_mouseMoveCallback)
						windowUnderPointer->_mouseMoveCallback(*windowUnderPointer, windowUnderPointer->_mouseState);
				}
			},
		.leave =
			[](void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface)
			{
				windowUnderPointer = nullptr;
			},
		.motion =
			[](void* data, wl_pointer* pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
			{
				// handle unknown window
				if(windowUnderPointer == nullptr)
					return;

				int x = surface_x >> 8;
				int y = surface_y >> 8;
				if(windowUnderPointer->_mouseState.posX != x ||
					windowUnderPointer->_mouseState.posY != y)
				{
					windowUnderPointer->_mouseState.relX = x - windowUnderPointer->_mouseState.posX;
					windowUnderPointer->_mouseState.relY = y - windowUnderPointer->_mouseState.posY;
					windowUnderPointer->_mouseState.posX = x;
					windowUnderPointer->_mouseState.posY = y;
					if(windowUnderPointer->_mouseMoveCallback)
						windowUnderPointer->_mouseMoveCallback(*windowUnderPointer, windowUnderPointer->_mouseState);
				}
			},
		.button =
			[](void* data, wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
			{
				// handle unknown window
				if(windowUnderPointer == nullptr)
					return;

				MouseButton::EnumType index;
				switch(button) {
				// button codes taken from linux/input-event-codes.h
				case 0x110: index = MouseButton::Left; break;
				case 0x111: index = MouseButton::Right; break;
				case 0x112: index = MouseButton::Middle; break;
				case 0x113: index = MouseButton::X1; break;
				case 0x114: index = MouseButton::X2; break;
				default: index = MouseButton::Unknown;
				}
				windowUnderPointer->_mouseState.buttons.set(index, state == WL_POINTER_BUTTON_STATE_PRESSED);
				if(windowUnderPointer->_mouseButtonCallback) {
					ButtonAction a = (state == WL_POINTER_BUTTON_STATE_PRESSED) ? ButtonAction::Down : ButtonAction::Up;
					windowUnderPointer->_mouseButtonCallback(*windowUnderPointer, index, a, windowUnderPointer->_mouseState);
				}
			},
		.axis =
			[](void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
			{
				// handle unknown window
				if(windowUnderPointer == nullptr)
					return;

				int v = (value * 8) / 256;
				int wheelX, wheelY;
				if(axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
					wheelX = 0;
					wheelY = -v;
				}
				else if(axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
					wheelX = v;
					wheelY = 0;
				}
				if(windowUnderPointer->_mouseWheelCallback)
					windowUnderPointer->_mouseWheelCallback(*windowUnderPointer, wheelX, wheelY,
					                                        windowUnderPointer->_mouseState);
			},
	};

	// keyboard
	keyboardListener = {
		.keymap =
			[](void* data, wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size)
			{
				cout << "keyboard keymap format: " << format << endl;

				if(format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
					return;

				// map memory
				char* m = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
				if(m == MAP_FAILED)
					throw runtime_error("VulkanWindow::init(): Failed to map memory in keymap event.");

				// create keymap
				xkb_keymap* keymap = xkb_keymap_new_from_string(_xkbContext, m,
					XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
				int r1 = munmap(m, size);
				int r2 = close(fd);

				// handle errors
				if(keymap == nullptr)
					throw runtime_error("VulkanWindow::init(): Failed to create keymap in keymap event.");
				if(r1 != 0) {
					xkb_keymap_unref(keymap);
					throw runtime_error("VulkanWindow::init(): Failed to unmap memory in keymap event.");
				}
				if(r2 != 0) {
					xkb_keymap_unref(keymap);
					throw runtime_error("VulkanWindow::init(): Failed to close file descriptor in keymap event.");
				}

				// unref old xkb_state
				if(_xkbState)
					xkb_state_unref(_xkbState);

				// create new xkb_state
				_xkbState = xkb_state_new(keymap);
				xkb_keymap_unref(keymap);
				if(_xkbState == nullptr)
					throw runtime_error("VulkanWindow::create(): Cannot create XKB state object in keymap event.");
			},
		.enter =
			[](void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface,
			   wl_array* keys)
			{
				cout << "enter keyboard" <<endl;

				auto it = surface2windowMap.find(surface);
				if(it == surface2windowMap.end()) {
					// unknown window
					windowWithKbFocus = nullptr;
					return;
				}
				windowWithKbFocus = it->second;

				// iterate keys array
				// (following for loop is equivalent to wl_array_for_each(key, keys) used in C)
				uint32_t* key;
				for(key = static_cast<uint32_t*>(keys->data);
				    reinterpret_cast<char*>(key) < static_cast<char*>(keys->data) + keys->size;
				    key++)
				{
					xkb_state_key_get_one_sym(_xkbState, *key + 8);
				}
			},
		.leave =
			[](void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface)
			{
				windowWithKbFocus = nullptr;
			},
		.key =
			[](void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time,
			   uint32_t key, uint32_t state)
			{
				cout << "key " << key << ", state " << state << endl;

				// process key
				uint32_t keyCode = key + 8;
				xkb_keysym_t sym = xkb_state_key_get_one_sym(_xkbState, keyCode);

				// callback
				if(windowWithKbFocus->_keyCallback) {
					char utf8char[8];
					xkb_state_key_get_utf8(_xkbState, keyCode, utf8char, sizeof(utf8char));
					windowWithKbFocus->_keyCallback(
						*windowWithKbFocus,
						state==WL_KEYBOARD_KEY_STATE_PRESSED ? KeyAction::Down : KeyAction::Up,
						key,  // scanCode
						sym,  // keyCode
						utf8char);  // utf8character
				}
			},
		.modifiers =
			[](void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed,
			   uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
			{
				cout << "modifier" << endl;

				xkb_state_update_mask(_xkbState, mods_depressed, mods_latched, mods_locked, 0, 0, group);
			},
	};

	// seat listener
	seatListener = {
		.capabilities =
			[](void* data, wl_seat* seat, uint32_t capabilities) {
				if(capabilities & WL_SEAT_CAPABILITY_POINTER) {
					if(_pointer == nullptr) {
						_pointer = wl_seat_get_pointer(seat);
						if(wl_pointer_add_listener(_pointer, &pointerListener, nullptr))
							throw runtime_error("wl_pointer_add_listener() failed.");
					}
				}
				else
					if(_pointer != nullptr) {
						wl_pointer_release(_pointer);
						_pointer = nullptr;
					}
				if(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
					if(_keyboard == nullptr) {
						_keyboard = wl_seat_get_keyboard(seat);
						if(wl_keyboard_add_listener(_keyboard, &keyboardListener, nullptr))
							throw runtime_error("wl_keyboard_add_listener() failed.");
					}
				}
				else
					if(_keyboard != nullptr) {
						wl_keyboard_release(_keyboard);
						_keyboard = nullptr;
					}
			},
	};
	if(wl_seat_add_listener(_seat, &seatListener, nullptr))
		throw runtime_error("wl_seat_add_listener() failed.");
	if(wl_display_roundtrip(_display) == -1)
		throw runtime_error("wl_display_roundtrip() failed.");

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
		if(!UnregisterClass(MAKEINTATOM(_windowClass), HINSTANCE(_hInstance)))
			assert(0 && "UnregisterClass(): The function failed.");
		_windowClass = 0;
	}
# endif

#elif defined(USE_PLATFORM_XLIB)

	if(_display) {
		if(!externalDisplayHandle)
			XCloseDisplay(_display);
		_display = nullptr;
		vulkanWindowMap.clear();
	}

#elif defined(USE_PLATFORM_WAYLAND)

	if(_pointer) {
		wl_pointer_release(_pointer);
		_pointer = nullptr;
	}
	if(_keyboard) {
		wl_keyboard_release(_keyboard);
		_keyboard = nullptr;
	}
	if(_seat) {
		wl_seat_release(_seat);
		_seat = nullptr;
	}
	if(_xkbState) {
		xkb_state_unref(_xkbState);
		_xkbState = nullptr;
	}
	if(_xkbContext) {
		xkb_context_unref(_xkbContext);
		_xkbContext = nullptr;
	}
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

#elif defined(USE_PLATFORM_SDL2)

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



VulkanWindow::~VulkanWindow()
{
	destroy();

#if defined(USE_PLATFORM_WAYLAND)
	delete _listeners;
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
		if(!DestroyWindow(HWND(_hwnd)))
			assert(0 && "DestroyWindow(): The function failed.");
		_hwnd = nullptr;
	}
# endif

#elif defined(USE_PLATFORM_XLIB)

	// release resources
	if(_window) {
		vulkanWindowMap.erase(_window);
		XDestroyWindow(_display, _window);
		_window = 0;
	}

#elif defined(USE_PLATFORM_WAYLAND)

	// erase from global map
	surface2windowMap.erase(_wlSurface);
	if(windowUnderPointer == this)
		windowUnderPointer = nullptr;

	// release resources
	if(_scheduledFrameCallback) {
		wl_callback_destroy(_scheduledFrameCallback);
		_scheduledFrameCallback = nullptr;
	}
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

#elif defined(USE_PLATFORM_SDL2)

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


VulkanWindow::VulkanWindow(VulkanWindow&& other)
{
#if defined(USE_PLATFORM_WIN32)

	// move Win32 members
	_hwnd = other._hwnd;
	other._hwnd = nullptr;
	_framePendingState = other._framePendingState;
	_visible = other._visible;
	_hiddenWindowFramePending = other._hiddenWindowFramePending;

	// update pointers to this object
	if(_hwnd)
		SetWindowLongPtr(HWND(_hwnd), 0, LONG_PTR(this));
	for(VulkanWindow*& w : framePendingWindows)
		if(w == &other) {
			w = this;
			break;
		}

#elif defined(USE_PLATFORM_XLIB)

	// move Xlib members
	_window = other._window;
	other._window = 0;
	_framePending = other._framePending;
	_visible = other._visible;
	_fullyObscured = other._fullyObscured;
	_iconVisible = other._iconVisible;
	_minimized = other._minimized;

	// update pointers to this object
	if(_window != 0)
		vulkanWindowMap[_window] = this;

#elif defined(USE_PLATFORM_WAYLAND)

	// move Wayland members
	_wlSurface = other._wlSurface;
	other._wlSurface = nullptr;
	_xdgSurface = other._xdgSurface;
	other._xdgSurface = nullptr;
	_xdgTopLevel = other._xdgTopLevel;
	other._xdgTopLevel = nullptr;
	_decoration = other._decoration;
	other._decoration = nullptr;
	_scheduledFrameCallback = other._scheduledFrameCallback;
	other._scheduledFrameCallback = nullptr;
	_listeners = other._listeners;
	if(_listeners) {
		other._listeners = nullptr;
		_listeners->vulkanWindow = this;
	}
	_forcedFrame = other._forcedFrame;
	_title = move(other._title);

	// update global map
	auto it = surface2windowMap.find(_wlSurface);
	if(it != surface2windowMap.end())
		it->second = this;

#elif defined(USE_PLATFORM_SDL2)

	// move SDL members
	_window = other._window;
	other._window = nullptr;
	_framePending = other._framePending;
	_hiddenWindowFramePending = other._hiddenWindowFramePending;
	_visible = other._visible;
	_minimized = other._minimized;

	// update pointer to this object
	if(_window)
		SDL_SetWindowData(_window, windowPointerName, this);

#elif defined(USE_PLATFORM_GLFW)

	// move GLFW members
	_window = other._window;
	other._window = nullptr;
	_framePendingState = other._framePendingState;
	_visible = other._visible;
	_minimized = other._minimized;

	// update pointers to this object
	if(_window)
		glfwSetWindowUserPointer(_window, this);
	for(VulkanWindow*& w : framePendingWindows)
		if(w == &other) {
			w = this;
			break;
		}

#elif defined(USE_PLATFORM_QT)

	// move Qt members
	_window = other._window;
	if(_window) {
		other._window = nullptr;
		static_cast<QtRenderingWindow*>(_window)->vulkanWindow = this;
	}

#endif

	// move members
	_frameCallback = move(other._frameCallback);
	_instance = move(other._instance);
	_physicalDevice = move(other._physicalDevice);
	_device = move(other._device);
	_surface = other._surface;
	other._surface = nullptr;
	_surfaceExtent = other._surfaceExtent;
	_swapchainResizePending = other._swapchainResizePending;
	_recreateSwapchainCallback = move(other._recreateSwapchainCallback);
	_closeCallback = move(other._closeCallback);
}


VulkanWindow& VulkanWindow::operator=(VulkanWindow&& other) noexcept
{
	// destroy previous content
	destroy();

#if defined(USE_PLATFORM_WIN32)

	// move Win32 members
	_hwnd = other._hwnd;
	other._hwnd = nullptr;
	_framePendingState = other._framePendingState;
	_visible = other._visible;
	_hiddenWindowFramePending = other._hiddenWindowFramePending;

	// update pointers to this object
	if(_hwnd)
		SetWindowLongPtr(HWND(_hwnd), 0, LONG_PTR(this));
	for(VulkanWindow*& w : framePendingWindows)
		if(w == &other) {
			w = this;
			break;
		}

#elif defined(USE_PLATFORM_XLIB)

	// move Xlib members
	_window = other._window;
	other._window = 0;
	_framePending = other._framePending;
	_visible = other._visible;
	_fullyObscured = other._fullyObscured;
	_iconVisible = other._iconVisible;
	_minimized = other._minimized;

	// update pointers to this object
	if(_window != 0)
		vulkanWindowMap[_window] = this;

#elif defined(USE_PLATFORM_WAYLAND)

	// move Wayland members
	_wlSurface = other._wlSurface;
	other._wlSurface = nullptr;
	_xdgSurface = other._xdgSurface;
	other._xdgSurface = nullptr;
	_xdgTopLevel = other._xdgTopLevel;
	other._xdgTopLevel = nullptr;
	_decoration = other._decoration;
	other._decoration = nullptr;
	_scheduledFrameCallback = other._scheduledFrameCallback;
	other._scheduledFrameCallback = nullptr;
	WaylandListeners* tmp = _listeners;
	_listeners = other._listeners;
	other._listeners = tmp;
	if(_listeners)
		_listeners->vulkanWindow = this;
	if(other._listeners)
		other._listeners->vulkanWindow = &other;
	_forcedFrame = other._forcedFrame;
	_title = move(other._title);

	// update global map
	auto it = surface2windowMap.find(_wlSurface);
	if(it != surface2windowMap.end())
		it->second = this;

#elif defined(USE_PLATFORM_SDL2)

	// move SDL members
	_window = other._window;
	other._window = nullptr;
	_framePending = other._framePending;
	_hiddenWindowFramePending = other._hiddenWindowFramePending;
	_visible = other._visible;
	_minimized = other._minimized;

	// set pointer to this
	if(_window)
		SDL_SetWindowData(_window, windowPointerName, this);

#elif defined(USE_PLATFORM_GLFW)

	// move GLFW members
	_window = other._window;
	other._window = nullptr;
	_framePendingState = other._framePendingState;
	_visible = other._visible;
	_minimized = other._minimized;

	// update pointers to this object
	if(_window)
		glfwSetWindowUserPointer(_window, this);
	for(VulkanWindow*& w : framePendingWindows)
		if(w == &other) {
			w = this;
			break;
		}

#elif defined(USE_PLATFORM_QT)

	// move Qt members
	_window = other._window;
	if(_window) {
		other._window = nullptr;
		static_cast<QtRenderingWindow*>(_window)->vulkanWindow = this;
	}

#endif

	// move members
	_frameCallback = move(other._frameCallback);
	_instance = move(other._instance);
	_physicalDevice = move(other._physicalDevice);
	_device = move(other._device);
	_surface = other._surface;
	other._surface = nullptr;
	_surfaceExtent = other._surfaceExtent;
	_swapchainResizePending = other._swapchainResizePending;
	_recreateSwapchainCallback = move(other._recreateSwapchainCallback);
	_closeCallback = move(other._closeCallback);

	return *this;
}


vk::SurfaceKHR VulkanWindow::create(vk::Instance instance, vk::Extent2D surfaceExtent, const char* title)
{
	// asserts for valid usage
	assert(instance && "The parameter instance must not be null.");
#if defined(USE_PLATFORM_WIN32)
	assert(_windowClass && "VulkanWindow class was not initialized. Call VulkanWindow::init() before VulkanWindow::create().");
#elif defined(USE_PLATFORM_XLIB) || defined(USE_PLATFORM_WAYLAND)
	assert(_display && "VulkanWindow class was not initialized. Call VulkanWindow::init() before VulkanWindow::create().");
#elif defined(USE_PLATFORM_SDL2)
	assert(sdlInitialized && "VulkanWindow class was not initialized. Call VulkanWindow::init() before VulkanWindow::create().");
#elif defined(USE_PLATFORM_QT)
	assert(qGuiApplication && "VulkanWindow class was not initialized. Call VulkanWindow::init() before VulkanWindow::create().");
#endif

	// destroy any previous window data
	// (this makes calling init() multiple times safe operation)
	destroy();

	// set Vulkan instance
	_instance = instance;

	// set surface extent
	_surfaceExtent = surfaceExtent;

#if defined(USE_PLATFORM_WIN32)

	// init variables
	_framePendingState = FramePendingState::NotPending;
	_visible = false;
	_hiddenWindowFramePending = false;

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
			NULL, NULL, HINSTANCE(_hInstance), NULL  // hWndParent, hMenu, hInstance, lpParam
		);
	if(_hwnd == NULL)
		throw runtime_error("Cannot create window.");

	// store this pointer with the window data
	SetWindowLongPtr(HWND(_hwnd), 0, LONG_PTR(this));

	// create surface
	_surface =
		instance.createWin32SurfaceKHR(
			vk::Win32SurfaceCreateInfoKHR(
				vk::Win32SurfaceCreateFlagsKHR(),  // flags
				HINSTANCE(_hInstance),  // hinstance
				HWND(_hwnd)  // hwnd
			)
		);
	return _surface;

#elif defined(USE_PLATFORM_XLIB)

	// init variables
	_framePending = true;
	_visible = false;
	_fullyObscured = false;
	_iconVisible = false;
	_minimized = false;

	// create window
	XSetWindowAttributes attr;
	attr.event_mask = ExposureMask | StructureNotifyMask | VisibilityChangeMask | PropertyChangeMask |
	                  PointerMotionMask | ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask;
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
	if(vulkanWindowMap.emplace(_window, this).second == false)
		throw runtime_error("Window already exists.");
	XSetStandardProperties(_display, _window, title, title, None, NULL, 0, NULL);
	XSetWMProtocols(_display, _window, &_wmDeleteMessage, 1);

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

	// init variables
	_forcedFrame = false;
	_title = title;
	if(!_listeners) {
		 _listeners = new WaylandListeners;
		 _listeners->vulkanWindow = this;
	}

	// create wl surface
	_wlSurface = wl_compositor_create_surface(_compositor);
	if(_wlSurface == nullptr)
		throw runtime_error("wl_compositor_create_surface() failed.");

	// update surface2windowMap
	surface2windowMap.insert_or_assign(_wlSurface, this);

	// frame listener
	_listeners->frameListener = {
		.done =
			[](void *data, wl_callback* cb, uint32_t time)
			{
				cout << "c" << flush;
				VulkanWindow* w = reinterpret_cast<WaylandListeners*>(data)->vulkanWindow;
				w->_scheduledFrameCallback = nullptr;
				w->renderFrame();
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

#elif defined(USE_PLATFORM_SDL2)

	// init variables
	_framePending = true;
	_hiddenWindowFramePending = false;
	_visible = false;
	_minimized = false;

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
	if(!SDL_Vulkan_CreateSurface(_window, VkInstance(instance), reinterpret_cast<VkSurfaceKHR*>(&_surface)))
		throw runtime_error(string("VulkanWindow: SDL_Vulkan_CreateSurface() function failed. Error details: ") + SDL_GetError());

	return _surface;

#elif defined(USE_PLATFORM_GLFW)

	// init variables
	_framePendingState = FramePendingState::NotPending;
	_visible = true;
	_minimized = false;

	// create window
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
# if !defined(_WIN32)
	glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);  // disabled focus on show prevents fail inside
		// glfwShowWindow(): "Wayland: Focusing a window requires user interaction"; seen on glfw 3.3.8 and Kubuntu 22.10,
		// however we need the focus on show on Win32 to get proper window focus
# endif
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
				w->_closeCallback(*w);  // VulkanWindow object might be already destroyed when returning from the callback
			else {
				w->hide();
				VulkanWindow::exitMainLoop();
			}
		}
	);

	glfwSetCursorPosCallback(
		_window,
		[](GLFWwindow* window, double xpos, double ypos)
		{
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(glfwGetWindowUserPointer(window));
			int x = lround(xpos);
			int y = lround(ypos);
			if(w->_mouseState.posX != x ||
			   w->_mouseState.posY != y)
			{
				w->_mouseState.relX = x - w->_mouseState.posX;
				w->_mouseState.relY = y - w->_mouseState.posY;
				w->_mouseState.posX = x;
				w->_mouseState.posY = y;
				if(w->_mouseMoveCallback)
					w->_mouseMoveCallback(*w, w->_mouseState);
			}
		}
	);
	glfwSetMouseButtonCallback(
		_window,
		[](GLFWwindow* window, int button, int action, int mods)
		{
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(glfwGetWindowUserPointer(window));
			MouseButton::EnumType b;
			switch(button) {
			case GLFW_MOUSE_BUTTON_LEFT:   b = MouseButton::Left; break;
			case GLFW_MOUSE_BUTTON_RIGHT:  b = MouseButton::Right; break;
			case GLFW_MOUSE_BUTTON_MIDDLE: b = MouseButton::Middle; break;
			case GLFW_MOUSE_BUTTON_4:      b = MouseButton::X1; break;
			case GLFW_MOUSE_BUTTON_5:      b = MouseButton::X2; break;
			default: b = MouseButton::Unknown;
			}
			ButtonAction a = (action == GLFW_PRESS) ? ButtonAction::Down : ButtonAction::Up;
			w->_mouseState.buttons.set(b, action == GLFW_PRESS);
			if(w->_mouseButtonCallback)
				w->_mouseButtonCallback(*w, b, a, w->_mouseState);
		}
	);
	glfwSetScrollCallback(
		_window,
		[](GLFWwindow* window, double xoffset, double yoffset) {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(glfwGetWindowUserPointer(window));
			if(w->_mouseWheelCallback)
				w->_mouseWheelCallback(*w, -lround(xoffset*120), lround(yoffset*120), w->_mouseState);
		}
	);
	glfwSetKeyCallback(
		_window,
		[](GLFWwindow* window, int key, int scanCode, int action, int mods) {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(glfwGetWindowUserPointer(window));
			if(action != GLFW_REPEAT) {
				if(key >= GLFW_KEY_LEFT_SHIFT && key <= GLFW_KEY_RIGHT_SUPER) {
					bool down = (action == GLFW_PRESS);
					switch(key) {
					case GLFW_KEY_LEFT_SHIFT:    w->_mouseState.mods.set(Modifier::Shift, down); break;
					case GLFW_KEY_LEFT_CONTROL:  w->_mouseState.mods.set(Modifier::Ctrl,  down); break;
					case GLFW_KEY_LEFT_ALT:      w->_mouseState.mods.set(Modifier::Alt,   down); break;
					case GLFW_KEY_LEFT_SUPER:    w->_mouseState.mods.set(Modifier::Meta,  down); break;
					case GLFW_KEY_RIGHT_SHIFT:   w->_mouseState.mods.set(Modifier::Shift, down); break;
					case GLFW_KEY_RIGHT_CONTROL: w->_mouseState.mods.set(Modifier::Ctrl,  down); break;
					case GLFW_KEY_RIGHT_ALT:     w->_mouseState.mods.set(Modifier::Alt,   down); break;
					case GLFW_KEY_RIGHT_SUPER:   w->_mouseState.mods.set(Modifier::Meta,  down); break;
					}
				}
				if(w->_keyCallback)
					w->_keyCallback(*w, (action == GLFW_PRESS) ? KeyAction::Down : KeyAction::Up, scanCode, key, "");
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
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

	// recreate swapchain if requested
	if(_swapchainResizePending) {

		// make sure that we finished all the rendering
		// (this is necessary for swapchain re-creation)
		_device.waitIdle();

		// get surface capabilities
		// On Win32 and Xlib, currentExtent, minImageExtent and maxImageExtent of returned surfaceCapabilites are all equal.
		// It means that we can create a new swapchain only with imageExtent being equal to the window size.
		// The currentExtent might become 0,0 on Win32 and Xlib platform, for example, when the window is minimized.
		// If the currentExtent is not 0,0, both width and height must be greater than 0.
		// On Wayland, currentExtent might be 0xffffffff, 0xffffffff with the meaning that the window extent
		// will be determined by the extent of the swapchain.
		// Wayland's minImageExtent is 1,1 and maxImageExtent is the maximum supported surface size.
		vk::SurfaceCapabilitiesKHR surfaceCapabilities(_physicalDevice.getSurfaceCapabilitiesKHR(_surface));

#if defined(USE_PLATFORM_WIN32)
		_surfaceExtent = surfaceCapabilities.currentExtent;
#elif defined(USE_PLATFORM_XLIB)
		_surfaceExtent = surfaceCapabilities.currentExtent;
#elif defined(USE_PLATFORM_WAYLAND)
		// do nothing here
		// because _surfaceExtent is set in _xdgToplevelListener's configure callback
#elif defined(USE_PLATFORM_SDL2)
		// get surface size using SDL
		SDL_Vulkan_GetDrawableSize(_window, reinterpret_cast<int*>(&_surfaceExtent.width), reinterpret_cast<int*>(&_surfaceExtent.height));
		_surfaceExtent.width  = clamp(_surfaceExtent.width,  surfaceCapabilities.minImageExtent.width,  surfaceCapabilities.maxImageExtent.width);
		_surfaceExtent.height = clamp(_surfaceExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
#elif defined(USE_PLATFORM_GLFW)
		// get surface size using GLFW
		// (0xffffffff values might be returned on Wayland)
		if(surfaceCapabilities.currentExtent.width != 0xffffffff && surfaceCapabilities.currentExtent.height != 0xffffffff)
			_surfaceExtent = surfaceCapabilities.currentExtent;
		else {
			glfwGetFramebufferSize(_window, reinterpret_cast<int*>(&_surfaceExtent.width), reinterpret_cast<int*>(&_surfaceExtent.height));
			checkError("glfwGetFramebufferSize");
			_surfaceExtent.width  = clamp(_surfaceExtent.width,  surfaceCapabilities.minImageExtent.width,  surfaceCapabilities.maxImageExtent.width);
			_surfaceExtent.height = clamp(_surfaceExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
		}
#elif defined(USE_PLATFORM_QT)
		// get surface size using Qt
		// (0xffffffff values might be returned on Wayland)
		if(surfaceCapabilities.currentExtent.width != 0xffffffff && surfaceCapabilities.currentExtent.height != 0xffffffff)
			_surfaceExtent = surfaceCapabilities.currentExtent;
		else {
			QSize size = _window->size();
			auto ratio = _window->devicePixelRatio();
			_surfaceExtent = vk::Extent2D(uint32_t(float(size.width()) * ratio + 0.5f), uint32_t(float(size.height()) * ratio + 0.5f));
			_surfaceExtent.width  = clamp(_surfaceExtent.width,  surfaceCapabilities.minImageExtent.width,  surfaceCapabilities.maxImageExtent.width);
			_surfaceExtent.height = clamp(_surfaceExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
		}
		cout << "New Qt window size in device independent pixels: " << _window->width() << "x" << _window->height()
		     << ", in physical pixels: " << _surfaceExtent.width << "x" << _surfaceExtent.height << endl;
#endif

		// zero size swapchain is not allowed,
		// so we will repeat the resize and rendering attempt after the next window resize
		// (this may happen on Win32-based and Xlib-based systems, for instance;
		// in reality, it never happened on my KDE 5.80.0 (Kubuntu 21.04) and KDE 5.44.0 (Kubuntu 18.04.5)
		// because window minimalizing just unmaps the window)
		if(_surfaceExtent == vk::Extent2D(0,0))
			return;  // new frame will be scheduled on the next window resize

		// recreate swapchain
		_swapchainResizePending = false;
		_recreateSwapchainCallback(*this, surfaceCapabilities, _surfaceExtent);
	}

	// render scene
#if !defined(USE_PLATFORM_QT)
	_frameCallback(*this);
#else
# if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
	qVulkanInstance->presentAboutToBeQueued(_window);
# endif
	_frameCallback(*this);
	qVulkanInstance->presentQueued(_window);
#endif
}


#if defined(USE_PLATFORM_WIN32)


void VulkanWindow::show()
{
	// asserts for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	// show window
	ShowWindow(HWND(_hwnd), SW_SHOW);
}


void VulkanWindow::hide()
{
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

	// hide window
	ShowWindow(HWND(_hwnd), SW_HIDE);
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
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

	if(_framePendingState == FramePendingState::Pending)
		return;

	if(_framePendingState == FramePendingState::NotPending) {

		// invalidate window content (this will cause WM_PAINT message to be sent)
		if(!InvalidateRect(HWND(_hwnd), NULL, FALSE))
			throw runtime_error("InvalidateRect(): The function failed.");

		framePendingWindows.push_back(this);
	}

	_framePendingState = FramePendingState::Pending;
}


#elif defined(USE_PLATFORM_XLIB)


void VulkanWindow::show()
{
	// asserts for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	if(_visible)
		return;

	// show window
	_visible = true;
	_iconVisible = true;
	_fullyObscured = false;
	XMapWindow(_display, _window);
	if(_minimized)
		XIconifyWindow(_display, _window, XDefaultScreen(_display));
	else
		scheduleFrame();
}


void VulkanWindow::hide()
{
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

	if(!_visible && !_iconVisible)
		return;

	_visible = false;
	_iconVisible = false;
	_fullyObscured = true;

	// update _minimized
	updateMinimized();

	// unmap window and hide taskbar icon
	XWithdrawWindow(_display, _window, XDefaultScreen(_display));

	// delete all Expose events and cancel any pending frames
	XEvent tmp;
	while(XCheckTypedWindowEvent(_display, _window, Expose, &tmp) == True);
	_framePending = false;
}


void VulkanWindow::updateMinimized()
{
	// read the new value of WM_STATE property
	unsigned char* data;
	Atom actualType;
	int actualFormat;
	unsigned long itemsRead;
	unsigned long bytesAfter;
	if(XGetWindowProperty(
			_display,  // display
			_window,  // window
			_wmStateProperty,  // property
			0, 1,  // long_offset, long_length
			False,  // delete
			_wmStateProperty, // req_type
			&actualType,  // actual_type_return
			&actualFormat,  // actual_format_return
			&itemsRead,  // nitems_return
			&bytesAfter,  // bytes_after_return
			&data  // prop_return
		) == Success && itemsRead > 0)
	{
		cout << "New WM_STATE: " << *reinterpret_cast<unsigned*>(data) << endl;
		_minimized = *reinterpret_cast<unsigned*>(data) == 3;
		XFree(data);
	}
	else
		cout << "WM_STATE reading failed" << endl;
}


void VulkanWindow::mainLoop()
{
	// mouse functions
	auto handleModifiers =
		[](VulkanWindow* w, unsigned int state)
		{
			w->_mouseState.mods.set(VulkanWindow::Modifier::Ctrl,  state & ControlMask);
			w->_mouseState.mods.set(VulkanWindow::Modifier::Shift, state & ShiftMask);
			w->_mouseState.mods.set(VulkanWindow::Modifier::Alt,   state & (Mod1Mask|Mod5Mask));
			w->_mouseState.mods.set(VulkanWindow::Modifier::Meta,  state & Mod4Mask);
		};
	auto handleMouseMove =
		[](VulkanWindow* w, int newX, int newY)
		{
			if(w->_mouseState.posX != newX ||
				w->_mouseState.posY != newY)
			{
				w->_mouseState.relX = newX - w->_mouseState.posX;
				w->_mouseState.relY = newY - w->_mouseState.posY;
				w->_mouseState.posX = newX;
				w->_mouseState.posY = newY;
				if(w->_mouseMoveCallback)
					w->_mouseMoveCallback(*w, w->_mouseState);
			}
		};
	auto getMouseButton =
		[](unsigned int button) -> MouseButton::EnumType
		{
			switch(button) {
			case Button1: return MouseButton::Left;
			case Button2: return MouseButton::Middle;
			case Button3: return MouseButton::Right;
			case 8: return MouseButton::X1;
			case 9: return MouseButton::X2;
			default: return MouseButton::Unknown;
			}
		};

	// run Xlib event loop
	XEvent e;
	running = true;
	while(running) {

		// get event
		XNextEvent(_display, &e);

		// get VulkanWindow
		// (we use std::map because per-window data using XGetWindowProperty() would require X-server roundtrip)
		auto it = vulkanWindowMap.find(e.xany.window);
		if(it == vulkanWindowMap.end())
			continue;
		VulkanWindow* w = it->second;

		// expose event
		if(e.type == Expose)
		{
			// remove all other Expose events
			XEvent tmp;
			while(XCheckTypedWindowEvent(_display, w->_window, Expose, &tmp) == True);

			// perform rendering
			w->_framePending = false;
			w->renderFrame();
			continue;
		}

		// configure event
		if(e.type == ConfigureNotify) {
			if(e.xconfigure.width != w->_surfaceExtent.width || e.xconfigure.height != w->_surfaceExtent.height) {
				cout << "Configure event " << e.xconfigure.width << "x" << e.xconfigure.height << endl;
				w->scheduleSwapchainResize();
			}
			continue;
		}

		// mouse events
		if(e.type == MotionNotify) {
			handleModifiers(w, e.xmotion.state);
			handleMouseMove(w, e.xmotion.x, e.xmotion.y);
			continue;
		}
		if(e.type == ButtonPress) {
			cout << "state: " << e.xbutton.state << endl;
			handleModifiers(w, e.xbutton.state);
			handleMouseMove(w, e.xbutton.x, e.xbutton.y);
			if(e.xbutton.button < Button4 || e.xbutton.button > 7) {
				MouseButton::EnumType button = getMouseButton(e.xbutton.button);
				w->_mouseState.buttons.set(button, true);
				if(w->_mouseButtonCallback)
					w->_mouseButtonCallback(*w, button, ButtonAction::Down, w->_mouseState);
			}
			else {
				int wheelX, wheelY;
				if(e.xbutton.button <= Button5) {
					wheelX = 0;
					wheelY = (e.xbutton.button == Button5) ? -120 : 120;
				}
				else {
					wheelX = (e.xbutton.button == 6) ? -120 : 120;
					wheelY = 0;
				}
				if(w->_mouseWheelCallback)
					w->_mouseWheelCallback(*w, wheelX, wheelY, w->_mouseState);
			}
			continue;
		}
		if(e.type == ButtonRelease) {
			handleModifiers(w, e.xbutton.state);
			handleMouseMove(w, e.xbutton.x, e.xbutton.y);
			if(e.xbutton.button < Button4 || e.xbutton.button > 7) {
				MouseButton::EnumType button = getMouseButton(e.xbutton.button);
				w->_mouseState.buttons.set(button, false);
				if(w->_mouseButtonCallback)
					w->_mouseButtonCallback(*w, button, ButtonAction::Up, w->_mouseState);
			}
			continue;
		}

		// keyboard events
		if(e.type == KeyPress) {

			// callback
			if(w->_keyCallback) {
#if 0
				uint16_t s = XLookupKeysym(&e.xkey, e.xkey.state);
				char text[8];
				int l = xkb_keysym_to_utf8(s, text, sizeof(text)-1);
				text[l] = 0;
#else
				KeySym s;
				char text[8];
				XLookupString(&e.xkey, text, sizeof(text)-1, &s, nullptr);
				int l = xkb_keysym_to_utf8(s, text, sizeof(text)-1);
				text[l] = 0;
#endif
				w->_keyCallback(*w, KeyAction::Down, e.xkey.keycode, s, text);
			}

			continue;
		}
		if(e.type == KeyRelease) {

			// skip auto-repeat key events
			if(XEventsQueued(_display, QueuedAfterReading)) {
				XEvent nextEvent;
				XPeekEvent(_display, &nextEvent);
				if(nextEvent.type == KeyPress && nextEvent.xkey.time == e.xkey.time &&
				   nextEvent.xkey.keycode == e.xkey.keycode)
				{
					XNextEvent(_display, &nextEvent);
					continue;
				}
			}

			// callback
			if(w->_keyCallback) {
				uint16_t s = XLookupKeysym(&e.xkey, 0);
				char text[8];
				int l = xkb_keysym_to_utf8(s, text, sizeof(text)-1);
				text[l] = 0;
				w->_keyCallback(*w, KeyAction::Up, e.xkey.keycode, s, text);
			}
			continue;
		}

		// map, unmap, obscured, unobscured
		if(e.type == MapNotify)
		{
			cout<<"MapNotify"<<endl;
			if(w->_visible)
				continue;
			w->_visible = true;
			w->_fullyObscured = false;
			w->scheduleFrame();
			continue;
		}
		if(e.type == UnmapNotify)
		{
			cout<<"UnmapNotify"<<endl;
			if(w->_visible == false)
				continue;
			w->_visible = false;
			w->_fullyObscured = true;

			XEvent tmp;
			while(XCheckTypedWindowEvent(_display, w->_window, Expose, &tmp) == True);
			w->_framePending = false;
			continue;
		}
		if(e.type == VisibilityNotify) {
			if(e.xvisibility.state != VisibilityFullyObscured)
			{
				cout << "Window not fully obscured" << endl;
				w->_fullyObscured = false;
				w->scheduleFrame();
				continue;
			}
			else {
				cout << "Window fully obscured" << endl;
				w->_fullyObscured = true;
				XEvent tmp;
				while(XCheckTypedWindowEvent(_display, w->_window, Expose, &tmp) == True);
				w->_framePending = false;
				continue;
			}
		}

		// minimize state
		if(e.type == PropertyNotify) {
			if(e.xproperty.atom == _wmStateProperty && e.xproperty.state == PropertyNewValue)
				w->updateMinimized();
			continue;
		}

		// handle window close
		if(e.type==ClientMessage && ulong(e.xclient.data.l[0])==_wmDeleteMessage) {
			if(w->_closeCallback)
				w->_closeCallback(*w);  // VulkanWindow object might be already destroyed when returning from the callback
			else {
				w->hide();
				VulkanWindow::exitMainLoop();
			}
			continue;
		}
	}
}


void VulkanWindow::exitMainLoop()
{
	running = false;
}


void VulkanWindow::scheduleFrame()
{
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

	if(_framePending || !_visible || _fullyObscured)
		return;

	_framePending = true;

	XSendEvent(
		_display,  // display
		_window,  // w
		False,  // propagate
		ExposureMask,  // event_mask
		(XEvent*)(&(const XExposeEvent&)  // event_send
			XExposeEvent{
				Expose,  // type
				0,  // serial
				True,  // send_event
				_display,  // display
				_window,  // window
				0, 0,  // x, y
				0, 0,  // width, height
				0  // count
			})
	);
}


#elif defined(USE_PLATFORM_WAYLAND)


void VulkanWindow::show()
{
	// asserts for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	// check for already shown window
	if(_xdgSurface)
		return;

	// create xdg surface
	_xdgSurface = xdg_wm_base_get_xdg_surface(_xdgWmBase, _wlSurface);
	if(_xdgSurface == nullptr)
		throw runtime_error("xdg_wm_base_get_xdg_surface() failed.");
	_listeners->xdgSurfaceListener = {
		.configure =
			[](void* data, xdg_surface* xdgSurface, uint32_t serial) {
				cout << "surface configure" << endl;
				VulkanWindow* w = reinterpret_cast<WaylandListeners*>(data)->vulkanWindow;
				xdg_surface_ack_configure(xdgSurface, serial);
				wl_surface_commit(w->_wlSurface);

				// we need to explicitly generate frame
				// the first frame otherwise the window is not shown
				// and no frame callbacks will be delivered through _frameListener
				if(w->_forcedFrame) {
					w->_forcedFrame = false;
					w->renderFrame();
				}
			},
	};
	if(xdg_surface_add_listener(_xdgSurface, &_listeners->xdgSurfaceListener, _listeners))
		throw runtime_error("xdg_surface_add_listener() failed.");

	// create xdg toplevel
	_xdgTopLevel = xdg_surface_get_toplevel(_xdgSurface);
	if(_xdgTopLevel == nullptr)
		throw runtime_error("xdg_surface_get_toplevel() failed.");
	if(_zxdgDecorationManagerV1) {
		_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(_zxdgDecorationManagerV1, _xdgTopLevel);
		zxdg_toplevel_decoration_v1_set_mode(_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	xdg_toplevel_set_title(_xdgTopLevel, _title.c_str());
	_listeners->xdgToplevelListener = {
		.configure =
			[](void* data, xdg_toplevel* toplevel, int32_t width, int32_t height, wl_array*) -> void
			{
				cout << "toplevel configure (width=" << width << ", height=" << height << ")" << endl;

				// if width or height of the window changed,
				// schedule swapchain resize and force new frame rendering
				// (width and height of zero means that the compositor does not know the window dimension)
				VulkanWindow* w = reinterpret_cast<WaylandListeners*>(data)->vulkanWindow;
				if(uint32_t(width) != w->_surfaceExtent.width && width != 0) {
					w->_surfaceExtent.width = width;
					if(uint32_t(height) != w->_surfaceExtent.height && height != 0)
						w->_surfaceExtent.height = height;
					w->scheduleSwapchainResize();
				}
				else if(uint32_t(height) != w->_surfaceExtent.height && height != 0) {
					w->_surfaceExtent.height = height;
					w->scheduleSwapchainResize();
				}
			},
		.close =
			[](void* data, xdg_toplevel* xdgTopLevel) {
				VulkanWindow* w = reinterpret_cast<WaylandListeners*>(data)->vulkanWindow;
				if(w->_closeCallback)
					w->_closeCallback(*w);  // VulkanWindow object might be already destroyed when returning from the callback
				else {
					w->hide();
					VulkanWindow::exitMainLoop();
				}
			},
	};
	if(xdg_toplevel_add_listener(_xdgTopLevel, &_listeners->xdgToplevelListener, _listeners))
		throw runtime_error("xdg_toplevel_add_listener() failed.");
	wl_surface_commit(_wlSurface);
	_forcedFrame = true;
	_swapchainResizePending = true;
}


void VulkanWindow::hide()
{
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

	// destroy xdg toplevel
	// and associated objects
	_forcedFrame = false;
	if(_scheduledFrameCallback) {
		wl_callback_destroy(_scheduledFrameCallback);
		_scheduledFrameCallback = nullptr;
	}
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
		wl_surface_attach(_wlSurface, nullptr, 0, 0);
		wl_surface_commit(_wlSurface);
	}
}


void VulkanWindow::mainLoop()
{
	// flush outgoing buffers
	cout << "Entering main loop." << endl;
	if(wl_display_flush(_display) == -1)
		throw runtime_error("wl_display_flush() failed.");

	// main loop
	running = true;
	while(running) {

		// dispatch events
		if(wl_display_dispatch(_display) == -1)  // it blocks if there are no events
			throw runtime_error("wl_display_dispatch() failed.");

		// flush outgoing buffers
		if(wl_display_flush(_display) == -1)
			throw runtime_error("wl_display_flush() failed.");

	}
	cout << "Main loop left." << endl;
}


void VulkanWindow::exitMainLoop()
{
	running = false;
}


void VulkanWindow::scheduleFrame()
{
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

	if(_scheduledFrameCallback)
		return;

	cout << "s" << flush;
	_scheduledFrameCallback = wl_surface_frame(_wlSurface);
	wl_callback_add_listener(_scheduledFrameCallback, &_listeners->frameListener, _listeners);
	wl_surface_commit(_wlSurface);
}


#elif defined(USE_PLATFORM_SDL2)


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
	// asserts for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::mainLoop().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::mainLoop() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::mainLoop().");

	// show window
	// and set _visible flag immediately
	SDL_ShowWindow(_window);
	_visible = true;
}


void VulkanWindow::hide()
{
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

	// hide window
	// and set _visible flag immediately
	SDL_HideWindow(_window);
	_visible = false;
}


void VulkanWindow::mainLoop()
{
	// mouse functions
	auto handleModifiers =
		[](VulkanWindow* w) -> void
		{
			SDL_Keymod m = SDL_GetModState();
			w->_mouseState.mods.set(VulkanWindow::Modifier::Ctrl,  m & (KMOD_LCTRL|KMOD_RCTRL));
			w->_mouseState.mods.set(VulkanWindow::Modifier::Shift, m & (KMOD_LSHIFT|KMOD_RSHIFT));
			w->_mouseState.mods.set(VulkanWindow::Modifier::Alt,   m & (KMOD_LALT|KMOD_RALT));
			w->_mouseState.mods.set(VulkanWindow::Modifier::Meta,  m & (KMOD_LGUI|KMOD_RGUI));
		};
	auto handleMouseMove =
		[](VulkanWindow* w, int newX, int newY) -> void
		{
			if(w->_mouseState.posX != newX ||
			   w->_mouseState.posY != newY)
			{
				w->_mouseState.relX = newX - w->_mouseState.posX;
				w->_mouseState.relY = newY - w->_mouseState.posY;
				w->_mouseState.posX = newX;
				w->_mouseState.posY = newY;
				if(w->_mouseMoveCallback)
					w->_mouseMoveCallback(*w, w->_mouseState);
			}
		};
	auto handleMouseButton =
		[](VulkanWindow* w, SDL_Event& event, ButtonAction downOrUp) -> void
		{
			// button
			MouseButton::EnumType mouseButton;
			switch(event.button.button) {
			case SDL_BUTTON_LEFT:   mouseButton = MouseButton::Left; break;
			case SDL_BUTTON_RIGHT:  mouseButton = MouseButton::Right; break;
			case SDL_BUTTON_MIDDLE: mouseButton = MouseButton::Middle; break;
			case SDL_BUTTON_X1:     mouseButton = MouseButton::X1; break;
			case SDL_BUTTON_X2:     mouseButton = MouseButton::X2; break;
			default: mouseButton = MouseButton::Unknown;
			}

			// callback with new button state
			w->_mouseState.buttons.set(mouseButton, downOrUp==ButtonAction::Down);
			if(w->_mouseButtonCallback)
				w->_mouseButtonCallback(*w, mouseButton, downOrUp, w->_mouseState);
		};

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
				w->_minimized = false;
				if(w->_hiddenWindowFramePending) {
					w->_hiddenWindowFramePending = false;
					w->scheduleFrame();
				}
				break;
			}

			case SDL_WINDOWEVENT_HIDDEN: {
				cout << "Hidden event" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
					SDL_GetWindowData(SDL_GetWindowFromID(event.window.windowID), windowPointerName));
				w->_visible = false;
				if(w->_framePending) {
					w->_hiddenWindowFramePending = true;
					w->_framePending = false;
				}
				break;
			}

			case SDL_WINDOWEVENT_MINIMIZED: {
				cout << "Minimized event" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
					SDL_GetWindowData(SDL_GetWindowFromID(event.window.windowID), windowPointerName));
				w->_minimized = true;
				if(w->_framePending) {
					w->_hiddenWindowFramePending = true;
					w->_framePending = false;
				}
				break;
			}

			case SDL_WINDOWEVENT_RESTORED: {
				cout << "Restored event" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
					SDL_GetWindowData(SDL_GetWindowFromID(event.window.windowID), windowPointerName));
				w->_minimized = false;
				if(w->_hiddenWindowFramePending) {
					w->_hiddenWindowFramePending = false;
					w->scheduleFrame();
				}
				break;
			}

			case SDL_WINDOWEVENT_CLOSE: {
				cout << "Close event" << endl;
				VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
					SDL_GetWindowData(SDL_GetWindowFromID(event.window.windowID), windowPointerName));
				if(w->_closeCallback)
					w->_closeCallback(*w);  // VulkanWindow object might be already destroyed when returning from the callback
				else {
					w->hide();
					VulkanWindow::exitMainLoop();
				}
				break;
			}

			}
			break;

		case SDL_MOUSEMOTION: {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
				SDL_GetWindowData(SDL_GetWindowFromID(event.motion.windowID), windowPointerName));
			handleModifiers(w);
			handleMouseMove(w, event.motion.x, event.motion.y);
			break;
		}
		case SDL_MOUSEBUTTONDOWN: {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
				SDL_GetWindowData(SDL_GetWindowFromID(event.button.windowID), windowPointerName));
			handleModifiers(w);
			handleMouseMove(w, event.button.x, event.button.y);
			handleMouseButton(w, event, ButtonAction::Down);
			break;
		}
		case SDL_MOUSEBUTTONUP: {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
				SDL_GetWindowData(SDL_GetWindowFromID(event.button.windowID), windowPointerName));
			handleModifiers(w);
			handleMouseMove(w, event.button.x, event.button.y);
			handleMouseButton(w, event, ButtonAction::Up);
			break;
		}
		case SDL_MOUSEWHEEL:
		{
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
				SDL_GetWindowData(SDL_GetWindowFromID(event.button.windowID), windowPointerName));

			if(w->_mouseWheelCallback)
			{
				handleModifiers(w);

				// handle wheel rotation
				// (value is relative since last wheel event)
#if SDL_MAJOR_VERSION == 2 || SDL_MINOR_VERSION >= 18
				int wheelX = lround(event.wheel.preciseX*120);
				int wheelY = lround(event.wheel.preciseY*120);
#else
				int wheelX = event.wheel.x*120;
				int wheelY = event.wheel.y*120;
#endif
				w->_mouseWheelCallback(*w, wheelX, wheelY, w->_mouseState);
			}
			break;
		}

		case SDL_KEYDOWN: {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
				SDL_GetWindowData(SDL_GetWindowFromID(event.key.windowID), windowPointerName));
			if(w->_keyCallback && event.key.repeat == 0)
				w->_keyCallback(*w, KeyAction::Down, event.key.keysym.scancode, event.key.keysym.sym, "");
			break;
		}
		case SDL_KEYUP: {
			VulkanWindow* w = reinterpret_cast<VulkanWindow*>(
				SDL_GetWindowData(SDL_GetWindowFromID(event.key.windowID), windowPointerName));
			if(w->_keyCallback && event.key.repeat == 0)
				w->_keyCallback(*w, KeyAction::Up, event.key.keysym.scancode, event.key.keysym.sym, "");
			break;
		}

		case SDL_QUIT:  // SDL_QUIT is generated on variety of reasons, including SIGINT and SIGTERM, or pressing Command-Q on Mac OS X.
		                // By default, it would also be generated by SDL on the last window close.
		                // This would interfere with VulkanWindow expected behaviour to exit main loop after the call of exitMainLoop().
		                // So, we disabled it by SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE hint that is available since SDL 2.0.22.
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
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

	if(_framePending)
		return;

	// handle invisible and minimized window
	if(_visible==false || _minimized) {
		_hiddenWindowFramePending = true;
		return;
	}

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
	// asserts for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");
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
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

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
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

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
	// asserts for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");
	assert(_recreateSwapchainCallback && "Recreate swapchain callback need to be set before VulkanWindow::show() call. Please, call VulkanWindow::setRecreateSwapchainCallback() before VulkanWindow::show().");
	assert(_frameCallback && "Frame callback need to be set before VulkanWindow::show() call. Please, call VulkanWindow::setFrameCallback() before VulkanWindow::show().");

	// show window
	_window->setVisible(true);
}


void VulkanWindow::hide()
{
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

	// hide window
	_window->setVisible(false);
}


bool VulkanWindow::isVisible() const
{
	return _window->isVisible();
}


void VulkanWindow::mainLoop()
{
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


bool QtRenderingWindow::event(QEvent* event)
{
	try {

		// mouse functions
		auto getMouseButton =
			[](QMouseEvent* e) -> VulkanWindow::MouseButton::EnumType {
				switch(e->button()) {
				case Qt::LeftButton: return VulkanWindow::MouseButton::Left;
				case Qt::RightButton: return VulkanWindow::MouseButton::Right;
				case Qt::MiddleButton: return VulkanWindow::MouseButton::Middle;
				case Qt::XButton1: return VulkanWindow::MouseButton::X1;
				case Qt::XButton2: return VulkanWindow::MouseButton::X2;
				default: return VulkanWindow::MouseButton::Unknown;
				}
			};
		auto handleModifiers =
			[](VulkanWindow* vulkanWindow, QInputEvent* e)
			{
				Qt::KeyboardModifiers m = e->modifiers();
				vulkanWindow->_mouseState.mods.set(VulkanWindow::Modifier::Ctrl,  m & Qt::ControlModifier);
				vulkanWindow->_mouseState.mods.set(VulkanWindow::Modifier::Shift, m & Qt::ShiftModifier);
				vulkanWindow->_mouseState.mods.set(VulkanWindow::Modifier::Alt,   m & Qt::AltModifier);
				vulkanWindow->_mouseState.mods.set(VulkanWindow::Modifier::Meta,  m & Qt::MetaModifier);
			};
		auto handleMouseMove =
			[](VulkanWindow* vulkanWindow, int newX, int newY)
			{
				if(vulkanWindow->_mouseState.posX != newX ||
				   vulkanWindow->_mouseState.posY != newY)
				{
					vulkanWindow->_mouseState.relX = newX - vulkanWindow->_mouseState.posX;
					vulkanWindow->_mouseState.relY = newY - vulkanWindow->_mouseState.posY;
					vulkanWindow->_mouseState.posX = newX;
					vulkanWindow->_mouseState.posY = newY;
					if(vulkanWindow->_mouseMoveCallback)
						vulkanWindow->_mouseMoveCallback(*vulkanWindow, vulkanWindow->_mouseState);
				}
			};
		auto handleMouseButton =
			[](VulkanWindow* vulkanWindow, QMouseEvent* e, VulkanWindow::MouseButton::EnumType mouseButton, VulkanWindow::ButtonAction downOrUp) -> bool
			{
				// handle mouse move, if any
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
				QPoint p = e->position().toPoint();
#else
				QPoint p = e->pos();
#endif
				if(vulkanWindow->_mouseState.posX != p.x() ||
				   vulkanWindow->_mouseState.posY != p.y())
				{
					vulkanWindow->_mouseState.relX = p.x() - vulkanWindow->_mouseState.posX;
					vulkanWindow->_mouseState.relY = p.y() - vulkanWindow->_mouseState.posY;
					vulkanWindow->_mouseState.posX = p.x();
					vulkanWindow->_mouseState.posY = p.y();
					if(vulkanWindow->_mouseMoveCallback)
						vulkanWindow->_mouseMoveCallback(*vulkanWindow, vulkanWindow->_mouseState);
				}

				// callback with new button state
				vulkanWindow->_mouseState.buttons.set(mouseButton, downOrUp==VulkanWindow::ButtonAction::Down);
				if(vulkanWindow->_mouseButtonCallback)
					vulkanWindow->_mouseButtonCallback(*vulkanWindow, mouseButton, downOrUp, vulkanWindow->_mouseState);
				return true;

			};

		// handle verious events
		switch(event->type()) {

		case QEvent::Type::Timer:
			cout<<"t";
			killTimer(timer);
			timer = 0;
			if(isExposed())
				vulkanWindow->renderFrame();
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
			// schedule swapchain resize
			// (we do not call vulkanWindow->scheduleSwapchainResize() directly
			// as Resize event might be delivered already inside VulkanWindow::create() function,
			// resulting in assert error of invalid usage of scheduleFrame(); instead, we schedule frame manually)
			vulkanWindow->_swapchainResizePending = true;
			scheduleFrameTimer();
			return QWindow::event(event);
		}

		// handle mouse events
		case QEvent::Type::MouseMove: {
			QMouseEvent* e = static_cast<QMouseEvent*>(event);
			handleModifiers(vulkanWindow, e);
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
			QPoint p = e->position().toPoint();
#else
			QPoint p = e->pos();
#endif
			handleMouseMove(vulkanWindow, p.x(), p.y());
			return true;
		}
		case QEvent::Type::MouseButtonPress: {
			QMouseEvent* e = static_cast<QMouseEvent*>(event);
			handleModifiers(vulkanWindow, e);
			return handleMouseButton(vulkanWindow, e, getMouseButton(e), VulkanWindow::ButtonAction::Down);
		}
		case QEvent::Type::MouseButtonRelease: {
			QMouseEvent* e = static_cast<QMouseEvent*>(event);
			handleModifiers(vulkanWindow, e);
			return handleMouseButton(vulkanWindow, e, getMouseButton(e), VulkanWindow::ButtonAction::Up);
		}
		case QEvent::Type::Wheel: {

			QWheelEvent* e = static_cast<QWheelEvent*>(event);
			handleModifiers(vulkanWindow, e);

			// handle mouse move, if any
			QPoint p = e->position().toPoint();
			handleMouseMove(vulkanWindow, p.x(), p.y());

			// handle wheel rotation
			// (value is relative since last wheel event)
			if(vulkanWindow->_mouseWheelCallback) {
				p = e->angleDelta();
				vulkanWindow->_mouseWheelCallback(*vulkanWindow, -p.x(), p.y(), vulkanWindow->_mouseState);
			}
			return true;

		}

		// handle key events
		case QEvent::Type::KeyPress: {
			if(vulkanWindow->_keyCallback) {
				QKeyEvent *k = static_cast<QKeyEvent*>(event);
				if(!k->isAutoRepeat())
					vulkanWindow->_keyCallback(*vulkanWindow, VulkanWindow::KeyAction::Down, k->nativeScanCode(), k->nativeVirtualKey(), k->text().toStdString());
			}
			return true;
		}
		case QEvent::Type::KeyRelease: {
			if(vulkanWindow->_keyCallback) {
				QKeyEvent *k = static_cast<QKeyEvent*>(event);
				if(!k->isAutoRepeat())
					vulkanWindow->_keyCallback(*vulkanWindow, VulkanWindow::KeyAction::Up, k->nativeScanCode(), k->nativeVirtualKey(), k->text().toStdString());
			}
			return true;
		}

		// hide window on close
		// (we must not really close it as Vulkan surface would be destroyed
		// and this would make a problem as swapchain still exists and Vulkan
		// requires the swapchain to be destroyed first)
		case QEvent::Type::Close:
			if(vulkanWindow->_closeCallback)
				vulkanWindow->_closeCallback(*vulkanWindow);  // VulkanWindow object might be already destroyed when returning from the callback
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


void QtRenderingWindow::scheduleFrameTimer()
{
	// start zero timeout timer
	if(timer == 0) {
		timer = startTimer(0);
		if(timer == 0)
			throw runtime_error("VulkanWindow::scheduleNextFrame(): Cannot allocate timer.");
	}
}


void VulkanWindow::scheduleFrame()
{
	// assert for valid usage
	assert(_surface && "VulkanWindow::_surface is null, indicating invalid VulkanWindow object. Call VulkanWindow::create() to initialize it.");

	// start zero timeout timer
	static_cast<QtRenderingWindow*>(_window)->scheduleFrameTimer();
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
