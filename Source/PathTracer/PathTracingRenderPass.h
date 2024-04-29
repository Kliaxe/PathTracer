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

    std::shared_ptr<Texture2DObject> CalculateHdriCache(std::shared_ptr<Texture2DObject> hdri, int& width, int& height);

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
        GLuint64 emissionTextureHandle = 0;
        GLuint64 albedoTextureHandle = 0;
        GLuint64 normalTextureHandle = 0;
        GLuint64 specularTextureHandle = 0;
        GLuint64 specularColorTextureHandle = 0;
        GLuint64 metallicRoughnessTextureHandle = 0;
        GLuint64 sheenRoughnessTextureHandle = 0;
        GLuint64 sheenColorTextureHandle = 0;
        GLuint64 clearcoatTextureHandle = 0;
        GLuint64 clearcoatRoughnessTextureHandle = 0;
        GLuint64 transmissionTextureHandle = 0;

        // Attributes
        glm::vec3 emission = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 albedo = glm::vec3(1.0f, 1.0f, 1.0f);
        float specular = 0.5f;
        float specularTint = 0.0f;
        float metallic = 0.0f;
        float roughness = 1.0f;
        float subsurface = 0.0f;
        float anisotropy = 0.0f;
        float sheenRoughness = 0.0f;
        float sheenTint = 0.5f;
        float clearcoat = 0.0f;
        float clearcoatRoughness = 0.0f;
        float refraction = 1.5f;
        float transmission = 0.0f;
    };

private:
    struct alignas(16) SettingsAlign
    {
        float debugValueA;
        float debugValueB;
    };

    struct alignas(16) EnvironmentAlign
    {
        GLuint64 hdriHandle;
        GLuint64 hdriCacheHandle;
        alignas(16) glm::vec2 hdriDimensions;
    };

    struct alignas(16) MaterialAlign
    {
        // Texture handles
        GLuint64 emissionTextureHandle;
        GLuint64 albedoTextureHandle;
        GLuint64 normalTextureHandle;
        GLuint64 specularTextureHandle;
        GLuint64 specularColorTextureHandle;
        GLuint64 metallicRoughnessTextureHandle;
        GLuint64 sheenRoughnessTextureHandle;
        GLuint64 sheenColorTextureHandle;
        GLuint64 clearcoatTextureHandle;
        GLuint64 clearcoatRoughnessTextureHandle;
        GLuint64 transmissionTextureHandle;

        // Attributes
        alignas(16) glm::vec3 emission;
        alignas(16) glm::vec3 albedo;
        float specular;
        float specularTint;
        float metallic;
        float roughness;
        float subsurface;
        float anisotropy;
        float sheenRoughness;
        float sheenTint;
        float clearcoat;
        float clearcoatRoughness;
        float refraction;
        float transmission;
    };

private:
    int m_width;
    int m_height;
    PathTracingApplication* m_pathTracingApplication;

    // Textures
    std::shared_ptr<Texture2DObject> m_pathTracingRadianceTexture;
    std::shared_ptr<Texture2DObject> m_pathTracingPrimaryAlbedoTexture;
    std::shared_ptr<Texture2DObject> m_pathTracingPrimaryNormalTexture;
    std::shared_ptr<Texture2DObject> m_pathTracingDenoisedRadianceTexture;
    std::shared_ptr<Texture2DObject> m_outputTexture;

    // Framebuffers
    std::shared_ptr<FramebufferObject> m_framebuffer;

    // Denoiser data
    glm::vec3* m_denoiserRadianceInputPtr;
    glm::vec3* m_denoiserPrimaryAlbedoInputPtr;
    glm::vec3* m_denoiserPrimaryNormalInputPtr;
    glm::vec3* m_denoiserDenoisedRadianceOutputPtr;

    // Saved materials
    std::vector<MaterialSave> m_materialsSaved;

    // Hdri Cache
    std::shared_ptr<Texture2DObject> m_hdriCache;

    // Bindless texture handles
    GLuint64 m_hdriHandle;
    GLuint64 m_hdriCacheHandle;

    // SSBOs
    std::shared_ptr<ShaderStorageBufferObject> m_ssboSettings;
    std::shared_ptr<ShaderStorageBufferObject> m_ssboEnvironment;
    std::shared_ptr<ShaderStorageBufferObject> m_ssboMaterials;
    std::shared_ptr<ShaderStorageBufferObject> m_ssboBvhNodes;
    std::shared_ptr<ShaderStorageBufferObject> m_ssboBvhPrimitives;
};
