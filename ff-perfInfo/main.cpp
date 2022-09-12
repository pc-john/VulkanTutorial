#include <vulkan/vulkan.hpp>
#include <iostream>

using namespace std;


// constants
constexpr const char* appName = "ff-perfInfo";


// shader code in SPIR-V binary
static const uint32_t csSpirv[] = {
#include "shader.comp.spv"
};


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
			cout << "Vulkan instance version:  " << VK_VERSION_MAJOR(version) << "."
			     << VK_VERSION_MINOR(version) << "." << VK_VERSION_PATCH(version) << endl;
		} else
			cout << "Vulkan instance version:  1.0" << endl;

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
					{},  // flags
					&(const vk::ApplicationInfo&)vk::ApplicationInfo{
						appName,                 // application name
						VK_MAKE_VERSION(0,0,0),  // application version
						nullptr,                 // engine name
						VK_MAKE_VERSION(0,0,0),  // engine version
						VK_API_VERSION_1_0,      // api version
					},
					0, nullptr,  // no layers
					uint32_t(physicalDeviceProperties2Supported ? 1 : 0),  // enabledExtensionCount
					array{"VK_KHR_get_physical_device_properties2"}.data(),  // ppEnabledExtensionNames
				});

		struct InstanceFuncs : vk::DispatchLoaderBase {
			PFN_vkGetPhysicalDeviceProperties2KHR vkGetPhysicalDeviceProperties2KHR = PFN_vkGetPhysicalDeviceProperties2KHR(instance->getProcAddr("vkGetPhysicalDeviceProperties2KHR"));
		} vkFuncs;

		// print device info
		vector<vk::PhysicalDevice> deviceList = instance->enumeratePhysicalDevices();
		cout << "Number of Vulkan devices:  " << deviceList.size() << endl;
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
			cout << "   VendorID:  0x" << hex << p.vendorID;
			switch(p.vendorID) {
			case 0x1002: cout << " (AMD/ATI)"; break;
			case 0x10DE: cout << " (NVIDIA)"; break;
			case 0x8086: cout << " (Intel)"; break;
			case 0x10005: cout << " (Mesa)"; break;
			}
			cout << endl;
			cout << "   DeviceID:  0x" << p.deviceID << dec << endl;
			cout << "   Type:      " << to_string(p.deviceType) << endl;
			if(pciBusInfoSupported)
				cout << "   PCI info:  domain=" << pciBusInfo.pciDomain << ", pciBus=" << pciBusInfo.pciBus
				     << ", pciDevice=" << pciBusInfo.pciDevice << ", function=" << pciBusInfo.pciFunction << endl;
			else
				cout << "   PCI info:  n/a" << endl;

			// driver info
			cout << "   Vulkan version:  " << VK_VERSION_MAJOR(p.apiVersion) << "."
			     << VK_VERSION_MINOR(p.apiVersion) << "." << VK_VERSION_PATCH(p.apiVersion) << endl;
			cout << "   Driver version:  0x" << hex << p.driverVersion << dec << endl;
			if(driverPropertiesSupported) {
				cout << "   Driver name:  " << driverProperties.driverName << endl;
				cout << "   Driver info:  " << driverProperties.driverInfo << endl;
				cout << "   DriverID:     " << to_string(driverProperties.driverID) << endl;
				cout << "   Driver conformance version:  " << unsigned(driverProperties.conformanceVersion.major)
				     << "." << unsigned(driverProperties.conformanceVersion.minor) 
				     << "." << unsigned(driverProperties.conformanceVersion.subminor)
				     << "." << unsigned(driverProperties.conformanceVersion.patch) << endl;
			} else {
				cout << "   Driver name:  n/a" << endl;
				cout << "   Driver info:  n/a" << endl;
				cout << "   DriverID:     n/a" << endl;
				cout << "   Driver conformance version:  n/a" << endl;
			}

			// hardware info
			if(amdShaderCorePropertiesSupported) {
				cout << "   Shader Engine count:  " << amdShaderInfo.shaderEngineCount << endl;
				cout << "   Shader Arrays per Engine count:  " << amdShaderInfo.shaderArraysPerEngineCount << endl;
				cout << "   ComputeUnits per Shader Array:  " << amdShaderInfo.computeUnitsPerShaderArray << endl;
				cout << "   SIMDs per Compute Unit:  " << amdShaderInfo.simdPerComputeUnit << endl;
				cout << "   Wavefront slots in SIMD:  " << amdShaderInfo.wavefrontsPerSimd << endl;
				cout << "   Threads per wavefront:  " << amdShaderInfo.wavefrontSize << endl;
			}
			if(amdShaderCoreProperties2Supported) {
				cout << "   Active Compute Units:  " << amdShaderInfo2.activeComputeUnitCount << endl;
			}
			if(nvShaderSmBuiltinsSupported) {
				cout << "   Shader SM count:  " << nvShaderInfo.shaderSMCount << endl;
				cout << "   Shader Warps per SM:  " << nvShaderInfo.shaderWarpsPerSM << endl;
			}

			// get compute queue family
			uint32_t computeQueueFamily;
			vector<vk::QueueFamilyProperties> queueFamilyList = pd.getQueueFamilyProperties();
			for(uint32_t i=0, c=uint32_t(queueFamilyList.size()); i<c; i++) {

				// test for compute support
				if(queueFamilyList[i].queueFlags & vk::QueueFlagBits::eCompute) {
					computeQueueFamily = i;
					goto computeFamilyFound;
				}
			}
			cout << "   GFLOPS: n/a" << endl;
			continue;
		computeFamilyFound:

			// timestamp support
			uint32_t timestampValidBits = queueFamilyList[computeQueueFamily].timestampValidBits;
			float timestampPeriod_ns = p.limits.timestampPeriod;
			if(timestampValidBits == 0) {
				cout << "   GFLOPS: n/a (no timestamp support)" << endl;
				continue;
			}
			uint64_t timestampMask = timestampValidBits>=64 ? ~uint64_t(0) : (uint64_t(1) << timestampValidBits) - 1;

			// create device
			vk::UniqueDevice device =
				pd.createDeviceUnique(
					vk::DeviceCreateInfo{
						{},  // flags
						uint32_t(1),  // queueCreateInfoCount
						array{  // pQueueCreateInfos
							vk::DeviceQueueCreateInfo{
								{},  // flags
								computeQueueFamily,  // queueFamilyIndex
								1,  // queueCount
								&(const float&)1.f,  // pQueuePriorities
							},
						}.data(),
						0, nullptr,  // no layers
						0,  // number of enabled extensions
						nullptr,  // enabled extension names
						nullptr,    // enabled features
					}
				);

			// get queue
			vk::Queue computeQueue = device->getQueue(computeQueueFamily, 0);

			// shader module
			vk::UniqueShaderModule shader =
				device->createShaderModuleUnique(
					vk::ShaderModuleCreateInfo(
						{},  // flags
						sizeof(csSpirv),  // code size
						csSpirv  // pCode
					)
				);

			// descriptor set layout
			vk::UniqueDescriptorSetLayout descriptorSetLayout =
				device->createDescriptorSetLayoutUnique(
					vk::DescriptorSetLayoutCreateInfo(
						{},  // flags
						1,  // bindingCount
						&vk::DescriptorSetLayoutBinding(
							0,  // binding
							vk::DescriptorType::eStorageBuffer,  // descriptorType
							1,  // descriptorCount
							vk::ShaderStageFlagBits::eCompute,  // stageFlags
							nullptr  // pImmutableSamplers
						)
					)
				);

			// pipeline layout
			vk::UniquePipelineLayout pipelineLayout =
				device->createPipelineLayoutUnique(
					vk::PipelineLayoutCreateInfo{
						{},      // flags
						1,       // setLayoutCount
						&descriptorSetLayout.get(),  // pSetLayouts
						0,       // pushConstantRangeCount
						nullptr  // pPushConstantRanges
					}
				);

			// create pipeline
			auto [ ignore, pipeline ] =
				device->createComputePipelineUnique(
					nullptr,  // pipelineCache
					vk::ComputePipelineCreateInfo(
						{},  // flags
						vk::PipelineShaderStageCreateInfo(  // stage
							{},  // flags
							vk::ShaderStageFlagBits::eCompute,  // stage
							shader.get(),  // module
							"main",  // pName
							nullptr  // pSpecializationInfo
						),
						pipelineLayout.get()  // layout
					)
				);

			// buffer
			vk::UniqueBuffer buffer =
				device->createBufferUnique(
					vk::BufferCreateInfo(
						{},  // flags
						4,  // size
						vk::BufferUsageFlagBits::eStorageBuffer,  // usage
						vk::SharingMode::eExclusive,  // sharingMode
						0,  // queueFamilyIndexCount
						nullptr  // pQueueFamilyIndices
					)
				);

			// memory for images
			vk::UniqueDeviceMemory memory =
				[](vk::Buffer buffer, vk::MemoryPropertyFlags requiredFlags, vk::PhysicalDevice physicalDevice, vk::Device device)
				{
					vk::MemoryRequirements memoryRequirements = device.getBufferMemoryRequirements(buffer);
					vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
					for(uint32_t i=0; i<memoryProperties.memoryTypeCount; i++)
						if(memoryRequirements.memoryTypeBits & (1<<i))
							if((memoryProperties.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags)
								return
									device.allocateMemoryUnique(
										vk::MemoryAllocateInfo(
											memoryRequirements.size,  // allocationSize
											i                         // memoryTypeIndex
										)
									);
					throw std::runtime_error("No suitable memory type found for the buffer.");
				}
				(buffer.get(), vk::MemoryPropertyFlagBits::eDeviceLocal, pd, device.get());

			// bind buffer memory
			device->bindBufferMemory(
				buffer.get(),  // image
				memory.get(),  // memory
				0              // memoryOffset
			);

			// descriptor set
			vk::UniqueDescriptorPool descriptorPool =
				device->createDescriptorPoolUnique(
					vk::DescriptorPoolCreateInfo(
						{},  // flags
						1,  // maxSets
						1,  // poolSizeCount
						array<vk::DescriptorPoolSize, 1>{  // pPoolSizes
							vk::DescriptorPoolSize(
								vk::DescriptorType::eStorageBuffer,  // type
								1  // descriptorCount
							),
						}.data()
					)
				);
			vk::DescriptorSet descriptorSet =
				device->allocateDescriptorSets(
					vk::DescriptorSetAllocateInfo(
						descriptorPool.get(),  // descriptorPool
						1,  // descriptorSetCount
						&descriptorSetLayout.get()  // pSetLayouts
					)
				)[0];
			device->updateDescriptorSets(
				vk::WriteDescriptorSet(  // descriptorWrites
					descriptorSet,  // dstSet
					0,  // dstBinding
					0,  // dstArrayElement
					1,  // descriptorCount
					vk::DescriptorType::eStorageBuffer,  // descriptorType
					nullptr,  // pImageInfo
					array<vk::DescriptorBufferInfo, 1>{  // pBufferInfo
						vk::DescriptorBufferInfo(
							buffer.get(),  // buffer
							0,  // offset
							4  // range
						),
					}.data(),
					nullptr  // pTexelBufferView
				),
				nullptr  // descriptorCopies
			);

			// timestamp pool
			vk::UniqueQueryPool timestampPool =
				device->createQueryPoolUnique(
					vk::QueryPoolCreateInfo(
						{},  // flags
						vk::QueryType::eTimestamp,  // queryType
						2,  // queryCount
						{}  // pipelineStatistics
					)
				);

			// command pool
			vk::UniqueCommandPool commandPool =
				device->createCommandPoolUnique(
					vk::CommandPoolCreateInfo(
						{},  // flags
						computeQueueFamily  // queueFamilyIndex
					)
				);

			// allocate command buffer
			vk::UniqueCommandBuffer commandBuffer = std::move(
				device->allocateCommandBuffersUnique(
					vk::CommandBufferAllocateInfo(
						commandPool.get(),                 // commandPool
						vk::CommandBufferLevel::ePrimary,  // level
						1                                  // commandBufferCount
					)
				)[0]);

			// record command buffer
			commandBuffer->begin(
				vk::CommandBufferBeginInfo(
					{},  // flags
					nullptr  // pInheritanceInfo
				)
			);
			commandBuffer->resetQueryPool(timestampPool.get(), 0, 2);
			commandBuffer->bindPipeline(
				vk::PipelineBindPoint::eCompute,  // pipelineBindPoint
				pipeline.get()  // pipeline
			);
			commandBuffer->bindDescriptorSets(
				vk::PipelineBindPoint::eCompute,  // pipelineBindPoint
				pipelineLayout.get(),  // layout
				0,  // firstSet
				descriptorSet,  // descriptorSets
				nullptr  // dynamicOffsets
			);
			commandBuffer->writeTimestamp(
				vk::PipelineStageFlagBits::eTopOfPipe,  // pipelineStage
				timestampPool.get(),  // queryPool
				0  // query
			);
			commandBuffer->dispatch(100, 100, 1);
			commandBuffer->writeTimestamp(
				vk::PipelineStageFlagBits::eBottomOfPipe,  // pipelineStage
				timestampPool.get(),  // queryPool
				1  // query
			);
			commandBuffer->end();

			// fence
			vk::UniqueFence computingFinishedFence =
				device->createFenceUnique(
					vk::FenceCreateInfo(
						{}  // flags
					)
				);

			uint64_t bestTsDelta = UINT64_MAX;
			array<uint64_t, 2> timestamps;
			uint64_t finishTS;
			do {

				// submit work
				computeQueue.submit(
					vk::SubmitInfo(  // submits
						0, nullptr, nullptr,       // waitSemaphoreCount, pWaitSemaphores, pWaitDstStageMask
						1, &commandBuffer.get(),   // commandBufferCount, pCommandBuffers
						0, nullptr                 // signalSemaphoreCount, pSignalSemaphores
					),
					computingFinishedFence.get()  // fence
				);

				// wait for the work
				vk::Result r = device->waitForFences(
					computingFinishedFence.get(),  // fences (vk::ArrayProxy)
					VK_TRUE,       // waitAll
					uint64_t(3e9)  // timeout (3s)
				);
				if(r == vk::Result::eTimeout)
					throw std::runtime_error("GPU timeout. Task is probably hanging.");
				device->resetFences(computingFinishedFence.get());

				// read timestamps
				r =
					device->getQueryPoolResults(
						timestampPool.get(),  // queryPool
						0,  // firstQuery
						2,  // queryCount
						2*sizeof(uint64_t),  // dataSize
						timestamps.data(),  // pData
						sizeof(uint64_t),  // stride
						vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait  // flags
					);
				if(r != vk::Result::eSuccess)
					throw std::runtime_error("vkGetQueryPoolResults() did not finish with VK_SUCCESS result.");

				// finishTS
				// make it 1 second from start of the measurement
				if(bestTsDelta == UINT64_MAX)
					finishTS = uint64_t(1e9/timestampPeriod_ns) + timestamps[0];

				// timestamps delta
				uint64_t tsDelta = (timestamps[1] - timestamps[0]) & timestampMask;
				if(tsDelta < bestTsDelta)
					bestTsDelta = tsDelta;

			} while(timestamps[1] < finishTS);

			cout << "   time: " << double(bestTsDelta) * timestampPeriod_ns * 1e-6 << "ms" << endl;
			cout << "   GFLOPS: " << (20000. * 64 * 100 * 100) / (double(bestTsDelta) * timestampPeriod_ns * 1e-9) * 1e-9 << endl;
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
