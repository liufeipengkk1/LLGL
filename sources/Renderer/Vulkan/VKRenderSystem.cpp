/*
 * VKRenderSystem.cpp
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015-2018 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include <LLGL/Platform/Platform.h>
#include "VKRenderSystem.h"
#include "Ext/VKExtensionLoader.h"
#include "Ext/VKExtensions.h"
#include "Memory/VKDeviceMemory.h"
#include "Buffer/VKIndexBuffer.h"
#include "../CheckedCast.h"
#include "../../Core/Helper.h"
#include "../../Core/Vendor.h"
#include "../GLCommon/GLTypes.h"
#include "VKCore.h"
#include "VKTypes.h"
#include <LLGL/Log.h>

//#define TEST_VULKAN_MEMORY_MNGR
#ifdef TEST_VULKAN_MEMORY_MNGR
#   include <iostream>
#endif


namespace LLGL
{


/* ----- Internal functions ----- */

static VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* createInfo, const VkAllocationCallbacks* allocator, VkDebugReportCallbackEXT* callback)
{
    auto func = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT"));
    if (func != nullptr)
        return func(instance, createInfo, allocator, callback);
    else
        return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* allocator)
{
    auto func = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT"));
    if (func != nullptr)
        func(instance, callback, allocator);
}

static VkBufferUsageFlags GetVkBufferUsageFlags(long bufferFlags)
{
    if ((bufferFlags & BufferFlags::MapReadAccess) != 0)
        return VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    else
        return VK_BUFFER_USAGE_TRANSFER_DST_BIT;
}

static VkBufferUsageFlags GetStagingVkBufferUsageFlags(long bufferFlags)
{
    if ((bufferFlags & BufferFlags::MapWriteAccess) != 0)
        return VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    else
        return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
}

static void FillBufferCreateInfo(VkBufferCreateInfo& createInfo, VkDeviceSize size, VkBufferUsageFlags usage)
{
    createInfo.sType                    = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.pNext                    = nullptr;
    createInfo.flags                    = 0;
    createInfo.size                     = size;
    createInfo.usage                    = usage;
    createInfo.sharingMode              = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount    = 0;
    createInfo.pQueueFamilyIndices      = nullptr;
}

#ifdef TEST_VULKAN_MEMORY_MNGR

