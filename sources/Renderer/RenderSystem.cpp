/*
 * RenderSystem.cpp
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015-2018 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "../Platform/Module.h"
#include "../Core/Helper.h"
#include <LLGL/Platform/Platform.h>
#include <LLGL/Log.h>
#include "BuildID.h"
#include "StaticLimits.h"

#include <LLGL/RenderSystem.h>
#include <array>
#include <map>

#ifdef LLGL_ENABLE_DEBUG_LAYER
#   include "DebugLayer/DbgRenderSystem.h"
#endif

#ifdef LLGL_BUILD_STATIC_LIB
#   include "ModuleInterface.h"
#endif


namespace LLGL
{


/* ----- Render system ----- */

static std::map<RenderSystem*, std::unique_ptr<Module>> g_renderSystemModules;

std::vector<std::string> RenderSystem::FindModules()
{
    /* Iterate over all known modules and return those that are available on the current platform */
    const std::vector<std::string> knownModules
    {
        #if defined(LLGL_OS_IOS) || defined(LLGL_OS_ANDROID)
        "OpenGLES3",
        #else
        "OpenGL",
        #endif

        #if defined(LLGL_OS_MACOS) || defined(LLGL_OS_IOS)
        "Metal",
        #else
        "Vulkan",
        #endif

        #ifdef LLGL_OS_WIN32
        "Direct3D11",
        "Direct3D12",
        #endif
    };

    std::vector<std::string> modules;

    for (const auto& m : knownModules)
    {
        if (Module::IsAvailable(Module::GetModuleFilename(m)))
            modules.push_back(m);
    }

    return modules;
}

#ifndef LLGL_BUILD_STATIC_LIB

static bool LoadRenderSystemBuildID(Module& module, const std::string& moduleFilename)
{
    /* Load "LLGL_RenderSystem_BuildID" procedure */
    LLGL_PROC_INTERFACE(int, PFN_RENDERSYSTEM_BUILDID, (void));

    auto RenderSystem_BuildID = reinterpret_cast<PFN_RENDERSYSTEM_BUILDID>(module.LoadProcedure("LLGL_RenderSystem_BuildID"));
    if (!RenderSystem_BuildID)
        throw std::runtime_error("failed to load \"LLGL_RenderSystem_BuildID\" procedure from module \"" + moduleFilename + "\"");

    return (RenderSystem_BuildID() == LLGL_BUILD_ID);
}

static int LoadRenderSystemRendererID(Module& module)
{
    /* Load "LLGL_RenderSystem_RendererID" procedure */
    LLGL_PROC_INTERFACE(int, PFN_RENDERSYSTEM_RENDERERID, (void));

    auto RenderSystem_RendererID = reinterpret_cast<PFN_RENDERSYSTEM_RENDERERID>(module.LoadProcedure("LLGL_RenderSystem_RendererID"));
    if (RenderSystem_RendererID)
        return RenderSystem_RendererID();

    return RendererID::Undefined;
}

static std::string LoadRenderSystemName(Module& module)
{
    /* Load "LLGL_RenderSystem_Name" procedure */
    LLGL_PROC_INTERFACE(const char*, PFN_RENDERSYSTEM_NAME, (void));

    auto RenderSystem_Name = reinterpret_cast<PFN_RENDERSYSTEM_NAME>(module.LoadProcedure("LLGL_RenderSystem_Name"));
    if (RenderSystem_Name)
        return std::string(RenderSystem_Name());

    return "";
}

static RenderSystem* LoadRenderSystem(Module& module, const std::string& moduleFilename, const RenderSystemDescriptor& renderSystemDesc)
{
    /* Load "LLGL_RenderSystem_Alloc" procedure */
    LLGL_PROC_INTERFACE(void*, PFN_RENDERSYSTEM_ALLOC, (const void*));

    auto RenderSystem_Alloc = reinterpret_cast<PFN_RENDERSYSTEM_ALLOC>(module.LoadProcedure("LLGL_RenderSystem_Alloc"));
    if (!RenderSystem_Alloc)
        throw std::runtime_error("failed to load \"LLGL_RenderSystem_Alloc\" procedure from module \"" + moduleFilename + "\"");

    return reinterpret_cast<RenderSystem*>(RenderSystem_Alloc(&renderSystemDesc));
}

#endif // /LLGL_BUILD_STATIC_LIB

