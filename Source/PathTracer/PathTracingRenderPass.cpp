﻿#include "PathTracingRenderPass.h"
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
#include "BVH.h"
#include "Utils/Timer.h"

//#define DEBUG_VBO
//#define DEBUG_EBO

PathTracingRenderPass::PathTracingRenderPass(int width, int height, PathTracingApplication* pathTracingApplication, std::shared_ptr<Material> pathTracingMaterial, std::shared_ptr<const FramebufferObject> framebuffer)
    : RenderPass(framebuffer), m_width(width), m_height(height), m_pathTracingApplication(pathTracingApplication), m_pathTracingMaterial(pathTracingMaterial)
{
    InitializeTextures();
    InitializeBuffers();

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
        assert(m_pathTracingMaterial);

        // Mark all as resident
        for (const MaterialSave& materialSave : m_materialsSaved)
        {
            glMakeTextureHandleResidentARB(materialSave.emissionTextureHandle);
            glMakeTextureHandleResidentARB(materialSave.albedoTextureHandle);
            glMakeTextureHandleResidentARB(materialSave.normalTextureHandle);
            glMakeTextureHandleResidentARB(materialSave.specularTextureHandle);
            glMakeTextureHandleResidentARB(materialSave.specularColorTextureHandle);
            glMakeTextureHandleResidentARB(materialSave.metallicRoughnessTextureHandle);
            glMakeTextureHandleResidentARB(materialSave.sheenRoughnessTextureHandle);
            glMakeTextureHandleResidentARB(materialSave.sheenColorTextureHandle);
            glMakeTextureHandleResidentARB(materialSave.clearcoatTextureHandle);
            glMakeTextureHandleResidentARB(materialSave.clearcoatRoughnessTextureHandle);
            glMakeTextureHandleResidentARB(materialSave.transmissionTextureHandle);
        }
        glMakeTextureHandleResidentARB(m_hdriHandle);
        glMakeTextureHandleResidentARB(m_hdriCacheHandle);
        
        // Bind SSBO buffers
        m_ssboSettings->Bind();
        m_ssboEnvironment->Bind();
        m_ssboMaterials->Bind();
        m_ssboBvhNodes->Bind();
        m_ssboBvhPrimitives->Bind();

        // Use material
        m_pathTracingMaterial->Use();

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
        for (const MaterialSave& materialSave : m_materialsSaved)
        {
            glMakeTextureHandleNonResidentARB(materialSave.emissionTextureHandle);
            glMakeTextureHandleNonResidentARB(materialSave.albedoTextureHandle);
            glMakeTextureHandleNonResidentARB(materialSave.normalTextureHandle);
            glMakeTextureHandleNonResidentARB(materialSave.specularTextureHandle);
            glMakeTextureHandleNonResidentARB(materialSave.specularColorTextureHandle);
            glMakeTextureHandleNonResidentARB(materialSave.metallicRoughnessTextureHandle);
            glMakeTextureHandleNonResidentARB(materialSave.sheenRoughnessTextureHandle);
            glMakeTextureHandleNonResidentARB(materialSave.sheenColorTextureHandle);
            glMakeTextureHandleNonResidentARB(materialSave.clearcoatTextureHandle);
            glMakeTextureHandleNonResidentARB(materialSave.clearcoatRoughnessTextureHandle);
            glMakeTextureHandleNonResidentARB(materialSave.transmissionTextureHandle);
        }
        glMakeTextureHandleNonResidentARB(m_hdriHandle);
        glMakeTextureHandleNonResidentARB(m_hdriCacheHandle);
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
        assert(m_pathTracingApplication->GetCopyMaterial());

        m_pathTracingApplication->GetCopyMaterial()->Use();

        m_outputTexture->Bind();
        m_pathTracingApplication->GetCopyMaterial()->SetUniformValue("SouceTexture", m_outputTexture);

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

void PathTracingRenderPass::InitializeBuffers()
{
    // VBO and EBO data of all meshes
    std::vector<std::vector<VertexSave>> totalVertexData;
    std::vector<std::vector<unsigned int>> totalIndexData;

    // Go through each model from application
    for (Model model : m_pathTracingApplication->GetModels())
    {
        // Each model has one VBO, one EBO, one Material, per mesh, where there can be many meshes per model.

        Mesh& mesh = model.GetMesh();
        unsigned int submeshCount = mesh.GetSubmeshCount();

        for (unsigned int i = 0; i < submeshCount; i++)
        {
            // Fetch VBO
            {
                // Get VBO from mesh
                VertexBufferObject& vbo = mesh.GetVertexBuffer(i);
                vbo.Bind();

                // Get VBO size
                GLint vboSize;
                glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vboSize);

                // Debug VBO
                PrintVBOData(vbo, vboSize);

                // Extract data from VBO
                std::vector<GLubyte> vboData(vboSize);
                glGetBufferSubData(GL_ARRAY_BUFFER, 0, vboSize, vboData.data());

                // Convert GLubyte data to Vertex data
                std::vector<VertexSave> vertexData(vboSize / sizeof(VertexSave));
                const GLubyte* currentPtr = vboData.data();

                // Map vboData to vertexData of type 'Vertex'
                for (size_t j = 0; j < vboSize / sizeof(VertexSave); j++)
                {
                    VertexSave vertex { };

                    // Copy position data (assuming it's tightly packed)
                    memcpy(&vertex.pos, currentPtr, sizeof(glm::vec3));
                    currentPtr += sizeof(glm::vec3);

                    // Copy normal data (assuming it's tightly packed)
                    memcpy(&vertex.nor, currentPtr, sizeof(glm::vec3));
                    currentPtr += sizeof(glm::vec3);

                    // Copy UV data (assuming it's tightly packed)
                    memcpy(&vertex.uv, currentPtr, sizeof(glm::vec2));
                    currentPtr += sizeof(glm::vec2);

                    vertexData[j] = vertex;
                }

                // Add vertexData to totalVertexData
                totalVertexData.push_back(vertexData);

                vboData.clear();
                vertexData.clear();

                // Unbind VBO
                vbo.Unbind();
            }

            // Fetch EBO
            {
                // Get EBO
                ElementBufferObject& ebo = mesh.GetElementBuffer(i);
                ebo.Bind();

                // Get EBO size
                GLint eboSize;
                glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &eboSize);

                // Out index data
                std::vector<unsigned int> indexData;

                // Support for different EBO Types, based on ElementBufferObject::GetSmallestType
                // Add more if needed
                {
                    Drawcall drawcall = mesh.GetSubmesh(i).drawcall;
                    Data::Type EBOType = drawcall.GetEBOType();
                    int EBOTypeValue = (int)EBOType;

                    if (EBOType == Data::Type::UByte)
                    {
                        indexData = FetchAndConvertEBO<GLubyte>(ebo, eboSize, i, EBOTypeValue);
                    }
                    else if (EBOType == Data::Type::UShort)
                    {
                        indexData = FetchAndConvertEBO<GLushort>(ebo, eboSize, i, EBOTypeValue);
                    }
                    else if (EBOType == Data::Type::UInt)
                    {
                        indexData = FetchAndConvertEBO<GLuint>(ebo, eboSize, i, EBOTypeValue);
                    }
                    else
                    {
                        throw std::runtime_error("EBO Type not supported!");
                    }
                }

                // Add indexData to totalIndexData
                totalIndexData.push_back(indexData);

                // Unbind EBO here and not in EBOConvertAndBufferDataSSBO
                ebo.Unbind();
            }

            // Fetch Materials
            {
                // Current mesh material
                Material& material = model.GetMaterial(i);

                // Placeholder
                MaterialSave materialSave{ };

                // Emission Texture
                std::shared_ptr<Texture2DObject> emissionTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::EmissionTexture);
                if (emissionTexture) { materialSave.emissionTextureHandle = emissionTexture->GetBindlessTextureHandle(); }

                // Albedo Texture
                std::shared_ptr<Texture2DObject> albedoTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::AlbedoTexture);
                if (albedoTexture) { materialSave.albedoTextureHandle = albedoTexture->GetBindlessTextureHandle(); }

                // Normal Texture
                std::shared_ptr<Texture2DObject> normalTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::NormalTexture);
                if (normalTexture) { materialSave.normalTextureHandle = normalTexture->GetBindlessTextureHandle(); }

                // Specular Texture
                std::shared_ptr<Texture2DObject> specularTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::SpecularTexture);
                if (specularTexture) { materialSave.specularTextureHandle = specularTexture->GetBindlessTextureHandle(); }

                // Specular Color Texture
                std::shared_ptr<Texture2DObject> specularColorTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::SpecularColorTexture);
                if (specularColorTexture) { materialSave.specularColorTextureHandle = specularColorTexture->GetBindlessTextureHandle(); }

                // Metallic Roughness Texture
                std::shared_ptr<Texture2DObject> metallicRoughnessTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::MetallicRoughnessTexture);
                if (metallicRoughnessTexture) { materialSave.metallicRoughnessTextureHandle = metallicRoughnessTexture->GetBindlessTextureHandle(); }

                // Sheen Roughness Texture
                std::shared_ptr<Texture2DObject> sheenRoughnessTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::SheenRoughnessTexture);
                if (sheenRoughnessTexture) { materialSave.sheenRoughnessTextureHandle = sheenRoughnessTexture->GetBindlessTextureHandle(); }

                // Sheen Color Texture
                std::shared_ptr<Texture2DObject> sheenColorTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::SheenColorTexture);
                if (sheenColorTexture) { materialSave.sheenColorTextureHandle = sheenColorTexture->GetBindlessTextureHandle(); }

                // Clearcoat Texture
                std::shared_ptr<Texture2DObject> clearcoatTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::ClearcoatTexture);
                if (clearcoatTexture) { materialSave.clearcoatTextureHandle = clearcoatTexture->GetBindlessTextureHandle(); }

                // Clearcoat Roughness Texture
                std::shared_ptr<Texture2DObject> clearcoatRoughnessTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::ClearcoatRoughnessTexture);
                if (clearcoatRoughnessTexture) { materialSave.clearcoatRoughnessTextureHandle = clearcoatRoughnessTexture->GetBindlessTextureHandle(); }

                // Transmission Texture
                std::shared_ptr<Texture2DObject> transmissionTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::TransmissionTexture);
                if (transmissionTexture) { materialSave.transmissionTextureHandle = transmissionTexture->GetBindlessTextureHandle(); }

                // Get material attributes
                Material::MaterialAttributes materialAttributes = material.GetMaterialAttributes();

                // Translate attributes
                materialSave.emission = materialAttributes.emission;
                materialSave.albedo = materialAttributes.albedo;
                materialSave.specular = materialAttributes.specular;
                materialSave.specularTint = materialAttributes.specularTint;
                materialSave.metallic = materialAttributes.metallic;
                materialSave.roughness = materialAttributes.roughness;
                materialSave.subsurface = materialAttributes.subsurface;
                materialSave.anisotropy = materialAttributes.anisotropy;
                materialSave.sheenRoughness = materialAttributes.sheenRoughness;
                materialSave.sheenTint = materialAttributes.sheenTint;
                materialSave.clearcoat = materialAttributes.clearcoat;
                materialSave.clearcoatRoughness = materialAttributes.clearcoatRoughness;
                materialSave.refraction = materialAttributes.refraction;
                materialSave.transmission = materialAttributes.transmission;

                // Push
                m_materialsSaved.push_back(materialSave);
            }
        }
    }

    // Settings allocation
    {
        // Create SSBO for settings
        m_ssboSettings = std::make_shared<ShaderStorageBufferObject>();
        m_ssboSettings->Bind();

        // Binding index
        glBindBufferBase(m_ssboSettings->GetTarget(), 0, m_ssboSettings->GetHandle()); // Binding index: 0

        // Parse settings
        SettingsAlign settingsAlign{ };
        settingsAlign.debugValueA = m_pathTracingApplication->GetDebugValueA();
        settingsAlign.debugValueB = m_pathTracingApplication->GetDebugValueB();

        // Convert to span
        std::span<SettingsAlign> span = std::span(&settingsAlign, 1);

        // Allocate
        m_ssboSettings->AllocateData(span);
        m_ssboSettings->Unbind();
    }

    // Environment allocation
    {
        // Create SSBO for environment
        m_ssboEnvironment = std::make_shared<ShaderStorageBufferObject>();
        m_ssboEnvironment->Bind();

        // Binding index
        glBindBufferBase(m_ssboEnvironment->GetTarget(), 1, m_ssboEnvironment->GetHandle()); // Binding index: 1

        // Get HDRI
        std::shared_ptr<Texture2DObject> hdri = m_pathTracingApplication->GetHdri();

        // Start timer
        Timer timer("HDRI Cache");

        // Calculate and save HDRI Cache
        int hdriWidth, hdriHeight;
        m_hdriCache = CalculateHdriCache(hdri, hdriWidth, hdriHeight);

        // End time point in milliseconds and print
        timer.Stop();
        timer.Print();

        // Get HDRI handle
        const GLuint64 hdriHandle = hdri->GetBindlessTextureHandle();
        if (hdriHandle == 0) { throw new std::runtime_error("Error! HDRI Handle returned null"); }
        m_hdriHandle = hdriHandle;

        // Get HDRI Cache handle
        const GLuint64 hdriCacheHandle = m_hdriCache->GetBindlessTextureHandle();
        if (hdriCacheHandle == 0) { throw new std::runtime_error("Error! HDRI Cache Handle returned null"); }
        m_hdriCacheHandle = hdriCacheHandle;

        // Environment
        EnvironmentAlign environment { };
        environment.hdriHandle = hdriHandle;
        environment.hdriCacheHandle = hdriCacheHandle;
        environment.hdriDimensions = glm::vec2(float(hdriWidth), float(hdriHeight));

        // Convert to span
        std::span<EnvironmentAlign> span = std::span(&environment, 1);

        // Allocate
        m_ssboEnvironment->AllocateData(span);
        m_ssboEnvironment->Unbind();
    }

    // Material allocation
    {
        // Create SSBO for materials
        m_ssboMaterials = std::make_shared<ShaderStorageBufferObject>();
        m_ssboMaterials->Bind();

        // Binding index
        glBindBufferBase(m_ssboMaterials->GetTarget(), 2, m_ssboMaterials->GetHandle()); // Binding index: 2

        // Convert to MaterialAlign for GPU consumption

        std::vector<MaterialAlign> materialsAlign;
        for (const MaterialSave& materialSave : m_materialsSaved)
        {
            MaterialAlign materialAlign{ };

            // Texture handles
            materialAlign.emissionTextureHandle = materialSave.emissionTextureHandle;
            materialAlign.albedoTextureHandle = materialSave.albedoTextureHandle;
            materialAlign.normalTextureHandle = materialSave.normalTextureHandle;
            materialAlign.specularTextureHandle = materialSave.specularTextureHandle;
            materialAlign.specularColorTextureHandle = materialSave.specularColorTextureHandle;
            materialAlign.metallicRoughnessTextureHandle = materialSave.metallicRoughnessTextureHandle;
            materialAlign.sheenRoughnessTextureHandle = materialSave.sheenRoughnessTextureHandle;
            materialAlign.sheenColorTextureHandle = materialSave.sheenColorTextureHandle;
            materialAlign.clearcoatTextureHandle = materialSave.clearcoatTextureHandle;
            materialAlign.clearcoatRoughnessTextureHandle = materialSave.clearcoatRoughnessTextureHandle;
            materialAlign.transmissionTextureHandle = materialSave.transmissionTextureHandle;

            // Attributes
            materialAlign.emission = materialSave.emission;
            materialAlign.albedo = materialSave.albedo;
            materialAlign.specular = materialSave.specular;
            materialAlign.specularTint = materialSave.specularTint;
            materialAlign.metallic = materialSave.metallic;
            materialAlign.roughness = materialSave.roughness;
            materialAlign.subsurface = materialSave.subsurface;
            materialAlign.anisotropy = materialSave.anisotropy;
            materialAlign.sheenRoughness = materialSave.sheenRoughness;
            materialAlign.sheenTint = materialSave.sheenTint;
            materialAlign.clearcoat = materialSave.clearcoat;
            materialAlign.clearcoatRoughness = materialSave.clearcoatRoughness;
            materialAlign.refraction = materialSave.refraction;
            materialAlign.transmission = materialSave.transmission;

            // Push
            materialsAlign.push_back(materialAlign);
        }

        // Convert to span
        std::span<MaterialAlign> span = std::span(materialsAlign);

        // Allocate
        m_ssboMaterials->AllocateData(span);

        m_ssboMaterials->Unbind();
    }

    // BVH allocation
    {
        // Convert corrosponding vertices to a format that BVH can use to build BVH nodes

        std::vector<BVH::BvhPrimitive> bvhPrimitives;
        for (unsigned int meshIndex = 0; meshIndex < totalIndexData.size(); meshIndex++) 
        {
            const std::vector<VertexSave>& vertexData = totalVertexData[meshIndex];
            const std::vector<unsigned int>& indexData = totalIndexData[meshIndex];

            for (size_t i = 0; i < indexData.size(); i += 3)
            {
                unsigned int index1 = indexData[i + 0];
                unsigned int index2 = indexData[i + 1];
                unsigned int index3 = indexData[i + 2];

                BVH::BvhPrimitive primitive { };

                primitive.posA = vertexData[index1].pos;
                primitive.posB = vertexData[index2].pos;
                primitive.posC = vertexData[index3].pos;

                primitive.norA = vertexData[index1].nor;
                primitive.norB = vertexData[index2].nor;
                primitive.norC = vertexData[index3].nor;

                primitive.uvA = vertexData[index1].uv;
                primitive.uvB = vertexData[index2].uv;
                primitive.uvC = vertexData[index3].uv;

                primitive.meshIndex = meshIndex;

                // Push
                bvhPrimitives.push_back(primitive);
            }
        }

        // Create SSBO for BVH nodes
        {
            // Create SSBO for BVH nodes
            m_ssboBvhNodes = std::make_shared<ShaderStorageBufferObject>();
            m_ssboBvhNodes->Bind();

            // Binding index
            glBindBufferBase(m_ssboBvhNodes->GetTarget(), 3, m_ssboBvhNodes->GetHandle()); // Binding index: 3

            // Initialize BVH nodes
            BVH::BvhNode initNode{ };
            std::vector<BVH::BvhNode> bvhNodes{ initNode };

            // Start timer
            Timer timer("BVH Calculation");

            // Calculate BVH
            //BVH::BuildBvh(bvhPrimitives, bvhNodes, 0, (int)bvhPrimitives.size() - 1, 4);
            BVH::BuildBvhWithSah(bvhPrimitives, bvhNodes, 0, (int)bvhPrimitives.size() - 1, 4);

            // End time point in milliseconds and print
            timer.Stop();
            timer.Print();

            // Align BVH nodes
            std::vector<BVH::BvhNodeAlign> bvhNodesAligned;
            for (const BVH::BvhNode& node : bvhNodes)
            {
                BVH::BvhNodeAlign nodeAligned { };

                nodeAligned.left = node.left;
                nodeAligned.right = node.right;
                nodeAligned.n = node.n;
                nodeAligned.index = node.index;
                nodeAligned.AA = node.AA;
                nodeAligned.BB = node.BB;

                // Add
                bvhNodesAligned.push_back(nodeAligned);
            }

            // Convert to span
            std::span<BVH::BvhNodeAlign> span = std::span(bvhNodesAligned);

            // Allocate
            m_ssboBvhNodes->AllocateData(span);
            m_ssboBvhNodes->Unbind();
        }

        // Create SSBO for BVH primitives
        {
            // Create SSBO for BVH primitives
            m_ssboBvhPrimitives = std::make_shared<ShaderStorageBufferObject>();
            m_ssboBvhPrimitives->Bind();

            // Binding index
            glBindBufferBase(m_ssboBvhPrimitives->GetTarget(), 4, m_ssboBvhPrimitives->GetHandle()); // Binding index: 4

            // Align BVH primitives
            std::vector<BVH::BvhPrimitiveAlign> bvhPrimitivesAligned;
            for (const BVH::BvhPrimitive& primitive : bvhPrimitives)
            {
                glm::vec3 posA = primitive.posA;
                glm::vec3 posB = primitive.posB;
                glm::vec3 posC = primitive.posC;

                glm::vec3 norA = primitive.norA;
                glm::vec3 norB = primitive.norB;
                glm::vec3 norC = primitive.norC;

                glm::vec2 uvA = primitive.uvA;
                glm::vec2 uvB = primitive.uvB;
                glm::vec2 uvC = primitive.uvC;
                
                BVH::BvhPrimitiveAlign primitiveAligned{ };

                primitiveAligned.posAuvX = glm::vec4(posA, uvA.x);
                primitiveAligned.norAuvY = glm::vec4(norA, uvA.y);

                primitiveAligned.posBuvX = glm::vec4(posB, uvB.x);
                primitiveAligned.norBuvY = glm::vec4(norB, uvB.y);

                primitiveAligned.posCuvX = glm::vec4(posC, uvC.x);
                primitiveAligned.norCuvY = glm::vec4(norC, uvC.y);

                primitiveAligned.meshIndex = primitive.meshIndex;

                // Push
                bvhPrimitivesAligned.push_back(primitiveAligned);
            }

            // Convert to span
            std::span<BVH::BvhPrimitiveAlign> span = std::span(bvhPrimitivesAligned);

            // Allocate
            m_ssboBvhPrimitives->AllocateData(span);
            m_ssboBvhPrimitives->Unbind();
        }
    }
}

