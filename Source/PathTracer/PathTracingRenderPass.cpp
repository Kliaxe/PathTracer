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
#include "BVH.h"

//#define DEBUG_VBO
//#define DEBUG_EBO

PathTracingRenderPass::PathTracingRenderPass(int width, int height, PathTracingApplication* pathTracingApplication, std::shared_ptr<Material> pathTracingMaterial, std::shared_ptr<const FramebufferObject> framebuffer)
    : RenderPass(framebuffer), m_width(width), m_height(height), m_pathTracingApplication(pathTracingApplication), m_pathTracingMaterial(pathTracingMaterial)
{
    InitializeTextures();
    InitializeBuffers();

    m_denoiserRenderInputPtr = new glm::vec3[width * height];
    m_denoiserRenderOutputPtr = new glm::vec3[width * height];
}

PathTracingRenderPass::~PathTracingRenderPass()
{
    delete[] m_denoiserRenderInputPtr;
    delete[] m_denoiserRenderOutputPtr;
}

void PathTracingRenderPass::Render()
{
    // Path Tracing render
    if (m_pathTracingApplication->GetShouldPathTrace())
    {
        assert(m_pathTracingMaterial);

        // Mark all as resident
        for (GLuint64 diffuseHandle : m_totalDiffuseTextureHandles)
        {
            glMakeTextureHandleResidentARB(diffuseHandle);
        }
        for (GLuint64 normalHandle : m_totalNormalTextureHandles)
        {
            glMakeTextureHandleResidentARB(normalHandle);
        }
        glMakeTextureHandleResidentARB(m_HDRIHandle);
        
        // Bind SSBO buffers
        //m_ssboMeshInstances->Bind();
        //m_ssboVertices->Bind();
        //m_ssboIndices->Bind();
        m_ssboDiffuseTextures->Bind();
        m_ssboNormalTextures->Bind();
        m_ssboBVHNodes->Bind();
        m_ssboBVHPrimitives->Bind();

        // Bind material and set uniforms
        m_pathTracingMaterial->Use();
        m_pathTracingMaterial->SetUniformValue("PreviousPathTracingTexture", m_pathTracingRenderTexture);

        // Uniform image output
        m_pathTracingRenderTexture->BindImageTexture(0, 0, GL_FALSE, 0, GL_READ_WRITE, TextureObject::InternalFormatRGBA32F);

        const int threadSize = 8;
        const int numGroupsX = static_cast<uint32_t>(std::ceil(m_width / threadSize));
        const int numGroupsY = static_cast<uint32_t>(std::ceil(m_height / threadSize));
        glDispatchCompute(numGroupsX, numGroupsY, 1);

        // Make sure writing to image has finished before read
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        m_outputTexture = m_pathTracingRenderTexture;

        // Mark all as non-resident - can be skipped if you know the same textures
        // will all be used for the next frame
        for (GLuint64 diffuseHandle : m_totalDiffuseTextureHandles)
        {
            glMakeTextureHandleNonResidentARB(diffuseHandle);
        }
        for (GLuint64 normalHandle : m_totalNormalTextureHandles)
        {
            glMakeTextureHandleNonResidentARB(normalHandle);
        }
        glMakeTextureHandleNonResidentARB(m_HDRIHandle);
    }
    
    // Denoise
    if (m_pathTracingApplication->GetShouldDenoise() && !(m_pathTracingApplication->GetDenoised()))
    {
        // Extract texture to memory
        m_pathTracingRenderTexture->Bind();
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_FLOAT, m_denoiserRenderInputPtr);
        m_pathTracingRenderTexture->Unbind();

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
        filter.setImage("color", m_denoiserRenderInputPtr, oidn::Format::Float3, m_width, m_height, 0, 0, 0);
        filter.setImage("output", m_denoiserRenderOutputPtr, oidn::Format::Float3, m_width, m_height, 0, 0, 0);
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
            // m_denoiserRenderOutputPtr to std::span
            std::byte* bytePtr = reinterpret_cast<std::byte*>(m_denoiserRenderOutputPtr);
            size_t sizeInBytes = (size_t)m_width * (size_t)m_height * 3 * sizeof(float); // RGB
            std::span<const std::byte> data(bytePtr, sizeInBytes);

            // Insert data
            m_denoisedPathTracingRenderTexture->Bind();
            m_denoisedPathTracingRenderTexture->SetImage(0, m_width, m_height, TextureObject::FormatRGB, TextureObject::InternalFormat::InternalFormatRGB32F, data, Data::Type::Float);
            m_denoisedPathTracingRenderTexture->Unbind();

            // Assign output texture
            m_outputTexture = m_denoisedPathTracingRenderTexture;
        }

        // Even though we can get error, let us tell that this has been denoised
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
    // Path Tracing Render Texture
    m_pathTracingRenderTexture = std::make_shared<Texture2DObject>();
    m_pathTracingRenderTexture->Bind();
    m_pathTracingRenderTexture->SetImage(0, m_width, m_height, TextureObject::FormatRGBA, TextureObject::InternalFormat::InternalFormatRGBA32F);
    m_pathTracingRenderTexture->SetParameter(TextureObject::ParameterEnum::MinFilter, GL_NEAREST);
    m_pathTracingRenderTexture->SetParameter(TextureObject::ParameterEnum::MagFilter, GL_NEAREST);
    Texture2DObject::Unbind();

    // Denoised Path Tracing Render Texture
    m_denoisedPathTracingRenderTexture = std::make_shared<Texture2DObject>();
    m_denoisedPathTracingRenderTexture->Bind();
    m_denoisedPathTracingRenderTexture->SetImage(0, m_width, m_height, TextureObject::FormatRGB, TextureObject::InternalFormat::InternalFormatRGB32F);
    m_denoisedPathTracingRenderTexture->SetParameter(TextureObject::ParameterEnum::MinFilter, GL_NEAREST);
    m_denoisedPathTracingRenderTexture->SetParameter(TextureObject::ParameterEnum::MagFilter, GL_NEAREST);
    Texture2DObject::Unbind();
}

