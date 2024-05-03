#include "PathTracingRenderPass.h"
#include "PathTracingApplication.h"

#include "Asset/ShaderLoader.h"
#include "OpenImageDenoise/oidn.hpp"
#include "Renderer/Renderer.h"
#include "Shader/Material.h"
#include "Shader/Shader.h"
#include "Texture/FramebufferObject.h"
#include "Texture/Texture2DObject.h"
#include "Geometry/ShaderStorageBufferObject.h"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <span>
#include <chrono>
#include "Utils/Timer.h"
#include "PathTracingRenderer.h"

PathTracingRenderPass::PathTracingRenderPass(int width, int height, PathTracingRenderer* pathTracingRenderer, PathTracingApplication* pathTracingApplication, std::shared_ptr<const FramebufferObject> framebuffer)
    : RenderPass(framebuffer), m_width(width), m_height(height), m_pathTracingRenderer(pathTracingRenderer), m_pathTracingApplication(pathTracingApplication)
{
    InitializeTextures();

    m_denoiserRadianceInputPtr = new glm::vec3[width * height];
    m_denoiserPrimaryAlbedoInputPtr = new glm::vec3[width * height];
    m_denoiserPrimaryNormalInputPtr = new glm::vec3[width * height];
    m_denoiserDenoisedRadianceOutputPtr = new glm::vec3[width * height];
}

PathTracingRenderPass::~PathTracingRenderPass()
{
    delete[] m_denoiserRadianceInputPtr;
    delete[] m_denoiserPrimaryAlbedoInputPtr;
    delete[] m_denoiserPrimaryNormalInputPtr;
    delete[] m_denoiserDenoisedRadianceOutputPtr;
}

void PathTracingRenderPass::Render()
{
    // Path Tracing render
    if (m_pathTracingApplication->GetShouldPathTrace())
    {
        assert(m_pathTracingApplication->GetPathTracingMaterial());

        // Mark all as resident
        for (const GLuint64& handle : m_pathTracingRenderer->GetBindlessHandles())
        {
            glMakeTextureHandleResidentARB(handle);
        }
        
        // Bind SSBO buffers
        m_pathTracingRenderer->GetSsboEnvironment()->Bind();
        m_pathTracingRenderer->GetSsboMaterials()->Bind();
        m_pathTracingRenderer->GetSsboBvhNodes()->Bind();
        m_pathTracingRenderer->GetSsboBvhPrimitives()->Bind();

        // Use material
        m_pathTracingRenderer->GetPathTracingMaterial()->Use();

        // Uniform image output
        m_pathTracingRadianceTexture->BindImageTexture(0, 0, GL_FALSE, 0, GL_READ_WRITE, TextureObject::InternalFormatRGBA32F);
        m_pathTracingPrimaryAlbedoTexture->BindImageTexture(1, 0, GL_FALSE, 0, GL_READ_WRITE, TextureObject::InternalFormatRGBA32F);
        m_pathTracingPrimaryNormalTexture->BindImageTexture(2, 0, GL_FALSE, 0, GL_READ_WRITE, TextureObject::InternalFormatRGBA32F);

        const int threadSize = 16;
        const int numGroupsX = static_cast<uint32_t>(std::ceil(m_width / threadSize));
        const int numGroupsY = static_cast<uint32_t>(std::ceil(m_height / threadSize));
        glDispatchCompute(numGroupsX, numGroupsY, 1);

        // Make sure writing to image has finished before read
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        m_outputTexture = m_pathTracingRadianceTexture;

        // Mark all as non-resident - can be skipped if you know the same textures
        // will all be used for the next frame
        for (const GLuint64& handle : m_pathTracingRenderer->GetBindlessHandles())
        {
            glMakeTextureHandleNonResidentARB(handle);
        }
    }
    
    // Denoise
    if (m_pathTracingApplication->GetShouldDenoise() && !(m_pathTracingApplication->GetDenoised()))
    {
        // Extract texture to memory

        m_pathTracingRadianceTexture->Bind();
        m_pathTracingRadianceTexture->GetTextureData(0, TextureObject::Format::FormatRGB, Data::Type::Float, m_denoiserRadianceInputPtr);
        m_pathTracingRadianceTexture->Unbind();

        m_pathTracingPrimaryAlbedoTexture->Bind();
        m_pathTracingPrimaryAlbedoTexture->GetTextureData(0, TextureObject::Format::FormatRGB, Data::Type::Float, m_denoiserPrimaryAlbedoInputPtr);
        m_pathTracingPrimaryAlbedoTexture->Unbind();

        m_pathTracingPrimaryNormalTexture->Bind();
        m_pathTracingPrimaryNormalTexture->GetTextureData(0, TextureObject::Format::FormatRGB, Data::Type::Float, m_denoiserPrimaryNormalInputPtr);
        m_pathTracingPrimaryNormalTexture->Unbind();

        // https://github.com/OpenImageDenoise/oidn#c11-api-example

        // Create an Intel Open Image Denoise device
        static oidn::DeviceRef device = oidn::newDevice(); // CPU version is just fine here
        static bool init = false;
        if (!init)
        {
            device.commit();
            init = true;
        }

        // Create a denoising filter
        oidn::FilterRef filter = device.newFilter("RT"); // generic ray tracing filter
        filter.setImage("color", m_denoiserRadianceInputPtr, oidn::Format::Float3, m_width, m_height, 0, 0, 0);
        filter.setImage("albedo", m_denoiserPrimaryAlbedoInputPtr, oidn::Format::Float3, m_width, m_height, 0, 0, 0);
        filter.setImage("normal", m_denoiserPrimaryNormalInputPtr, oidn::Format::Float3, m_width, m_height, 0, 0, 0);
        filter.setImage("output", m_denoiserDenoisedRadianceOutputPtr, oidn::Format::Float3, m_width, m_height, 0, 0, 0);
        filter.set("hdr", true);
        filter.commit();

        // Denoise progress callback
        m_pathTracingApplication->SetDenoiseProgress(0.0f);
        filter.setProgressMonitorFunction([](void* userPtr, double n) { 
            return ((PathTracingRenderPass*)userPtr)->DenoiserCallback(userPtr, n); 
        }, this);

        // Filter the image
        filter.execute();

        // Check for errors
        const char* errorMessage;
        auto error = device.getError(errorMessage);
        if (error != oidn::Error::None && error != oidn::Error::Cancelled)
        {
            std::cout << "Error: " << errorMessage << std::endl;
        }
        else
        {
            // m_denoiserDenoisedRadianceOutputPtr to std::span
            std::byte* bytePtr = reinterpret_cast<std::byte*>(m_denoiserDenoisedRadianceOutputPtr);
            size_t sizeInBytes = (size_t)m_width * (size_t)m_height * 3 * sizeof(float); // RGB
            std::span<const std::byte> data(bytePtr, sizeInBytes);

            // Insert data
            m_pathTracingDenoisedRadianceTexture->Bind();
            m_pathTracingDenoisedRadianceTexture->SetImage(0, m_width, m_height, TextureObject::FormatRGB, TextureObject::InternalFormat::InternalFormatRGB32F, data, Data::Type::Float);
            m_pathTracingDenoisedRadianceTexture->Unbind();

            // Assign output texture
            m_outputTexture = m_pathTracingDenoisedRadianceTexture;
        }

        // Even though we can get error from denoiser, let us tell that this has been denoised
        m_pathTracingApplication->SetDenoised(true);
    }

    // Copy render
    {
        assert(m_pathTracingRenderer->GetPathTracingCopyMaterial());

        m_pathTracingRenderer->GetPathTracingCopyMaterial()->Use();

        m_pathTracingRenderer->GetPathTracingCopyMaterial()->SetUniformValue("SourceTexture", m_outputTexture);

        Renderer& renderer = GetRenderer();
        const Mesh* mesh = &renderer.GetFullscreenMesh();
        mesh->DrawSubmesh(0);
    }
}