bool PathTracingRenderPass::DenoiserCallback(void* userPtr, double n)
{
    // TODO: GUI doesn't seem to update, even though we call it here...
    //m_pathTracingApplication.RenderGUI();

    m_pathTracingApplication->SetDenoiseProgress((float)n);
    return true;
}

void PathTracingRenderPass::PrintVBOData(VertexBufferObject& vbo, GLint vboSize)
{
#ifdef DEBUG_VBO
    vbo.Bind();

    // Map the buffer to CPU memory
    GLvoid* bufferData = glMapBuffer(GL_ARRAY_BUFFER, GL_READ_ONLY);

    if (bufferData != nullptr)
    {
        // Assuming the data in the VBO is floats
        GLfloat* data = static_cast<GLfloat*>(bufferData);

        // Output the contents of the VBO to the console
        for (int i = 0; i < vboSize / sizeof(GLfloat); i += 3)
        {
            printf("Vertex %d: (%f, %f, %f)\n", i / 3, data[i], data[i + 1], data[i + 2]);
        }

        // Unmap the buffer
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    else
    {
        // Error handling if mapping fails
        printf("Failed to map buffer data to CPU memory\n");
    }
#endif // DEBUG_VBO
}

template<typename T>
void PathTracingRenderPass::PrintEBOData(ElementBufferObject& ebo, GLint eboSize)
{
#ifdef DEBUG_EBO
    ebo.Bind();

    // Map the buffer to CPU memory
    GLvoid* bufferData = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_READ_ONLY);

    if (bufferData != nullptr)
    {
        // Assuming the data in the EBO is ushorts
        T* data = static_cast<T*>(bufferData);

        // Output the contents of the EBO to the console
        for (int i = 0; i < eboSize / sizeof(T); i++)
        {
            printf("Index %d: (%d)\n", i, data[i]);
        }

        // Unmap the buffer
        glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
    }
    else
    {
        // Error handling if mapping fails
        printf("Failed to map buffer data to CPU memory\n");
    }
#endif // DEBUG_EBO
}

