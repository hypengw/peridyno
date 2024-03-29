﻿/*
* Assorted Vulkan helper functions
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include "vulkan/vulkan.h"
#include "VulkanInitializers.hpp"
#include "VkSystem.h"

#include <math.h>
#include <stdlib.h>
#include <string>
#include <cstring>
#include <fstream>
#include <assert.h>
#include <stdio.h>
#include <vector>
#include <map>
#include <iostream>
#include <stdexcept>
#include <fstream>
#if defined(_WIN32)
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#elif defined(__ANDROID__)
#include "VulkanAndroid.h"
#include <android/asset_manager.h>
#elif defined(__GNUC__) || defined(__clang__)
#include "Platform.h"
#endif

// Custom define for better code readability
#define VK_FLAGS_NONE 0
// Default fence timeout in nanoseconds
#define DEFAULT_FENCE_TIMEOUT 100000000000

// Macro to check and display Vulkan return results
#if defined(__ANDROID__)
#define VK_CHECK_RESULT(f)																				\
{																										\
	VkResult res = (f);																					\
	if (res != VK_SUCCESS)																				\
	{																									\
		LOGE("Fatal : VkResult is \" %s \" in %s at line %d", vks::tools::errorString(res).c_str(), __FILE__, __LINE__); \
		assert(res == VK_SUCCESS);																		\
	}																									\
}
#else
#define VK_CHECK_RESULT(f)																				\
{																										\
	VkResult res = (f);																					\
	if (res != VK_SUCCESS)																				\
	{																									\
		logVkError(res, __FILE__, __LINE__);                                                          \
		assert(res == VK_SUCCESS);																		\
	}																									\
}
#endif

void logVkError(VkResult res, const char* file, int line);


std::string getSpvFile(const std::string& fileName);

template<typename T>
std::string getDynamicSpvFile(const std::string &fileName)
{
    std::string typeName;
    if (typeid(T).name() == typeid(int).name())
        typeName = "int";
    else if (typeid(T).name() == typeid(uint32_t).name())
        typeName = "uint";
    else if (typeid(T).name() == typeid(float).name())
        typeName = "float";

    const static std::string suffix = ".comp.spv";
    std::string outFileName = fileName;
    unsigned int suffixPos = outFileName.rfind(suffix);
    if (suffixPos != (outFileName.length() - suffix.length()))
    {
        // suffix not ".comp.spv", return origin filename
        return fileName;
    }

    // test.comp.spv --> test.int.comp.spv
    outFileName.insert(suffixPos, "." + typeName);
	return getSpvFile(outFileName);
}

namespace vks
{
	namespace tools
	{
		/** @brief Disable message boxes on fatal errors */
		extern bool errorModeSilent;

		/** @brief Returns an error code as a string */
		std::string errorString(VkResult errorCode);

		/** @brief Returns the device type as a string */
		std::string physicalDeviceTypeString(VkPhysicalDeviceType type);

		// Selected a suitable supported depth format starting with 32 bit down to 16 bit
		// Returns false if none of the depth formats in the list is supported by the device
		VkBool32 getSupportedDepthFormat(VkPhysicalDevice physicalDevice, VkFormat *depthFormat);

		// Returns if a given format support LINEAR filtering
		VkBool32 formatIsFilterable(VkPhysicalDevice physicalDevice, VkFormat format, VkImageTiling tiling);

		// Put an image memory barrier for setting an image layout on the sub resource into the given command buffer
		void setImageLayout(
			VkCommandBuffer cmdbuffer,
			VkImage image,
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			VkImageSubresourceRange subresourceRange,
			VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
		// Uses a fixed sub resource layout with first mip level and layer
		void setImageLayout(
			VkCommandBuffer cmdbuffer,
			VkImage image,
			VkImageAspectFlags aspectMask,
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

		/** @brief Insert an image memory barrier into the command buffer */
		void insertImageMemoryBarrier(
			VkCommandBuffer cmdbuffer,
			VkImage image,
			VkAccessFlags srcAccessMask,
			VkAccessFlags dstAccessMask,
			VkImageLayout oldImageLayout,
			VkImageLayout newImageLayout,
			VkPipelineStageFlags srcStageMask,
			VkPipelineStageFlags dstStageMask,
			VkImageSubresourceRange subresourceRange);

		// Display error message and exit on fatal error
		void exitFatal(const std::string& message, int32_t exitCode);
		void exitFatal(const std::string& message, VkResult resultCode);

		VkShaderModule loadShaderModule(const std::string fileName, VkDevice device);
		VkShaderModule loadShaderModule(const std::string& fileName, const std::map<std::string, std::string>& macros, const std::string& MD5Encode, VkDevice device);

		// Load a SPIR-V shader (binary)
#if defined(__ANDROID__)
		VkShaderModule loadShader(AAssetManager* assetManager, const char *fileName, VkDevice device);
		VkShaderModule loadShader(AAssetManager* assetManager, const std::string& fileName, const std::map<std::string, std::string>& macros, const std::string& MD5EnCode, VkDevice device);
#else
		VkShaderModule loadShader(const char *fileName, VkDevice device);
		VkShaderModule loadShader(const std::string &fileName, const std::map<std::string, std::string> &macros, const std::string &MD5Encode, VkDevice device);
#endif

		/** @brief Checks if a file exists */
		bool fileExists(const std::string &filename);

		uint32_t alignedSize(uint32_t value, uint32_t alignment);
	}
}
