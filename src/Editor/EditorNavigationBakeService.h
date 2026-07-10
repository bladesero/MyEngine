#pragma once

class EditorContext;
class Scene;

class EditorNavigationBakeService {
public:
    bool Bake(EditorContext& context, Scene& scene) const;
};
