#pragma once

#include "Application/Application.h"

#include "Camera/CameraController.h"
#include "Geometry/Model.h"
#include "Utils/DearImGui.h"

class Camera;
class Texture2DObject;
class PathTracingRenderer;

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

    void UpdateMaterial(const Camera& camera, int width, int height);

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

private:
    // Helper object for debug GUI
    DearImGui m_imGui;

    unsigned int m_frameCount = 0;              // Current path tracing frame count
    unsigned int m_maxFrameCount = 50;          // Total rendered amount of frames
    bool m_shouldRasterizeAsPreview = false;    // If this is enabled, we would render the models in rasterization instead of raytracing for scene invalidation (when camera is enabled)
    bool m_shouldPathTrace = false;             // Only render path tracing if frame count is below max
    bool m_shouldDenoise = false;               // We should only denoise once frame count has reached max
    bool m_denoised = false;                    // Whether the fully converged render has been denoised
    float m_denoiseProgress = 0.0f;             // The progress 0..1 of the denoiser
    bool m_denoiserEnabled = false;             // Denoiser enabled for the rendered image

    // Settings
    bool m_AntiAliasingEnabled = true;                  // Whether path tracer should anti-aliase during rendering
    float m_exposure = 1.0f;                            // Frame exposure. Can be tuned any time
    float m_focalLength = 3.5f;                         // Controls the Depth of Field's focus distance
    float m_apertureSize = 0.0f;                        // Controls the Depth of Field's strength
    glm::vec2 m_apertureShape = glm::vec2(1.0f, 1.0f);  // Controls Depth of Field's bokeh shape
    float m_debugValueA = 0.0f;
    float m_debugValueB = 0.0f;

    // HDRI texture
    std::shared_ptr<Texture2DObject> m_hdri;

    // All loaded models
    std::vector<Model> m_models;

    // Camera controller
    CameraController m_cameraController;

    // PathTracing Renderer
    std::shared_ptr<PathTracingRenderer> m_pathTracingRenderer;
};
