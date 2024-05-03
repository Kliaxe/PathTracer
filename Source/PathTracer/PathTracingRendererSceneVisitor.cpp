#include "PathTracingRendererSceneVisitor.h"

#include "PathTracingRenderer.h"
#include "Scene/SceneModel.h"
#include "Scene/Transform.h"

PathTracingRendererSceneVisitor::PathTracingRendererSceneVisitor(std::shared_ptr<PathTracingRenderer> pathTracingRenderer) : m_pathTracingRenderer(pathTracingRenderer)
{
}

void PathTracingRendererSceneVisitor::VisitModel(SceneModel& sceneModel)
{
    assert(sceneModel.GetTransform());
    m_pathTracingRenderer->AddPathTracingModel(*sceneModel.GetModel(), sceneModel.GetTransform()->GetTransformMatrix());
}
