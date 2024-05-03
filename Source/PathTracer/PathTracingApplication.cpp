#include "PathTracingApplication.h"

#include "Asset/ShaderLoader.h"
#include "Camera/Camera.h"
#include "Lighting/DirectionalLight.h"
#include "PathTracingRenderPass.h"
#include "Renderer/PostFXRenderPass.h"
#include "Scene/RendererSceneVisitor.h"
#include "Scene/SceneCamera.h"
#include "Scene/Transform.h"
#include "Shader/Material.h"
#include "Texture/FramebufferObject.h"
#include "Texture/Texture2DObject.h"
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/transform.hpp>
#include <imgui.h>
#include "Scene/Scene.h"
#include "Scene/SceneModel.h"

#include "Geometry/Mesh.h"
#include "Geometry/ShaderStorageBufferObject.h"
#include <Asset/ModelLoader.h>
#include <Geometry/VertexFormat.h>
#include <iostream>
#include "PathTracingRenderer.h"
#include "PathTracingRendererSceneVisitor.h"

PathTracingApplication::PathTracingApplication() : Application(1024, 1024, "PathTracer")
{
    // OpenGL Extension Control
    if (!(GL_ARB_bindless_texture))
    {
        std::cerr << "Bindless textures extension is not supported by your vendor!" << std::endl;
        exit(-1);
    }
    if (!(GL_ARB_gpu_shader_int64))
    {
        std::cerr << "64 bit shader integers extension is not supported by your vendor!" << std::endl;
        exit(-1);
    }

    // Get width and height for renderer
    int width, height;
    GetMainWindow().GetDimensions(width, height);

    // Create PathTracing Renderer
    m_pathTracingRenderer = std::make_shared<PathTracingRenderer>(width, height, this, GetDevice());
}

void PathTracingApplication::Initialize()
{
    Application::Initialize();

    // Initialize DearImGUI
    m_imGui.Initialize(GetMainWindow());

    // Initialize PathTracing Renderer
    m_pathTracingRenderer->Initialize();

    // Initialize application specifics
    InitializeCamera();
    InitializeLoader();

    // Process the chosen Hdri (based on default)
    // Don't process here, we're going to process all buffers anyway
    ProcessHdri(false);

    // Process the chosen scene (based on default)
    // Relies on HDRI and models
    ProcessScene();
}

void PathTracingApplication::Update()
{
    Application::Update();

    int width, height;
    GetMainWindow().GetDimensions(width, height);

    // Increment frame count (capped by max)
    m_frameCount = m_frameCount < m_maxFrameCount ? ++m_frameCount : m_frameCount;

    // Determine if we should do the actual rendering
    m_shouldPathTrace = m_frameCount < m_maxFrameCount;

    // Determine if we should denoise
    m_shouldDenoise = m_frameCount >= m_maxFrameCount && m_denoiserEnabled;

    // Update camera controller
    m_cameraController.Update(GetMainWindow(), GetDeltaTime());

    // Invalidate accumulation when moving the camera
    if (m_cameraController.IsEnabled())
    {
        InvalidateScene();
    }

    // Set PathTracing Renderer camera
    const Camera& camera = *m_cameraController.GetCamera()->GetCamera();
    m_pathTracingRenderer->SetCurrentCamera(camera);

    // Update materials
    UpdateMaterial(camera, width, height);
}

void PathTracingApplication::Render()
{
    Application::Render();

    GetDevice().Clear(true, Color(0.0f, 0.0f, 0.0f, 1.0f), true, 1.0f);

    // Render the scene
    if (m_shouldRasterizeAsPreview && m_cameraController.IsEnabled())
    {
        // Using traditional rasterization
        //for (const Model& model : (SceneModel)m_currentScene.GetSceneNodes())
        //{
        //    model.Draw();
        //}
    }
    else
    {
        // Using path trace renderer
        m_pathTracingRenderer->Render();
    }

    // Render the debug user interface
    RenderGUI();
}

