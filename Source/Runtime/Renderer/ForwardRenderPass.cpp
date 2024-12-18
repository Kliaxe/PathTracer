#include "ForwardRenderPass.h"

#include "Camera/Camera.h"
#include "Shader/Material.h"
#include "Geometry/VertexArrayObject.h"
#include "Renderer.h"

ForwardRenderPass::ForwardRenderPass()
    : ForwardRenderPass(0)
{
}

ForwardRenderPass::ForwardRenderPass(int drawcallCollectionIndex)
    : m_drawcallCollectionIndex(drawcallCollectionIndex)
{
}

void ForwardRenderPass::Render()
{
    Renderer& renderer = GetRenderer();

    const auto& lights = renderer.GetLights();
    const auto& drawcallCollection = renderer.GetDrawcalls(m_drawcallCollectionIndex);

    // for all drawcalls
    for (const Renderer::DrawcallInfo& drawcallInfo : drawcallCollection)
    {
        // Prepare drawcall states
        renderer.PrepareDrawcall(drawcallInfo);

        std::shared_ptr<const ShaderProgram> shaderProgram = drawcallInfo.material.GetShaderProgram();

        //for all lights
        bool first = true;
        unsigned int lightIndex = 0;
        while (renderer.UpdateLights(shaderProgram, lights, lightIndex))
        {
            // Set the renderstates
            renderer.SetLightingRenderStates(first);

            // Draw
            drawcallInfo.drawcall.Draw();

            first = false;
        }
    }
}
