#pragma once

#include <cstdint>
#include <tuple>
#include <vulkan/vulkan.hpp>


class TimestampGenerator {
protected:
	vk::Device _device;
	bool _useCalibratedTimestamps;
	vk::TimeDomainEXT _hostTimeDomain;
	vk::Queue _queue;
	uint32_t _queueFamilyIndex;
	vk::QueryPool _timestampQueryPool;
	vk::CommandPool _precompiledCommandPool;
	vk::CommandBuffer _readTimestampCommandBuffer;
	vk::Fence _fence;
	struct VkFuncs : vk::DispatchLoaderBase {
		PFN_vkGetCalibratedTimestampsEXT vkGetCalibratedTimestampsEXT;
	} vkFuncs;
public:

	TimestampGenerator() = default;
	TimestampGenerator(vk::Device device);
	~TimestampGenerator();
	void init(vk::Device device, vk::Queue queue, uint32_t queueFamilyIndex,
		bool useCalibratedTimestamps, vk::TimeDomainEXT hostTimeDomain, uint32_t numTSToRecord);
	void destroy() noexcept;

	std::tuple<uint64_t, uint64_t> getCalibratedTimestamps();
	uint64_t getCpuTimestamp();
	uint64_t getGpuTimestamp();
	double getCpuTimestampPeriod();

	void writeGpuTimestamp(vk::CommandBuffer commandBuffer,
		vk::PipelineStageFlagBits pipelineStage, uint32_t valueIndex);
	uint64_t readGpuTimestamp(uint32_t valueIndex);
	void readGpuTimestamps(uint32_t numTimestamps, uint64_t* timestamps);
	void readGpuTimestamps(uint32_t firstIndex, uint32_t numTimestamps, uint64_t* timestamps);
};


inline TimestampGenerator::~TimestampGenerator()  { destroy(); }
inline uint64_t TimestampGenerator::readGpuTimestamp(uint32_t valueIndex)  { uint64_t v; readGpuTimestamps(valueIndex, 1, &v); return v; }
inline void TimestampGenerator::readGpuTimestamps(uint32_t numTimestamps, uint64_t* timestamps)  { readGpuTimestamps(0, numTimestamps, timestamps); }
