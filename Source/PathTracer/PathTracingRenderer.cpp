#include "PathTracingRenderer.h"
#include "Shader/Shader.h"
#include "Shader/Material.h"
#include "Texture/FramebufferObject.h"
#include "Texture/Texture2DObject.h"
#include "Renderer/PostFXRenderPass.h"
#include "Asset/ShaderLoader.h"
#include "PathTracingRenderPass.h"
#include "PathTracingApplication.h"
#include "BVH.h"
#include <stdexcept>
#include "Geometry/ShaderStorageBufferObject.h"
#include "Utils/Timer.h"
#include <algorithm>
#include <numeric>

//#define DEBUG_VBO
//#define DEBUG_EBO

PathTracingRenderer::PathTracingRenderer(int width, int height, PathTracingApplication* pathTracingApplication, DeviceGL& device)
    : Renderer(device), m_width(width), m_height(height), m_pathTracingApplication(pathTracingApplication) { }

void PathTracingRenderer::Initialize()
{
    InitializeFramebuffer();
    InitializeMaterial();
    InitializeRenderPasses();
    InitializeBuffers();
}

void PathTracingRenderer::InitializeFramebuffer()
{
    // Path Tracing Texture
    m_pathTracingTexture = std::make_shared<Texture2DObject>();
    m_pathTracingTexture->Bind();
    m_pathTracingTexture->SetImage(0, m_width, m_height, TextureObject::FormatRGBA, TextureObject::InternalFormat::InternalFormatRGBA32F);
    m_pathTracingTexture->SetParameter(TextureObject::ParameterEnum::MinFilter, GL_LINEAR);
    m_pathTracingTexture->SetParameter(TextureObject::ParameterEnum::MagFilter, GL_LINEAR);
    Texture2DObject::Unbind();

    // Path Tracing Framebuffer
    m_pathTracingFramebuffer = std::make_shared<FramebufferObject>();
    m_pathTracingFramebuffer->Bind();
    m_pathTracingFramebuffer->SetTexture(FramebufferObject::Target::Draw, FramebufferObject::Attachment::Color0, *m_pathTracingTexture);
    m_pathTracingFramebuffer->SetDrawBuffers(std::array<FramebufferObject::Attachment, 1>({ FramebufferObject::Attachment::Color0 }));
    FramebufferObject::Unbind();
}

void PathTracingRenderer::InitializeMaterial()
{
    // Path Tracing material
    {
        // Create material
        m_pathTracingMaterial = CreatePathTracingMaterial();

        // Initialize material uniforms
        // ...
    }

    // Path Tracing Copy material
    {
        // Create material
        m_pathTracingCopyMaterial = CreatePostFXMaterial("Shaders/Renderer/copy.frag");

        // Initialize material uniforms
        // ...
    }

    // Tone Mapping material
    {
        // Create material
        m_toneMappingMaterial = CreatePostFXMaterial("Shaders/tonemapping.frag");

        // Initialize material uniforms
        m_toneMappingMaterial->SetUniformValue("SourceTexture", m_pathTracingTexture);
    }
}

void PathTracingRenderer::InitializeRenderPasses()
{
	// Path Tracing render pass
	AddRenderPass(std::make_unique<PathTracingRenderPass>(m_width, m_height, this, m_pathTracingApplication, m_pathTracingFramebuffer));

	// Tone Mapping render pass
	AddRenderPass(std::make_unique<PostFXRenderPass>(m_toneMappingMaterial, GetDefaultFramebuffer()));
}

void PathTracingRenderer::InitializeBuffers()
{
    m_ssboEnvironment = std::make_shared<ShaderStorageBufferObject>();
    m_ssboMaterials = std::make_shared<ShaderStorageBufferObject>();
    m_ssboBvhNodes = std::make_shared<ShaderStorageBufferObject>();
    m_ssboBvhPrimitives = std::make_shared<ShaderStorageBufferObject>();
}