static void TestVulkanMemoryMngr(VKDeviceMemoryManager& mngr)
{
    std::uint32_t typeBits = 1665;
    VkDeviceSize alignment = 1;

    auto reg0 = mngr.Allocate(6, alignment, typeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto reg1 = mngr.Allocate(7, alignment, typeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto reg2 = mngr.Allocate(12, alignment, typeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto reg3 = mngr.Allocate(5, 16, typeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto reg4 = mngr.Allocate(5, alignment, typeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mngr.PrintBlocks(std::cout, "Allocate: 6, 7, 12, 5 (alignment 16), 5");
    std::cout << std::endl;

    mngr.Release(reg1);
    mngr.PrintBlocks(std::cout, "Release second allocation (7)");
    std::cout << std::endl;

    reg1 = mngr.Allocate(3, alignment, typeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mngr.PrintBlocks(std::cout, "Allocate: 3");
    std::cout << std::endl;

    auto reg5 = mngr.Allocate(4, alignment, typeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mngr.PrintBlocks(std::cout, "Allocate: 4");
    std::cout << std::endl;

    mngr.Release(reg1);
    mngr.PrintBlocks(std::cout, "Release previous 3");
    std::cout << std::endl;

    mngr.Release(reg2);
    mngr.PrintBlocks(std::cout, "Release previous 12");
    std::cout << std::endl;

    mngr.Release(reg5);
    mngr.Release(reg4);
    mngr.PrintBlocks(std::cout, "Release previous 4, 5");
    std::cout << std::endl;

    reg5 = mngr.Allocate(9, 8, typeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    mngr.PrintBlocks(std::cout, "Allocate: 9 with alignment 8");
    std::cout << std::endl;
}

#endif


/* ----- Common ----- */

static const std::vector<const char*> g_deviceExtensions
{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_MAINTENANCE1_EXTENSION_NAME,
};

VKRenderSystem::VKRenderSystem(const RenderSystemDescriptor& renderSystemDesc) :
    instance_            { vkDestroyInstance                        },
    device_              { vkDestroyDevice                          },
    debugReportCallback_ { instance_, DestroyDebugReportCallbackEXT }
{
    /* Extract optional renderer configuartion */
    const VulkanRendererConfiguration* rendererConfigVK= nullptr;

    if (renderSystemDesc.rendererConfig != nullptr && renderSystemDesc.rendererConfigSize > 0)
    {
        if (renderSystemDesc.rendererConfigSize == sizeof(VulkanRendererConfiguration))
            rendererConfigVK = reinterpret_cast<const VulkanRendererConfiguration*>(renderSystemDesc.rendererConfig);
        else
            throw std::invalid_argument("invalid renderer configuration structure (expected size of 'VulkanRendererConfiguration' structure)");
    }

    #ifdef LLGL_DEBUG
    debugLayerEnabled_ = true;
    #endif

    /* Create Vulkan instance and device objects */
    CreateInstance(rendererConfigVK != nullptr ? &(rendererConfigVK->application) : nullptr);
    LoadExtensions();

    if (!PickPhysicalDevice())
        throw std::runtime_error("failed to find physical device with Vulkan support");

    QueryDeviceProperties();
    CreateLogicalDevice();
    CreateStagingCommandResources();
    CreateDefaultPipelineLayout();

    /* Create device memory manager */
    deviceMemoryMngr_ = MakeUnique<VKDeviceMemoryManager>(
        device_,
        memoryProperties_,
        (rendererConfigVK != nullptr ? rendererConfigVK->minDeviceMemoryAllocationSize : 1024*1024),
        (rendererConfigVK != nullptr ? rendererConfigVK->reduceDeviceMemoryFragmentation : false)
    );

    #ifdef TEST_VULKAN_MEMORY_MNGR
    TestVulkanMemoryMngr(*deviceMemoryMngr_);
    #endif
}

VKRenderSystem::~VKRenderSystem()
{
    /* Release resource and wait until device becomes idle */
    ReleaseStagingCommandResources();
    vkDeviceWaitIdle(device_);
}

/* ----- Render Context ----- */

RenderContext* VKRenderSystem::CreateRenderContext(const RenderContextDescriptor& desc, const std::shared_ptr<Surface>& surface)
{
    return TakeOwnership(
        renderContexts_,
        MakeUnique<VKRenderContext>(instance_, physicalDevice_, device_, *deviceMemoryMngr_, desc, surface)
    );
}

void VKRenderSystem::Release(RenderContext& renderContext)
{
    RemoveFromUniqueSet(renderContexts_, &renderContext);
}

/* ----- Command queues ----- */

CommandQueue* VKRenderSystem::GetCommandQueue()
{
    return commandQueue_.get();
}

/* ----- Command buffers ----- */

CommandBuffer* VKRenderSystem::CreateCommandBuffer(const CommandBufferDescriptor& desc)
{
    return TakeOwnership(
        commandBuffers_,
        MakeUnique<VKCommandBuffer>(device_, graphicsQueue_, queueFamilyIndices_, desc)
    );
}

CommandBufferExt* VKRenderSystem::CreateCommandBufferExt(const CommandBufferDescriptor& /*desc*/)
{
    /* Extended command buffers are not spported */
    return nullptr;
}

void VKRenderSystem::Release(CommandBuffer& commandBuffer)
{
    RemoveFromUniqueSet(commandBuffers_, &commandBuffer);
}

/* ----- Buffers ------ */

Buffer* VKRenderSystem::CreateBuffer(const BufferDescriptor& desc, const void* initialData)
{
    static const long g_stagingBufferRelatedFlags = (BufferFlags::MapReadWriteAccess | BufferFlags::DynamicUsage);

    AssertCreateBuffer(desc, static_cast<uint64_t>(std::numeric_limits<VkDeviceSize>::max()));

    /* Create staging buffer */
    VkBufferCreateInfo stagingCreateInfo;
    FillBufferCreateInfo(
        stagingCreateInfo,
        static_cast<VkDeviceSize>(desc.size),
        GetStagingVkBufferUsageFlags(desc.flags)
    );

    VKBufferWithRequirements stagingBuffer { device_ };
    VKDeviceMemoryRegion* memoryRegionStaging = nullptr;

    std::tie(stagingBuffer, memoryRegionStaging) = CreateStagingBuffer(stagingCreateInfo, initialData, static_cast<std::size_t>(desc.size));

    /* Create device buffer */
    auto buffer = CreateHardwareBuffer(desc, GetVkBufferUsageFlags(desc.flags));

    /* Allocate device memory */
    const auto& requirements = buffer->GetRequirements();

    auto memoryRegion = deviceMemoryMngr_->Allocate(
        requirements.size,
        requirements.alignment,
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    buffer->BindToMemory(device_, memoryRegion);

    /* Copy staging buffer into hardware buffer */
    CopyBuffer(stagingBuffer.buffer, buffer->GetVkBuffer(), static_cast<VkDeviceSize>(desc.size));

    if ((desc.flags & g_stagingBufferRelatedFlags) != 0)
    {
        /* Store ownership of staging buffer */
        buffer->TakeStagingBuffer(std::move(stagingBuffer), memoryRegionStaging);
    }
    else
    {
        /* Release staging buffer */
        deviceMemoryMngr_->Release(memoryRegionStaging);
    }

    return buffer;
}

BufferArray* VKRenderSystem::CreateBufferArray(std::uint32_t numBuffers, Buffer* const * bufferArray)
{
    AssertCreateBufferArray(numBuffers, bufferArray);
    auto type = (*bufferArray)->GetType();
    return TakeOwnership(bufferArrays_, MakeUnique<VKBufferArray>(type, numBuffers, bufferArray));
}

void VKRenderSystem::Release(Buffer& buffer)
{
    /* Release device memory regions for primary buffer and internal staging buffer, then release buffer object */
    auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);
    deviceMemoryMngr_->Release(bufferVK.GetMemoryRegion());
    deviceMemoryMngr_->Release(bufferVK.GetMemoryRegionStaging());
    RemoveFromUniqueSet(buffers_, &buffer);
}

void VKRenderSystem::Release(BufferArray& bufferArray)
{
    RemoveFromUniqueSet(bufferArrays_, &bufferArray);
}

void VKRenderSystem::WriteBuffer(Buffer& buffer, const void* data, std::size_t dataSize, std::size_t offset)
{
    auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);

    auto memorySize     = static_cast<VkDeviceSize>(dataSize);
    auto memoryOffset   = static_cast<VkDeviceSize>(offset);

    if (bufferVK.GetStagingVkBuffer() != VK_NULL_HANDLE)
    {
        #if 1

        /* Copy data to staging buffer memory */
        bufferVK.UpdateStagingBuffer(device_, data, memorySize, memoryOffset);

        /* Copy staging buffer into hardware buffer */
        CopyBuffer(bufferVK.GetStagingVkBuffer(), bufferVK.GetVkBuffer(), memorySize, memoryOffset, memoryOffset);

        #else // TEST

        BeginStagingCommands();

        if (auto mappedData = bufferVK.MapStaging(device_, memorySize, memoryOffset))
        {
            ::memcpy(mappedData, data, static_cast<std::size_t>(dataSize));
            bufferVK.UnmapStaging(device_);
        }

        VkBufferMemoryBarrier barrier;
        {
            barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.pNext               = nullptr;
            barrier.srcAccessMask       = 0;//VK_ACCESS_HOST_WRITE_BIT;
            barrier.dstAccessMask       = 0;//VK_ACCESS_TRANSFER_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer              = bufferVK.GetStagingVkBuffer();
            barrier.offset              = 0;
            barrier.size                = VK_WHOLE_SIZE;
        }
        vkCmdPipelineBarrier(
            stagingCommandBuffer_,
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr,
            1, &barrier,
            0, nullptr
        );

        /* Copy staging buffer into hardware buffer */
        VkBufferCopy region;
        {
            region.srcOffset    = memoryOffset;
            region.dstOffset    = memoryOffset;
            region.size         = memorySize;
        }
        vkCmdCopyBuffer(stagingCommandBuffer_, bufferVK.GetStagingVkBuffer(), bufferVK.GetVkBuffer(), 1, &region);

        EndStagingCommands();

        #endif // /TEST
    }
    else
    {
        /* Create staging buffer */
        VkBufferCreateInfo stagingCreateInfo;
        FillBufferCreateInfo(
            stagingCreateInfo,
            static_cast<VkDeviceSize>(dataSize),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        );

        VKBufferWithRequirements stagingBuffer { device_ };
        VKDeviceMemoryRegion* memoryRegionStaging = nullptr;

        std::tie(stagingBuffer, memoryRegionStaging) = CreateStagingBuffer(stagingCreateInfo, data, dataSize);

        /* Copy staging buffer into hardware buffer */
        CopyBuffer(stagingBuffer.buffer, bufferVK.GetVkBuffer(), memorySize, 0, memoryOffset);

        /* Release device memory region */
        deviceMemoryMngr_->Release(memoryRegionStaging);
    }
}

void* VKRenderSystem::MapBuffer(Buffer& buffer, const CPUAccess access)
{
    auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);
    AssertBufferCPUAccess(bufferVK);

    /* Copy GPU local buffer into staging buffer for read accces */
    if (access != CPUAccess::WriteOnly)
        CopyBuffer(bufferVK.GetVkBuffer(), bufferVK.GetStagingVkBuffer(), bufferVK.GetSize());

    /* Map staging buffer */
    return bufferVK.Map(device_, access);
}

void VKRenderSystem::UnmapBuffer(Buffer& buffer)
{
    auto& bufferVK = LLGL_CAST(VKBuffer&, buffer);
    AssertBufferCPUAccess(bufferVK);

    /* Unmap staging buffer */
    bufferVK.Unmap(device_);

    /* Copy staging buffer into GPU local buffer for write access */
    if (bufferVK.GetMappingCPUAccess() != CPUAccess::ReadOnly)
        CopyBuffer(bufferVK.GetStagingVkBuffer(), bufferVK.GetVkBuffer(), bufferVK.GetSize());
}

/* ----- Textures ----- */

// Returns the extent for the specified texture dimensionality (used for the dimension of 'VK_IMAGE_TYPE_1D/ 2D/ 3D')
static VkExtent3D GetTextureVkExtent(const TextureDescriptor& desc)
{
    switch (desc.type)
    {
        case TextureType::Texture1D:        /*pass*/
        case TextureType::Texture1DArray:   return { desc.extent.width, 1u, 1u };
        case TextureType::Texture2D:        /*pass*/
        case TextureType::Texture2DArray:   /*pass*/
        case TextureType::TextureCube:      /*pass*/
        case TextureType::TextureCubeArray: /*pass*/
        case TextureType::Texture2DMS:      /*pass*/
        case TextureType::Texture2DMSArray: return { desc.extent.width, desc.extent.height, 1u };
        case TextureType::Texture3D:        return { desc.extent.width, desc.extent.height, desc.extent.depth };
    }
    throw std::invalid_argument("cannot determine texture extent for unknown texture type");
}

static std::uint32_t GetTextureLayertCount(const TextureDescriptor& desc)
{
    switch (desc.type)
    {
        case TextureType::Texture1DArray:   return desc.arrayLayers;
        case TextureType::Texture2DArray:   return desc.arrayLayers;
        case TextureType::TextureCubeArray: return desc.arrayLayers * 6;
        case TextureType::Texture2DMSArray: return desc.arrayLayers;
        default:                            return 1;
    }
}

Texture* VKRenderSystem::CreateTexture(const TextureDescriptor& textureDesc, const SrcImageDescriptor* imageDesc)
{
    const auto& cfg = GetConfiguration();

    /* Determine size of image for staging buffer */
    const auto imageSize        = TextureSize(textureDesc);
    const auto initialDataSize  = static_cast<VkDeviceSize>(TextureBufferSize(textureDesc.format, imageSize));

    /* Set up initial image data */
    const void* initialData = nullptr;
    ByteBuffer tempImageBuffer;

    if (imageDesc)
    {
        /* Check if image data must be converted */
        ImageFormat dstFormat   = ImageFormat::RGBA;
        DataType    dstDataType = DataType::Int8;

        if (FindSuitableImageFormat(textureDesc.format, dstFormat, dstDataType))
        {
            /* Convert image format (will be null if no conversion is necessary) */
            tempImageBuffer = ConvertImageBuffer(*imageDesc, dstFormat, dstDataType, cfg.threadCount);
        }

        if (tempImageBuffer)
        {
            /*
            Validate that source image data was large enough so conversion is valid,
            then use temporary image buffer as source for initial data
            */
            const auto srcImageDataSize = imageSize * ImageFormatSize(imageDesc->format) * DataTypeSize(imageDesc->dataType);
            AssertImageDataSize(imageDesc->dataSize, static_cast<std::size_t>(srcImageDataSize));
            initialData = tempImageBuffer.get();
        }
        else
        {
            /*
            Validate that image data is large enough,
            then use input data as source for initial data
            */
            AssertImageDataSize(imageDesc->dataSize, static_cast<std::size_t>(initialDataSize));
            initialData = imageDesc->data;
        }
    }
    else if (cfg.imageInitialization.enabled)
    {
        /* Allocate default image data */
        ImageFormat imageFormat = ImageFormat::RGBA;
        DataType imageDataType = DataType::Float64;

        if (FindSuitableImageFormat(textureDesc.format, imageFormat, imageDataType))
        {
            const ColorRGBAd fillColor { cfg.imageInitialization.clearValue.color.Cast<double>() };
            tempImageBuffer = GenerateImageBuffer(imageFormat, imageDataType, imageSize, fillColor);
        }
        else
            tempImageBuffer = GenerateEmptyByteBuffer(static_cast<std::size_t>(initialDataSize));

        initialData = tempImageBuffer.get();
    }

    /* Create staging buffer */
    VkBufferCreateInfo stagingCreateInfo;
    FillBufferCreateInfo(stagingCreateInfo, initialDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT); // <-- TODO: support read/write mapping //GetStagingVkBufferUsageFlags(desc.flags)

    VKBufferWithRequirements stagingBuffer { device_ };
    VKDeviceMemoryRegion* memoryRegionStaging = nullptr;

    std::tie(stagingBuffer, memoryRegionStaging) = CreateStagingBuffer(
        stagingCreateInfo,
        initialData,
        static_cast<std::size_t>(initialDataSize)
    );

    /* Create device texture */
    auto textureVK      = MakeUnique<VKTexture>(device_, *deviceMemoryMngr_, textureDesc);

    auto image          = textureVK->GetVkImage();
    auto mipLevels      = textureVK->GetNumMipLevels();
    auto arrayLayers    = textureVK->GetNumArrayLayers();

    /* Copy staging buffer into hardware texture, then transfer image into sampling-ready state */
    auto formatVK = VKTypes::Map(textureDesc.format);
    TransitionImageLayout(image, formatVK, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels, arrayLayers);
    {
        CopyBufferToImage(
            stagingBuffer.buffer,
            image,
            GetTextureVkExtent(textureDesc),
            GetTextureLayertCount(textureDesc)
        );
    }
    TransitionImageLayout(image, formatVK, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels, arrayLayers);

    /* Release staging buffer */
    deviceMemoryMngr_->Release(memoryRegionStaging);

    /* Create image view for texture */
    textureVK->CreateInternalImageView(device_);

    return TakeOwnership(textures_, std::move(textureVK));
}

void VKRenderSystem::Release(Texture& texture)
{
    /* Release device memory region, then release texture object */
    auto& textureVK = LLGL_CAST(VKTexture&, texture);
    deviceMemoryMngr_->Release(textureVK.GetMemoryRegion());
    RemoveFromUniqueSet(textures_, &texture);
}

void VKRenderSystem::WriteTexture(Texture& texture, const SubTextureDescriptor& subTextureDesc, const SrcImageDescriptor& imageDesc)
{
    //todo
}

void VKRenderSystem::ReadTexture(const Texture& texture, std::uint32_t mipLevel, const DstImageDescriptor& imageDesc)
{
    //todo
}

void VKRenderSystem::GenerateMips(Texture& texture)
{
    auto& textureVK = LLGL_CAST(VKTexture&, texture);
    GenerateMipsPrimary(
        textureVK,
        0,
        textureVK.GetNumMipLevels(),
        0,
        textureVK.GetNumArrayLayers()
    );
}

void VKRenderSystem::GenerateMips(Texture& texture, std::uint32_t baseMipLevel, std::uint32_t numMipLevels, std::uint32_t baseArrayLayer, std::uint32_t numArrayLayers)
{
    auto& textureVK = LLGL_CAST(VKTexture&, texture);

    const auto maxNumMipLevels      = textureVK.GetNumMipLevels();
    const auto maxNumArrayLayers    = std::uint32_t(1u); //TODO...

    if (baseMipLevel < maxNumMipLevels && baseArrayLayer < maxNumArrayLayers && numMipLevels > 0 && numArrayLayers > 0)
    {
        GenerateMipsPrimary(
            textureVK,
            baseMipLevel,
            std::min(numMipLevels, maxNumMipLevels - baseMipLevel),
            baseArrayLayer,
            std::min(numArrayLayers, maxNumArrayLayers - baseArrayLayer)
        );
    }
}

/* ----- Sampler States ---- */

Sampler* VKRenderSystem::CreateSampler(const SamplerDescriptor& desc)
{
    return TakeOwnership(samplers_, MakeUnique<VKSampler>(device_, desc));
}

void VKRenderSystem::Release(Sampler& sampler)
{
    RemoveFromUniqueSet(samplers_, &sampler);
}

/* ----- Resource Heaps ----- */

ResourceHeap* VKRenderSystem::CreateResourceHeap(const ResourceHeapDescriptor& desc)
{
    return TakeOwnership(resourceHeaps_, MakeUnique<VKResourceHeap>(device_, desc));
}

void VKRenderSystem::Release(ResourceHeap& resourceHeap)
{
    RemoveFromUniqueSet(resourceHeaps_, &resourceHeap);
}

/* ----- Render Passes ----- */

RenderPass* VKRenderSystem::CreateRenderPass(const RenderPassDescriptor& desc)
{
    AssertCreateRenderPass(desc);
    return TakeOwnership(renderPasses_, MakeUnique<VKRenderPass>(device_, desc));
}

void VKRenderSystem::Release(RenderPass& renderPass)
{
    RemoveFromUniqueSet(renderPasses_, &renderPass);
}

/* ----- Render Targets ----- */

RenderTarget* VKRenderSystem::CreateRenderTarget(const RenderTargetDescriptor& desc)
{
    AssertCreateRenderTarget(desc);
    return TakeOwnership(renderTargets_, MakeUnique<VKRenderTarget>(device_, *deviceMemoryMngr_, desc));
}

void VKRenderSystem::Release(RenderTarget& renderTarget)
{
    /* Release device memory region, then release texture object */
    auto& renderTargetVL = LLGL_CAST(VKRenderTarget&, renderTarget);
    renderTargetVL.ReleaseDeviceMemoryResources(*deviceMemoryMngr_);
    RemoveFromUniqueSet(renderTargets_, &renderTarget);
}

/* ----- Shader ----- */

Shader* VKRenderSystem::CreateShader(const ShaderDescriptor& desc)
{
    AssertCreateShader(desc);
    return TakeOwnership(shaders_, MakeUnique<VKShader>(device_, desc));
}

ShaderProgram* VKRenderSystem::CreateShaderProgram(const ShaderProgramDescriptor& desc)
{
    AssertCreateShaderProgram(desc);
    return TakeOwnership(shaderPrograms_, MakeUnique<VKShaderProgram>(desc));
}

void VKRenderSystem::Release(Shader& shader)
{
    RemoveFromUniqueSet(shaders_, &shader);
}

void VKRenderSystem::Release(ShaderProgram& shaderProgram)
{
    RemoveFromUniqueSet(shaderPrograms_, &shaderProgram);
}

/* ----- Pipeline Layouts ----- */

PipelineLayout* VKRenderSystem::CreatePipelineLayout(const PipelineLayoutDescriptor& desc)
{
    return TakeOwnership(pipelineLayouts_, MakeUnique<VKPipelineLayout>(device_, desc));
}

void VKRenderSystem::Release(PipelineLayout& pipelineLayout)
{
    RemoveFromUniqueSet(pipelineLayouts_, &pipelineLayout);
}

/* ----- Pipeline States ----- */

GraphicsPipeline* VKRenderSystem::CreateGraphicsPipeline(const GraphicsPipelineDescriptor& desc)
{
    return TakeOwnership(
        graphicsPipelines_,
        MakeUnique<VKGraphicsPipeline>(
            device_,
            defaultPipelineLayout_,
            (!renderContexts_.empty() ? (*renderContexts_.begin())->GetRenderPass() : nullptr),
            desc,
            gfxPipelineLimits_
        )
    );
}

ComputePipeline* VKRenderSystem::CreateComputePipeline(const ComputePipelineDescriptor& desc)
{
    return TakeOwnership(computePipelines_, MakeUnique<VKComputePipeline>(device_, desc, defaultPipelineLayout_));
}

void VKRenderSystem::Release(GraphicsPipeline& graphicsPipeline)
{
    RemoveFromUniqueSet(graphicsPipelines_, &graphicsPipeline);
}

void VKRenderSystem::Release(ComputePipeline& computePipeline)
{
    RemoveFromUniqueSet(computePipelines_, &computePipeline);
}

/* ----- Queries ----- */

Query* VKRenderSystem::CreateQuery(const QueryDescriptor& desc)
{
    return TakeOwnership(queries_, MakeUnique<VKQuery>(device_, desc));
}

void VKRenderSystem::Release(Query& query)
{
    RemoveFromUniqueSet(queries_, &query);
}

/* ----- Fences ----- */

Fence* VKRenderSystem::CreateFence()
{
    return TakeOwnership(fences_, MakeUnique<VKFence>(device_));
}

void VKRenderSystem::Release(Fence& fence)
{
    RemoveFromUniqueSet(fences_, &fence);
}


/*
 * ======= Private: =======
 */

void VKRenderSystem::CreateInstance(const ApplicationDescriptor* applicationDesc)
{
    /* Initialize application descriptor */
    VkApplicationInfo appInfo;

    appInfo.sType                   = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext                   = nullptr;

    if (applicationDesc)
    {
        appInfo.pApplicationName    = applicationDesc->applicationName.c_str();
        appInfo.applicationVersion  = applicationDesc->applicationVersion;
        appInfo.pEngineName         = applicationDesc->engineName.c_str();
        appInfo.engineVersion       = applicationDesc->engineVersion;
    }
    else
    {
        appInfo.pApplicationName    = nullptr;
        appInfo.applicationVersion  = 0;
        appInfo.pEngineName         = nullptr;
        appInfo.engineVersion       = 0;
    }

    appInfo.apiVersion              = VK_API_VERSION_1_0;

    /* Query instance layer properties */
    auto layerProperties = VKQueryInstanceLayerProperties();
    std::vector<const char*> layerNames;

    for (const auto& prop : layerProperties)
    {
        if (IsLayerRequired(prop.layerName))
            layerNames.push_back(prop.layerName);
    }

    /* Query instance extension properties */
    auto extensionProperties = VKQueryInstanceExtensionProperties();
    std::vector<const char*> extensionNames;

    for (const auto& prop : extensionProperties)
    {
        if (IsExtensionRequired(prop.extensionName))
            extensionNames.push_back(prop.extensionName);
    }

    /* Setup Vulkan instance descriptor */
    VkInstanceCreateInfo instanceInfo;

    instanceInfo.sType                          = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pNext                          = nullptr;
    instanceInfo.flags                          = 0;
    instanceInfo.pApplicationInfo               = (&appInfo);

    if (layerNames.empty())
    {
        instanceInfo.enabledLayerCount          = 0;
        instanceInfo.ppEnabledLayerNames        = nullptr;
    }
    else
    {
        instanceInfo.enabledLayerCount          = static_cast<std::uint32_t>(layerNames.size());
        instanceInfo.ppEnabledLayerNames        = layerNames.data();
    }

    if (extensionNames.empty())
    {
        instanceInfo.enabledExtensionCount      = 0;
        instanceInfo.ppEnabledExtensionNames    = nullptr;
    }
    else
    {
        instanceInfo.enabledExtensionCount      = static_cast<std::uint32_t>(extensionNames.size());
        instanceInfo.ppEnabledExtensionNames    = extensionNames.data();
    }

    /* Create Vulkan instance */
    VkResult result = vkCreateInstance(&instanceInfo, nullptr, instance_.ReleaseAndGetAddressOf());
    VKThrowIfFailed(result, "failed to create Vulkan instance");

    if (debugLayerEnabled_)
        CreateDebugReportCallback();
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VKDebugCallback(
    VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
    uint64_t object, size_t location, int32_t messageCode,
    const char* layerPrefix, const char* message, void* userData)
{
    //auto renderSystemVK = reinterpret_cast<VKRenderSystem*>(userData);
    Log::StdErr() << message << std::endl;
    return VK_FALSE;
}

void VKRenderSystem::CreateDebugReportCallback()
{
    /* Initialize flags */
    VkDebugReportFlagsEXT flags = 0;

    //flags |= VK_DEBUG_REPORT_INFORMATION_BIT_EXT;
    flags |= VK_DEBUG_REPORT_WARNING_BIT_EXT;
    //flags |= VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    flags |= VK_DEBUG_REPORT_ERROR_BIT_EXT;
    //flags |= VK_DEBUG_REPORT_DEBUG_BIT_EXT;

    /* Create report callback */
    VkDebugReportCallbackCreateInfoEXT createInfo;
    {
        createInfo.sType        = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        createInfo.pNext        = nullptr;
        createInfo.flags        = flags;
        createInfo.pfnCallback  = VKDebugCallback;
        createInfo.pUserData    = reinterpret_cast<void*>(this);
    }
    auto result = CreateDebugReportCallbackEXT(instance_, &createInfo, nullptr, debugReportCallback_.ReleaseAndGetAddressOf());
    VKThrowIfFailed(result, "failed to create Vulkan debug report callback");
}

void VKRenderSystem::LoadExtensions()
{
    LoadAllExtensions(instance_);
}

bool VKRenderSystem::PickPhysicalDevice()
{
    /* Query all physical devices and pick suitable */
    auto devices = VKQueryPhysicalDevices(instance_);

    for (const auto& dev : devices)
    {
        if (IsPhysicalDeviceSuitable(dev))
        {
            physicalDevice_ = dev;
            return true;
        }
    }

    return false;
}

void VKRenderSystem::QueryDeviceProperties()
{
    /* Query physical device features and memory propertiers */
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);
    vkGetPhysicalDeviceFeatures(physicalDevice_, &features_);

    /* Query properties of selected physical device */
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);

    /* Map properties to output renderer info */
    RendererInfo info;

    info.rendererName           = "Vulkan " + VKApiVersionToString(properties.apiVersion);
    info.deviceName             = std::string(properties.deviceName);
    info.vendorName             = GetVendorByID(properties.vendorID);
    info.shadingLanguageName    = "SPIR-V";

    SetRendererInfo(info);

    /* Map limits to output rendering capabilites */
    const auto& limits = properties.limits;

    RenderingCapabilities caps;
    {
        /* Query common attributes */
        caps.screenOrigin                               = ScreenOrigin::UpperLeft;
        caps.clippingRange                              = ClippingRange::ZeroToOne;
        caps.shadingLanguages                           = { ShadingLanguage::SPIRV, ShadingLanguage::SPIRV_100 };
        //caps.textureFormats                             = ; //???

        /* Query features */
        caps.features.hasRenderTargets                  = true;
        caps.features.has3DTextures                     = true;
        caps.features.hasCubeTextures                   = true;
        caps.features.hasArrayTextures                  = true;
        caps.features.hasCubeArrayTextures              = (features_.imageCubeArray != VK_FALSE);
        caps.features.hasMultiSampleTextures            = true;
        caps.features.hasSamplers                       = true;
        caps.features.hasConstantBuffers                = true;
        caps.features.hasStorageBuffers                 = true;
        caps.features.hasUniforms                       = true;
        caps.features.hasGeometryShaders                = (features_.geometryShader != VK_FALSE);
        caps.features.hasTessellationShaders            = (features_.tessellationShader != VK_FALSE);
        caps.features.hasComputeShaders                 = true;
        caps.features.hasInstancing                     = true;
        caps.features.hasOffsetInstancing               = true;
        caps.features.hasViewportArrays                 = (features_.multiViewport != VK_FALSE);
        caps.features.hasConservativeRasterization      = false;
        caps.features.hasStreamOutputs                  = false;
        caps.features.hasLogicOp                        = true;

        /* Query limits */
        caps.limits.lineWidthRange[0]                   = limits.lineWidthRange[0];
        caps.limits.lineWidthRange[1]                   = limits.lineWidthRange[1];
        caps.limits.maxNumTextureArrayLayers            = limits.maxImageArrayLayers;
        caps.limits.maxNumRenderTargetAttachments       = static_cast<std::uint32_t>(limits.framebufferColorSampleCounts);
        caps.limits.maxPatchVertices                    = limits.maxTessellationPatchSize;
        caps.limits.max1DTextureSize                    = limits.maxImageDimension1D;
        caps.limits.max2DTextureSize                    = limits.maxImageDimension2D;
        caps.limits.max3DTextureSize                    = limits.maxImageDimension3D;
        caps.limits.maxCubeTextureSize                  = limits.maxImageDimensionCube;
        caps.limits.maxAnisotropy                       = static_cast<std::uint32_t>(limits.maxSamplerAnisotropy);
        caps.limits.maxNumComputeShaderWorkGroups[0]    = limits.maxComputeWorkGroupCount[0];
        caps.limits.maxNumComputeShaderWorkGroups[1]    = limits.maxComputeWorkGroupCount[1];
        caps.limits.maxNumComputeShaderWorkGroups[2]    = limits.maxComputeWorkGroupCount[2];
        caps.limits.maxComputeShaderWorkGroupSize[0]    = limits.maxComputeWorkGroupSize[0];
        caps.limits.maxComputeShaderWorkGroupSize[1]    = limits.maxComputeWorkGroupSize[1];
        caps.limits.maxComputeShaderWorkGroupSize[2]    = limits.maxComputeWorkGroupSize[2];
        caps.limits.maxNumViewports                     = limits.maxViewports;
        caps.limits.maxViewportSize[0]                  = limits.maxViewportDimensions[0];
        caps.limits.maxViewportSize[1]                  = limits.maxViewportDimensions[1];
        caps.limits.maxBufferSize                       = std::numeric_limits<VkDeviceSize>::max();
        caps.limits.maxConstantBufferSize               = limits.maxUniformBufferRange;
    }
    SetRenderingCaps(caps);

    /* Store graphics pipeline spcific limitations */
    gfxPipelineLimits_.lineWidthRange[0]    = limits.lineWidthRange[0];
    gfxPipelineLimits_.lineWidthRange[1]    = limits.lineWidthRange[1];
    gfxPipelineLimits_.lineWidthGranularity = limits.lineWidthGranularity;
}

// Device-only layers are deprecated -> set 'enabledLayerCount' and 'ppEnabledLayerNames' members to zero during device creation.
// see https://www.khronos.org/registry/vulkan/specs/1.0/html/vkspec.html#extended-functionality-device-layer-deprecation
void VKRenderSystem::CreateLogicalDevice()
{
    /* Initialize queue create description */
    queueFamilyIndices_ = VKFindQueueFamilies(physicalDevice_, (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT));

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<std::uint32_t> uniqueQueueFamilies = { queueFamilyIndices_.graphicsFamily, queueFamilyIndices_.presentFamily };

    float queuePriority = 1.0f;
    for (auto family : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo info;
        {
            info.sType              = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            info.pNext              = nullptr;
            info.flags              = 0;
            info.queueFamilyIndex   = family;
            info.queueCount         = 1;
            info.pQueuePriorities   = &queuePriority;
        }
        queueCreateInfos.push_back(info);
    }

    /* Create logical device */
    VkDeviceCreateInfo createInfo;
    {
        createInfo.sType                    = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext                    = nullptr;
        createInfo.flags                    = 0;
        createInfo.queueCreateInfoCount     = static_cast<std::uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos        = queueCreateInfos.data();
        createInfo.enabledLayerCount        = 0;
        createInfo.ppEnabledLayerNames      = nullptr;
        createInfo.enabledExtensionCount    = static_cast<std::uint32_t>(g_deviceExtensions.size());
        createInfo.ppEnabledExtensionNames  = g_deviceExtensions.data();
        createInfo.pEnabledFeatures         = &features_;
    }
    VkResult result = vkCreateDevice(physicalDevice_, &createInfo, nullptr, device_.ReleaseAndGetAddressOf());
    VKThrowIfFailed(result, "failed to create Vulkan logical device");

    /* Query device graphics queue */
    vkGetDeviceQueue(device_, queueFamilyIndices_.graphicsFamily, 0, &graphicsQueue_);

    /* Create command queue interface */
    commandQueue_ = MakeUnique<VKCommandQueue>(device_, graphicsQueue_);
}

void VKRenderSystem::CreateStagingCommandResources()
{
    /* Create staging command pool */
    VkCommandPoolCreateInfo createInfo;
    {
        createInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        createInfo.pNext            = nullptr;
        createInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = queueFamilyIndices_.graphicsFamily;
    }
    auto result = vkCreateCommandPool(device_, &createInfo, nullptr, stagingCommandPool_.ReleaseAndGetAddressOf());
    VKThrowIfFailed(result, "failed to create Vulkan command pool for staging buffers");

    /* Allocate staging command buffer */
    VkCommandBufferAllocateInfo allocInfo;
    {
        allocInfo.sType                 = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.pNext                 = nullptr;
        allocInfo.commandPool           = stagingCommandPool_;
        allocInfo.level                 = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount    = 1;
    }
    result = vkAllocateCommandBuffers(device_, &allocInfo, &stagingCommandBuffer_);
    VKThrowIfFailed(result, "failed to create Vulkan command buffer for staging buffers");
}

void VKRenderSystem::ReleaseStagingCommandResources()
{
    /* Release staging command buffer */
    vkFreeCommandBuffers(device_, stagingCommandPool_, 1, &stagingCommandBuffer_);
    stagingCommandBuffer_ = VK_NULL_HANDLE;
}

void VKRenderSystem::CreateDefaultPipelineLayout()
{
    VkPipelineLayoutCreateInfo layoutCreateInfo = {};
    {
        layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    }
    auto result = vkCreatePipelineLayout(device_, &layoutCreateInfo, nullptr, defaultPipelineLayout_.ReleaseAndGetAddressOf());
    VKThrowIfFailed(result, "failed to create Vulkan default pipeline layout");
}

bool VKRenderSystem::IsLayerRequired(const std::string& name) const
{
    //TODO: make this statically optional
    #ifdef LLGL_DEBUG
    if (name == "VK_LAYER_LUNARG_core_validation")
        return true;
    #endif
    return false;
}

bool VKRenderSystem::IsExtensionRequired(const std::string& name) const
{
    return
    (
        name == VK_KHR_SURFACE_EXTENSION_NAME
        #ifdef LLGL_OS_WIN32
        || name == VK_KHR_WIN32_SURFACE_EXTENSION_NAME
        #endif
        #ifdef LLGL_OS_LINUX
        || name == VK_KHR_XLIB_SURFACE_EXTENSION_NAME
        #endif
        || (debugLayerEnabled_ && name == VK_EXT_DEBUG_REPORT_EXTENSION_NAME)
    );
}

bool VKRenderSystem::IsPhysicalDeviceSuitable(VkPhysicalDevice device) const
{
    if (CheckDeviceExtensionSupport(device, g_deviceExtensions))
    {
        //TODO...
        return true;
    }
    return false;
}

bool VKRenderSystem::CheckDeviceExtensionSupport(VkPhysicalDevice device, const std::vector<const char*>& extensionNames) const
{
    /* Check if device supports all required extensions */
    auto availableExtensions = VKQueryDeviceExtensionProperties(device);

    std::set<std::string> requiredExtensions(extensionNames.begin(), extensionNames.end());

    for (const auto& ext : availableExtensions)
        requiredExtensions.erase(ext.extensionName);

    return requiredExtensions.empty();
}

std::uint32_t VKRenderSystem::FindMemoryType(std::uint32_t memoryTypeBits, VkMemoryPropertyFlags properties) const
{
    return VKFindMemoryType(memoryProperties_, memoryTypeBits, properties);
}

VKBuffer* VKRenderSystem::CreateHardwareBuffer(const BufferDescriptor& desc, VkBufferUsageFlags usage)
{
    /* Create hardware buffer */
    VkBufferCreateInfo createInfo;

    switch (desc.type)
    {
        case BufferType::Vertex:
        {
            FillBufferCreateInfo(createInfo, static_cast<VkDeviceSize>(desc.size), (usage | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            return TakeOwnership(buffers_, MakeUnique<VKBuffer>(BufferType::Vertex, device_, createInfo));
        }
        break;

        case BufferType::Index:
        {
            FillBufferCreateInfo(createInfo, static_cast<VkDeviceSize>(desc.size), (usage | VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
            return TakeOwnership(buffers_, MakeUnique<VKIndexBuffer>(device_, createInfo, desc.indexBuffer.format));
        }
        break;

        case BufferType::Constant:
        {
            FillBufferCreateInfo(createInfo, static_cast<VkDeviceSize>(desc.size), (usage | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT));
            return TakeOwnership(buffers_, MakeUnique<VKBuffer>(BufferType::Constant, device_, createInfo));
        }
        break;

        case BufferType::Storage:
        {
            FillBufferCreateInfo(createInfo, static_cast<VkDeviceSize>(desc.size), (usage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
            return TakeOwnership(buffers_, MakeUnique<VKBuffer>(BufferType::Storage, device_, createInfo));
        }
        break;

        case BufferType::StreamOutput:
        {
            throw std::runtime_error("stream output buffer not supported by Vulkan renderer");
        }
        break;

        default:
        break;
    }
    return nullptr;
}

std::tuple<VKBufferWithRequirements, VKDeviceMemoryRegion*> VKRenderSystem::CreateStagingBuffer(const VkBufferCreateInfo& stagingCreateInfo, const void* initialData, std::size_t initialDataSize)
{
    VKBufferWithRequirements stagingBuffer { device_ };
    stagingBuffer.Create(device_, stagingCreateInfo);

    /* Allocate statging device memory */
    auto memoryRegionStaging = deviceMemoryMngr_->Allocate(
        stagingBuffer.requirements.size,
        stagingBuffer.requirements.alignment,
        stagingBuffer.requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    memoryRegionStaging->BindBuffer(device_, stagingBuffer.buffer);

    /* Copy initial data to buffer memory */
    if (initialData != nullptr)
    {
        auto stagingDeviceMemory = memoryRegionStaging->GetParentChunk();

        if (auto memory = stagingDeviceMemory->Map(device_, memoryRegionStaging->GetOffset(), static_cast<VkDeviceSize>(initialDataSize)))
        {
            ::memcpy(memory, initialData, initialDataSize);
            stagingDeviceMemory->Unmap(device_);
        }
    }

    return std::make_tuple(std::move(stagingBuffer), memoryRegionStaging);
}

void VKRenderSystem::BeginStagingCommands()
{
    /* Begin command buffer record */
    VkCommandBufferBeginInfo beginInfo;
    {
        beginInfo.sType             = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext             = nullptr;
        beginInfo.flags             = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo  = nullptr;
    }
    auto result = vkBeginCommandBuffer(stagingCommandBuffer_, &beginInfo);
    VKThrowIfFailed(result, "failed to begin recording Vulkan command buffer");
}

void VKRenderSystem::EndStagingCommands()
{
    /* End command buffer record */
    vkEndCommandBuffer(stagingCommandBuffer_);

    /* Submit command buffer to queue */
    VkSubmitInfo submitInfo = {};
    {
        submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount   = 1;
        submitInfo.pCommandBuffers      = (&stagingCommandBuffer_);
    }
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
}

void VKRenderSystem::TransitionImageLayout(
    VkImage image, VkFormat /*format*/, VkImageLayout oldLayout, VkImageLayout newLayout, std::uint32_t numMipLevels, std::uint32_t numArrayLayers)
{
    BeginStagingCommands();

    /* Initialize image memory barrier descriptor */
    VkImageMemoryBarrier barrier;
    {
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.pNext                           = nullptr;
        barrier.srcAccessMask                   = 0;
        barrier.dstAccessMask                   = 0;
        barrier.oldLayout                       = oldLayout;
        barrier.newLayout                       = newLayout;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = numMipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = numArrayLayers;
    }

    /* Initialize pipeline state flags */
    VkPipelineStageFlags srcStageMask = 0;
    VkPipelineStageFlags dstStageMask = 0;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    /* Record image barrier command */
    vkCmdPipelineBarrier(stagingCommandBuffer_, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    EndStagingCommands();
}

void VKRenderSystem::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset)
{
    BeginStagingCommands();

    /* Record copy command */
    VkBufferCopy region;
    {
        region.srcOffset    = srcOffset;
        region.dstOffset    = dstOffset;
        region.size         = size;
    }
    vkCmdCopyBuffer(stagingCommandBuffer_, srcBuffer, dstBuffer, 1, &region);

    EndStagingCommands();
}

void VKRenderSystem::CopyBufferToImage(VkBuffer srcBuffer, VkImage dstImage, const VkExtent3D& extent, std::uint32_t numLayers)
{
    BeginStagingCommands();

    /* Record copy command */
    VkBufferImageCopy region;
    {
        region.bufferOffset                     = 0;
        region.bufferRowLength                  = 0;
        region.bufferImageHeight                = 0;
        region.imageSubresource.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel        = 0;
        region.imageSubresource.baseArrayLayer  = 0;
        region.imageSubresource.layerCount      = numLayers;
        region.imageOffset                      = { 0, 0, 0 };
        region.imageExtent                      = extent;
    }
    vkCmdCopyBufferToImage(stagingCommandBuffer_, srcBuffer, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    EndStagingCommands();
}

void VKRenderSystem::AssertBufferCPUAccess(const VKBuffer& bufferVK)
{
    if (bufferVK.GetStagingVkBuffer() == VK_NULL_HANDLE)
        throw std::runtime_error("hardware buffer was not created with CPU access (missing staging VkBuffer)");
}

//TODO: add parameters
void VKRenderSystem::GenerateMipsPrimary(
    VKTexture&      textureVK,
    std::uint32_t   baseMipLevel,
    std::uint32_t   numMipLevels,
    std::uint32_t   baseArrayLayer,
    std::uint32_t   numArrayLayers)
{
    /* Get Vulkan image object */
    auto image  = textureVK.GetVkImage();
    auto extent = textureVK.GetVkExtent();

    TransitionImageLayout(image, VK_FORMAT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, numMipLevels, numArrayLayers);

    BeginStagingCommands();

    /* Initialize image memory barrier */
    VkImageMemoryBarrier barrier;

    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext                           = nullptr;
    barrier.srcAccessMask                   = 0;
    barrier.dstAccessMask                   = 0;
    barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    /* Blit each MIP-map from previous (lower) MIP level */
    for (std::uint32_t arrayLayer = 0; arrayLayer < numArrayLayers; ++arrayLayer)
    {
        auto currExtent = extent;

        for (std::uint32_t mipLevel = 1; mipLevel < numMipLevels; ++mipLevel)
        {
            /* Determine extent of next MIP level */
            auto nextExtent = currExtent;

            nextExtent.width    = std::max(1u, currExtent.width  / 2);
            nextExtent.height   = std::max(1u, currExtent.height / 2);
            nextExtent.depth    = std::max(1u, currExtent.depth  / 2);

            /* Transition previous MIP level to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL */
            barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.subresourceRange.baseMipLevel   = mipLevel - 1;
            barrier.subresourceRange.baseArrayLayer = arrayLayer;

            vkCmdPipelineBarrier(
                stagingCommandBuffer_,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );

            /* Blit previous MIP level into next higher MIP level (with smaller extent) */
            VkImageBlit blit;

            blit.srcSubresource.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel        = mipLevel - 1;
            blit.srcSubresource.baseArrayLayer  = arrayLayer;
            blit.srcSubresource.layerCount      = 1;
            blit.srcOffsets[0]                  = { 0, 0, 0 };
            blit.srcOffsets[1].x                = static_cast<std::int32_t>(currExtent.width);
            blit.srcOffsets[1].y                = static_cast<std::int32_t>(currExtent.height);
            blit.srcOffsets[1].z                = static_cast<std::int32_t>(currExtent.depth);
            blit.dstSubresource.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel        = mipLevel;
            blit.dstSubresource.baseArrayLayer  = arrayLayer;
            blit.dstSubresource.layerCount      = 1;
            blit.dstOffsets[0]                  = { 0, 0, 0 };
            blit.dstOffsets[1].x                = static_cast<std::int32_t>(nextExtent.width);
            blit.dstOffsets[1].y                = static_cast<std::int32_t>(nextExtent.height);
            blit.dstOffsets[1].z                = static_cast<std::int32_t>(nextExtent.depth);

            vkCmdBlitImage(
                stagingCommandBuffer_,
                image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit,
                VK_FILTER_LINEAR
            );

            /* Transition previous MIP level back to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL */
            barrier.srcAccessMask   = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask   = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout       = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            vkCmdPipelineBarrier(
                stagingCommandBuffer_,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );

            /* Reduce image extent to next MIP level */
            currExtent = nextExtent;
        }

        /* Transition last MIP level back to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL */
        barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.subresourceRange.baseMipLevel   = numMipLevels - 1;

        vkCmdPipelineBarrier(
            stagingCommandBuffer_,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );
    }

    EndStagingCommands();
}


} // /namespace LLGL



// ================================================================================
