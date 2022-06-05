#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.hpp>
#include <array>
#include <iostream>
#include <tchar.h>

using namespace std;

// vk::Instance
// (we destroy it as the last one)
static vk::UniqueInstance instance;

// handles and atoms
static HINSTANCE hInstance = NULL;
static struct UniqueWindowClass {
	ATOM handle = 0;
	~UniqueWindowClass()  { if(handle) UnregisterClass(MAKEINTATOM(handle), hInstance); }
	operator ATOM() const  { return handle; }
} windowClass;
static struct UniqueWindow {
	HWND handle = nullptr;
	~UniqueWindow()  { if(handle) DestroyWindow(handle); }
	operator HWND() const  { return handle; }
} window;
static exception_ptr wndProcException = nullptr;

// Vulkan window surface
static vk::UniqueSurfaceKHR surface;


int main(int, char**)
{
	// catch exceptions
	// (vulkan.hpp functions throws if they fail)
	try {

		// Vulkan instance
		instance =
			vk::createInstanceUnique(
				vk::InstanceCreateInfo{
					vk::InstanceCreateFlags(),  // flags
					&(const vk::ApplicationInfo&)vk::ApplicationInfo{
						"09-helloWindow-Win32",  // application name
						VK_MAKE_VERSION(0,0,0),  // application version
						nullptr,                 // engine name
						VK_MAKE_VERSION(0,0,0),  // engine version
						VK_API_VERSION_1_0,      // api version
					},
					0, nullptr,  // no layers
					2,           // enabled extension count
					array<const char*, 2>{  // enabled extension names
						"VK_KHR_surface",
						"VK_KHR_win32_surface",
					}.data(),
				});

		// window's message handling procedure
		auto wndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept -> LRESULT {
			switch(msg)
			{
				case WM_CLOSE:
					if(!DestroyWindow(hwnd))
						wndProcException = make_exception_ptr(runtime_error("DestroyWindow(): The function failed."));
					window.handle = nullptr;
					return 0;

				case WM_DESTROY:
					PostQuitMessage(0);
					return 0;

				default:
					return DefWindowProc(hwnd, msg, wParam, lParam);
			}
		};

		// register window class
		hInstance = GetModuleHandle(NULL);
		windowClass.handle =
			RegisterClassEx(
				&(const WNDCLASSEX&)WNDCLASSEX{
					sizeof(WNDCLASSEX),  // cbSize
					0,                   // style
					wndProc,             // lpfnWndProc
					0,                   // cbClsExtra
					0,                   // cbWndExtra
					hInstance,           // hInstance
					LoadIcon(NULL, IDI_APPLICATION),  // hIcon
					LoadCursor(NULL, IDC_ARROW),  // hCursor
					NULL,                // hbrBackground
					NULL,                // lpszMenuName
					_T("HelloWindow"),   // lpszClassName
					LoadIcon(NULL, IDI_APPLICATION)  // hIconSm
				}
			);
		if(!windowClass.handle)
			throw runtime_error("Cannot register window class.");

		// create window
		window.handle =
			CreateWindowEx(
				WS_EX_CLIENTEDGE,  // dwExStyle
				MAKEINTATOM(windowClass.handle),  // lpClassName
				_T("Hello window!"),  // lpWindowName
				WS_OVERLAPPEDWINDOW,  // dwStyle
				CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,  // X, Y, nWidth, nHeight
				NULL, NULL, hInstance, NULL  // hWndParent, hMenu, hInstance, lpParam
			);
		if(window == NULL)
			throw runtime_error("Cannot create window.");

		// create surface
		surface =
			instance->createWin32SurfaceKHRUnique(
				vk::Win32SurfaceCreateInfoKHR(
					vk::Win32SurfaceCreateFlagsKHR(),  // flags
					hInstance,  // hinstance
					window  // hwnd
				)
			);

		// find compatible devices
		vector<vk::PhysicalDevice> deviceList = instance->enumeratePhysicalDevices();
		vector<string> compatibleDevices;
		for(vk::PhysicalDevice pd : deviceList) {
			uint32_t numQueues;
			pd.getQueueFamilyProperties(&numQueues, nullptr);
			for(uint32_t i=0; i<numQueues; i++)
				if(pd.getSurfaceSupportKHR(i, surface.get())) {
					compatibleDevices.push_back(pd.getProperties().deviceName);
					break;
				}
		}
		cout << "Compatible devices:" << endl;
		for(string& name : compatibleDevices)
			cout << "   " << name << endl;

		// show window
		ShowWindow(window, SW_SHOWDEFAULT);

		// run event loop
		MSG msg;
		BOOL r;
		while((r = GetMessage(&msg, NULL, 0, 0)) != 0) {

			// handle GetMessage() errors
			if(r == -1)
				throw runtime_error("GetMessage(): The function failed.");

			// handle message
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			// handle exceptions raised in window procedure
			if(wndProcException)
				rethrow_exception(wndProcException);
		}

	// catch exceptions
	} catch(vk::Error &e) {
		cout << "Failed because of Vulkan exception: " << e.what() << endl;
	} catch(exception &e) {
		cout << "Failed because of exception: " << e.what() << endl;
	} catch(...) {
		cout << "Failed because of unspecified exception." << endl;
	}

	return 0;
}