std::shared_ptr<Material> PathTracingRenderer::CreatePathTracingMaterial()
{
    std::vector<const char*> computeShaderPaths;
    computeShaderPaths.push_back("Shaders/Library/version.glsl");
    computeShaderPaths.push_back("Shaders/Library/common.glsl");
    computeShaderPaths.push_back("Shaders/Library/resources.glsl");
    computeShaderPaths.push_back("Shaders/Library/utility.glsl");
    computeShaderPaths.push_back("Shaders/Library/intersection.glsl");
    computeShaderPaths.push_back("Shaders/Library/montecarlo.glsl");
    computeShaderPaths.push_back("Shaders/Library/brdf.glsl");
    computeShaderPaths.push_back("Shaders/Library/disney.glsl");
    computeShaderPaths.push_back("Shaders/Library/hdri.glsl");
    computeShaderPaths.push_back("Shaders/Library/debug.glsl");
    computeShaderPaths.push_back("Shaders/pathtracing.comp");
    Shader computeShader = ShaderLoader(Shader::ComputeShader).Load(computeShaderPaths);

    std::shared_ptr<ShaderProgram> shaderProgramPtr = std::make_shared<ShaderProgram>();
    shaderProgramPtr->Build(computeShader);

    // Create material
    std::shared_ptr<Material> material = std::make_shared<Material>(shaderProgramPtr);

    return material;
}

std::shared_ptr<Material> PathTracingRenderer::CreatePostFXMaterial(const char* fragmentShaderPath)
{
    std::vector<const char*> vertexShaderPaths;
    vertexShaderPaths.push_back("Shaders/Renderer/fullscreen.vert");
    Shader vertexShader = ShaderLoader(Shader::VertexShader).Load(vertexShaderPaths);

    std::vector<const char*> fragmentShaderPaths;
    fragmentShaderPaths.push_back(fragmentShaderPath);
    Shader fragmentShader = ShaderLoader(Shader::FragmentShader).Load(fragmentShaderPaths);

    std::shared_ptr<ShaderProgram> shaderProgramPtr = std::make_shared<ShaderProgram>();
    shaderProgramPtr->Build(vertexShader, fragmentShader);

    // Create material
    std::shared_ptr<Material> material = std::make_shared<Material>(shaderProgramPtr);

    return material;
}

void PathTracingRenderer::ClearPathTracingTexture()
{
    // Black with full opacity
    GLubyte clearColor[4] = { 0, 0, 0, 255 };

    m_pathTracingTexture->Bind();
    //m_pathTracingTexture->ClearTexture(0, TextureObject::Format::FormatRGBA, Data::Type::Float, &clearColor);
    m_pathTracingTexture->ClearTexture(0, TextureObject::Format::FormatRGBA, Data::Type::Float, &clearColor);
    m_pathTracingTexture->Unbind();

    m_pathTracingFramebuffer->Bind();
    glClearTexImage(m_pathTracingFramebuffer->GetHandle(), 0, GL_RGBA, GL_FLOAT, &clearColor);
    m_pathTracingFramebuffer->Unbind();
}

void PathTracingRenderer::AddPathTracingModel(const Model& model, const glm::mat4& worldMatrix)
{
    PathTracingModel pathTracingModel{ };
    pathTracingModel.model = model;
    pathTracingModel.worldMatrix = worldMatrix;

    m_pathTracingModels.push_back(pathTracingModel);
}

void PathTracingRenderer::ClearPathTracingModels()
{
    m_pathTracingModels.clear();
}

