#include <vulkan/vulkan.hpp>
#include <iostream>

using namespace std;


// vk::Instance
// (we destroy it as the last one)
static vk::UniqueInstance instance;


// main function of the application
int main(int, char**)
{
	// catch exceptions
	// (vulkan.hpp functions throw if they fail)
	try {

		// Vulkan version
		auto vkEnumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
			vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
		if(vkEnumerateInstanceVersion) {
			uint32_t version;
			vkEnumerateInstanceVersion(&version);
			cout << "Vulkan instance version: " << VK_VERSION_MAJOR(version) << "."
			     << VK_VERSION_MINOR(version) << "." << VK_VERSION_PATCH(version) << endl;
		} else
			cout << "Vulkan version: 1.0" << endl;

		// physical_device_properties2 support
		bool physicalDeviceProperties2Supported = false;

		// detect instance extension support
		auto extensionList = vk::enumerateInstanceExtensionProperties();
		for(vk::ExtensionProperties& e : extensionList) {
			if(strcmp(e.extensionName, "VK_KHR_get_physical_device_properties2") == 0)
				physicalDeviceProperties2Supported = true;
		}

		// Vulkan instance
		instance =
			vk::createInstanceUnique(
				vk::InstanceCreateInfo{
					vk::InstanceCreateFlags(),  // flags
					&(const vk::ApplicationInfo&)vk::ApplicationInfo{
						"ff-perfInfo",         // application name
						VK_MAKE_VERSION(0,0,0),  // application version
						nullptr,                 // engine name
						VK_MAKE_VERSION(0,0,0),  // engine version
						VK_API_VERSION_1_0,      // api version
					},
					0, nullptr,  // no layers
					uint32_t(physicalDeviceProperties2Supported ? 1 : 0),  // enabledExtensionCount
					array{"VK_KHR_get_physical_device_properties2"}.data(),        // ppEnabledExtensionNames
				});

		struct InstanceFuncs : vk::DispatchLoaderBase {
			PFN_vkGetPhysicalDeviceProperties2KHR vkGetPhysicalDeviceProperties2KHR = PFN_vkGetPhysicalDeviceProperties2KHR(instance->getProcAddr("vkGetPhysicalDeviceProperties2KHR"));
		} vkFuncs;

		// print device info
		vector<vk::PhysicalDevice> deviceList = instance->enumeratePhysicalDevices();
		cout << "Number of Vulkan devices: " << deviceList.size() << endl;
		for(vk::PhysicalDevice pd : deviceList) {

			// variables of extension support
			bool driverPropertiesSupported = false;
			bool pciBusInfoSupported = false;
			bool amdShaderCorePropertiesSupported = false;
			bool amdShaderCoreProperties2Supported = false;
			bool nvShaderSmBuiltinsSupported = false;

			// detect extension support
			auto extensionList = pd.enumerateDeviceExtensionProperties();
			for(vk::ExtensionProperties& e : extensionList) {
				if(strcmp(e.extensionName, "VK_KHR_driver_properties") == 0)
					driverPropertiesSupported = true;
				if(strcmp(e.extensionName, "VK_EXT_pci_bus_info") == 0)
					pciBusInfoSupported = true;
				if(strcmp(e.extensionName, "VK_AMD_shader_core_properties") == 0)
					amdShaderCorePropertiesSupported = true;
				if(strcmp(e.extensionName, "VK_AMD_shader_core_properties2") == 0)
					amdShaderCoreProperties2Supported = true;
				if(strcmp(e.extensionName, "VK_NV_shader_sm_builtins") == 0)
					nvShaderSmBuiltinsSupported = true;
			}

			// device properties structures
			vk::PhysicalDeviceProperties2KHR properties2;
			vk::PhysicalDeviceDriverPropertiesKHR driverProperties;
			vk::PhysicalDevicePCIBusInfoPropertiesEXT pciBusInfo;
			vk::PhysicalDeviceShaderCorePropertiesAMD amdShaderInfo;
			vk::PhysicalDeviceShaderCoreProperties2AMD amdShaderInfo2;
			vk::PhysicalDeviceShaderSMBuiltinsPropertiesNV nvShaderInfo;
			vk::PhysicalDeviceProperties& p = properties2.properties;

			// link structures
			auto* lastStructure = reinterpret_cast<vk::BaseOutStructure*>(&properties2);
			if(driverPropertiesSupported) {
				lastStructure->pNext = reinterpret_cast<vk::BaseOutStructure*>(&driverProperties);
				lastStructure = reinterpret_cast<vk::BaseOutStructure*>(&driverProperties);
			}
			if(pciBusInfoSupported) {
				lastStructure->pNext = reinterpret_cast<vk::BaseOutStructure*>(&pciBusInfo);
				lastStructure = reinterpret_cast<vk::BaseOutStructure*>(&pciBusInfo);
			}
			if(amdShaderCorePropertiesSupported) {
				lastStructure->pNext = reinterpret_cast<vk::BaseOutStructure*>(&amdShaderInfo);
				lastStructure = reinterpret_cast<vk::BaseOutStructure*>(&amdShaderInfo);
			}
			if(amdShaderCoreProperties2Supported) {
				lastStructure->pNext = reinterpret_cast<vk::BaseOutStructure*>(&amdShaderInfo2);
				lastStructure = reinterpret_cast<vk::BaseOutStructure*>(&amdShaderInfo2);
			}
			if(nvShaderSmBuiltinsSupported) {
				lastStructure->pNext = reinterpret_cast<vk::BaseOutStructure*>(&nvShaderInfo);
				lastStructure = reinterpret_cast<vk::BaseOutStructure*>(&nvShaderInfo);
			}

			// get properties
			if(physicalDeviceProperties2Supported)
				pd.getProperties2KHR(&properties2, vkFuncs);
			else
				pd.getProperties(&p);

			// device info
			cout << endl;
			cout << p.deviceName << ":" << endl;
			cout << "   VendorID: 0x" << hex << p.vendorID;
			switch(p.vendorID) {
			case 0x1002: cout << " (AMD/ATI)"; break;
			case 0x10DE: cout << " (NVIDIA)"; break;
			case 0x8086: cout << " (Intel)"; break;
			case 0x10005: cout << " (Mesa)"; break;
			}
			cout << endl;
			cout << "   DeviceID: 0x" << p.deviceID << dec << endl;
			cout << "   Type:     " << to_string(p.deviceType) << endl;
			cout << "   PCI info: domain=" << pciBusInfo.pciDomain << ", pciBus=" << pciBusInfo.pciBus
			     << ", pciDevice=" << pciBusInfo.pciDevice << ", function=" << pciBusInfo.pciFunction << endl;

			// driver info
			cout << "   Vulkan version: " << VK_VERSION_MAJOR(p.apiVersion) << "."
			     << VK_VERSION_MINOR(p.apiVersion) << "." << VK_VERSION_PATCH(p.apiVersion) << endl;
			cout << "   Driver version: 0x" << hex << p.driverVersion << dec << endl;
			cout << "   Driver name: " << driverProperties.driverName << endl;
			cout << "   Driver info: " << driverProperties.driverInfo << endl;
			cout << "   DriverID:    " << to_string(driverProperties.driverID) << endl;
			cout << "   Driver conformance version: " << unsigned(driverProperties.conformanceVersion.major)
			     << "." << unsigned(driverProperties.conformanceVersion.minor) 
			     << "." << unsigned(driverProperties.conformanceVersion.subminor)
			     << "." << unsigned(driverProperties.conformanceVersion.patch) << endl;

			// hardware info
			if(amdShaderCorePropertiesSupported) {
				cout << "   Shader Engine count: " << amdShaderInfo.shaderEngineCount << endl;
				cout << "   Shader Arrays per Engine count: " << amdShaderInfo.shaderArraysPerEngineCount << endl;
				cout << "   ComputeUnits per Shader Array: " << amdShaderInfo.computeUnitsPerShaderArray << endl;
				cout << "   SIMDs per Compute Unit: " << amdShaderInfo.simdPerComputeUnit << endl;
				cout << "   Wavefront slots in SIMD: " << amdShaderInfo.wavefrontsPerSimd << endl;
				cout << "   Threads per wavefront: " << amdShaderInfo.wavefrontSize << endl;
			}
			if(amdShaderCoreProperties2Supported) {
				cout << "   Active Compute Units: " << amdShaderInfo2.activeComputeUnitCount << endl;
			}
			if(nvShaderSmBuiltinsSupported) {
				cout << "   Shader SM count: " << nvShaderInfo.shaderSMCount << endl;
				cout << "   Shader Warps per SM: " << nvShaderInfo.shaderWarpsPerSM << endl;
			}

		}

	// catch exceptions
	} catch(vk::Error& e) {
		cout << "Failed because of Vulkan exception: " << e.what() << endl;
	} catch(exception& e) {
		cout << "Failed because of exception: " << e.what() << endl;
	} catch(...) {
		cout << "Failed because of unspecified exception." << endl;
	}

	return 0;
}