std::unique_ptr<RenderSystem> RenderSystem::Load(
    const RenderSystemDescriptor& renderSystemDesc, RenderingProfiler* profiler, RenderingDebugger* debugger)
{
    #ifdef LLGL_BUILD_STATIC_LIB

    /*
    Verify build ID from render system module to detect a module,
    that has compiled with a different compiler (type, version, debug/release mode etc.)
    */
    if (LLGL_RenderSystem_BuildID() != LLGL_BUILD_ID)
        throw std::runtime_error("build ID mismatch in render system module");

    /* Allocate render system */
    auto renderSystem   = std::unique_ptr<RenderSystem>(reinterpret_cast<RenderSystem*>(LLGL_RenderSystem_Alloc()));

    if (profiler != nullptr || debugger != nullptr)
    {
        #ifdef LLGL_ENABLE_DEBUG_LAYER

        /* Create debug layer render system */
        renderSystem = MakeUnique<DbgRenderSystem>(std::move(renderSystem), profiler, debugger);

        #else

        Log::StdErr() << "LLGL was not compiled with debug layer support" << std::endl;

        #endif // /LLGL_ENABLE_DEBUG_LAYER
    }

    renderSystem->name_         = LLGL_RenderSystem_Name();
    renderSystem->rendererID_   = LLGL_RenderSystem_RendererID();

    /* Return new render system and unique pointer */
    return renderSystem;

    #else

    /* Load render system module */
    auto moduleFilename = Module::GetModuleFilename(renderSystemDesc.moduleName);
    auto module         = Module::Load(moduleFilename);

    /*
    Verify build ID from render system module to detect a module,
    that has compiled with a different compiler (type, version, debug/release mode etc.)
    */
    if (!LoadRenderSystemBuildID(*module, moduleFilename))
        throw std::runtime_error("build ID mismatch in render system module");

    try
    {
        /* Allocate render system */
        auto renderSystem = std::unique_ptr<RenderSystem>(LoadRenderSystem(*module, moduleFilename, renderSystemDesc));

        if (profiler != nullptr || debugger != nullptr)
        {
            #ifdef LLGL_ENABLE_DEBUG_LAYER

            /* Create debug layer render system */
            renderSystem = MakeUnique<DbgRenderSystem>(std::move(renderSystem), profiler, debugger);

            #else

            Log::StdErr() << "LLGL was not compiled with debug layer support" << std::endl;

            #endif // /LLGL_ENABLE_DEBUG_LAYER
        }

        renderSystem->name_         = LoadRenderSystemName(*module);
        renderSystem->rendererID_   = LoadRenderSystemRendererID(*module);

        /* Store new module inside internal map */
        g_renderSystemModules[renderSystem.get()] = std::move(module);

        /* Return new render system and unique pointer */
        return renderSystem;
    }
    catch (const std::exception&)
    {
        /* Keep module, otherwise the exception's vtable might be corrupted because it's part of the module */
        g_renderSystemModules[nullptr] = std::move(module);
        throw;
    }

    #endif // /LLGL_BUILD_STATIC_LIB
}

void RenderSystem::Unload(std::unique_ptr<RenderSystem>&& renderSystem)
{
    auto it = g_renderSystemModules.find(renderSystem.get());
    if (it != g_renderSystemModules.end())
    {
        renderSystem.release();
        g_renderSystemModules.erase(it);
    }
}

void RenderSystem::SetConfiguration(const RenderSystemConfiguration& config)
{
    config_ = config;
}


/*
 * ======= Protected: =======
 */

void RenderSystem::SetRendererInfo(const RendererInfo& info)
{
    info_ = info;
}

void RenderSystem::SetRenderingCaps(const RenderingCapabilities& caps)
{
    caps_ = caps;
}

void RenderSystem::AssertCreateBuffer(const BufferDescriptor& desc, std::uint64_t maxSize)
{
    if (desc.type < BufferType::Vertex || desc.type > BufferType::StreamOutput)
        throw std::invalid_argument("cannot create buffer of unknown type (0x" + ToHex(static_cast<std::uint8_t>(desc.type)) + ")");
    if (desc.size > maxSize)
        throw std::runtime_error("cannot create buffer with size of " + std::to_string(desc.size) + " bytes (limit is " + std::to_string(maxSize) + " bytes)");
}

static void AssertCreateResourceArrayCommon(std::uint32_t numResources, void* const * resourceArray, const std::string& resourceName)
{
    /* Validate number of buffers */
    if (numResources == 0)
        throw std::invalid_argument("cannot create " + resourceName + " array with zero " + resourceName + "s");

    /* Validate array pointer */
    if (resourceArray == nullptr)
        throw std::invalid_argument("cannot create " + resourceName + " array with invalid array pointer");

    /* Validate pointers in array */
    for (std::uint32_t i = 0; i < numResources; ++i)
    {
        if (resourceArray[i] == nullptr)
            throw std::invalid_argument("cannot create " + resourceName + " array with invalid pointer in array");
    }
}