void PathTracingRenderer::ProcessBuffers()
{
    std::cout << "Processing all buffers!" << std::endl;

    // Clear bindless handles!
    // We're going to fill it with new data
    m_bindlessHandles.clear();

    // VBO and EBO data of all meshes
    std::vector<std::vector<VertexSave>>    totalVertexData;
    std::vector<std::vector<unsigned int>>  totalIndexData;
    std::vector<MaterialSave>               totalMaterialData;

    // Go through each model from application
    for (const PathTracingModel& pathTracingModel : m_pathTracingModels)
    {
        // Fetch model
        Model model = pathTracingModel.model;

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
                    VertexSave vertex{ };

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
                if (emissionTexture)
                {
                    materialSave.emissionTextureHandle = emissionTexture->GetBindlessTextureHandle();
                    m_bindlessHandles.push_back(materialSave.emissionTextureHandle);
                }

                // Albedo Texture
                std::shared_ptr<Texture2DObject> albedoTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::AlbedoTexture);
                if (albedoTexture)
                { 
                    materialSave.albedoTextureHandle = albedoTexture->GetBindlessTextureHandle();
                    m_bindlessHandles.push_back(materialSave.albedoTextureHandle);
                }

                // Normal Texture
                std::shared_ptr<Texture2DObject> normalTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::NormalTexture);
                if (normalTexture)
                {
                    materialSave.normalTextureHandle = normalTexture->GetBindlessTextureHandle();
                    m_bindlessHandles.push_back(materialSave.normalTextureHandle);
                }

                // Specular Texture
                std::shared_ptr<Texture2DObject> specularTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::SpecularTexture);
                if (specularTexture)
                {
                    materialSave.specularTextureHandle = specularTexture->GetBindlessTextureHandle();
                    m_bindlessHandles.push_back(materialSave.specularTextureHandle);
                }

                // Specular Color Texture
                std::shared_ptr<Texture2DObject> specularColorTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::SpecularColorTexture);
                if (specularColorTexture)
                { 
                    materialSave.specularColorTextureHandle = specularColorTexture->GetBindlessTextureHandle();
                    m_bindlessHandles.push_back(materialSave.specularColorTextureHandle);
                }

                // Metallic Roughness Texture
                std::shared_ptr<Texture2DObject> metallicRoughnessTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::MetallicRoughnessTexture);
                if (metallicRoughnessTexture)
                {
                    materialSave.metallicRoughnessTextureHandle = metallicRoughnessTexture->GetBindlessTextureHandle();
                    m_bindlessHandles.push_back(materialSave.metallicRoughnessTextureHandle);
                }

                // Sheen Roughness Texture
                std::shared_ptr<Texture2DObject> sheenRoughnessTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::SheenRoughnessTexture);
                if (sheenRoughnessTexture)
                {
                    materialSave.sheenRoughnessTextureHandle = sheenRoughnessTexture->GetBindlessTextureHandle();
                    m_bindlessHandles.push_back(materialSave.sheenRoughnessTextureHandle);
                }

                // Sheen Color Texture
                std::shared_ptr<Texture2DObject> sheenColorTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::SheenColorTexture);
                if (sheenColorTexture)
                {
                    materialSave.sheenColorTextureHandle = sheenColorTexture->GetBindlessTextureHandle();
                    m_bindlessHandles.push_back(materialSave.sheenColorTextureHandle);
                }

                // Clearcoat Texture
                std::shared_ptr<Texture2DObject> clearcoatTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::ClearcoatTexture);
                if (clearcoatTexture)
                {
                    materialSave.clearcoatTextureHandle = clearcoatTexture->GetBindlessTextureHandle();
                    m_bindlessHandles.push_back(materialSave.clearcoatTextureHandle);
                }

                // Clearcoat Roughness Texture
                std::shared_ptr<Texture2DObject> clearcoatRoughnessTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::ClearcoatRoughnessTexture);
                if (clearcoatRoughnessTexture)
                {
                    materialSave.clearcoatRoughnessTextureHandle = clearcoatRoughnessTexture->GetBindlessTextureHandle();
                    m_bindlessHandles.push_back(materialSave.clearcoatRoughnessTextureHandle);
                }

                // Transmission Texture
                std::shared_ptr<Texture2DObject> transmissionTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::TransmissionTexture);
                if (transmissionTexture)
                {
                    materialSave.transmissionTextureHandle = transmissionTexture->GetBindlessTextureHandle();
                    m_bindlessHandles.push_back(materialSave.transmissionTextureHandle);
                }

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
                totalMaterialData.push_back(materialSave);
            }
        }
    }

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

            BVH::BvhPrimitive primitive{ };

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

    // Environment allocation
    ProcessEnvironmentBuffer();

    // Material allocation
    ProcessMaterialBuffer(totalMaterialData);

    // Create SSBO for BVH nodes
    ProcessBvhNodeBuffer(bvhPrimitives);

    // Create SSBO for BVH primitives
    ProcessBvhPrimitiveBuffer(bvhPrimitives);

    std::cout << "Done processing buffers..." << std::endl;
}

