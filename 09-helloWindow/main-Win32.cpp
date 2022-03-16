#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.hpp>
#include <array>
#include <iostream>

using namespace std;

static vk::UniqueInstance instance;
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
		auto wndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
			switch(msg)
			{
				case WM_CLOSE:
					DestroyWindow(hwnd);
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
		WNDCLASSEX wc;
		wc.cbSize        = sizeof(WNDCLASSEX);
		wc.style         = 0;
		wc.lpfnWndProc   = wndProc;
		wc.cbClsExtra    = 0;
		wc.cbWndExtra    = 0;
		wc.hInstance     = hInstance;
		wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
		wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = NULL;
		wc.lpszMenuName  = NULL;
		wc.lpszClassName = "HelloWindow";
		wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
		windowClass.handle = RegisterClassEx(&wc);
		if(!windowClass.handle)
			throw runtime_error("Can not register window class.");

		// create window
		window.handle = CreateWindowEx(
			WS_EX_CLIENTEDGE,  // dwExStyle
			MAKEINTATOM(windowClass.handle),  // lpClassName
			"Hello window!",  // lpWindowName
			WS_OVERLAPPEDWINDOW,  // dwStyle
			CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,  // X, Y, nWidth, nHeight
			NULL, NULL, hInstance, NULL);  // hWndParent, hMenu, hInstance, lpParam
		if(window == NULL)
			throw runtime_error("Can not create window.");

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
		while(GetMessage(&msg, NULL, 0, 0) > 0) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
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
