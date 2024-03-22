#include "RenderPass.h"

#include "Renderer.h"
#include <cassert>

RenderPass::RenderPass(std::shared_ptr<const FramebufferObject> targetFramebuffer)
    : m_targetFramebuffer(targetFramebuffer)
    , m_renderer(nullptr)
{
}

RenderPass::~RenderPass()
{
}

std::shared_ptr<const FramebufferObject> RenderPass::GetTargetFramebuffer() const
{
    return m_targetFramebuffer;
}

void RenderPass::SetRenderer(Renderer* renderer)
{
    m_renderer = renderer;
}

Renderer& RenderPass::GetRenderer()
{
    assert(m_renderer);
    return *m_renderer;
}

const Renderer& RenderPass::GetRenderer() const
{
    assert(m_renderer);
    return *m_renderer;
}
