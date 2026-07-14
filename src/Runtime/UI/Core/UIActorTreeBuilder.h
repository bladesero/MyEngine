#pragma once

#include <cstddef>
#include <string>

class Actor;
class UICanvasComponent;

class UIActorTreeBuilder {
public:
    static bool BuildDocument(const Actor& canvasActor, const UICanvasComponent& canvas, std::string& outRml,
                              std::string* error = nullptr);
    static std::size_t ComputeSignature(const Actor& canvasActor);
};
