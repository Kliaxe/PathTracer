#pragma once

#include "Renderer/RenderPass.h"
#include "glm/vec3.hpp"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class PathTracingRenderer;
class PathTracingApplication;
class Texture2DObject;
class FramebufferObject;

class PathTracingRenderPass : public RenderPass
{
public:
    PathTracingRenderPass(int width, int height, PathTracingRenderer* pathTracingRenderer, PathTracingApplication* pathTracingApplication, std::shared_ptr<const FramebufferObject> targetFramebuffer = nullptr);
    ~PathTracingRenderPass();

    void Render() override;

private:
    void InitializeTextures();

    bool DenoiserCallback(void* userPtr, double n);

private:
    int m_width;
    int m_height;
    PathTracingRenderer* m_pathTracingRenderer;
    PathTracingApplication* m_pathTracingApplication;

    // Textures
    std::shared_ptr<Texture2DObject> m_pathTracingRadianceTexture;
    std::shared_ptr<Texture2DObject> m_pathTracingPrimaryAlbedoTexture;
    std::shared_ptr<Texture2DObject> m_pathTracingPrimaryNormalTexture;
    std::shared_ptr<Texture2DObject> m_pathTracingDenoisedRadianceTexture;
    std::shared_ptr<Texture2DObject> m_outputTexture;

    // Denoiser data
    glm::vec3* m_denoiserRadianceInputPtr;
    glm::vec3* m_denoiserPrimaryAlbedoInputPtr;
    glm::vec3* m_denoiserPrimaryNormalInputPtr;
    glm::vec3* m_denoiserDenoisedRadianceOutputPtr;

    // Framebuffers
    std::shared_ptr<FramebufferObject> m_framebuffer;
};
