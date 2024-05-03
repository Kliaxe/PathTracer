#pragma once

#include "Application/Application.h"

#include "Camera/CameraController.h"
#include "Utils/DearImGui.h"
#include <vector>

class Scene;
class SceneModel;
class Camera;
class Texture2DObject;
class ModelLoader;
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
    void InitializeCamera();
    void InitializeLoader();

    void UpdateMaterial(const Camera& camera, int width, int height);

    void RefreshScene();
    void InvalidateScene();

    void RenderGUI();

private:
    enum PathTracingHdri
    {
        BrownPhotostudio,
        ChineseGarden,
        Meadow,
        SymmetricalGarden,
    };
    
    enum PathTracingScene
    {
        TestScene1,
        TestScene2,
    };

    void ProcessHdri(bool processEnvironmentBuffer);

    void ProcessScene();
    Scene LoadTestScene1();
    Scene LoadTestScene2();

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

private:
    // Helper object for debug GUI
    DearImGui m_imGui;

    // Configuration
    unsigned int m_maxFrameCount = 50;          // Total rendered amount of frames
    bool m_shouldRasterizeAsPreview = false;    // If this is enabled, we would render the models in rasterization instead of raytracing for scene invalidation (when camera is enabled)
    bool m_denoiserEnabled = false;             // Denoiser enabled for the rendered image

    // Settings
    bool m_AntiAliasingEnabled = true;                  // Whether path tracer should anti-aliase during rendering
    float m_exposure = 1.0f;                            // Frame exposure. Can be tuned any time
    float m_focalLength = 3.5f;                         // Controls the Depth of Field's focus distance
    float m_apertureSize = 0.0f;                        // Controls the Depth of Field's strength
    glm::vec2 m_apertureShape = glm::vec2(1.0f, 1.0f);  // Controls Depth of Field's bokeh shape
    
    // Modifiers
    float m_specularModifier = 0.0f;
    float m_specularTintModifier = 0.0f;
    float m_metallicModifier = 0.0f;
    float m_roughnessModifier = 0.0f;
    float m_subsurfaceModifier = 0.0f;
    float m_anisotropyModifier = 0.0f;
    float m_sheenRoughnessModifier = 0.0f;
    float m_sheenTintModifier = 0.0f;
    float m_clearcoatModifier = 0.0f;
    float m_clearcoatRoughnessModifier = 0.0f;
    float m_refractionModifier = 0.0f;
    float m_transmissionModifier = 0.0f;
    
    float m_debugValueA = 0.0f;
    float m_debugValueB = 0.0f;

    unsigned int m_frameCount = 0;              // Current path tracing frame count
    bool m_shouldPathTrace = false;             // Only render path tracing if frame count is below max
    bool m_shouldDenoise = false;               // We should only denoise once frame count has reached max
    float m_denoiseProgress = 0.0f;             // The progress 0..1 of the denoiser
    bool m_denoised = false;                    // Whether the fully converged render has been denoised

    // Current chosen data
    PathTracingHdri m_currentPathTracingHdri = PathTracingHdri::BrownPhotostudio;
    PathTracingScene m_currentPathTracingScene = PathTracingScene::TestScene1;

    // Camera controller
    CameraController m_cameraController;

    // HDRI texture
    std::shared_ptr<Texture2DObject> m_hdri;

    // Model loader
    std::shared_ptr<ModelLoader> m_modelLoader;

    // PathTracing Renderer
    std::shared_ptr<PathTracingRenderer> m_pathTracingRenderer;
};