void PathTracingApplication::Cleanup()
{
    // Cleanup DearImGUI
    m_imGui.Cleanup();

    Application::Cleanup();
}

void PathTracingApplication::InvalidateScene()
{
    // If scene was dirty, we should reset the denoised state as well
    m_frameCount = 1;
    m_denoiseProgress = 0.0f;
    m_denoised = false;
}

void PathTracingApplication::RefreshScene()
{
    // Reset to default state
    m_frameCount = 0;
    m_shouldPathTrace = false;
    m_shouldDenoise = false;
    m_denoiseProgress = 0.0f;
    m_denoised = false;
}

void PathTracingApplication::InitializeLoader()
{
    // Load and build shader
    Shader vertexShader = ShaderLoader::Load(Shader::VertexShader, "Shaders/blinn-phong.vert");
    Shader fragmentShader = ShaderLoader::Load(Shader::FragmentShader, "Shaders/blinn-phong.frag");
    std::shared_ptr<ShaderProgram> shaderProgram = std::make_shared<ShaderProgram>();
    shaderProgram->Build(vertexShader, fragmentShader);

    // Filter out uniforms that are not material properties
    ShaderUniformCollection::NameSet filteredUniforms;
    filteredUniforms.insert("WorldMatrix");
    filteredUniforms.insert("ViewProjMatrix");
    filteredUniforms.insert("CameraPosition");

    // Create reference material
    std::shared_ptr<Material> material = std::make_shared<Material>(shaderProgram, filteredUniforms);

    // Setup function
    ShaderProgram::Location worldMatrixLocation = shaderProgram->GetUniformLocation("WorldMatrix");
    ShaderProgram::Location viewProjMatrixLocation = shaderProgram->GetUniformLocation("ViewProjMatrix");
    ShaderProgram::Location cameraPositionLocation = shaderProgram->GetUniformLocation("CameraPosition");
    material->SetShaderSetupFunction([=](ShaderProgram& shaderProgram)
        {
            shaderProgram.SetUniform(worldMatrixLocation, glm::scale(glm::vec3(1.0f)));
            shaderProgram.SetUniform(viewProjMatrixLocation, m_cameraController.GetCamera()->GetCamera()->GetViewProjectionMatrix());
            shaderProgram.SetUniform(cameraPositionLocation, m_cameraController.GetCamera()->GetCamera()->ExtractTranslation());
        });

    // Configure loader
    m_modelLoader = std::make_shared<ModelLoader>(material);

    // Create a new material copy for each submaterial
    m_modelLoader->SetCreateMaterials(true);

    // Flip vertically textures loaded by the model loader
    m_modelLoader->GetTexture2DLoader().SetFlipVertical(true);

    // Specify exact semantics, must match VBO fetching, see VertexSave in PathTracingRenderer
    m_modelLoader->SetSemanticAttribute(VertexAttribute::Semantic::Position);
    m_modelLoader->SetSemanticAttribute(VertexAttribute::Semantic::Normal);
    m_modelLoader->SetSemanticAttribute(VertexAttribute::Semantic::TexCoord0);

    // Material Arributes for rasterizer
    m_modelLoader->SetMaterialAttribute(VertexAttribute::Semantic::Position, "VertexPosition");
    m_modelLoader->SetMaterialAttribute(VertexAttribute::Semantic::Normal, "VertexNormal");
    m_modelLoader->SetMaterialAttribute(VertexAttribute::Semantic::TexCoord0, "VertexTexCoord");

    // Material Properties for rasterizer
    m_modelLoader->SetMaterialProperty(ModelLoader::MaterialProperty::DiffuseTexture, "DiffuseTexture");
    m_modelLoader->SetMaterialProperty(ModelLoader::MaterialProperty::NormalTexture, "NormalTexture");

    // Load models
    //m_models.push_back(loader.Load("Content/Models/BrickCubes/BrickCubes.gltf"));
    //m_models.push_back(loader.Load("Content/Models/Mill/Mill.gltf"));
    //m_models.push_back(loader.Load("Content/Models/Sponza/Sponza.gltf"));
    //m_models.push_back(loader.Load("Content/Models/Bunny.glb"));
    //m_models.push_back(loader.Load("Content/Models/Circle.glb"));
    //m_models.push_back(loader.Load("Content/Models/Cone.glb"));
    //m_models.push_back(loader.Load("Content/Models/Cube.glb"));
    //m_models.push_back(loader.Load("Content/Models/Dragon.glb"));
    //m_models.push_back(loader.Load("Content/Models/Floor.glb"));
    //m_models.push_back(loader.Load("Content/Models/Icosphere.glb"));
    //m_models.push_back(loader.Load("Content/Models/Plane.glb"));
    //m_models.push_back(loader.Load("Content/Models/Sphere.glb"));
    //m_models.push_back(loader.Load("Content/Models/Teapot.glb"));
    //m_models.push_back(loader.Load("Content/Models/Torus.glb"));

#if 0
    {
        // Specify material attributes
        Material::MaterialAttributes floorMaterialAttributes{ };
        floorMaterialAttributes.albedo = glm::vec3(1.0f, 1.0f, 1.0f) * 0.7f;
        floorMaterialAttributes.metallic = 1.0f;
        floorMaterialAttributes.roughness = 0.2f;

        Model floorModel = loader.Load("Content/Models/Floor.glb");
        
        // Set material attributes
        std::shared_ptr<Material> floorMaterial = std::make_shared<Material>(floorModel.GetMaterial(0));
        floorMaterial->SetMaterialAttributes(floorMaterialAttributes);
        floorModel.SetMaterial(0, floorMaterial);

        m_models.push_back(floorModel);
    }
    {
        // Specify material attributes
        Material::MaterialAttributes bunnyMaterialAttributes{ };
        //bunnyMaterialAttributes.emission = glm::vec3(1.0f, 0.73f, 0.05f) * 10.0f;
        //bunnyMaterialAttributes.albedo = glm::vec3(0.4f, 1.0f, 0.0f) * 0.8f;
        bunnyMaterialAttributes.albedo = glm::vec3(1.0f, 0.73f, 0.05f) * 0.8f;
        //bunnyMaterialAttributes.albedo = glm::vec3(0.6f, 0.0f, 1.0f) * 0.8f;
        //bunnyMaterialAttributes.specular = 0.05f;
        //bunnyMaterialAttributes.metallic = 1.0f;
        bunnyMaterialAttributes.roughness = 0.05f;
        //bunnyMaterialAttributes.sheenRoughness = 1.0f;
        //bunnyMaterialAttributes.sheenTint = 1.0f;
        //bunnyMaterialAttributes.clearcoat = 1.0f;
        //bunnyMaterialAttributes.clearcoatRoughness = 0.01f;
        //bunnyMaterialAttributes.subsurface = 2.0f;
        //bunnyMaterialAttributes.transmission = 1.0f;
        //bunnyMaterialAttributes.refraction = 1.5f;

        Model bunnyModel = loader.Load("Content/Models/Bunny.glb");
        //Model bunnyModel = loader.Load("Content/Models/Cone.glb");
        //Model bunnyModel = loader.Load("Content/Models/Dragon.glb");
        //Model bunnyModel = loader.Load("Content/Models/Sphere.glb");

        // Set material attributes
        std::shared_ptr<Material> bunnyMaterial = std::make_shared<Material>(bunnyModel.GetMaterial(0));
        bunnyMaterial->SetMaterialAttributes(bunnyMaterialAttributes);
        bunnyModel.SetMaterial(0, bunnyMaterial);

        m_models.push_back(bunnyModel);
    }
#endif
}

