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
    void InitializeHDRI();
    void InitializeModel();
    void InitializeCamera();
    void InitializeFramebuffer();
    void InitializeMaterials();
    void InitializeRenderer();

    std::shared_ptr<Material> CreatePathTracingMaterial();
    std::shared_ptr<Material> CreateCopyMaterial();

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

    const std::shared_ptr<Texture2DObject> GetHDRI() { return m_HDRI; }

    const std::vector<Model> GetModels() const { return m_models; }

    const std::shared_ptr<Material> GetPathTracingMaterial() const { return m_pathTracingMaterial; }
    const std::shared_ptr<Material> GetCopyMaterial() const { return m_copyMaterial; }

private:
    // Helper object for debug GUI
    DearImGui m_imGui;

    // Frame counter
    unsigned int m_frameCount;
    unsigned int m_maxFrameCount;

    // If this is enabled, we would render the models in rasterization instead of raytracing for scene invalidation (when camera is enabled)
    bool m_shouldRasterizeAsPreview;

    // Only render path tracing if frame count is below max
    bool m_shouldPathTrace;

    // We should only denoise once frame count has reached max
    bool m_shouldDenoise;

    // Whether the fully converged render has been denoised
    bool m_denoised;

    // The progress 0..1 of the denoiser
    float m_denoiseProgress;

    // Denoiser enabled for the rendered image
    bool m_denoiserEnabled;

    // Settings
    float m_focalLength = 3.5f; // Controls the Depth of Field's focus distance
    float m_apertureSize = 0.0f; // Controls the Depth of Field's strength
    glm::vec2 m_apertureShape = glm::vec2(1.0f, 1.0f); // Controls Depth of Field's bokeh shape
    float m_debugValueA;
    float m_debugValueB;

    // HDRI texture
    std::shared_ptr<Texture2DObject> m_HDRI;

    // All loaded models
    std::vector<Model> m_models;

    // Camera controller
    CameraController m_cameraController;

    // Framebuffer
    std::shared_ptr<Texture2DObject> m_pathTracingTexture;
    std::shared_ptr<FramebufferObject> m_pathTracingFramebuffer;

    // Materials
    std::shared_ptr<Material> m_pathTracingMaterial;
    std::shared_ptr<Material> m_copyMaterial;

    // Renderer
    Renderer m_renderer;
};
