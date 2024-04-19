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
    // What VBO is made out of, based on allowed semantics
    struct VertexSave
    {
        glm::vec3 pos;
        glm::vec3 nor;
        glm::vec2 uv;
    };
    
    struct MaterialSave
    {
        // Texture handles
        GLuint64 BaseColorTextureHandle = 0;
        GLuint64 NormalTextureHandle = 0;
        GLuint64 SpecularTextureHandle = 0;
        GLuint64 SpecularColorTextureHandle = 0;
        GLuint64 MetallicRoughnessTextureHandle = 0;
        GLuint64 SheenRoughnessTextureHandle = 0;
        GLuint64 SheenColorTextureHandle = 0;
        GLuint64 ClearcoatTextureHandle = 0;
        GLuint64 ClearcoatRoughnessTextureHandle = 0;
        GLuint64 ClearcoatNormalTextureHandle = 0;
        GLuint64 TransmissionTextureHandle = 0;
        GLuint64 EmissiveTextureHandle = 0;

        // Attributes
        glm::vec3 baseColor = glm::vec3(1.0f, 1.0f, 1.0f);
        float specular = 1.0f;
        glm::vec3 specularColor = glm::vec3(1.0f, 1.0f, 1.0f);
        float metallic = 0.0f;
        float roughness = 1.0f;
        float subsurface = 0.0f;
        glm::vec3 subsurfaceColor = glm::vec3(0.5f, 0.5f, 0.5f);
        float anisotropy = 0.0f;
        float sheenRoughness = 0.0f;
        glm::vec3 sheenColor = glm::vec3(0.0f, 0.0f, 0.0f);
        float clearcoat = 0.0f;
        float clearcoatRoughness = 0.0f;
        float refraction = 1.0f;
        float transmission = 0.0f;
        glm::vec3 emissiveColor = glm::vec3(0.0f, 0.0f, 0.0f);
    };

    struct alignas(16) EnvironmentAlign
    {
        GLuint64 HDRIHandle;
        float skyRotationCos;
        float skyRotationSin;
    };

    struct alignas(16) MaterialAlign
    {
        // Texture handles
        GLuint64 BaseColorTextureHandle;
        GLuint64 NormalTextureHandle;
        GLuint64 SpecularTextureHandle;
        GLuint64 SpecularColorTextureHandle;
        GLuint64 MetallicRoughnessTextureHandle;
        GLuint64 SheenRoughnessTextureHandle;
        GLuint64 SheenColorTextureHandle;
        GLuint64 ClearcoatTextureHandle;
        GLuint64 ClearcoatRoughnessTextureHandle;
        GLuint64 ClearcoatNormalTextureHandle;
        GLuint64 TransmissionTextureHandle;
        GLuint64 EmissiveTextureHandle;

        // Attributes
        alignas(16) glm::vec3 baseColor;
        float specular;
        alignas(16) glm::vec3 specularColor;
        float metallic;
        float roughness;
        float subsurface;
        alignas(16) glm::vec3 subsurfaceColor;
        float anisotropy;
        float sheenRoughness;
        alignas(16) glm::vec3 sheenColor;
        float clearcoat;
        float clearcoatRoughness;
        float refraction;
        float transmission;
        alignas(16) glm::vec3 emissiveColor;
    };

private:
    int m_width;
    int m_height;
    PathTracingApplication* m_pathTracingApplication;

    // Denoiser data
    glm::vec3* m_denoiserRenderInputPtr;
    glm::vec3* m_denoiserRenderOutputPtr;

    // Bindless handles
    GLuint64 m_HDRIHandle;

    // Saved materials
    std::vector<MaterialSave> m_materialsSaved;

    // SSBOs
    std::shared_ptr<ShaderStorageBufferObject> m_ssboMaterials;
    std::shared_ptr<ShaderStorageBufferObject> m_ssboEnvironment;
    std::shared_ptr<ShaderStorageBufferObject> m_ssboBVHNodes;
    std::shared_ptr<ShaderStorageBufferObject> m_ssboBVHPrimitives;
};