template<typename T>
std::vector<unsigned int> PathTracingRenderPass::FetchAndConvertEBO(ElementBufferObject& ebo, GLint eboSize, int subMeshIndex, int EBOTypeValue)
{
    // Bind EBO
    ebo.Bind();

    // Extract data from EBO
    T* eboData = new T[eboSize / sizeof(T)];
    glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)eboSize, eboData);

#ifdef DEBUG_EBO
    // Output the contents of the EBO to the console
    for (int i = 0; i < eboSize / sizeof(T); i++)
    {
        printf("Submesh %d: Type %d: Index %d: (%d)\n", subMeshIndex, EBOTypeValue, i, eboData[i]);
    }
#endif // DEBUG_EBO

    // Convert data to unsigned int data based on T
    std::vector<unsigned int> uintData(eboSize / sizeof(T));
    std::transform(eboData, eboData + eboSize / sizeof(T), uintData.begin(),
        [](const T& value) { return static_cast<unsigned int>(value); });

    // Cleanup before return
    delete[] eboData;

    return uintData;
};

template<typename T>
std::vector<T> PathTracingRenderPass::FlattenVector(const std::vector<std::vector<T>>& nestedVector)
{
    // Calculate the total size needed for the flattened vector
    size_t totalSize = std::accumulate(nestedVector.begin(), nestedVector.end(), size_t(0),
        [](size_t acc, const std::vector<T>& innerVec) {
            return acc + innerVec.size();
        });

    // Create the flattened vector with the appropriate size
    std::vector<T> flattenedVector;
    flattenedVector.reserve(totalSize);

    // Flatten the nested vector into the flattened vector
    for (const auto& innerVec : nestedVector)
    {
        flattenedVector.insert(flattenedVector.end(), innerVec.begin(), innerVec.end());
    }

    return flattenedVector;
}