void PathTracingRenderPass::InitializeTextures()
{
    // Path Tracing Radiance Texture
    m_pathTracingRadianceTexture = std::make_shared<Texture2DObject>();
    m_pathTracingRadianceTexture->Bind();
    m_pathTracingRadianceTexture->SetImage(0, m_width, m_height, TextureObject::FormatRGBA, TextureObject::InternalFormat::InternalFormatRGBA32F);
    m_pathTracingRadianceTexture->SetParameter(TextureObject::ParameterEnum::MinFilter, GL_NEAREST);
    m_pathTracingRadianceTexture->SetParameter(TextureObject::ParameterEnum::MagFilter, GL_NEAREST);
    Texture2DObject::Unbind();

    // Path Tracing Primary Albedo Texture
    m_pathTracingPrimaryAlbedoTexture = std::make_shared<Texture2DObject>();
    m_pathTracingPrimaryAlbedoTexture->Bind();
    m_pathTracingPrimaryAlbedoTexture->SetImage(0, m_width, m_height, TextureObject::FormatRGBA, TextureObject::InternalFormat::InternalFormatRGBA32F);
    m_pathTracingPrimaryAlbedoTexture->SetParameter(TextureObject::ParameterEnum::MinFilter, GL_NEAREST);
    m_pathTracingPrimaryAlbedoTexture->SetParameter(TextureObject::ParameterEnum::MagFilter, GL_NEAREST);
    Texture2DObject::Unbind();

    // Path Tracing Primary Normal Texture
    m_pathTracingPrimaryNormalTexture = std::make_shared<Texture2DObject>();
    m_pathTracingPrimaryNormalTexture->Bind();
    m_pathTracingPrimaryNormalTexture->SetImage(0, m_width, m_height, TextureObject::FormatRGBA, TextureObject::InternalFormat::InternalFormatRGBA32F);
    m_pathTracingPrimaryNormalTexture->SetParameter(TextureObject::ParameterEnum::MinFilter, GL_NEAREST);
    m_pathTracingPrimaryNormalTexture->SetParameter(TextureObject::ParameterEnum::MagFilter, GL_NEAREST);
    Texture2DObject::Unbind();

    // Denoised Path Tracing Radiance Texture
    m_pathTracingDenoisedRadianceTexture = std::make_shared<Texture2DObject>();
    m_pathTracingDenoisedRadianceTexture->Bind();
    m_pathTracingDenoisedRadianceTexture->SetImage(0, m_width, m_height, TextureObject::FormatRGB, TextureObject::InternalFormat::InternalFormatRGB32F);
    m_pathTracingDenoisedRadianceTexture->SetParameter(TextureObject::ParameterEnum::MinFilter, GL_NEAREST);
    m_pathTracingDenoisedRadianceTexture->SetParameter(TextureObject::ParameterEnum::MagFilter, GL_NEAREST);
    Texture2DObject::Unbind();
}

bool PathTracingRenderPass::DenoiserCallback(void* userPtr, double n)
{
    // TODO: GUI doesn't seem to update, even though we call it here...
    //m_pathTracingApplication.RenderGUI();

    m_pathTracingApplication->SetDenoiseProgress((float)n);
    return true;
}