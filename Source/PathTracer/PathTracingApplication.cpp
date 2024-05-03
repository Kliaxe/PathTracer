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

#include "Geometry/Mesh.h"
#include "Geometry/ShaderStorageBufferObject.h"
#include <Asset/ModelLoader.h>
#include <Geometry/VertexFormat.h>
#include <iostream>
#include "PathTracingRenderer.h"

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

    // Initialize application specifics
    InitializeHdri();
    InitializeModel();
    InitializeCamera();
    
    // Initialize PathTracing Renderer
    m_pathTracingRenderer->Initialize();
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
        for (Model model : m_models)
        {
            model.Draw();
        }
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
    m_frameCount = 1;

    // If scene was dirty, we should reset the denoised state as well
    m_denoiseProgress = 0.0f;
    m_denoised = false;
}

void PathTracingApplication::InitializeHdri()
{
    m_hdri = Texture2DLoader::LoadTextureShared("Content/HDRI/BrownPhotostudio.hdr", TextureObject::FormatRGB, TextureObject::InternalFormatRGB32F);
    //m_hdri = Texture2DLoader::LoadTextureShared("Content/HDRI/ChineseGarden.hdr", TextureObject::FormatRGB, TextureObject::InternalFormatRGB32F);
    //m_hdri = Texture2DLoader::LoadTextureShared("Content/HDRI/Meadow.hdr", TextureObject::FormatRGB, TextureObject::InternalFormatRGB32F);
    //m_hdri = Texture2DLoader::LoadTextureShared("Content/HDRI/SymmetricalGarden.hdr", TextureObject::FormatRGB, TextureObject::InternalFormatRGB32F);
}

void PathTracingApplication::InitializeModel()
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
    ModelLoader loader(material);

    // Create a new material copy for each submaterial
    loader.SetCreateMaterials(true);

    // Flip vertically textures loaded by the model loader
    loader.GetTexture2DLoader().SetFlipVertical(true);

    // Material Arributes
    loader.SetMaterialAttribute(VertexAttribute::Semantic::Position, "VertexPosition");
    loader.SetMaterialAttribute(VertexAttribute::Semantic::Normal, "VertexNormal");
    loader.SetMaterialAttribute(VertexAttribute::Semantic::TexCoord0, "VertexTexCoord");

    // Material Properties
    loader.SetMaterialProperty(ModelLoader::MaterialProperty::DiffuseTexture, "DiffuseTexture");
    loader.SetMaterialProperty(ModelLoader::MaterialProperty::NormalTexture, "NormalTexture");

    // Specify exact semantics, must match VBO fetching
    loader.SetSemanticAttribute(VertexAttribute::Semantic::Position);
    loader.SetSemanticAttribute(VertexAttribute::Semantic::Normal);
    loader.SetSemanticAttribute(VertexAttribute::Semantic::TexCoord0);

    // Load models
    //m_models.push_back(loader.Load("Content/Models/BrickCubes/BrickCubes.gltf"));
    m_models.push_back(loader.Load("Content/Models/Mill/Mill.gltf"));
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

    bool changed = false;

    if (auto window = m_imGui.UseWindow("Frame Data"))
    {
        float milliSeconds = GetDeltaTime() * 1000.0f;
        ImGui::Text(std::string("Frame Render Time (ms): " + std::to_string(milliSeconds)).c_str());

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

        changed |= ImGui::InputInt("Max Frame Count", (int*)(&m_maxFrameCount));
        //ImGui::Spacing();
        ImGui::Checkbox("Denoiser Enabled", (bool*)(&m_denoiserEnabled));
        //ImGui::Spacing();
        changed |= ImGui::Button("Invalidate Scene");
        //ImGui::Spacing();
        changed |= ImGui::Checkbox("Use Rasterization as Preview", (bool*)(&m_shouldRasterizeAsPreview));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        changed |= ImGui::Checkbox("Anti-Aliasing", (bool*)(&m_AntiAliasingEnabled));
        ImGui::SliderFloat("Exposure", (float*)(&m_exposure), 0.0f, 5.0f);

        ImGui::Spacing();

        changed |= ImGui::SliderFloat("Focal Length", (float*)(&m_focalLength), 0.0f, 15.0f);
        changed |= ImGui::SliderFloat("Aperture Size", (float*)(&m_apertureSize), 0.0f, 1.0f);
        changed |= ImGui::SliderFloat2("Aperture Shape", (float*)(&m_apertureShape), 0.0f, 1.0f);

        ImGui::Spacing();

        changed |= ImGui::SliderFloat("Debug Value A", (float*)(&m_debugValueA), 0.0f, 10.0f);
        changed |= ImGui::SliderFloat("Debug Value B", (float*)(&m_debugValueB), 0.0f, 10.0f);
    }

    if (changed)
    {
        InvalidateScene();
    }

    m_imGui.EndFrame();
}