std::shared_ptr<Texture2DObject> PathTracingRenderPass::CalculateHdriCache(std::shared_ptr<Texture2DObject> hdri, int& width, int& height)
{
    // Bind texture
    hdri->Bind();

    // Get the internal format of the texture
    int internalFormat;
    hdri->GetParameter(0, TextureObject::ParameterInt::InternalFormat, internalFormat);

    // Check if the texture is RGB and HDR
    // The following code assumes 3 components of RGB...
    if (internalFormat != GL_RGB32F)
    {
        throw new std::runtime_error("The HDRI does not follow the RGB32F format...");
    }

    // Get texture dimensions
    hdri->GetParameter(0, TextureObject::ParameterInt::Width, width);
    hdri->GetParameter(0, TextureObject::ParameterInt::Height, height);

    // Retrieve the HDR texture data
    std::vector<float> textureData(width * height * 3);
    hdri->GetTextureData(0, TextureObject::Format::FormatRGB, Data::Type::Float, textureData.data());
    
    // We're done retrieving information
    hdri->Unbind();

    float lumSum = 0.0f;

    // Initialize a probability density function (pdf) of height h and width w, and calculate the total brightness
    std::vector<std::vector<float>> pdf(height);
    for (auto& line : pdf) line.resize(width);
    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {
            int index = 3 * (i * width + j);
            float R = textureData[index + 0];
            float G = textureData[index + 1];
            float B = textureData[index + 2];
            float lum = 0.212671f * R + 0.715160f * G + 0.072169f * B;
            pdf[i][j] = lum;
            lumSum += lum;
        }
    }

    // Probability density normalization
    for (int i = 0; i < height; i++)
        for (int j = 0; j < width; j++)
            pdf[i][j] /= lumSum;

    // Accumulate each column to get the marginal probability density for x
    std::vector<float> pdf_x_margin;
    pdf_x_margin.resize(width);
    for (int j = 0; j < width; j++)
        for (int i = 0; i < height; i++)
            pdf_x_margin[j] += pdf[i][j];

    // Calculate the marginal distribution function for x
    std::vector<float> cdf_x_margin = pdf_x_margin;
    for (int i = 1; i < width; i++)
        cdf_x_margin[i] += cdf_x_margin[i - 1];

    // Calculate the conditional probability density function of y fixen X = x
    std::vector<std::vector<float>> pdf_y_condiciton = pdf;
    for (int j = 0; j < width; j++)
        for (int i = 0; i < height; i++)
            pdf_y_condiciton[i][j] /= pdf_x_margin[j];

    // Calculate the conditional probability distribution function y given X = x
    std::vector<std::vector<float>> cdf_y_condiciton = pdf_y_condiciton;
    for (int j = 0; j < width; j++)
        for (int i = 1; i < height; i++)
            cdf_y_condiciton[i][j] += cdf_y_condiciton[i - 1][j];

    // Transpose cdf_y_condiciton to store by columns
    // cdf_y_condiciton[i] represetns the conditional probability distribution function for y given X = i
    std::vector<std::vector<float>> temp = cdf_y_condiciton;
    cdf_y_condiciton = std::vector<std::vector<float>>(width);
    for (auto& line : cdf_y_condiciton) line.resize(height);
    for (int j = 0; j < width; j++)
        for (int i = 0; i < height; i++)
            cdf_y_condiciton[j][i] = temp[i][j];

    // Exhaustively compute samples for xy based on xi_1 and xi_2
    // sample_x[i][j] represents the x value from (x,y), when xi_1=i/height and xi_2=j/width
    // sample_y[i][j] represents the y value from (x,y) when xi_1=i/height and xi_2=j/width
    // sample_p[i][j] represents the probability density when selecting point (i, j)
    std::vector<std::vector<float>> sample_x(height);
    for (auto& line : sample_x) line.resize(width);
    std::vector<std::vector<float>> sample_y(height);
    for (auto& line : sample_y) line.resize(width);
    std::vector<std::vector<float>> sample_p(height);
    for (auto& line : sample_p) line.resize(width);
    for (int j = 0; j < width; j++)
    {
        for (int i = 0; i < height; i++)
        {
            float xi_1 = float(i) / height;
            float xi_2 = float(j) / width;

            // Use xi_1 to find the lower bound in cdf_x_margin to obtain sample x
            __int64 x = std::lower_bound(cdf_x_margin.begin(), cdf_x_margin.end(), xi_1) - cdf_x_margin.begin();
            
            // Use xi_2 to obtain the sample y given X = x
            __int64 y = std::lower_bound(cdf_y_condiciton[x].begin(), cdf_y_condiciton[x].end(), xi_2) - cdf_y_condiciton[x].begin();

            // Store the texture coordinates xy and the probability density corresponding to the xy position
            sample_x[i][j] = float(x) / width;
            sample_y[i][j] = float(y) / height;
            sample_p[i][j] = pdf[i][j];
        }
    }

    // Integrate the results into the texture
    // The R and G channels store the sample (x,y), while the B channel stores the probability density pdf(i, j)
    float* cache = new float[width * height * 3];
    for (int i = 0; i < height; i++)
    {
        for (int j = 0; j < width; j++)
        {
            cache[3 * (i * width + j) + 0] = sample_x[i][j]; // R
            cache[3 * (i * width + j) + 1] = sample_y[i][j]; // G
            cache[3 * (i * width + j) + 2] = sample_p[i][j]; // B
        }
    }

    // Translate to span of bytes
    std::span<const float> dataSpanFloat(cache, width * height * 3);
    std::span<const std::byte> data = Data::GetBytes(dataSpanFloat);

    // Create hdriCache texture
    std::shared_ptr<Texture2DObject> hdriCache = std::make_shared<Texture2DObject>();
    hdriCache->Bind();
    hdriCache->SetImage(0, width, height, TextureObject::FormatRGB, TextureObject::InternalFormatRGB32F, data, Data::Type::Float);
    hdriCache->SetParameter(TextureObject::ParameterEnum::MinFilter, GL_LINEAR);
    hdriCache->SetParameter(TextureObject::ParameterEnum::MagFilter, GL_LINEAR);
    hdriCache->Unbind();

    return hdriCache;
}