void PathTracingApplication::InitializeCamera()
{
    // Create the main camera
    std::shared_ptr<Camera> camera = std::make_shared<Camera>();
    //camera->SetViewMatrix(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0));
    camera->SetViewMatrix(glm::vec3(-7.5f, 5.0f, 0.0f), glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0));
    float fov = 1.57f;
    camera->SetPerspectiveProjectionMatrix(fov, GetMainWindow().GetAspectRatio(), 0.01f, 1000.0f);

    // Create a scene node for the camera
    std::shared_ptr<SceneCamera> sceneCamera = std::make_shared<SceneCamera>("camera", camera);

    // Set the camera scene node to be controlled by the camera controller
    m_cameraController.SetCamera(sceneCamera);
}

void PathTracingApplication::UpdateMaterial(const Camera& camera, int width, int height)
{
    // Path Tracing material
    {
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("ViewMatrix", camera.GetViewMatrix());
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("ProjMatrix", camera.GetProjectionMatrix());
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("InvProjMatrix", glm::inverse(camera.GetProjectionMatrix()));
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("FrameCount", m_frameCount);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("FrameDimensions", glm::vec2((float)width, (float)height));

        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("AntiAliasingEnabled", (unsigned int)m_AntiAliasingEnabled);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("FocalLength", m_focalLength);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("ApertureSize", m_apertureSize);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("ApertureShape", m_apertureShape);

        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("SpecularModifier", m_specularModifier);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("SpecularTintModifier", m_specularTintModifier);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("MetallicModifier", m_metallicModifier);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("RoughnessModifier", m_roughnessModifier);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("SubsurfaceModifier", m_subsurfaceModifier);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("AnisotropyModifier", m_anisotropyModifier);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("SheenRoughnessModifier", m_sheenRoughnessModifier);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("SheenTintModifier", m_sheenTintModifier);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("ClearcoatModifier", m_clearcoatModifier);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("ClearcoatRoughnessModifier", m_clearcoatRoughnessModifier);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("RefractionModifier", m_refractionModifier);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("TransmissionModifier", m_transmissionModifier);

        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("DebugValueA", m_debugValueA);
        m_pathTracingRenderer->GetPathTracingMaterial()->SetUniformValue("DebugValueB", m_debugValueB);
    }

    // Tone Mapping material
    {
        m_pathTracingRenderer->GetToneMappingMaterial()->SetUniformValue("Exposure", m_exposure);
        m_pathTracingRenderer->GetToneMappingMaterial()->SetUniformValue("DebugValueA", m_debugValueA);
        m_pathTracingRenderer->GetToneMappingMaterial()->SetUniformValue("DebugValueB", m_debugValueB);
    }
}

