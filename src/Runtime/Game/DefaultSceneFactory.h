#pragma once

class Scene;

class DefaultSceneFactory {
public:
    static void PopulateIfEmpty(Scene& scene);
};
