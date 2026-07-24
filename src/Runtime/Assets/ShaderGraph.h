#pragma once

#include "API/RuntimeApi.h"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

enum class ShaderSourceMode : uint8_t { Code = 0, Graph = 1 };
enum class ShaderDomain : uint8_t { Graphics = 0, Surface = 1, Compute = 2 };
enum class ShaderShadingModel : uint8_t { Lit = 0, Unlit = 1 };
enum class ShaderSurfaceType : uint8_t { Opaque = 0, Masked = 1, Transparent = 2 };
enum class ShaderPass : uint8_t { Default = 0, GBuffer = 1, Forward = 2, Shadow = 3, Count = 4 };
enum class ShaderPropertyType : uint8_t { Float = 0, Vec2 = 1, Vec3 = 2, Color = 3, Bool = 4, Texture2D = 5 };
enum class ShaderGraphValueType : uint8_t { Any = 0, Float = 1, Vec2 = 2, Vec3 = 3, Vec4 = 4, Color = 5, Bool = 6 };

struct ShaderPropertyDesc {
    std::string id;
    std::string name;
    ShaderPropertyType type = ShaderPropertyType::Float;
    float defaultValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float minValue = 0.0f;
    float maxValue = 1.0f;
    bool hasRange = false;
    bool sRGB = false;
    std::string defaultTexture;
    uint32_t constantSlot = UINT32_MAX;
    uint32_t textureSlot = UINT32_MAX;
};

struct ShaderGraphNode {
    uint64_t id = 0;
    std::string type;
    float x = 0.0f;
    float y = 0.0f;
    std::string propertyId;
    std::vector<float> value;
    struct Pin {
        uint64_t id = 0;
        std::string name;
        std::string type = "Any";
        bool input = true;
        std::vector<float> defaultValue;
    };
    std::vector<Pin> pins;
};

struct ShaderGraphLink {
    uint64_t id = 0;
    uint64_t fromNode = 0;
    std::string fromPin;
    uint64_t fromPinId = 0;
    uint64_t toNode = 0;
    std::string toPin;
    uint64_t toPinId = 0;
};

struct ShaderGraph {
    static constexpr uint32_t kLegacyVersion = 1;
    static constexpr uint32_t kVersion = 2;
    uint32_t version = kVersion;
    std::vector<ShaderGraphNode> nodes;
    std::vector<ShaderGraphLink> links;
};

struct ShaderGraphDiagnostic {
    enum class Severity : uint8_t { Warning = 0, Error = 1 };
    Severity severity = Severity::Error;
    uint64_t nodeId = 0;
    std::string message;
    uint64_t pinId = 0;
    uint64_t linkId = 0;
    std::string code;
    ShaderPass pass = ShaderPass::Default;
    std::string backend;
    std::string stage;
    uint32_t line = 0;
    uint32_t column = 0;
};

struct ShaderGraphPinDefinition {
    const char* name = "";
    ShaderGraphValueType type = ShaderGraphValueType::Any;
    bool input = true;
    std::vector<float> defaultValue;
};

struct ShaderGraphNodeDefinition {
    const char* type = "";
    const char* displayName = "";
    const char* category = "";
    const char* keywords = "";
    std::vector<ShaderGraphPinDefinition> pins;
    std::vector<float> defaultValue;
    bool propertyReference = false;
    bool texturePropertyOnly = false;
    bool surfaceOutput = false;
};

MYENGINE_RUNTIME_API const char* ShaderPassName(ShaderPass pass);
MYENGINE_RUNTIME_API const char* ShaderPropertyTypeName(ShaderPropertyType type);
const char* ShaderGraphValueTypeName(ShaderGraphValueType type);
bool ShaderGraphValueTypeFromString(const std::string& value, ShaderGraphValueType& out);
MYENGINE_RUNTIME_API const std::vector<ShaderGraphNodeDefinition>& GetShaderGraphNodeLibrary();
MYENGINE_RUNTIME_API const ShaderGraphNodeDefinition* FindShaderGraphNodeDefinition(const std::string& type);
MYENGINE_RUNTIME_API ShaderGraphNode CreateShaderGraphNode(const std::string& type, uint64_t nodeId, uint64_t& nextPinId);
MYENGINE_RUNTIME_API bool ShaderGraphCanConnect(const ShaderGraphNode::Pin& from, const ShaderGraphNode::Pin& to);
bool ShaderPropertyTypeFromString(const std::string& value, ShaderPropertyType& out);
bool ParseShaderProperties(const nlohmann::json& value, std::vector<ShaderPropertyDesc>& out,
                           std::vector<ShaderGraphDiagnostic>* diagnostics = nullptr);
MYENGINE_RUNTIME_API bool ParseShaderGraph(const nlohmann::json& value, ShaderGraph& out,
                      std::vector<ShaderGraphDiagnostic>* diagnostics = nullptr);
MYENGINE_RUNTIME_API nlohmann::json SerializeShaderProperties(const std::vector<ShaderPropertyDesc>& properties);
MYENGINE_RUNTIME_API nlohmann::json SerializeShaderGraph(const ShaderGraph& graph);
MYENGINE_RUNTIME_API bool ShaderGraphUsesTime(const ShaderGraph& graph);
