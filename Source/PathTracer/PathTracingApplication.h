#pragma once

#include "Application/Application.h"

#include "Renderer/Renderer.h"
#include "Camera/CameraController.h"
#include "Geometry/Model.h"
#include "Utils/DearImGui.h"

class Material;
class Texture2DObject;
class FramebufferObject;

class PathTracingApplication : public Application
{
public:
    PathTracingApplication();

protected:
    void Initialize() override;
    void Update() override;
    void Render() override;
    void Cleanup() override;

private:
    void InitializeHdri();
    void InitializeModel();
    void InitializeCamera();
    void InitializeFramebuffer();
    void InitializeMaterial();
    void InitializeRenderer();

    void UpdateMaterial(const Camera& camera, int width, int height);

    std::shared_ptr<Material> CreatePathTracingMaterial();
    std::shared_ptr<Material> CreatePostFXMaterial(const char* fragmentShaderPath);

    void InvalidateScene();

    void RenderGUI();

public:
    const bool GetShouldPathTrace() const { return m_shouldPathTrace; }
    
    const bool GetShouldDenoise() const { return m_shouldDenoise; }

    void SetDenoised(bool value) { m_denoised = value; }
    const bool GetDenoised() const { return m_denoised; }

    void SetDenoiseProgress(float value) { m_denoiseProgress = value; }
    const float GetDenoiseProgress() const { return m_denoiseProgress; }

    const bool GetDenoiserEnabled() const { return m_denoiserEnabled; }

    const float GetDebugValueA() const { return m_debugValueA; }
    const float GetDebugValueB() const { return m_debugValueB; }

    const std::shared_ptr<Texture2DObject> GetHdri() { return m_hdri; }

    const std::vector<Model> GetModels() const { return m_models; }

    const std::shared_ptr<Material> GetPathTracingMaterial() const { return m_pathTracingMaterial; }
    const std::shared_ptr<Material> GetPathTracingCopyMaterial() const { return m_pathTracingCopyMaterial; }
    const std::shared_ptr<Material> GetToneMappingMaterial() const { return m_toneMappingMaterial; }

private:
    // Helper object for debug GUI
    DearImGui m_imGui;

    // Frame counter
    unsigned int m_frameCount = 0;
    unsigned int m_maxFrameCount = 50;

    // If this is enabled, we would render the models in rasterization instead of raytracing for scene invalidation (when camera is enabled)
    bool m_shouldRasterizeAsPreview = false;

    // Only render path tracing if frame count is below max
    bool m_shouldPathTrace = false;

    // We should only denoise once frame count has reached max
    bool m_shouldDenoise = false;

    // Whether the fully converged render has been denoised
    bool m_denoised = false;

    // The progress 0..1 of the denoiser
    float m_denoiseProgress = 0.0f;

    // Denoiser enabled for the rendered image
    bool m_denoiserEnabled = false;

    // Settings
    bool m_AntiAliasingEnabled = true;
    float m_exposure = 1.0f;
    float m_focalLength = 3.5f; // Controls the Depth of Field's focus distance
    float m_apertureSize = 0.0f; // Controls the Depth of Field's strength
    glm::vec2 m_apertureShape = glm::vec2(1.0f, 1.0f); // Controls Depth of Field's bokeh shape
    float m_debugValueA = 0.0f;
    float m_debugValueB = 0.0f;

    // HDRI texture
    std::shared_ptr<Texture2DObject> m_hdri;

    // All loaded models
    std::vector<Model> m_models;

    // Camera controller
    CameraController m_cameraController;

    // Framebuffer
    std::shared_ptr<Texture2DObject> m_pathTracingTexture;
    std::shared_ptr<FramebufferObject> m_pathTracingFramebuffer;

    // Materials
    std::shared_ptr<Material> m_pathTracingMaterial;
    std::shared_ptr<Material> m_pathTracingCopyMaterial;
    std::shared_ptr<Material> m_toneMappingMaterial;

    // Renderer
    Renderer m_renderer;
};