void PathTracingRenderPass::InitializeBuffers()
{
    // VBO and EBO data of all models
    std::vector<std::vector<Vertex>> totalVertexData;
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
                std::vector<Vertex> vertexData(vboSize / sizeof(Vertex));
                const GLubyte* currentPtr = vboData.data();

                // Map vboData to vertexData of type 'Vertex'
                for (size_t j = 0; j < vboSize / sizeof(Vertex); j++)
                {
                    Vertex vertex { };

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

            // Fetch all materials
            // Model contains a list of pointers to materials, one for each submesh, 
            // so we go through them here instead of per mesh
            {
                Material& material = model.GetMaterial(i);

                // Add diffuseTexture to totalDiffuseTextureHandles
                {
                    std::shared_ptr<Texture2DObject> diffuseTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::Diffuse);
                    if (diffuseTexture)
                    {
                        const GLuint64 diffuseHandle = glGetTextureHandleARB(diffuseTexture->GetHandle());
                        if (diffuseHandle == 0)
                        {
                            std::cerr << "Error! Diffuse Handle returned null" << std::endl;
                            exit(-1);
                        }

                        m_totalDiffuseTextureHandles.push_back(diffuseHandle);
                    }
                }

                // Add diffuseTexture to totalDiffuseTextureHandles
                {
                    std::shared_ptr<Texture2DObject> normalTexture = material.GetMaterialTexture(Material::MaterialTextureSlot::Normal);
                    if (normalTexture)
                    {
                        const GLuint64 normalHandle = glGetTextureHandleARB(normalTexture->GetHandle());
                        if (normalHandle == 0)
                        {
                            std::cerr << "Error! Normal Handle returned null" << std::endl;
                            exit(-1);
                        }

                        m_totalNormalTextureHandles.push_back(normalHandle);
                    }
                }
            }
        }
    }

    //// MeshInstance allocation
    //{
    //    // Create SSBO for mesh instances
    //    m_ssboMeshInstances = std::make_shared<ShaderStorageBufferObject>();
    //    m_ssboMeshInstances->Bind();
    //    glBindBufferBase(m_ssboMeshInstances->GetTarget(), 0, m_ssboMeshInstances->GetHandle()); // Binding index: 0

    //    std::vector<MeshInstanceAlign> meshInstances;

    //    // Iterate over each mesh instance
    //    for (size_t i = 0; i < totalVertexData.size(); ++i)
    //    {
    //        MeshInstanceAlign meshInstance{ };

    //        // Calculate vertices start index and count
    //        meshInstance.VerticesStartIndex = i > 0 ? meshInstances[i - 1].VerticesStartIndex + meshInstances[i - 1].VerticesCount : 0;
    //        meshInstance.VerticesCount = static_cast<unsigned int>(totalVertexData[i].size());

    //        // Calculate indices start index and count
    //        meshInstance.IndicesStartIndex = i > 0 ? meshInstances[i - 1].IndicesStartIndex + meshInstances[i - 1].IndicesCount : 0;
    //        meshInstance.IndicesCount = static_cast<unsigned int>(totalIndexData[i].size());

    //        meshInstances.push_back(meshInstance);
    //    }

    //    // Convert to span
    //    std::span<MeshInstanceAlign> spanVector = std::span(meshInstances);

    //    // Add the total data
    //    m_ssboMeshInstances->AllocateData(spanVector);
    //    m_ssboMeshInstances->Unbind();
    //}

    //// VBO allocation
    //{
    //    // Create SSBO for vertices
    //    m_ssboVertices = std::make_shared<ShaderStorageBufferObject>();
    //    m_ssboVertices->Bind();
    //    glBindBufferBase(m_ssboVertices->GetTarget(), 1, m_ssboVertices->GetHandle()); // Binding index: 1

    //    // Flatten and convert to span
    //    std::vector<VertexAlign> flattenedVector = FlattenVector<VertexAlign>(totalVertexData);
    //    std::span<VertexAlign> spanVector = std::span(flattenedVector);

    //    // Add the total data
    //    m_ssboVertices->AllocateData(spanVector);
    //    m_ssboVertices->Unbind();
    //}

    //// EBO allocation
    //{
    //    // Create SSBO Indices
    //    m_ssboIndices = std::make_shared<ShaderStorageBufferObject>();
    //    m_ssboIndices->Bind();
    //    glBindBufferBase(m_ssboIndices->GetTarget(), 2, m_ssboIndices->GetHandle()); // Binding index: 2

    //    // Flatten and convert to span
    //    std::vector<unsigned int> flattenedVector = FlattenVector<unsigned int>(totalIndexData);
    //    std::span<unsigned int> spanVector = std::span(flattenedVector);

    //    // Add the total data
    //    m_ssboIndices->AllocateData(spanVector);
    //    m_ssboIndices->Unbind();
    //}

    // Diffuse Texture allocation
    {
        // Create SSBO for diffuse textures
        m_ssboDiffuseTextures = std::make_shared<ShaderStorageBufferObject>();
        m_ssboDiffuseTextures->Bind();
        glBindBufferBase(m_ssboDiffuseTextures->GetTarget(), 3, m_ssboDiffuseTextures->GetHandle()); // Binding index: 3

        // Convert to span
        std::span<GLuint64> spanVector = std::span(m_totalDiffuseTextureHandles);

        // Add the total data
        m_ssboDiffuseTextures->AllocateData(spanVector);
        m_ssboDiffuseTextures->Unbind();
    }

    // Normal Texture allocation
    {
        // Create SSBO for diffuse textures
        m_ssboNormalTextures = std::make_shared<ShaderStorageBufferObject>();
        m_ssboNormalTextures->Bind();
        glBindBufferBase(m_ssboNormalTextures->GetTarget(), 4, m_ssboNormalTextures->GetHandle()); // Binding index: 4

        // Convert to span
        std::span<GLuint64> spanVector = std::span(m_totalNormalTextureHandles);

        // Add the total data
        m_ssboNormalTextures->AllocateData(spanVector);
        m_ssboNormalTextures->Unbind();
    }

    // Environment allocation
    {
        // Create SSBO for environment
        m_ssboEnvironment = std::make_shared<ShaderStorageBufferObject>();
        m_ssboEnvironment->Bind();
        glBindBufferBase(m_ssboEnvironment->GetTarget(), 5, m_ssboEnvironment->GetHandle()); // Binding index: 5

        // Get HDRI handle and save
        std::shared_ptr<Texture2DObject> HDRI = m_pathTracingApplication->GetHDRI();
        const GLuint64 HDRIHandle = glGetTextureHandleARB(HDRI->GetHandle());
        if (HDRIHandle == 0)
        {
            std::cerr << "Error! HDRI Handle returned null" << std::endl;
            exit(-1);
        }
        m_HDRIHandle = HDRIHandle;

        // Environment
        EnvironmentAlign environment { };
        environment.HDRIHandle = HDRIHandle;
        environment.skyRotationCos = 1.0f;
        environment.skyRotationSin = 0.0f;

        // Convert to span
        std::span<EnvironmentAlign> spanVector = std::span(&environment, 1);

        // Add the data
        m_ssboEnvironment->AllocateData(spanVector);
        m_ssboEnvironment->Unbind();
    }

    // BVH allocation
    {
        // Convert corrosponding vertices to a format that BVH can use to build BVH nodes

        std::vector<BVH::BVHPrimitive> BVHPrimitives;
        //unsigned int cumulativeVertices = 0;
        //unsigned int cumulativeIndices = 0;

        for (unsigned int meshIndex = 0; meshIndex < totalIndexData.size(); meshIndex++) 
        {
            const auto& vertexData = totalVertexData[meshIndex];
            const auto& indexData = totalIndexData[meshIndex];

            for (size_t i = 0; i < indexData.size(); i += 3)
            {
                unsigned int index1 = indexData[i + 0];
                unsigned int index2 = indexData[i + 1];
                unsigned int index3 = indexData[i + 2];

                BVH::BVHPrimitive primitive { };

                primitive.posA = vertexData[index1].pos;
                primitive.posB = vertexData[index2].pos;
                primitive.posC = vertexData[index3].pos;

                primitive.norA = vertexData[index1].nor;
                primitive.norB = vertexData[index2].nor;
                primitive.norC = vertexData[index3].nor;

                primitive.uvA = vertexData[index1].uv;
                primitive.uvB = vertexData[index2].uv;
                primitive.uvC = vertexData[index3].uv;

                BVHPrimitives.push_back(primitive);

                // Each primitive should have information about vertices it is associated with.
                // This way we don't store pure triangles, which should save memory

                //primitive.verticesStartIndex = cumulativeVertices;
                //primitive.indicesStartIndex = cumulativeIndices;

                // Add
                //BVHPrimitives.push_back(primitive);

                // Increment indices
                //cumulativeIndices += 3;
            }

            // Increment vertices
            //cumulativeVertices += static_cast<unsigned int>(vertexData.size());
        }

        // Create SSBO for BVH nodes
        {
            // Create SSBO for BVH nodes
            m_ssboBVHNodes = std::make_shared<ShaderStorageBufferObject>();
            m_ssboBVHNodes->Bind();
            glBindBufferBase(m_ssboBVHNodes->GetTarget(), 6, m_ssboBVHNodes->GetHandle()); // Binding index: 6

            // Initialize BVH nodes
            BVH::BVHNode InitNode{ };
            std::vector<BVH::BVHNode> BVHNodes{ InitNode };

            // Calculate BVH
            //BVH::BuildBVH(BVHPrimitives, BVHNodes, 0, (int)BVHPrimitives.size() - 1, 8);
            BVH::BuildBVHWithSAH(BVHPrimitives, BVHNodes, 0, (int)BVHPrimitives.size() - 1, 8);

            // Align BVH nodes
            std::vector<BVH::BVHNodeAlign> BVHNodesAligned;
            for (const BVH::BVHNode& node : BVHNodes)
            {
                BVH::BVHNodeAlign nodeAligned { };

                nodeAligned.left = node.left;
                nodeAligned.right = node.right;
                nodeAligned.n = node.n;
                nodeAligned.index = node.index;
                nodeAligned.AA = node.AA;
                nodeAligned.BB = node.BB;

                // Add
                BVHNodesAligned.push_back(nodeAligned);
            }

            // Convert to span
            std::span<BVH::BVHNodeAlign> spanVector = std::span(BVHNodesAligned);

            // Add the total data
            m_ssboBVHNodes->AllocateData(spanVector);
            m_ssboBVHNodes->Unbind();
        }

        // Create SSBO for BVH primitives
        {
            // Create SSBO for BVH primitives
            m_ssboBVHPrimitives = std::make_shared<ShaderStorageBufferObject>();
            m_ssboBVHPrimitives->Bind();
            glBindBufferBase(m_ssboBVHPrimitives->GetTarget(), 7, m_ssboBVHPrimitives->GetHandle()); // Binding index: 7

            // Align BVH primitives
            std::vector<BVH::BVHPrimitiveAlign> BVHPrimitivesAligned;
            for (const BVH::BVHPrimitive& primitive : BVHPrimitives)
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
                
                BVH::BVHPrimitiveAlign primitiveAligned{ };

                primitiveAligned.posAuvX = glm::vec4(posA, uvA.x);
                primitiveAligned.norAuvY = glm::vec4(norA, uvA.y);

                primitiveAligned.posBuvX = glm::vec4(posB, uvB.x);
                primitiveAligned.norBuvY = glm::vec4(norB, uvB.y);

                primitiveAligned.posCuvX = glm::vec4(posC, uvC.x);
                primitiveAligned.norCuvY = glm::vec4(norC, uvC.y);

                // Add
                BVHPrimitivesAligned.push_back(primitiveAligned);

                //primitiveAligned.verticesStartIndex = primitive.verticesStartIndex;
                //primitiveAligned.indicesStartIndex = primitive.indicesStartIndex;

                //// Add
                //BVHPrimitivesAligned.push_back(primitiveAligned);
            }

            // Convert to span
            std::span<BVH::BVHPrimitiveAlign> spanVector = std::span(BVHPrimitivesAligned);

            // Add the total data
            m_ssboBVHPrimitives->AllocateData(spanVector);
            m_ssboBVHPrimitives->Unbind();
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