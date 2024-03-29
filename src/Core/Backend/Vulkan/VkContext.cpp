﻿#include "VkContext.h"

#define VMA_IMPLEMENTATION
#include "VulkanMemoryAllocator/include/vk_mem_alloc.h"
#include "VkCompContext.h"

namespace
{
    auto toVmaMemUsage(dyno::VkContext::MemPoolType t) -> VmaMemoryUsage {
        using pt = dyno::VkContext::MemPoolType;
        switch (t) {
        case pt::DevicePool:
            return VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_ONLY;
        case pt::HostPool:
        case pt::UniformPool:
        default:
            return VmaMemoryUsage::VMA_MEMORY_USAGE_CPU_ONLY;
        }
    }
} // namespace

namespace dyno
{
    class VkContext::Private {
    public:
        /** @brief Physical device representation */
        VkPhysicalDevice physicalDevice;
        /** @brief Logical device representation (application's view of the device) */
        VkDevice logicalDevice;
        /** @brief Properties of the physical device including limits that the application can check against */
        VkPhysicalDeviceProperties properties;
        /** @brief Features of the physical device that an application can use to check if a feature is supported */
        VkPhysicalDeviceFeatures features;
        /** @brief Features that have been enabled for use on the physical device */
        VkPhysicalDeviceFeatures enabledFeatures;
        /** @brief Memory types and heaps of the physical device */
        VkPhysicalDeviceMemoryProperties memoryProperties;
        /** @brief Queue family properties of the physical device */
        std::vector<VkQueueFamilyProperties> queueFamilyProperties;
        /** @brief List of extensions supported by the device */
        std::vector<std::string> supportedExtensions;
        /** @brief Default command pool for the graphics queue family index */
        VkCommandPool commandPool {VK_NULL_HANDLE};

        VkQueue graphicsQueue;
        VkQueue computeQueue;
        VkQueue transferQueue;
        /** @brief Set to true when the debug marker extension is detected */
        bool enableDebugMarkers {false};
        /** @brief Contains queue family indices */

        // Pipeline cache object
        VkPipelineCache pipelineCache;

        struct {
            uint32_t graphics;
            uint32_t compute;
            uint32_t transfer;
        } queueFamilyIndices;

        struct MemoryPoolInfo {
            VmaPool pool;
            int32_t usage;
        };

        std::map<VkFlags, MemoryPoolInfo> poolMap;
        VmaAllocator g_Allocator {VK_NULL_HANDLE};
        bool useMemoryPool {true};
        std::unique_ptr<VkDescriptorCache> mDescriptorCache;

        // for Externally Synchronized vulkan api
        std::mutex mMutex;
    };