void RenderSystem::AssertCreateBufferArray(std::uint32_t numBuffers, Buffer* const * bufferArray)
{
    /* Validate common resource array parameters */
    AssertCreateResourceArrayCommon(numBuffers, reinterpret_cast<void* const*>(bufferArray), "buffer");

    /* Validate buffer types */
    auto refType = bufferArray[0]->GetType();
    for (std::uint32_t i = 1; i < numBuffers; ++i)
    {
        if (bufferArray[i]->GetType() != refType)
            throw std::invalid_argument("cannot create buffer array with type mismatch");
    }

    /* Validate buffer array type */
    if ( refType != BufferType::Vertex      &&
         refType != BufferType::Constant    &&
         refType != BufferType::Storage     &&
         refType != BufferType::StreamOutput )
    {
        throw std::invalid_argument("invalid buffer type for buffer array");
    }
}

void RenderSystem::AssertCreateShader(const ShaderDescriptor& desc)
{
    if (desc.source == nullptr)
        throw std::invalid_argument("cannot create shader with <source> being a null pointer");
    if (desc.sourceType == ShaderSourceType::BinaryBuffer && desc.sourceSize == 0)
        throw std::invalid_argument("cannot create shader from binary buffer with <sourceSize> being zero");
}

static void AssertShaderType(Shader* shader, const char* shaderName, const ShaderType type, const char* typeName)
{
    if (shader != nullptr)
    {
        if (shader->GetType() != type)
        {
            throw std::invalid_argument(
                "cannot create shader program with '" + std::string(shaderName) +
                "' not being of type <LLGL::ShaderType::" + std::string(typeName) + ">"
            );
        }
    }
}

void RenderSystem::AssertCreateShaderProgram(const ShaderProgramDescriptor& desc)
{
    AssertShaderType(desc.vertexShader,         "vertexShader",         ShaderType::Vertex,         "Vertex"        );
    AssertShaderType(desc.tessControlShader,    "tessControlShader",    ShaderType::TessControl,    "TessControl"   );
    AssertShaderType(desc.tessEvaluationShader, "tessEvaluationShader", ShaderType::TessEvaluation, "TessEvaluation");
    AssertShaderType(desc.geometryShader,       "geometryShader",       ShaderType::Geometry,       "Geometry"      );
    AssertShaderType(desc.fragmentShader,       "fragmentShader",       ShaderType::Fragment,       "Fragment"      );
    AssertShaderType(desc.computeShader,        "computeShader",        ShaderType::Compute,        "Compute"       );

    if (desc.computeShader != nullptr)
    {
        if ( desc.vertexShader         != nullptr ||
             desc.tessControlShader    != nullptr ||
             desc.tessEvaluationShader != nullptr ||
             desc.geometryShader       != nullptr ||
             desc.fragmentShader       != nullptr )
        {
            throw std::invalid_argument(
                "cannot create shader program with 'computeShader' in conjunction with any other shader"
            );
        }
    }
    else
    {
        if (desc.vertexShader == nullptr)
            throw std::invalid_argument("cannot create shader program without vertex shader");

        if ( ( desc.tessControlShader != nullptr && desc.tessEvaluationShader == nullptr ) ||
             ( desc.tessControlShader == nullptr && desc.tessEvaluationShader != nullptr ) )
        {
            throw std::invalid_argument(
                "cannot create shader program with 'tessControlShader' and 'tessEvaluationShader' being partially specified"
            );
        }
    }
}

[[noreturn]]
static void ErrTooManyColorAttachments(const char* contextInfo)
{
    throw std::invalid_argument(
        "too many color attachments for " + std::string(contextInfo) +
        " (exceeded limits of " + std::to_string(LLGL_MAX_NUM_COLOR_ATTACHMENTS) + ")"
    );
}

void RenderSystem::AssertCreateRenderTarget(const RenderTargetDescriptor& desc)
{
    if (desc.attachments.size() == LLGL_MAX_NUM_COLOR_ATTACHMENTS + 1)
    {
        /* Check if there is one depth-stencil attachment */
        for (const auto& attachment : desc.attachments)
        {
            if (attachment.type != AttachmentType::Color)
                return;
        }
        ErrTooManyColorAttachments("render target");
    }
    else if (desc.attachments.size() > LLGL_MAX_NUM_COLOR_ATTACHMENTS + 1)
        ErrTooManyColorAttachments("render target");
}

void RenderSystem::AssertCreateRenderPass(const RenderPassDescriptor& desc)
{
    if (desc.colorAttachments.size() > LLGL_MAX_NUM_COLOR_ATTACHMENTS)
        ErrTooManyColorAttachments("render pass");
}

void RenderSystem::AssertImageDataSize(std::size_t dataSize, std::size_t requiredDataSize, const char* info)
{
    if (dataSize < requiredDataSize)
    {
        std::string s;

        /* Build error message */
        s += "image data size is too small";
        if (info)
        {
            s += " for ";
            s += info;
        }

        s += " (";
        s += std::to_string(requiredDataSize);
        s += " byte(s) are required, but only ";
        s += std::to_string(dataSize);
        s += " is specified)";

        throw std::invalid_argument(s);
    }
}


} // /namespace LLGL



// ================================================================================
