#pragma once

#include "Scene/SceneVisitor.h"
#include <memory>

class PathTracingRenderer;
class SceneModel;
class Transform;

class PathTracingRendererSceneVisitor : public SceneVisitor
{
public:
    PathTracingRendererSceneVisitor(std::shared_ptr<PathTracingRenderer> pathTracingRenderer);

    void VisitModel(SceneModel& sceneModel) override;

private:
    std::shared_ptr<PathTracingRenderer> m_pathTracingRenderer;
};