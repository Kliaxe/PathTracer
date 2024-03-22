#pragma once

#include "Renderer/RenderPass.h"
#include "glm/vec3.hpp"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class PathTracingApplication;
class ShaderStorageBufferObject;
class FramebufferObject;
class Texture2DObject;
class Material;
class Model;
class VertexBufferObject;
class ElementBufferObject;

class PathTracingRenderPass : public RenderPass
{
public:
    PathTracingRenderPass(int width, int height, PathTracingApplication* pathTracingApplication, std::shared_ptr<Material> pathTracingMaterial, std::shared_ptr<const FramebufferObject> targetFramebuffer = nullptr);
    ~PathTracingRenderPass();

    void Render() override;

private:
    void InitializeTextures();

    void InitializeBuffers();

    bool DenoiserCallback(void* userPtr, double n);

    void PrintVBOData(VertexBufferObject& vbo, GLint vboSize);

    template<typename T>
    void PrintEBOData(ElementBufferObject& ebo, GLint eboSize);

    template<typename T>
    std::vector<unsigned int> FetchAndConvertEBO(ElementBufferObject& ebo, GLint eboSize, int subMeshIndex, int EBOTypeValue);

    template<typename T>
    std::vector<T> FlattenVector(const std::vector<std::vector<T>>& nestedVector);

private:
    // Materials
    std::shared_ptr<Material> m_pathTracingMaterial;

    // Textures
    std::shared_ptr<Texture2DObject> m_pathTracingRenderTexture;
    std::shared_ptr<Texture2DObject> m_denoisedPathTracingRenderTexture;
    std::shared_ptr<Texture2DObject> m_outputTexture;

    // Framebuffers
    std::shared_ptr<FramebufferObject> m_framebuffer;

private:
    int m_width;
    int m_height;
    PathTracingApplication* m_pathTracingApplication;

    // Denoiser data
    glm::vec3* m_denoiserRenderInputPtr;
    glm::vec3* m_denoiserRenderOutputPtr;

    // Bindless handles
    std::vector<GLuint64> m_totalDiffuseTextureHandles;
    std::vector<GLuint64> m_totalNormalTextureHandles;
    GLuint64 m_HDRIHandle;

    // SSBOs
    std::shared_ptr<ShaderStorageBufferObject> m_ssboMeshInstances;
    std::shared_ptr<ShaderStorageBufferObject> m_ssboVertices;
    std::shared_ptr<ShaderStorageBufferObject> m_ssboIndices;
    std::shared_ptr<ShaderStorageBufferObject> m_ssboDiffuseTextures;
    std::shared_ptr<ShaderStorageBufferObject> m_ssboNormalTextures;
    std::shared_ptr<ShaderStorageBufferObject> m_ssboEnvironment;

private:
    struct MeshInstanceAlign
    {
        unsigned int VerticesStartIndex;
        unsigned int VerticesCount;
        unsigned int IndicesStartIndex;
        unsigned int IndicesCount;
    };

    // What VBO is made out of, based on allowed semantics
    struct Vertex
    {
        glm::vec3 pos;
        glm::vec3 nor;
        glm::vec2 uv;
    };

    struct VertexAlign
    {
        glm::vec3 pos; float _padding1;
        glm::vec3 nor; float _padding2;
        glm::vec2 uv;  glm::vec2 _padding3;
    };

    struct EnvironmentAlign
    {
        GLuint64 HDRIHandle;
        float skyRotationCos;
        float skyRotationSin;
        float _padding1;
    };
};