    VkContext::VkContext(VkPhysicalDevice physicalDevice) : d_ptr(std::make_unique<Private>()) {
        DYNO_D(VkContext);
        assert(physicalDevice);
        d->physicalDevice = physicalDevice;

        // Store Properties features, limits and properties of the physical device for later use
        // Device properties also contain limits and sparse properties
        vkGetPhysicalDeviceProperties(physicalDevice, &d->properties);
        // Features should be checked by the examples before using them
        vkGetPhysicalDeviceFeatures(physicalDevice, &d->features);
        // Memory properties are used regularly for creating all kinds of buffers
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &d->memoryProperties);
        // Queue family properties, used for setting up requested queues upon device creation
        uint32_t queueFamilyCount;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        assert(queueFamilyCount > 0);
        d->queueFamilyProperties.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, d->queueFamilyProperties.data());

        // Get list of supported extensions
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
        if (extCount > 0) {
            std::vector<VkExtensionProperties> extensions(extCount);
            if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, &extensions.front()) ==
                VK_SUCCESS)
            {
                for (auto ext : extensions) {
                    d->supportedExtensions.push_back(ext.extensionName);
                }
            }
        }
    }

    /**
     * Default destructor
     *
     * @note Frees the logical device
     */
    VkContext::~VkContext() {
        DYNO_D(VkContext);
        VkCompContext::current().reset();

        d->mDescriptorCache.reset();
        if (d->g_Allocator) {
            for (const auto& pool : d->poolMap) {
                vmaDestroyPool(d->g_Allocator, pool.second.pool);
            }
            vmaDestroyAllocator(d->g_Allocator);
        }

        if (d->commandPool) {
            vkDestroyCommandPool(d->logicalDevice, d->commandPool, nullptr);
        }
        if (d->pipelineCache) {
            vkDestroyPipelineCache(d->logicalDevice, d->pipelineCache, NULL);
        }
        if (d->logicalDevice) {
            vkDestroyDevice(d->logicalDevice, nullptr);
        }
    }

    bool VkContext::isComputeQueueSpecial() {
        DYNO_D(VkContext);
        return d->queueFamilyIndices.graphics != d->queueFamilyIndices.compute;
    }

    /**
     * Get the index of a memory type that has all the requested property bits set
     *
     * @param typeBits Bit mask with bits set for each memory type supported by the resource to request for (from
     * VkMemoryRequirements)
     * @param properties Bit mask of properties for the memory type to request
     * @param (Optional) memTypeFound Pointer to a bool that is set to true if a matching memory type has been found
     *
     * @return Index of the requested memory type
     *
     * @throw Throws an exception if memTypeFound is null and no memory type could be found that supports the requested
     * properties
     */
    uint32_t VkContext::getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties,
                                      VkBool32* memTypeFound) const {
        DYNO_D(const VkContext);
        for (uint32_t i = 0; i < d->memoryProperties.memoryTypeCount; i++) {
            if ((typeBits & 1) == 1) {
                if ((d->memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    if (memTypeFound) {
                        *memTypeFound = true;
                    }
                    return i;
                }
            }
            typeBits >>= 1;
        }

        if (memTypeFound) {
            *memTypeFound = false;
            return 0;
        }
        else {
            throw std::runtime_error("Could not find a matching memory type");
        }
    }

    /**
     * Get the index of a queue family that supports the requested queue flags
     *
     * @param queueFlags Queue flags to find a queue family index for
     *
     * @return Index of the queue family index that matches the flags
     *
     * @throw Throws an exception if no queue family index could be found that supports the requested flags
     */
    uint32_t VkContext::getQueueFamilyIndex(VkQueueFlagBits queueFlags) const {
        DYNO_D(const VkContext);
        // Dedicated queue for compute
        // Try to find a queue family index that supports compute but not graphics
        if (queueFlags & VK_QUEUE_COMPUTE_BIT) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(d->queueFamilyProperties.size()); i++) {
                if ((d->queueFamilyProperties[i].queueFlags & queueFlags) &&
                    ((d->queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0))
                {
                    return i;
                }
            }
        }

        // Dedicated queue for transfer
        // Try to find a queue family index that supports transfer but not graphics and compute
        if (queueFlags & VK_QUEUE_TRANSFER_BIT) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(d->queueFamilyProperties.size()); i++) {
                if ((d->queueFamilyProperties[i].queueFlags & queueFlags) &&
                    ((d->queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) &&
                    ((d->queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0))
                {
                    return i;
                }
            }
        }

        // For other queue types or if no separate compute queue is present, return the first one to support the
        // requested flags
        for (uint32_t i = 0; i < static_cast<uint32_t>(d->queueFamilyProperties.size()); i++) {
            if (d->queueFamilyProperties[i].queueFlags & queueFlags) {
                return i;
            }
        }

        throw std::runtime_error("Could not find a matching queue family index");
    }

    /**
     * Create the logical device based on the assigned physical device, also gets default queue family indices
     *
     * @param enabledFeatures Can be used to enable certain features upon device creation
     * @param pNextChain Optional chain of pointer to extension structures
     * @param useSwapChain Set to false for headless rendering to omit the swapchain device extensions
     * @param requestedQueueTypes Bit flags specifying the queue types to be requested from the device
     *
     * @return VkResult of the device creation call
     */
    VkResult VkContext::createLogicalDevice(VkPhysicalDeviceFeatures enabledFeatures,
                                            std::vector<const char*> enabledExtensions, void* pNextChain,
                                            bool useSwapChain, VkQueueFlags requestedQueueTypes) {
        DYNO_D(VkContext);
        // Desired queues need to be requested upon logical device creation
        // Due to differing queue family configurations of Vulkan implementations this can be a bit tricky, especially
        // if the application requests different queue types

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos {};

        // Get queue family indices for the requested queue family types
        // Note that the indices may overlap depending on the implementation

        const float defaultQueuePriority(0.0f);
        auto& queueFamilyIndices = d->queueFamilyIndices;

        // Graphics queue
        if (requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT) {
            queueFamilyIndices.graphics = getQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT);
            VkDeviceQueueCreateInfo queueInfo {};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamilyIndices.graphics;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &defaultQueuePriority;
            queueCreateInfos.push_back(queueInfo);
        }
        else {
            queueFamilyIndices.graphics = VK_NULL_HANDLE;
        }

        // Dedicated compute queue
        if (requestedQueueTypes & VK_QUEUE_COMPUTE_BIT) {
            queueFamilyIndices.compute = getQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT);
            if (queueFamilyIndices.compute != queueFamilyIndices.graphics) {
                // If compute family index differs, we need an additional queue create info for the compute queue
                VkDeviceQueueCreateInfo queueInfo {};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = queueFamilyIndices.compute;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &defaultQueuePriority;
                queueCreateInfos.push_back(queueInfo);
            }
        }
        else {
            // Else we use the same queue
            queueFamilyIndices.compute = queueFamilyIndices.graphics;
        }

        // Dedicated transfer queue
        if (requestedQueueTypes & VK_QUEUE_TRANSFER_BIT) {
            queueFamilyIndices.transfer = getQueueFamilyIndex(VK_QUEUE_TRANSFER_BIT);
            if ((queueFamilyIndices.transfer != queueFamilyIndices.graphics) &&
                (queueFamilyIndices.transfer != queueFamilyIndices.compute))
            {
                // If compute family index differs, we need an additional queue create info for the compute queue
                VkDeviceQueueCreateInfo queueInfo {};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = queueFamilyIndices.transfer;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &defaultQueuePriority;
                queueCreateInfos.push_back(queueInfo);
            }
        }
        else {
            // Else we use the same queue
            queueFamilyIndices.transfer = queueFamilyIndices.graphics;
        }

        // Create the logical device representation
        std::vector<const char*> deviceExtensions(enabledExtensions);
        if (useSwapChain) {
            // If the device will be used for presenting to a display via a swapchain we need to request the swapchain
            // extension
            deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        ;
        deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceCreateInfo.pEnabledFeatures = &enabledFeatures;

        VkPhysicalDeviceShaderAtomicFloatFeaturesEXT physicalDeviceShaderAtomicFloatFeaturesEXT {};
        physicalDeviceShaderAtomicFloatFeaturesEXT.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT;
        physicalDeviceShaderAtomicFloatFeaturesEXT.shaderBufferFloat32AtomicAdd = true;
        deviceCreateInfo.pNext = &physicalDeviceShaderAtomicFloatFeaturesEXT;

        // If a pNext(Chain) has been passed, we need to add it to the device creation info
        VkPhysicalDeviceFeatures2 physicalDeviceFeatures2 {};
        if (pNextChain) {
            physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            physicalDeviceFeatures2.features = enabledFeatures;
            physicalDeviceFeatures2.pNext = pNextChain;
            deviceCreateInfo.pEnabledFeatures = nullptr;
            deviceCreateInfo.pNext = &physicalDeviceFeatures2;

            vkGetPhysicalDeviceFeatures2(d->physicalDevice, &physicalDeviceFeatures2);
        }

        // Enable the debug marker extension if it is present (likely meaning a debugging tool is present)
        if (extensionSupported(VK_EXT_DEBUG_MARKER_EXTENSION_NAME)) {
            deviceExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
            d->enableDebugMarkers = true;
        }

        if (extensionSupported(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME)) {
            deviceExtensions.push_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
        }

        if (extensionSupported(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME)) {
            deviceExtensions.push_back(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
        }

        if (deviceExtensions.size() > 0) {
            for (const char* enabledExtension : deviceExtensions) {
                if (!extensionSupported(enabledExtension)) {
                    std::cerr << "Enabled device extension \"" << enabledExtension
                              << "\" is not present at device level\n";
                }
            }

            deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
            deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        }

        d->enabledFeatures = enabledFeatures;

        VkResult result = vkCreateDevice(d->physicalDevice, &deviceCreateInfo, nullptr, &d->logicalDevice);
        if (result != VK_SUCCESS) {
            return result;
        }

        // Create a default command pool for graphics command buffers
        d->commandPool = createCommandPool(queueFamilyIndices.graphics);

        // Get a graphics queue from the device
        vkGetDeviceQueue(d->logicalDevice, queueFamilyIndices.graphics, 0, &d->graphicsQueue);

        vkGetDeviceQueue(d->logicalDevice, queueFamilyIndices.compute, 0, &d->computeQueue);

        vkGetDeviceQueue(d->logicalDevice, queueFamilyIndices.transfer, 0, &d->transferQueue);

        createPipelineCache();

        d->mDescriptorCache = std::make_unique<VkDescriptorCache>(d->logicalDevice);

        return result;
    }

    VkDescriptorCache& VkContext::descriptorCache() {
        DYNO_D(VkContext);
        return *(d->mDescriptorCache);
    }

    void VkContext::createPipelineCache() {
        DYNO_D(VkContext);
        VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
        pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VK_CHECK_RESULT(vkCreatePipelineCache(d->logicalDevice, &pipelineCacheCreateInfo, nullptr, &d->pipelineCache));
    }

    /**
     * Create a buffer on the device
     *
     * @param usageFlags Usage flag bit mask for the buffer (i.e. index, vertex, uniform buffer)
     * @param memoryPropertyFlags Memory properties for this buffer (i.e. device local, host visible, coherent)
     * @param size Size of the buffer in byes
     * @param buffer Pointer to the buffer handle acquired by the function
     * @param memory Pointer to the memory handle acquired by the function
     * @param data Pointer to the data that should be copied to the buffer after creation (optional, if not set, no data
     * is copied over)
     *
     * @return VK_SUCCESS if buffer handle and memory have been created and (optionally passed) data has been copied
     */
    VkResult VkContext::createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags,
                                     VkDeviceSize size, VkBuffer* buffer, VkDeviceMemory* memory, void* data) {
        DYNO_D(VkContext);
        auto logicalDevice = d->logicalDevice;
        // Create the buffer handle
        VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo(usageFlags, size);
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK_RESULT(vkCreateBuffer(logicalDevice, &bufferCreateInfo, nullptr, buffer));

        // Create the memory backing up the buffer handle
        VkMemoryRequirements memReqs;
        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        vkGetBufferMemoryRequirements(logicalDevice, *buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        // Find a memory type index that fits the properties of the buffer
        memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags);
        // If the buffer has VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT set we also need to enable the appropriate flag
        // during allocation
        VkMemoryAllocateFlagsInfoKHR allocFlagsInfo {};
        if (usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
            allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
            allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
            memAlloc.pNext = &allocFlagsInfo;
        }
        VK_CHECK_RESULT(vkAllocateMemory(logicalDevice, &memAlloc, nullptr, memory));

        // If a pointer to the buffer data has been passed, map the buffer and copy over the data
        if (data != nullptr) {
            void* mapped;
            VK_CHECK_RESULT(vkMapMemory(logicalDevice, *memory, 0, size, 0, &mapped));
            memcpy(mapped, data, size);
            // If host coherency hasn't been requested, do a manual flush to make writes visible
            if ((memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
                VkMappedMemoryRange mappedRange = vks::initializers::mappedMemoryRange();
                mappedRange.memory = *memory;
                mappedRange.offset = 0;
                mappedRange.size = size;
                vkFlushMappedMemoryRanges(logicalDevice, 1, &mappedRange);
            }
            vkUnmapMemory(logicalDevice, *memory);
        }

        // Attach the memory to the buffer object
        VK_CHECK_RESULT(vkBindBufferMemory(logicalDevice, *buffer, *memory, 0));

        return VK_SUCCESS;
    }

    /**
     * Create a buffer on the device
     *
     * @param usageFlags Usage flag bit mask for the buffer (i.e. index, vertex, uniform buffer)
     * @param memoryPropertyFlags Memory properties for this buffer (i.e. device local, host visible, coherent)
     * @param buffer Pointer to a vk::Vulkan buffer object
     * @param size Size of the buffer in bytes
     * @param data Pointer to the data that should be copied to the buffer after creation (optional, if not set, no data
     * is copied over)
     *
     * @return VK_SUCCESS if buffer handle and memory have been created and (optionally passed) data has been copied
     */
    VkResult VkContext::createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags,
                                     std::shared_ptr<vks::Buffer>& buffer, VkDeviceSize size, const void* data) {
        DYNO_D(VkContext);
        auto logicalDevice = d->logicalDevice;
        buffer->device = logicalDevice;

        // Create the buffer handle
        VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo(usageFlags, size);
        VK_CHECK_RESULT(vkCreateBuffer(logicalDevice, &bufferCreateInfo, nullptr, &buffer->buffer));

        // Create the memory backing up the buffer handle
        VkMemoryRequirements memReqs;
        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        vkGetBufferMemoryRequirements(logicalDevice, buffer->buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        // Find a memory type index that fits the properties of the buffer
        memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags);
        // If the buffer has VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT set we also need to enable the appropriate flag
        // during allocation
        VkMemoryAllocateFlagsInfoKHR allocFlagsInfo {};
        if (usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
            allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
            allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
            memAlloc.pNext = &allocFlagsInfo;
        }
        VK_CHECK_RESULT(vkAllocateMemory(logicalDevice, &memAlloc, nullptr, &buffer->memory));

        buffer->usePool = VK_FALSE;
        buffer->alignment = memReqs.alignment;
        buffer->size = size;
        buffer->usageFlags = usageFlags;
        buffer->memoryPropertyFlags = memoryPropertyFlags;

        // If a pointer to the buffer data has been passed, map the buffer and copy over the data
        if (data != nullptr) {
            VK_CHECK_RESULT(buffer->map());
            memcpy(buffer->mapped, data, size);
            if ((memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) buffer->flush();

            buffer->unmap();
        }

        // Initialize a default descriptor that covers the whole buffer size
        buffer->setupDescriptor();

        // Attach the memory to the buffer object
        return buffer->bind();
    }

    /**
     * Create a buffer on the device wth memory allocated from memory pool
     *
     * @param poolType memory type
     * @param buffer Pointer to a vk::Vulkan buffer object
     * @param data Pointer to the data that should be copied to the buffer after creation (optional, if not set, no data
     * is copied over)
     *
     * @return VK_SUCCESS if buffer handle and memory have been created and (optionally passed) data has been copied
     */
    VkResult VkContext::createBuffer(MemPoolType poolType, std::shared_ptr<vks::Buffer>& buffer, const void* data) {
        DYNO_D(VkContext);

        VkBufferCreateInfo bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferCreateInfo.size = buffer->size;
        bufferCreateInfo.usage = buffer->usageFlags;

        VmaAllocationCreateInfo allocationCreateInfo = {};
        allocationCreateInfo.usage = toVmaMemUsage(poolType);
        allocationCreateInfo.pool = VK_NULL_HANDLE; // poolMap[poolType].pool;

        VmaAllocationInfo allocationInfo;
        VkResult ret = vmaCreateBuffer(d->g_Allocator, &bufferCreateInfo, &allocationCreateInfo, &buffer->buffer,
                                       &buffer->allocation, &allocationInfo);
        VK_CHECK_RESULT(ret);

        buffer->usePool = VK_TRUE;
        buffer->memory = allocationInfo.deviceMemory;
        buffer->offset = allocationInfo.offset;
        buffer->allocator = d->g_Allocator;
        buffer->device = deviceHandle();

        if (data != nullptr) {
            ret = vmaMapMemory(buffer->allocator, buffer->allocation, (void**)&buffer->mapped);
            VK_CHECK_RESULT(ret);
            memcpy(buffer->mapped, data, buffer->size);

            if ((buffer->memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
                vmaFlushAllocation(buffer->allocator, buffer->allocation, allocationInfo.offset, allocationInfo.size);
            }
            vmaUnmapMemory(buffer->allocator, buffer->allocation);
            buffer->mapped = nullptr;
        }
        buffer->setupDescriptor();
        return VK_SUCCESS;
    }

    /**
     * Create memory pool from all type
     *
     * @param instance Vulkan instance
     * @param apiVersion Vulkan api version
     *
     * @return VK_SUCCESS if memory pool has been created.
     */
    VkResult VkContext::createMemoryPool(VkInstance instance, uint32_t apiVerion) {
        DYNO_D(VkContext);
        d->useMemoryPool = true;

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.vulkanApiVersion = apiVerion;
        allocatorInfo.physicalDevice = physicalDeviceHandle();
        allocatorInfo.device = deviceHandle();
        allocatorInfo.instance = instance;
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        vmaCreateAllocator(&allocatorInfo, &d->g_Allocator);
        return VK_SUCCESS;

        VmaPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.blockSize = 100 * 1024 * 1024; // 100M
        poolCreateInfo.minBlockCount = 1;
        poolCreateInfo.maxBlockCount = 10 * 2; // 2G

        VmaAllocationCreateInfo allocationCreateInfo = {};
        VkBufferCreateInfo bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferCreateInfo.size = 1024;

        Private::MemoryPoolInfo memoryPoolInfo = {};
        for (uint32_t type = DevicePool; type < EndType; ++type) {
            switch (type) {
            case DevicePool: {
                // Device memory pool
                allocationCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
                allocationCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                break;
            }
            case HostPool: {
                // Host memory pool
                allocationCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
                allocationCreateInfo.requiredFlags =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                break;
            }
            case UniformPool: {
                // Uniform memory pool
                allocationCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
                allocationCreateInfo.requiredFlags =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                break;
            }
            default:
                break;
            }

            memoryPoolInfo.usage = allocationCreateInfo.usage;
            vmaFindMemoryTypeIndexForBufferInfo(d->g_Allocator, &bufferCreateInfo, &allocationCreateInfo,
                                                &poolCreateInfo.memoryTypeIndex);
            VkResult res = vmaCreatePool(d->g_Allocator, &poolCreateInfo, &memoryPoolInfo.pool);
            if (res != VK_SUCCESS) {
                vks::tools::exitFatal("Could not create memory pool : \n" + vks::tools::errorString(res), res);
                return res;
            }
            d->poolMap[type] = memoryPoolInfo;
        }

        return VK_SUCCESS;
    }

    /**
     * Copy buffer data from src to dst using VkCmdCopyBuffer
     *
     * @param src Pointer to the source buffer to copy from
     * @param dst Pointer to the destination buffer to copy to
     * @param queue Pointer
     * @param copyRegion (Optional) Pointer to a copy region, if NULL, the whole buffer is copied
     *
     * @note Source and destination pointers must have the appropriate transfer usage flags set (TRANSFER_SRC /
     * TRANSFER_DST)
     */
    void VkContext::copyBuffer(vks::Buffer* src, vks::Buffer* dst, VkQueue queue, VkBufferCopy* copyRegion) {
        assert(dst->size <= src->size);
        assert(src->buffer);
        VkCommandBuffer copyCmd = createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
        VkBufferCopy bufferCopy {};
        if (copyRegion == nullptr) {
            bufferCopy.size = src->size;
        }
        else {
            bufferCopy = *copyRegion;
        }

        vkCmdCopyBuffer(copyCmd, src->buffer, dst->buffer, 1, &bufferCopy);

        flushCommandBuffer(copyCmd, queue);
    }

    /**
     * Create a command pool for allocation command buffers from
     *
     * @param queueFamilyIndex Family index of the queue to create the command pool for
     * @param createFlags (Optional) Command pool creation flags (Defaults to
     * VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
     *
     * @note Command buffers allocated from the created pool can only be submitted to a queue with the same family index
     *
     * @return A handle to the created command buffer
     */
    VkCommandPool VkContext::createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags) {
        DYNO_D(VkContext);
        VkCommandPoolCreateInfo cmdPoolInfo = {};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
        cmdPoolInfo.flags = createFlags;
        VkCommandPool cmdPool;
        VK_CHECK_RESULT(vkCreateCommandPool(d->logicalDevice, &cmdPoolInfo, nullptr, &cmdPool));
        return cmdPool;
    }

    /**
     * Allocate a command buffer from the command pool
     *
     * @param level Level of the new command buffer (primary or secondary)
     * @param pool Command pool from which the command buffer will be allocated
     * @param (Optional) begin If true, recording on the new command buffer will be started (vkBeginCommandBuffer)
     * (Defaults to false)
     *
     * @return A handle to the allocated command buffer
     */
    VkCommandBuffer VkContext::createCommandBuffer(VkCommandBufferLevel level, VkCommandPool pool, bool begin) {
        DYNO_D(VkContext);
        VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(pool, level, 1);
        VkCommandBuffer cmdBuffer;
        VK_CHECK_RESULT(vkAllocateCommandBuffers(d->logicalDevice, &cmdBufAllocateInfo, &cmdBuffer));
        // If requested, also start recording for the new command buffer
        if (begin) {
            VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
            VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
        }
        return cmdBuffer;
    }

    VkCommandBuffer VkContext::createCommandBuffer(VkCommandBufferLevel level, bool begin) {
        DYNO_D(VkContext);
        return createCommandBuffer(level, d->commandPool, begin);
    }

    /**
     * Finish command buffer recording and submit it to a queue
     *
     * @param commandBuffer Command buffer to flush
     * @param queue Queue to submit the command buffer to
     * @param pool Command pool on which the command buffer has been created
     * @param free (Optional) Free the command buffer once it has been submitted (Defaults to true)
     *
     * @note The queue that the command buffer is submitted to must be from the same family index as the pool it was
     * allocated from
     * @note Uses a fence to ensure command buffer has finished executing
     */
    void VkContext::flushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, VkCommandPool pool, bool free) {
        DYNO_D(VkContext);
        auto logicalDevice = d->logicalDevice;
        if (commandBuffer == VK_NULL_HANDLE) {
            return;
        }

        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        // auto sems = VkCompContext::current().semaphores();

        VkSubmitInfo submitInfo = vks::initializers::submitInfo();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        // submitInfo.waitSemaphoreCount = sems.size();
        // submitInfo.pWaitSemaphores = sems.data();

        // Create fence to ensure that the command buffer has finished executing
        VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo(VK_FLAGS_NONE);
        VkFence fence;
        VK_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence));
        // Submit to the queue
        this->vkQueueSubmitSync(queue, 1, &submitInfo, fence);
        // Wait for the fence to signal that command buffer has finished executing
        VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));
        vkDestroyFence(logicalDevice, fence, nullptr);
        if (free) {
            vkFreeCommandBuffers(logicalDevice, pool, 1, &commandBuffer);
        }
    }

    void VkContext::flushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free) {
        DYNO_D(VkContext);
        return flushCommandBuffer(commandBuffer, queue, d->commandPool, free);
    }

    /**
     * Check if an extension is supported by the (physical device)
     *
     * @param extension Name of the extension to check
     *
     * @return True if the extension is supported (present in the list read at device creation time)
     */
    bool VkContext::extensionSupported(std::string extension) {
        DYNO_D(VkContext);
        return (std::find(d->supportedExtensions.begin(), d->supportedExtensions.end(), extension) !=
                d->supportedExtensions.end());
    }

    /**
     * Select the best-fit depth format for this device from a list of possible depth (and stencil) formats
     *
     * @param checkSamplingSupport Check if the format can be sampled from (e.g. for shader reads)
     *
     * @return The depth format that best fits for the current device
     *
     * @throw Throws an exception if no depth format fits the requirements
     */
    VkFormat VkContext::getSupportedDepthFormat(bool checkSamplingSupport) {
        DYNO_D(VkContext);
        // All depth formats may be optional, so we need to find a suitable depth format to use
        std::vector<VkFormat> depthFormats = {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT,
                                              VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT,
                                              VK_FORMAT_D16_UNORM};
        for (auto& format : depthFormats) {
            VkFormatProperties formatProperties;
            vkGetPhysicalDeviceFormatProperties(d->physicalDevice, format, &formatProperties);
            // Format must support depth stencil attachment for optimal tiling
            if (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                if (checkSamplingSupport) {
                    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
                        continue;
                    }
                }
                return format;
            }
        }
        throw std::runtime_error("Could not find a matching depth format");
    }

    void VkContext::vkQueueSubmitSync(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits,
                                      VkFence fence) {
        DYNO_D(VkContext);
        // TODO: message loop for submit
        std::unique_lock lock {d->mMutex};
        VK_CHECK_RESULT(vkQueueSubmit(queue, submitCount, pSubmits, fence));
    }

    void VkContext::destroy(VkCommandPool pool, VkCommandBuffer cmd) {
        vkFreeCommandBuffers(deviceHandle(), pool, 1, &cmd);
    }
    bool VkContext::useMemPool() const {
        DYNO_D(const VkContext);
        return d->useMemoryPool;
    }

    VkDevice VkContext::deviceHandle() const {
        DYNO_D(const VkContext);
        return d->logicalDevice;
    }
    VkQueue VkContext::graphicsQueueHandle() const {
        DYNO_D(const VkContext);
        return d->graphicsQueue;
    }
    VkQueue VkContext::computeQueueHandle() const {
        DYNO_D(const VkContext);
        return d->computeQueue;
    }
    VkQueue VkContext::transferQueueHandle() const {
        DYNO_D(const VkContext);
        return d->transferQueue;
    }
    VkPhysicalDevice VkContext::physicalDeviceHandle() const {
        DYNO_D(const VkContext);
        return d->physicalDevice;
    }
    VkPipelineCache VkContext::pipelineCacheHandle() const {
        DYNO_D(const VkContext);
        return d->pipelineCache;
    }
    uint32_t VkContext::graphicsQueueFamilyIndex() const {
        DYNO_D(const VkContext);
        return d->queueFamilyIndices.graphics;
    }
    uint32_t VkContext::computeQueueFamilyIndex() const {
        DYNO_D(const VkContext);
        return d->queueFamilyIndices.compute;
    }
    uint32_t VkContext::transferQueueFamilyIndex() const {
        DYNO_D(const VkContext);
        return d->queueFamilyIndices.transfer;
    }
    const VkPhysicalDeviceProperties& VkContext::properties() const {
        DYNO_D(const VkContext);
        return d->properties;
    }
    const VkPhysicalDeviceFeatures& VkContext::enabledFeatures() const {
        DYNO_D(const VkContext);
        return d->enabledFeatures;
    }

    VkCommandPool VkContext::commandPool() const {
        DYNO_D(const VkContext);
        return d->commandPool;
    }
	bool VkContext::enableDebugMarkers() const {
        DYNO_D(const VkContext);
		return d->enableDebugMarkers;
	}
} // namespace dyno