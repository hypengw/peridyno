#include "VkConstant.h"

namespace dyno {

	template<typename T>
	VkConstant<T>::VkConstant()
		: VkVariable()
	{
	}

	template<typename T>
	VkConstant<T>::VkConstant(T val)
	{
		mVal = val;
	}

	template<typename T>
	VkConstant<T>::~VkConstant()
	{

	}

	template<typename T>
	void VkConstant<T>::setValue(const T val)
	{
		mVal = val;
	}

	template<typename T>
	T VkConstant<T>::getValue() const
	{
		return mVal;
	}

	template<typename T>
	VariableType VkConstant<T>::type()
	{
		return VariableType::Constant;
	}


}