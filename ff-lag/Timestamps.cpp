#include "Timestamps.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN  // exclude rarely-used services inclusion by windows.h; this speeds up compilation and avoids some compilation problems
#include <windows.h>  // we include windows.h only at the end of file to avoid compilation problems; windows.h define MemoryBarrier, near, far and many other problematic macros
#endif
#include <vulkan/vulkan.hpp>

using namespace std;


void TimestampGenerator::init(vk::Device device, vk::Queue queue, uint32_t queueFamilyIndex,
	bool useCalibratedTimestamps, vk::TimeDomainEXT hostTimeDomain, uint32_t numTSToRecord)
{
	destroy();

	_device = device;
	_useCalibratedTimestamps = useCalibratedTimestamps;
	_hostTimeDomain = hostTimeDomain;
	_queue = queue;
	_queueFamilyIndex = queueFamilyIndex;
	vkFuncs.vkGetCalibratedTimestampsEXT = PFN_vkGetCalibratedTimestampsEXT(device.getProcAddr("vkGetCalibratedTimestampsEXT"));
	
	// fence used throughout the class
	_fence =
		_device.createFence(vk::FenceCreateInfo{vk::FenceCreateFlags()});

	// timestamp query pool
	_timestampQueryPool =
		_device.createQueryPool(
			vk::QueryPoolCreateInfo(
				vk::QueryPoolCreateFlags(),  // flags
				vk::QueryType::eTimestamp,  // queryType
				numTSToRecord+1,  // queryCount (we use one record for getGpuTimestamp())
				vk::QueryPipelineStatisticFlags()  // pipelineStatistics
			)
		);

	// precompiledCommandPool and readTimestampCommandBuffer
	_precompiledCommandPool =
		_device.createCommandPool(
			vk::CommandPoolCreateInfo(
				vk::CommandPoolCreateFlagBits::eResetCommandBuffer,  // flags
				_queueFamilyIndex  // queueFamilyIndex
			)
		);
	_readTimestampCommandBuffer =
		_device.allocateCommandBuffers(
			vk::CommandBufferAllocateInfo(
				_precompiledCommandPool,             // commandPool
				vk::CommandBufferLevel::ePrimary,  // level
				1                                  // commandBufferCount
			)
		)[0];
	_readTimestampCommandBuffer.begin(
			vk::CommandBufferBeginInfo(
				vk::CommandBufferUsageFlags(),  // flags
				nullptr  // pInheritanceInfo
			)
		);
	_readTimestampCommandBuffer.resetQueryPool(
		_timestampQueryPool,  // queryPool
		0,  // firstQuery
		1  // queryCount
	);
	_readTimestampCommandBuffer.writeTimestamp(
		vk::PipelineStageFlagBits::eTopOfPipe,  // pipelineStage
		_timestampQueryPool,  // queryPool
		0  // query
	);
	_readTimestampCommandBuffer.end();
}


void TimestampGenerator::destroy() noexcept
{
	if(_precompiledCommandPool)
		_device.destroy(_precompiledCommandPool);
	if(_fence)
		_device.destroy(_fence);
	if(_timestampQueryPool)
		_device.destroy(_timestampQueryPool);
}


tuple<uint64_t, uint64_t> TimestampGenerator::getCalibratedTimestamps()
{
	// get calibrated timestamps
	if(_useCalibratedTimestamps) {
		array<uint64_t,2> ts;
		uint64_t maxDeviation;
		vk::Result r =
			_device.getCalibratedTimestampsEXT(
				2,  // timestampCount
				array{  // pTimestampInfos
					vk::CalibratedTimestampInfoEXT(vk::TimeDomainEXT::eDevice),
					vk::CalibratedTimestampInfoEXT(_hostTimeDomain),
				}.data(),
				ts.data(),  // pTimestamps
				&maxDeviation,  // pMaxDeviation
				vkFuncs  // dispatch
			);
		if(r != vk::Result::eSuccess)
			vk::throwResultException(r, "vk::Device::getCalibratedTimestampEXT");
		return {ts[0], ts[1]};
	}
	else
		return {getGpuTimestamp(), getCpuTimestamp()};
}


