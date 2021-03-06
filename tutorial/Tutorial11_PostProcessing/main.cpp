/*
 * main.cpp (Tutorial11_PostProcessing)
 *
 * This file is part of the "LLGL" project (Copyright (c) 2015-2018 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include <tutorial.h>


class Tutorial11 : public Tutorial
{

    const LLGL::ColorRGBAf  glowColor           = { 0.9f, 0.7f, 0.3f, 1.0f };

    LLGL::ShaderProgram*    shaderProgramScene  = nullptr;
    LLGL::ShaderProgram*    shaderProgramBlur   = nullptr;
    LLGL::ShaderProgram*    shaderProgramFinal  = nullptr;

    LLGL::GraphicsPipeline* pipelineScene       = nullptr;
    LLGL::GraphicsPipeline* pipelineBlur        = nullptr;
    LLGL::GraphicsPipeline* pipelineFinal       = nullptr;

    LLGL::VertexFormat      vertexFormatScene;

    std::uint32_t           numSceneVertices    = 0;

    LLGL::Buffer*           vertexBufferScene   = nullptr;
    LLGL::Buffer*           vertexBufferNull    = nullptr;

    LLGL::Buffer*           constantBufferScene = nullptr;
    LLGL::Buffer*           constantBufferBlur  = nullptr;

    LLGL::Sampler*          colorMapSampler     = nullptr;
    LLGL::Sampler*          glossMapSampler     = nullptr;

    LLGL::Texture*          colorMap            = nullptr;
    LLGL::Texture*          glossMap            = nullptr;
    LLGL::Texture*          glossMapBlurX       = nullptr;
    LLGL::Texture*          glossMapBlurY       = nullptr;

    LLGL::RenderTarget*     renderTargetScene   = nullptr;
    LLGL::RenderTarget*     renderTargetBlurX   = nullptr;
    LLGL::RenderTarget*     renderTargetBlurY   = nullptr;

    struct SceneSettings
    {
        Gs::Matrix4f        wvpMatrix;
        Gs::Matrix4f        wMatrix;
        LLGL::ColorRGBAf    diffuse;
        LLGL::ColorRGBAf    glossiness;
        float               intensity           = 3.0f;
        float               _pad0[3];
    }
    sceneSettings;

    struct BlurSettings
    {
        Gs::Vector2f        blurShift;
        float               _pad0[2];
    }
    blurSettings;

public:

    Tutorial11() :
        Tutorial { L"LLGL Tutorial 11: PostProcessing", { 800, 600 }, 0 }
    {
        // Create all graphics objects
        CreateBuffers();
        LoadShaders();
        CreatePipelines();
        CreateSamplers();
        CreateTextures();
        CreateRenderTargets();

        // Show some information
        std::cout << "press LEFT MOUSE BUTTON and move the mouse to rotate the outer box" << std::endl;
        std::cout << "press RIGHT MOUSE BUTTON and move the mouse on the X-axis to change the glow intensity" << std::endl;
    }

    void CreateBuffers()
    {
        // Specify vertex format for scene
        vertexFormatScene.AppendAttribute({ "position", LLGL::Format::RGB32Float });
        vertexFormatScene.AppendAttribute({ "normal",   LLGL::Format::RGB32Float });

        // Create scene buffers
        auto sceneVertices = LoadObjModel("../Media/Models/WiredBox.obj");
        numSceneVertices = static_cast<std::uint32_t>(sceneVertices.size());

        vertexBufferScene = CreateVertexBuffer(sceneVertices, vertexFormatScene);
        constantBufferScene = CreateConstantBuffer(sceneSettings);

        // Create empty vertex buffer for post-processors,
        // because to draw meshes a vertex buffer is always required, even if it's empty
        LLGL::BufferDescriptor vertexBufferDesc;
        {
            vertexBufferDesc.type   = LLGL::BufferType::Vertex;
            vertexBufferDesc.size   = 1;
        }
        vertexBufferNull = renderer->CreateBuffer(vertexBufferDesc);

        // Create post-processing buffers
        constantBufferBlur = CreateConstantBuffer(blurSettings);
    }

    void LoadShaders()
    {
        const auto& languages = renderer->GetRenderingCaps().shadingLanguages;
        if (std::find(languages.begin(), languages.end(), LLGL::ShadingLanguage::HLSL) != languages.end())
        {
            // Load scene shader program
            shaderProgramScene = LoadShaderProgram(
                {
                    { LLGL::ShaderType::Vertex, "shader.hlsl", "VScene", "vs_5_0" },
                    { LLGL::ShaderType::Fragment, "shader.hlsl", "PScene", "ps_5_0" }
                },
                { vertexFormatScene }
            );

            // Load blur shader program
            shaderProgramBlur = LoadShaderProgram(
                {
                    { LLGL::ShaderType::Vertex, "shader.hlsl", "VPP", "vs_5_0" },
                    { LLGL::ShaderType::Fragment, "shader.hlsl", "PBlur", "ps_5_0" }
                }
            );

            // Load final shader program
            shaderProgramFinal = LoadShaderProgram(
                {
                    { LLGL::ShaderType::Vertex, "shader.hlsl", "VPP", "vs_5_0" },
                    { LLGL::ShaderType::Fragment, "shader.hlsl", "PFinal", "ps_5_0" }
                }
            );
        }
        else
        {
            // Load scene shader program
            shaderProgramScene = LoadShaderProgram(
                {
                    { LLGL::ShaderType::Vertex, "scene.vertex.glsl" },
                    { LLGL::ShaderType::Fragment, "scene.fragment.glsl" }
                },
                { vertexFormatScene }
            );

            // Load blur shader program
            shaderProgramBlur = LoadShaderProgram(
                {
                    { LLGL::ShaderType::Vertex, "postprocess.vertex.glsl" },
                    { LLGL::ShaderType::Fragment, "blur.fragment.glsl" }
                }
            );

            // Load final shader program
            shaderProgramFinal = LoadShaderProgram(
                {
                    { LLGL::ShaderType::Vertex, "postprocess.vertex.glsl" },
                    { LLGL::ShaderType::Fragment, "final.fragment.glsl" }
                }
            );
        }

        // Set shader uniforms (only required for GLSL until 4.10)
        shaderProgramBlur->BindConstantBuffer("BlurSettings", 1);

        if (auto uniforms = shaderProgramBlur->LockShaderUniform())
        {
            uniforms->SetUniform1i("glossMap", 1);
            shaderProgramBlur->UnlockShaderUniform();
        }

        if (auto uniforms = shaderProgramFinal->LockShaderUniform())
        {
            uniforms->SetUniform1i("colorMap", 0);
            uniforms->SetUniform1i("glossMap", 1);
            shaderProgramFinal->UnlockShaderUniform();
        }
    }

    void CreatePipelines()
    {
        // Create graphics pipeline for scene rendering
        LLGL::GraphicsPipelineDescriptor pipelineDescScene;
        {
            pipelineDescScene.shaderProgram             = shaderProgramScene;
            //pipelineDescScene.renderPass                = renderTargetScene->GetRenderPass();

            pipelineDescScene.depth.testEnabled         = true;
            pipelineDescScene.depth.writeEnabled        = true;

            pipelineDescScene.rasterizer.cullMode       = LLGL::CullMode::Back;
            pipelineDescScene.rasterizer.multiSampling  = LLGL::MultiSamplingDescriptor(8);
        }
        pipelineScene = renderer->CreateGraphicsPipeline(pipelineDescScene);

        // Create graphics pipeline for blur post-processor
        LLGL::GraphicsPipelineDescriptor pipelineDescPP;
        {
            pipelineDescPP.shaderProgram = shaderProgramBlur;
        }
        pipelineBlur = renderer->CreateGraphicsPipeline(pipelineDescPP);

        // Create graphics pipeline for final post-processor
        LLGL::GraphicsPipelineDescriptor pipelineDescFinal;
        {
            pipelineDescFinal.shaderProgram = shaderProgramFinal;
        }
        pipelineFinal = renderer->CreateGraphicsPipeline(pipelineDescFinal);
    }

    void CreateSamplers()
    {
        // Create sampler states for all textures
        LLGL::SamplerDescriptor samplerDesc;
        {
            samplerDesc.mipMapping = false;
        }
        colorMapSampler = renderer->CreateSampler(samplerDesc);
        glossMapSampler = renderer->CreateSampler(samplerDesc);
    }

    void CreateTextures()
    {
        // Create empty color and gloss map
        auto resolution = context->GetVideoMode().resolution;
        colorMap        = renderer->CreateTexture(LLGL::Texture2DDesc(LLGL::Format::RGBA8UNorm, resolution.width, resolution.height));
        glossMap        = renderer->CreateTexture(LLGL::Texture2DDesc(LLGL::Format::RGBA8UNorm, resolution.width, resolution.height));

        // Create empty blur pass maps (in quarter resolution)
        resolution.width  /= 4;
        resolution.height /= 4;
        glossMapBlurX   = renderer->CreateTexture(LLGL::Texture2DDesc(LLGL::Format::RGBA8UNorm, resolution.width, resolution.height));
        glossMapBlurY   = renderer->CreateTexture(LLGL::Texture2DDesc(LLGL::Format::RGBA8UNorm, resolution.width, resolution.height));
    }

    void CreateRenderTargets()
    {
        auto resolution = context->GetVideoMode().resolution;

        // Create render-target for scene rendering
        LLGL::RenderTargetDescriptor renderTargetDesc;
        {
            renderTargetDesc.resolution = resolution;
            renderTargetDesc.attachments =
            {
                LLGL::AttachmentDescriptor { LLGL::AttachmentType::Depth },
                LLGL::AttachmentDescriptor { LLGL::AttachmentType::Color, colorMap },
                LLGL::AttachmentDescriptor { LLGL::AttachmentType::Color, glossMap },
            };
            renderTargetDesc.multiSampling = LLGL::MultiSamplingDescriptor(8);
        }
        renderTargetScene = renderer->CreateRenderTarget(renderTargetDesc);

        // Create render-target for horizontal blur pass (no depth buffer needed)
        resolution.width    /= 4;
        resolution.height   /= 4;

        LLGL::RenderTargetDescriptor renderTargetBlurXDesc;
        {
            renderTargetBlurXDesc.resolution = resolution;
            renderTargetBlurXDesc.attachments =
            {
                LLGL::AttachmentDescriptor { LLGL::AttachmentType::Color, glossMapBlurX }
            };
        }
        renderTargetBlurX = renderer->CreateRenderTarget(renderTargetBlurXDesc);

        // Create render-target for vertical blur pass (no depth buffer needed)
        LLGL::RenderTargetDescriptor renderTargetBlurYDesc;
        {
            renderTargetBlurYDesc.resolution = resolution;
            renderTargetBlurYDesc.attachments =
            {
                LLGL::AttachmentDescriptor { LLGL::AttachmentType::Color, glossMapBlurY }
            };
        }
        renderTargetBlurY = renderer->CreateRenderTarget(renderTargetBlurYDesc);
    }

    void UpdateScreenSize()
    {
        // Release previous textures
        renderer->Release(*renderTargetScene);
        renderer->Release(*renderTargetBlurX);
        renderer->Release(*renderTargetBlurY);
        renderer->Release(*colorMap);
        renderer->Release(*glossMap);
        renderer->Release(*glossMapBlurX);
        renderer->Release(*glossMapBlurY);

        // Recreate objects
        CreateTextures();
        CreateRenderTargets();
    }

private:

    void SetSceneSettingsInnerModel(float rotation)
    {
        // Transform scene mesh
        sceneSettings.wMatrix.LoadIdentity();
        Gs::Translate(sceneSettings.wMatrix, { 0, 0, 5 });

        // Rotate model around the (1, 1, 1) axis
        Gs::RotateFree(sceneSettings.wMatrix, Gs::Vector3f(1).Normalized(), rotation);
        Gs::Scale(sceneSettings.wMatrix, Gs::Vector3f(0.5f));

        // Set colors and matrix
        sceneSettings.diffuse       = glowColor;
        sceneSettings.glossiness    = glowColor;
        sceneSettings.wvpMatrix     = projection * sceneSettings.wMatrix;

        // Update constant buffer for scene settings
        UpdateBuffer(constantBufferScene, sceneSettings);
    }

    void SetSceneSettingsOuterModel(float deltaPitch, float deltaYaw)
    {
        // Rotate model around X and Y axes
        static Gs::Matrix4f rotation;

        Gs::Matrix4f deltaRotation;
        Gs::RotateFree(deltaRotation, { 1, 0, 0 }, deltaPitch);
        Gs::RotateFree(deltaRotation, { 0, 1, 0 }, deltaYaw);
        rotation = deltaRotation * rotation;

        // Transform scene mesh
        sceneSettings.wMatrix.LoadIdentity();
        Gs::Translate(sceneSettings.wMatrix, { 0, 0, 5 });
        sceneSettings.wMatrix *= rotation;

        // Set colors and matrix
        sceneSettings.diffuse       = { 0.6f, 0.6f, 0.6f, 1.0f };
        sceneSettings.glossiness    = { 0, 0, 0, 0 };
        sceneSettings.wvpMatrix     = projection * sceneSettings.wMatrix;

        // Update constant buffer for scene settings
        UpdateBuffer(constantBufferScene, sceneSettings);
    }

    void SetBlurSettings(const Gs::Vector2f& blurShift)
    {
        // Update constant buffer for blur pass
        blurSettings.blurShift = blurShift;
        UpdateBuffer(constantBufferBlur, blurSettings);
    }

    void OnDrawFrame() override
    {
        static const auto shaderStages = LLGL::StageFlags::VertexStage | LLGL::StageFlags::FragmentStage;

        // Update rotation of inner model
        static float innerModelRotation;
        innerModelRotation += 0.01f;

        // Update rotation of outer model
        Gs::Vector2f mouseMotion
        {
            static_cast<float>(input->GetMouseMotion().x),
            static_cast<float>(input->GetMouseMotion().y),
        };

        Gs::Vector2f outerModelDeltaRotation;
        if (input->KeyPressed(LLGL::Key::LButton))
            outerModelDeltaRotation = mouseMotion*0.005f;

        // Update effect intensity animation
        if (input->KeyPressed(LLGL::Key::RButton))
        {
            float delta = mouseMotion.x*0.01f;
            sceneSettings.intensity = std::max(0.0f, std::min(sceneSettings.intensity + delta, 3.0f));
            std::cout << "glow intensity: " << static_cast<int>(sceneSettings.intensity*100.0f) << "%    \r";
            std::flush(std::cout);
        }

        // Check if screen size has changed (this could also be done with an event listener)
        static LLGL::Extent2D screenSize { 800, 600 };

        if (screenSize != context->GetVideoMode().resolution)
        {
            screenSize = context->GetVideoMode().resolution;
            UpdateScreenSize();
        }

        // Initialize viewports
        const LLGL::Viewport viewportFull{ { 0, 0 }, screenSize };
        const LLGL::Viewport viewportQuarter{ { 0, 0 }, { screenSize.width / 4, screenSize.height/ 4 } };

        commandQueue->Begin(*commands);
        {
            // Set common buffers and sampler states
            commandsExt->SetConstantBuffer(*constantBufferScene, 0, shaderStages);
            commandsExt->SetConstantBuffer(*constantBufferBlur, 1, LLGL::StageFlags::FragmentStage);

            commandsExt->SetSampler(*colorMapSampler, 0, LLGL::StageFlags::FragmentStage);
            commandsExt->SetSampler(*glossMapSampler, 1, LLGL::StageFlags::FragmentStage);

            // Set graphics pipeline and vertex buffer for scene rendering
            commands->SetVertexBuffer(*vertexBufferScene);

            // Draw scene into multi-render-target (1st target: color, 2nd target: glossiness)
            commands->BeginRenderPass(*renderTargetScene);
            {
                // Clear individual buffers in render target (color, glossiness, depth)
                LLGL::AttachmentClear clearCmds[3] =
                {
                    LLGL::AttachmentClear { defaultClearColor, 0 },
                    LLGL::AttachmentClear { LLGL::ColorRGBAf { 0, 0, 0, 0 }, 1 },
                    LLGL::AttachmentClear { 1.0f }
                };
                commands->ClearAttachments(3, clearCmds);

                commands->SetGraphicsPipeline(*pipelineScene);

                // Draw outer scene model
                SetSceneSettingsOuterModel(outerModelDeltaRotation.y, outerModelDeltaRotation.x);
                commands->Draw(numSceneVertices, 0);

                // Draw inner scene model
                SetSceneSettingsInnerModel(innerModelRotation);
                commands->Draw(numSceneVertices, 0);
            }
            commands->EndRenderPass();

            // Set graphics pipeline and vertex buffer for post-processors
            commands->SetVertexBuffer(*vertexBufferNull);

            // Draw horizontal blur pass
            commands->BeginRenderPass(*renderTargetBlurX);
            {
                // Draw blur passes in quarter resolution
                commands->SetViewport(viewportQuarter);
                commands->SetGraphicsPipeline(*pipelineBlur);

                // Set gloss map from scene rendering
                commandsExt->SetTexture(*glossMap, 1, LLGL::StageFlags::FragmentStage);

                // Draw fullscreen triangle (triangle is spanned in the vertex shader)
                SetBlurSettings({ 4.0f / static_cast<float>(screenSize.width), 0.0f });
                commands->Draw(3, 0);
            }
            commands->EndRenderPass();

            // Draw vertical blur pass
            commands->BeginRenderPass(*renderTargetBlurY);
            {
                // Set gloss map from previous blur pass (Blur X)
                commandsExt->SetTexture(*glossMapBlurX, 1, LLGL::StageFlags::FragmentStage);

                // Draw fullscreen triangle (triangle is spanned in the vertex shader)
                SetBlurSettings({ 0.0f, 4.0f / static_cast<float>(screenSize.height) });
                commands->Draw(3, 0);
            }
            commands->EndRenderPass();

            // Draw final post-processing pass
            commands->BeginRenderPass(*context);
            {
                // Set viewport back to full resolution
                commands->SetViewport(viewportFull);
                commands->SetGraphicsPipeline(*pipelineFinal);

                // Set color map and gloss map from previous blur pass (Blur Y)
                commandsExt->SetTexture(*colorMap, 0, LLGL::StageFlags::FragmentStage);
                commandsExt->SetTexture(*glossMapBlurY, 1, LLGL::StageFlags::FragmentStage);

                // Draw fullscreen triangle (triangle is spanned in the vertex shader)
                commands->Draw(3, 0);
            }
            commands->EndRenderPass();
        }
        commandQueue->End(*commands);

        // Present result on the screen
        context->Present();
    }

};

LLGL_IMPLEMENT_TUTORIAL(Tutorial11);



