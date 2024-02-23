﻿
#include "VkTransfer.h"
#include "VkConstant.h"

namespace dyno {
	template<typename T>
	T VkReduce<T>::reduce(const std::vector<T>& input)
	{
		VkHostArray<T> hostArray;
		hostArray.resize(input.size(), input.data());

		VkDeviceArray<T> deviceArray;
		deviceArray.resize(input.size());

		vkTransfer(deviceArray, hostArray);

		T ret = reduce(deviceArray);

		deviceArray.clear();
		hostArray.clear();

		return ret;
	}

	template<typename T>
	T VkReduce<T>::reduce(const VkDeviceArray<T>& input)
	{
		VkCompContext::Holder holder;

		unsigned int localSize = 256;
		auto globalSize = input.size();

		std::vector<VkDeviceArray<T>> buffers;
		buffers.push_back(input);
		auto nextLevelSize = [](int input, int local) -> int {
			return (input + local) / local;
		};

		int n = nextLevelSize(globalSize, localSize);
		while (n > 1)
		{
			buffers.push_back(VkDeviceArray<T>(n));
			n = nextLevelSize(n, localSize);
		}

		buffers.push_back(VkDeviceArray<T>(1));

		VkConstant<int> num;

		mReduceKernel->begin();

		for (std::size_t i = 0; i < buffers.size() - 1; i++)
		{
			num.setValue(buffers[i].size());
			dim3 groupSize = vkDispatchSize(num.getValue(), localSize);
			mReduceKernel->enqueue(groupSize, &buffers[i], &buffers[i + 1], &num);
		}

		mReduceKernel->end();

		mReduceKernel->update(true);
		mReduceKernel->wait();

		std::vector<T> ret(1);
		vkTransfer(ret, buffers.back());

		T sum = ret[0];

		ret.clear();

		return sum;
	}

	template<typename T>
	VkReduce<T>::~VkReduce() {
		mReduceKernel = nullptr;
	}

	template<typename T>
	VkReduce<T>::VkReduce() {
		mReduceKernel = std::make_shared<VkProgram>(
			BUFFER(T),
			BUFFER(T),
			CONSTANT(int));

		mReduceKernel->load(getDynamicSpvFile<T>("shaders/glsl/core/Reduce.comp.spv"));
	}
}