#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

class Scene;

class MYENGINE_RUNTIME_API DefaultSceneFactory {
public:
    static void PopulateIfEmpty(Scene& scene);
};