double TimestampGenerator::getCpuTimestampPeriod()
{
#ifdef _WIN32
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	return 1.0 / f.QuadPart;
#else
	return 1e-9;  // on Linux, we use clock_gettime()
#endif
}


uint64_t TimestampGenerator::getCpuTimestamp()
{
#ifdef _WIN32
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return uint64_t(counter.QuadPart);
#else
	struct timespec tv;
	clock_gettime(CLOCK_MONOTONIC_RAW, &tv);
	return tv.tv_nsec + tv.tv_sec*1000000000ull;
#endif
}


uint64_t TimestampGenerator::getGpuTimestamp()
{
	if(_useCalibratedTimestamps) {

		uint64_t ts;
		uint64_t maxDeviation;
		vk::Result r =
			_device.getCalibratedTimestampsEXT(
				1,  // timestampCount
				&(const vk::CalibratedTimestampInfoEXT&)vk::CalibratedTimestampInfoEXT(vk::TimeDomainEXT::eDevice),  // pTimestampInfos
				&ts,  // pTimestamps
				&maxDeviation,  // pMaxDeviation
				vkFuncs  // dispatch
			);
		if(r != vk::Result::eSuccess)
			vk::throwResultException(r, "vk::Device::getCalibratedTimestampEXT");
		return ts;

	}
	else {

		// submit command buffer
		_queue.submit(
			vk::SubmitInfo(  // submits (vk::ArrayProxy)
				0, nullptr, nullptr,              // waitSemaphoreCount,pWaitSemaphores,pWaitDstStageMask
				1, &_readTimestampCommandBuffer,  // commandBufferCount,pCommandBuffers
				0, nullptr                        // signalSemaphoreCount,pSignalSemaphores
			),
			_fence  // fence
		);

		// wait for work to complete
		vk::Result r = _device.waitForFences(
			_fence,        // fences (vk::ArrayProxy)
			VK_TRUE,       // waitAll
			uint64_t(3e9)  // timeout (3s)
		);
		_device.resetFences(_fence);
		if(r != vk::Result::eSuccess) {
			if(r == vk::Result::eTimeout)
				throw std::runtime_error("GPU timeout. Task is probably hanging.");
			throw std::runtime_error("vk::Device::waitForFences() returned strange success code.");	 // error codes are already handled by throw inside waitForFences()
		}

		// read timestamps
		uint64_t ts;
		r = _device.getQueryPoolResults(
			_timestampQueryPool,  // queryPool
			0,  // firstQuery
			1,  // queryCount
			1*sizeof(uint64_t),  // dataSize
			&ts,  // pData
			sizeof(uint64_t),  // stride
			vk::QueryResultFlagBits::e64  // flags
		);

		// throw if not success
		if(r != vk::Result::eSuccess)
			vk::throwResultException(r, "vk::Device::getQueryPoolResults");

		// return timestamp
		return ts;

	}
}


void TimestampGenerator::writeGpuTimestamp(vk::CommandBuffer commandBuffer,
	vk::PipelineStageFlagBits pipelineStage, uint32_t valueIndex)
{
	commandBuffer.resetQueryPool(
		_timestampQueryPool,  // queryPool
		valueIndex+1,  // firstQuery (index zero is used by getGpuTimestamp())
		1  // queryCount
	);
	commandBuffer.writeTimestamp(
		pipelineStage,  // pipelineStage
		_timestampQueryPool,  // queryPool
		valueIndex+1  // query (index zero is used by getGpuTimestamp())
	);
}


void TimestampGenerator::readGpuTimestamps(uint32_t firstIndex, uint32_t numTimestamps, uint64_t* timestamps)
{
	// read timestamps
	vk::Result r =
		_device.getQueryPoolResults(
			_timestampQueryPool,  // queryPool
			firstIndex+1,  // firstQuery
			numTimestamps,  // queryCount
			numTimestamps*sizeof(uint64_t),  // dataSize
			timestamps,  // pData
			sizeof(uint64_t),  // stride
			vk::QueryResultFlagBits::e64  // flags
		);

	// throw if not success
	if(r != vk::Result::eSuccess)
		vk::throwResultException(r, "vk::Device::getQueryPoolResults");
}