void PathTracingRenderer::ProcessEnvironmentBuffer()
{
    // Bind SSBO for environment
    m_ssboEnvironment->Bind();

    // Binding index
    glBindBufferBase(m_ssboEnvironment->GetTarget(), 0, m_ssboEnvironment->GetHandle()); // Binding index: 0

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
    m_bindlessHandles.push_back(hdriHandle);

    // Get HDRI Cache handle
    const GLuint64 hdriCacheHandle = m_hdriCache->GetBindlessTextureHandle();
    if (hdriCacheHandle == 0) { throw new std::runtime_error("Error! HDRI Cache Handle returned null"); }
    m_bindlessHandles.push_back(hdriCacheHandle);

    // Environment
    EnvironmentAlign environment{ };
    environment.hdriHandle = hdriHandle;
    environment.hdriCacheHandle = hdriCacheHandle;
    environment.hdriDimensions = glm::vec2(float(hdriWidth), float(hdriHeight));

    // Convert to span
    std::span<EnvironmentAlign> span = std::span(&environment, 1);

    // Allocate
    m_ssboEnvironment->AllocateData(span);
    m_ssboEnvironment->Unbind();
}

void PathTracingRenderer::ProcessMaterialBuffer(std::vector<MaterialSave> totalMaterialData)
{
    // Bind SSBO for materials
    m_ssboMaterials->Bind();

    // Binding index
    glBindBufferBase(m_ssboMaterials->GetTarget(), 1, m_ssboMaterials->GetHandle()); // Binding index: 1

    // Convert to MaterialAlign for GPU consumption

    std::vector<MaterialAlign> materialsAlign;
    for (const MaterialSave& materialSave : totalMaterialData)
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

void PathTracingRenderer::ProcessBvhNodeBuffer(std::vector<BVH::BvhPrimitive>& bvhPrimitives)
{
    // Bind SSBO for BVH nodes
    m_ssboBvhNodes->Bind();

    // Binding index
    glBindBufferBase(m_ssboBvhNodes->GetTarget(), 2, m_ssboBvhNodes->GetHandle()); // Binding index: 2

    // Initialize BVH nodes
    BVH::BvhNode initNode{ };
    std::vector<BVH::BvhNode> bvhNodes{ initNode };

    // Start timer
    Timer timer("BVH Calculation");

    // Calculate BVH
    // It modifies bvhPrimitives!
    //BVH::BuildBvh(bvhPrimitives, bvhNodes, 0, (int)bvhPrimitives.size() - 1, 4);
    BVH::BuildBvhWithSah(bvhPrimitives, bvhNodes, 0, (int)bvhPrimitives.size() - 1, 4);

    // End time point in milliseconds and print
    timer.Stop();
    timer.Print();

    // Align BVH nodes
    std::vector<BVH::BvhNodeAlign> bvhNodesAligned;
    for (const BVH::BvhNode& node : bvhNodes)
    {
        BVH::BvhNodeAlign nodeAligned{ };

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

void PathTracingRenderer::ProcessBvhPrimitiveBuffer(std::vector<BVH::BvhPrimitive> bvhPrimitives)
{
    // Bind SSBO for BVH primitives
    m_ssboBvhPrimitives->Bind();

    // Binding index
    glBindBufferBase(m_ssboBvhPrimitives->GetTarget(), 3, m_ssboBvhPrimitives->GetHandle()); // Binding index: 3

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

void PathTracingRenderer::PrintVBOData(VertexBufferObject& vbo, GLint vboSize)
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
void PathTracingRenderer::PrintEBOData(ElementBufferObject& ebo, GLint eboSize)
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
std::vector<unsigned int> PathTracingRenderer::FetchAndConvertEBO(ElementBufferObject& ebo, GLint eboSize, int subMeshIndex, int EBOTypeValue)
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
std::vector<T> PathTracingRenderer::FlattenVector(const std::vector<std::vector<T>>& nestedVector)
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

std::shared_ptr<Texture2DObject> PathTracingRenderer::CalculateHdriCache(std::shared_ptr<Texture2DObject> hdri, int& width, int& height)
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

    // Cleanup
    delete[] cache;

    return hdriCache;
}