void PathTracingApplication::RenderGUI()
{
    m_imGui.BeginFrame();

    bool invalidate = false;
    bool refresh = false;

    if (auto window = m_imGui.UseWindow("Frame Data"))
    {
        float miliSeconds = GetDeltaTime() * 1000.0f;
        ImGui::Text(std::string("Frame Render Time (ms): " + std::to_string(miliSeconds)).c_str());

        ImGui::Text(std::string("Frame Count: " + std::to_string(m_frameCount)).c_str());
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        {
            ImGui::Text("Path Tracing Progress:");
            ImGui::SameLine();
            ImGui::ProgressBar((float)m_frameCount / (float)m_maxFrameCount);
        }

        if (m_denoiserEnabled)
        {
            ImGui::Spacing();

            ImGui::Text("Denoising Progress:");
            ImGui::SameLine();
            ImGui::ProgressBar(m_denoiseProgress);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        invalidate |= ImGui::InputInt("Max Frame Count", (int*)(&m_maxFrameCount));
                      ImGui::Checkbox("Denoiser Enabled", (bool*)(&m_denoiserEnabled));
        invalidate |= ImGui::Button("Invalidate Scene");
        invalidate |= ImGui::Checkbox("Use Rasterization as Preview", (bool*)(&m_shouldRasterizeAsPreview));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const char* hdriItems[] = { "Photostudio", "Chinese Garden", "Meadow", "Symmetrical Garden"};
        int currentHdriItem = static_cast<int>(m_currentPathTracingHdri);

        if (ImGui::Combo("Select HDRI", &currentHdriItem, hdriItems, IM_ARRAYSIZE(hdriItems)))
        {
            m_currentPathTracingHdri = static_cast<PathTracingHdri>(currentHdriItem);
            ProcessHdri(true);

            refresh = true;
        }

        const char* sceneItems[] = { "Test Scene 1", "TestScene 2" };
        int currentSceneItem = static_cast<int>(m_currentPathTracingScene);

        if (ImGui::Combo("Select Scene", &currentSceneItem, sceneItems, IM_ARRAYSIZE(sceneItems)))
        {
            m_currentPathTracingScene = static_cast<PathTracingScene>(currentSceneItem);
            ProcessScene();

            refresh = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        invalidate |= ImGui::Checkbox("Anti-Aliasing", (bool*)(&m_AntiAliasingEnabled));
        ImGui::SliderFloat("Exposure", (float*)(&m_exposure), 0.0f, 5.0f);

        ImGui::Spacing();

        invalidate |= ImGui::SliderFloat("Focal Length", (float*)(&m_focalLength), 0.0f, 15.0f);
        invalidate |= ImGui::SliderFloat("Aperture Size", (float*)(&m_apertureSize), 0.0f, 1.0f);
        invalidate |= ImGui::SliderFloat2("Aperture Shape", (float*)(&m_apertureShape), 0.0f, 1.0f);

        ImGui::Spacing();

        invalidate |= ImGui::SliderFloat("Specular Modifier", (float*)(&m_specularModifier), -1.0f, 1.0f);
        invalidate |= ImGui::SliderFloat("Specular Tint Modifier", (float*)(&m_specularTintModifier), -1.0f, 1.0f);
        invalidate |= ImGui::SliderFloat("Metallic Modifier", (float*)(&m_metallicModifier), -1.0f, 1.0f);
        invalidate |= ImGui::SliderFloat("Roughness Modifier", (float*)(&m_roughnessModifier), -1.0f, 1.0f);
        invalidate |= ImGui::SliderFloat("Subsurface Modifier", (float*)(&m_subsurfaceModifier), -1.0f, 1.0f);
        invalidate |= ImGui::SliderFloat("Anisotropy Modifier", (float*)(&m_anisotropyModifier), -1.0f, 1.0f);
        invalidate |= ImGui::SliderFloat("Sheen Roughness Modifier", (float*)(&m_sheenRoughnessModifier), -1.0f, 1.0f);
        invalidate |= ImGui::SliderFloat("Sheen Tint Modifier", (float*)(&m_sheenTintModifier), -1.0f, 1.0f);
        invalidate |= ImGui::SliderFloat("Clearcoat Modifier", (float*)(&m_clearcoatModifier), -1.0f, 1.0f);
        invalidate |= ImGui::SliderFloat("Clearcoat Roughness Modifier", (float*)(&m_clearcoatRoughnessModifier), -1.0f, 1.0f);
        invalidate |= ImGui::SliderFloat("Refraction Modifier", (float*)(&m_refractionModifier), -1.0f, 1.0f);
        invalidate |= ImGui::SliderFloat("Transmission Modifier", (float*)(&m_transmissionModifier), -1.0f, 1.0f);

        ImGui::Spacing();

        invalidate |= ImGui::SliderFloat("Debug Value A", (float*)(&m_debugValueA), 0.0f, 10.0f);
        invalidate |= ImGui::SliderFloat("Debug Value B", (float*)(&m_debugValueB), 0.0f, 10.0f);
    }

    if (refresh)
    {
        RefreshScene();
    }
    else if (invalidate)
    {
        InvalidateScene();
    }

    m_imGui.EndFrame();
}

void PathTracingApplication::ProcessHdri(bool processEnvironmentBuffer)
{
    switch (m_currentPathTracingHdri)
    {
    case PathTracingHdri::BrownPhotostudio:
        m_hdri = Texture2DLoader::LoadTextureShared("Content/HDRI/BrownPhotostudio.hdr", TextureObject::FormatRGB, TextureObject::InternalFormatRGB32F);
        break;
    case PathTracingHdri::ChineseGarden:
        m_hdri = Texture2DLoader::LoadTextureShared("Content/HDRI/ChineseGarden.hdr", TextureObject::FormatRGB, TextureObject::InternalFormatRGB32F);
        break;
    case PathTracingHdri::Meadow:
        m_hdri = Texture2DLoader::LoadTextureShared("Content/HDRI/Meadow.hdr", TextureObject::FormatRGB, TextureObject::InternalFormatRGB32F);
        break;
    case PathTracingHdri::SymmetricalGarden:
        m_hdri = Texture2DLoader::LoadTextureShared("Content/HDRI/SymmetricalGarden.hdr", TextureObject::FormatRGB, TextureObject::InternalFormatRGB32F);
        break;
    default:
        throw std::runtime_error("No such Path Tracing Hdri...");
    };

    // Once a Hdri has been chosen process the environment buffer for renderer
    // This function calls application
    if (processEnvironmentBuffer)
    {
        m_pathTracingRenderer->ProcessEnvironmentBuffer();
    }
}

void PathTracingApplication::ProcessScene()
{
    // Before we load any scene let's clear any previous added models
    m_pathTracingRenderer->ClearPathTracingModels();

    Scene scene;
    switch (m_currentPathTracingScene)
    {
    case PathTracingScene::TestScene1:
        scene = LoadTestScene1();
        break;
    case PathTracingScene::TestScene2:
        scene = LoadTestScene2();
        break;
    default:
        throw std::runtime_error("No such Path Tracing scene...");
    };

    // Add the scene nodes to the renderer
    PathTracingRendererSceneVisitor pathTracingRendererSceneVisitor(m_pathTracingRenderer);
    scene.AcceptVisitor(pathTracingRendererSceneVisitor);

    // Process buffers by renderer using the given models
    m_pathTracingRenderer->ProcessBuffers();

    int test = 0;
}

Scene PathTracingApplication::LoadTestScene1()
{
    std::shared_ptr<Model> bunnyModel = m_modelLoader->LoadShared("Content/Models/Bunny.glb");
    std::shared_ptr<Model> floorModel = m_modelLoader->LoadShared("Content/Models/Floor.glb");

    // Specify material attributes
    Material::MaterialAttributes bunnyMaterialAttributes{ };
    bunnyMaterialAttributes.albedo = glm::vec3(1.0f, 0.73f, 0.05f) * 0.8f;
    bunnyMaterialAttributes.roughness = 0.05f;

    // Set material attributes
    std::shared_ptr<Material> bunnyMaterial = std::make_shared<Material>(bunnyModel->GetMaterial(0));
    bunnyMaterial->SetMaterialAttributes(bunnyMaterialAttributes);
    bunnyModel->SetMaterial(0, bunnyMaterial);

    Scene testScene1;
    testScene1.AddSceneNode(std::make_shared<SceneModel>("bunny", bunnyModel));
    testScene1.AddSceneNode(std::make_shared<SceneModel>("floor", floorModel));

    return testScene1;
}

Scene PathTracingApplication::LoadTestScene2()
{
    std::shared_ptr<Model> dragonModel = m_modelLoader->LoadShared("Content/Models/Dragon.glb");
    std::shared_ptr<Model> floorModel = m_modelLoader->LoadShared("Content/Models/Floor.glb");

    Scene testScene2;
    testScene2.AddSceneNode(std::make_shared<SceneModel>("dragon", dragonModel));
    testScene2.AddSceneNode(std::make_shared<SceneModel>("floor", floorModel));

    return testScene2;
}
