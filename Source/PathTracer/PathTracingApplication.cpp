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
#include "Geometry/Mesh.h"
#include "Geometry/ShaderStorageBufferObject.h"
#include <Asset/ModelLoader.h>
#include <Geometry/VertexFormat.h>
#include <iostream>

PathTracingApplication::PathTracingApplication()
    : Application(1024, 1024, "PathTracer")
    , m_frameCount(0)
    , m_maxFrameCount(25)
    , m_shouldRasterizeAsPreview(false)
    , m_shouldPathTrace(false)
    , m_shouldDenoise(false)
    , m_denoised(false)
    , m_denoiseProgress(0.0f)
    , m_denoiserEnabled(false)
    , m_renderer(GetDevice())
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
}

void PathTracingApplication::Initialize()
{
    Application::Initialize();

    // Initialize DearImGUI
    m_imGui.Initialize(GetMainWindow());

    InitializeHDRI();
    InitializeModel();
    InitializeCamera();
    InitializeFramebuffer();
    InitializeMaterials();
    InitializeRenderer();
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

    // Set renderer camera
    const Camera& camera = *m_cameraController.GetCamera()->GetCamera();
    m_renderer.SetCurrentCamera(camera);

    // Update the material properties
    glm::mat4 viewMatrix = camera.GetViewMatrix();
    m_pathTracingMaterial->SetUniformValue("ViewMatrix", viewMatrix);
    m_pathTracingMaterial->SetUniformValue("ProjMatrix", camera.GetProjectionMatrix());
    m_pathTracingMaterial->SetUniformValue("InvProjMatrix", glm::inverse(camera.GetProjectionMatrix()));
    m_pathTracingMaterial->SetUniformValue("FrameCount", m_frameCount);
    m_pathTracingMaterial->SetUniformValue("FrameDimensions", glm::vec2((float)width, (float)height));
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
        m_renderer.Render();
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

void PathTracingApplication::InitializeHDRI()
{
    Texture2DLoader textureLoader(TextureObject::FormatRGBA, TextureObject::InternalFormatRGBA32F);
    //m_HDRI = textureLoader.LoadShared("Content/HDRI/Meadow.hdr");
    m_HDRI = textureLoader.LoadShared("Content/HDRI/SymmetricalGarden.hdr");
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
    //m_models.push_back(loader.Load("Content/Models/Mill/Mill.gltf"));
    //m_models.push_back(loader.Load("Content/Models/Sponza/Sponza.gltf"));
    //m_models.push_back(loader.Load("Content/Models/Bunny.glb"));
    //m_models.push_back(loader.Load("Content/Models/Circle.glb"));
    //m_models.push_back(loader.Load("Content/Models/Cone.glb"));
    //m_models.push_back(loader.Load("Content/Models/Cube.glb"));
    //m_models.push_back(loader.Load("Content/Models/Dragon.glb"));
    //m_models.push_back(loader.Load("Content/Models/Icosphere.glb"));
    //m_models.push_back(loader.Load("Content/Models/Plane.glb"));
    //m_models.push_back(loader.Load("Content/Models/Sphere.glb"));
    //m_models.push_back(loader.Load("Content/Models/Teapot.glb"));
    //m_models.push_back(loader.Load("Content/Models/Torus.glb"));

    Model model = loader.Load("Content/Models/Bunny.glb");
    //Model model = loader.Load("Content/Models/sphere.glb");

    Material::MaterialAttributes bunnyMaterialAttributes{ };
    bunnyMaterialAttributes.baseColor = glm::vec3(252.0f / 255.0f, 186.0f / 255.0f, 3.0f / 255.0f);
    bunnyMaterialAttributes.roughness = 0.1f;
    bunnyMaterialAttributes.metallic = 1.0f;

    std::shared_ptr<Material> bunnyMaterial = std::make_shared<Material>(model.GetMaterial(0));
    bunnyMaterial->SetMaterialAttributes(bunnyMaterialAttributes);
    model.SetMaterial(0, bunnyMaterial);

    m_models.push_back(model);
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

void PathTracingApplication::InitializeFramebuffer()
{
    int width, height;
    GetMainWindow().GetDimensions(width, height);

    // Path Tracing Texture
    m_pathTracingTexture = std::make_shared<Texture2DObject>();
    m_pathTracingTexture->Bind();
    m_pathTracingTexture->SetImage(0, width, height, TextureObject::FormatRGBA, TextureObject::InternalFormat::InternalFormatRGBA32F);
    m_pathTracingTexture->SetParameter(TextureObject::ParameterEnum::MinFilter, GL_LINEAR);
    m_pathTracingTexture->SetParameter(TextureObject::ParameterEnum::MagFilter, GL_LINEAR);
    Texture2DObject::Unbind();

    // Path Tracing Framebuffer
    m_pathTracingFramebuffer = std::make_shared<FramebufferObject>();
    m_pathTracingFramebuffer->Bind();
    m_pathTracingFramebuffer->SetTexture(FramebufferObject::Target::Draw, FramebufferObject::Attachment::Color0, *m_pathTracingTexture);
    m_pathTracingFramebuffer->SetDrawBuffers(std::array<FramebufferObject::Attachment, 1>({ FramebufferObject::Attachment::Color0 }));
    FramebufferObject::Unbind();
}

void PathTracingApplication::InitializeMaterials()
{
    // Path Tracing Material
    {
        // Create material
        m_pathTracingMaterial = CreatePathTracingMaterial();

        // Initialize material uniforms
        // ...
    }

    // Copy Material
    {
        // Create material
        m_copyMaterial = CreateCopyMaterial();

        // Initialize material uniforms
        m_copyMaterial->SetUniformValue("SouceTexture", m_pathTracingTexture);
    }
}

void PathTracingApplication::InitializeRenderer()
{
    int width, height;
    GetMainWindow().GetDimensions(width, height);

    // Path Tracing render pass
    m_renderer.AddRenderPass(std::make_unique<PathTracingRenderPass>(width, height, this, m_pathTracingMaterial, m_pathTracingFramebuffer));

    // Copy render pass
    m_renderer.AddRenderPass(std::make_unique<PostFXRenderPass>(m_copyMaterial, m_renderer.GetDefaultFramebuffer()));
}

std::shared_ptr<Material> PathTracingApplication::CreatePathTracingMaterial()
{
    std::vector<const char*> computeShaderPaths;
    //computeShaderPaths.push_back("Shaders/version460.glsl"); // VS GLSL Extension won't work without a #version, so temporally disabled
    computeShaderPaths.push_back("Shaders/pathtracing.comp");
    Shader computeShader = ShaderLoader(Shader::ComputeShader).Load(computeShaderPaths);

    std::shared_ptr<ShaderProgram> shaderProgramPtr = std::make_shared<ShaderProgram>();
    shaderProgramPtr->Build(computeShader);

    ShaderUniformCollection::NameSet filteredUniforms;
    filteredUniforms.insert("imgOutput");

    // Create material
    std::shared_ptr<Material> material = std::make_shared<Material>(shaderProgramPtr, filteredUniforms);

    return material;
}

std::shared_ptr<Material> PathTracingApplication::CreateCopyMaterial()
{
    // TODO: Create MaterialLibrary class for common materials, such as this

    std::vector<const char*> vertexShaderPaths;
    vertexShaderPaths.push_back("Shaders/version460.glsl");
    vertexShaderPaths.push_back("Shaders/Renderer/fullscreen.vert");
    Shader vertexShader = ShaderLoader(Shader::VertexShader).Load(vertexShaderPaths);

    std::vector<const char*> fragmentShaderPaths;
    fragmentShaderPaths.push_back("Shaders/version460.glsl");
    fragmentShaderPaths.push_back("Shaders/Renderer/copy.frag");
    Shader fragmentShader = ShaderLoader(Shader::FragmentShader).Load(fragmentShaderPaths);

    std::shared_ptr<ShaderProgram> shaderProgramPtr = std::make_shared<ShaderProgram>();
    shaderProgramPtr->Build(vertexShader, fragmentShader);

    // Create material
    std::shared_ptr<Material> material = std::make_shared<Material>(shaderProgramPtr);

    return material;
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
        ImGui::Spacing();
        /* changed |= */ ImGui::Checkbox("Denoiser Enabled", (bool*)(&m_denoiserEnabled));
        ImGui::Spacing();
        changed |= ImGui::Button("Invalidate Scene");
        ImGui::Spacing();
        changed |= ImGui::Checkbox("Use Rasterization as Preview", (bool*)(&m_shouldRasterizeAsPreview));
    }

    if (changed)
    {
        InvalidateScene();
    }

    m_imGui.EndFrame();
}
