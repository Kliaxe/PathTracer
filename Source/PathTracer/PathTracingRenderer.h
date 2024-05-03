#pragma once
#include "Renderer/Renderer.h"

class PathTracingApplication;
class Material;
class Texture2DObject;
class FramebufferObject;
class ShaderStorageBufferObject;

class PathTracingRenderer : public Renderer
{
public:
	PathTracingRenderer(int width, int height, PathTracingApplication* pathTracingApplication, DeviceGL& device);

	void Initialize();

	const int GetWidth()    const { return m_width; }
	const int GetHeight()   const { return m_height; }

	const std::shared_ptr<Material> GetPathTracingMaterial()        const { return m_pathTracingMaterial; }
	const std::shared_ptr<Material> GetPathTracingCopyMaterial()    const { return m_pathTracingCopyMaterial; }
	const std::shared_ptr<Material> GetToneMappingMaterial()        const { return m_toneMappingMaterial; }

    const std::shared_ptr<ShaderStorageBufferObject> GetSsboEnvironment()   const { return m_ssboEnvironment; }
    const std::shared_ptr<ShaderStorageBufferObject> GetSsboMaterials()     const { return m_ssboMaterials; }
    const std::shared_ptr<ShaderStorageBufferObject> GetSsboBvhNodes()      const { return m_ssboBvhNodes; }
    const std::shared_ptr<ShaderStorageBufferObject> GetSsboBvhPrimitives() const { return m_ssboBvhPrimitives; }
    
    const std::vector<GLuint64> GetBindlessHandles() const { return m_bindlessHandles; }

private:
	void InitializeFramebuffer();
	void InitializeMaterial();
	void InitializeRenderPasses();

	std::shared_ptr<Material> CreatePathTracingMaterial();
	std::shared_ptr<Material> CreatePostFXMaterial(const char* fragmentShaderPath);
    
    void CreateBuffers();
    void ClearBuffers();

private:
	void PrintVBOData(VertexBufferObject& vbo, GLint vboSize);

	template<typename T>
	void PrintEBOData(ElementBufferObject& ebo, GLint eboSize);

	template<typename T>
	std::vector<unsigned int> FetchAndConvertEBO(ElementBufferObject& ebo, GLint eboSize, int subMeshIndex, int EBOTypeValue);

	template<typename T>
	std::vector<T> FlattenVector(const std::vector<std::vector<T>>& nestedVector);

	std::shared_ptr<Texture2DObject> CalculateHdriCache(std::shared_ptr<Texture2DObject> hdri, int& width, int& height);

private:
    // Intermediate data translation to buffer structs
    struct VertexSave;
	struct MaterialSave;

    // Buffer structs that are aligned to 16 bytes
    struct alignas(16) EnvironmentAlign;
	struct alignas(16) MaterialAlign;

private:
	int m_width;
	int m_height;
	PathTracingApplication* m_pathTracingApplication;

	// Framebuffer
	std::shared_ptr<Texture2DObject>    m_pathTracingTexture;
	std::shared_ptr<FramebufferObject>  m_pathTracingFramebuffer;

	// Materials
	std::shared_ptr<Material> m_pathTracingMaterial;
	std::shared_ptr<Material> m_pathTracingCopyMaterial;
	std::shared_ptr<Material> m_toneMappingMaterial;

private:
	// Hdri Cache
	std::shared_ptr<Texture2DObject> m_hdriCache;

	// Bindless texture handles
    std::vector<GLuint64> m_bindlessHandles;

	// SSBOs
	std::shared_ptr<ShaderStorageBufferObject> m_ssboEnvironment;
	std::shared_ptr<ShaderStorageBufferObject> m_ssboMaterials;
	std::shared_ptr<ShaderStorageBufferObject> m_ssboBvhNodes;
	std::shared_ptr<ShaderStorageBufferObject> m_ssboBvhPrimitives;
};

struct PathTracingRenderer::VertexSave
{
    glm::vec3 pos;
    glm::vec3 nor;
    glm::vec2 uv;
};

struct PathTracingRenderer::MaterialSave
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

struct alignas(16) PathTracingRenderer::EnvironmentAlign
{
    GLuint64 hdriHandle;
    GLuint64 hdriCacheHandle;
    alignas(16) glm::vec2 hdriDimensions;
};

struct alignas(16) PathTracingRenderer::MaterialAlign
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