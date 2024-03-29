#pragma once
#include "VkDeviceArray.h"
#include "VkProgram.h"

using uint = unsigned int;
namespace dyno {
	/*! TODO:
	*  \brief implement functions for reducing a range to a single value
	*/
	// T suport int, float and uint32_t types
	template<typename T>
	class VkMax {
		
	public:
		VkMax();
		~VkMax();

		T reduce(const std::vector<T>& input);
		T reduce(const VkDeviceArray<T>& input);

	private:
		std::shared_ptr<VkProgram> mReduceKernel;
	};
}
#include "VkMax.inl"
