#include "Assets/ShaderGraph.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>

namespace {
void Error(std::vector<ShaderGraphDiagnostic>* diagnostics, uint64_t nodeId, std::string message) {
    if (diagnostics)
        diagnostics->push_back({ShaderGraphDiagnostic::Severity::Error, nodeId, std::move(message)});
}

bool ReadFiniteNumber(const nlohmann::json& value, float& out) {
    if (!value.is_number())
        return false;
    out = value.get<float>();
    return std::isfinite(out);
}

ShaderGraphPinDefinition In(const char* name, ShaderGraphValueType type = ShaderGraphValueType::Any,
                            std::vector<float> defaults = {}) {
    return {name, type, true, std::move(defaults)};
}

ShaderGraphPinDefinition Out(const char* name = "Out", ShaderGraphValueType type = ShaderGraphValueType::Any) {
    return {name, type, false, {}};
}
} // namespace

const char* ShaderPassName(ShaderPass pass) {
    switch (pass) {
    case ShaderPass::Default:
        return "Default";
    case ShaderPass::GBuffer:
        return "GBuffer";
    case ShaderPass::Forward:
        return "Forward";
    case ShaderPass::Shadow:
        return "Shadow";
    default:
        return "Unknown";
    }
}

const char* ShaderPropertyTypeName(ShaderPropertyType type) {
    switch (type) {
    case ShaderPropertyType::Float:
        return "Float";
    case ShaderPropertyType::Vec2:
        return "Vec2";
    case ShaderPropertyType::Vec3:
        return "Vec3";
    case ShaderPropertyType::Color:
        return "Color";
    case ShaderPropertyType::Bool:
        return "Bool";
    case ShaderPropertyType::Texture2D:
        return "Texture2D";
    }
    return "Float";
}

const char* ShaderGraphValueTypeName(ShaderGraphValueType type) {
    switch (type) {
    case ShaderGraphValueType::Float:
        return "Float";
    case ShaderGraphValueType::Vec2:
        return "Vec2";
    case ShaderGraphValueType::Vec3:
        return "Vec3";
    case ShaderGraphValueType::Vec4:
        return "Vec4";
    case ShaderGraphValueType::Color:
        return "Color";
    case ShaderGraphValueType::Bool:
        return "Bool";
    default:
        return "Any";
    }
}

bool ShaderGraphValueTypeFromString(const std::string& value, ShaderGraphValueType& out) {
    if (value == "Float")
        out = ShaderGraphValueType::Float;
    else if (value == "Vec2")
        out = ShaderGraphValueType::Vec2;
    else if (value == "Vec3")
        out = ShaderGraphValueType::Vec3;
    else if (value == "Vec4")
        out = ShaderGraphValueType::Vec4;
    else if (value == "Color")
        out = ShaderGraphValueType::Color;
    else if (value == "Bool")
        out = ShaderGraphValueType::Bool;
    else if (value == "Any" || value == "Numeric")
        out = ShaderGraphValueType::Any;
    else
        return false;
    return true;
}

const std::vector<ShaderGraphNodeDefinition>& GetShaderGraphNodeLibrary() {
    static const std::vector<ShaderGraphNodeDefinition> nodes = {
        {"Float", "Float", "Constants", "scalar number", {Out("Out", ShaderGraphValueType::Float)}, {0.0f}},
        {"Vec2", "Vector 2", "Constants", "vector float2", {Out("Out", ShaderGraphValueType::Vec2)}, {0.0f, 0.0f}},
        {"Vec3",
         "Vector 3",
         "Constants",
         "vector float3",
         {Out("Out", ShaderGraphValueType::Vec3)},
         {0.0f, 0.0f, 0.0f}},
        {"Vec4",
         "Vector 4",
         "Constants",
         "vector float4",
         {Out("Out", ShaderGraphValueType::Vec4)},
         {0.0f, 0.0f, 0.0f, 0.0f}},
        {"Color",
         "Color",
         "Constants",
         "rgba tint",
         {Out("Out", ShaderGraphValueType::Color)},
         {1.0f, 1.0f, 1.0f, 1.0f}},
        {"Bool", "Boolean", "Constants", "bool switch", {Out("Out", ShaderGraphValueType::Bool)}, {0.0f}},
        {"Property", "Property", "Properties", "parameter blackboard", {Out()}, {}, true},
        {"UV0", "UV 0", "Input", "texture coordinate texcoord", {Out("Out", ShaderGraphValueType::Vec2)}},
        {"WorldPosition",
         "World Position",
         "Input",
         "position absolute world",
         {Out("Out", ShaderGraphValueType::Vec3)}},
        {"WorldNormal", "World Normal", "Input", "normal world", {Out("Out", ShaderGraphValueType::Vec3)}},
        {"Tangent", "World Tangent", "Input", "tangent world", {Out("Out", ShaderGraphValueType::Vec3)}},
        {"ViewDirection", "View Direction", "Input", "camera view", {Out("Out", ShaderGraphValueType::Vec3)}},
        {"Time", "Time", "Input", "seconds animation", {Out("Out", ShaderGraphValueType::Float)}},
        {"TextureSample",
         "Sample Texture 2D",
         "Texture",
         "sample rgba image",
         {In("UV", ShaderGraphValueType::Vec2), Out("RGBA", ShaderGraphValueType::Color),
          Out("R", ShaderGraphValueType::Float), Out("G", ShaderGraphValueType::Float),
          Out("B", ShaderGraphValueType::Float), Out("A", ShaderGraphValueType::Float)},
         {},
         true,
         true},
        {"NormalUnpack",
         "Unpack Normal",
         "Texture",
         "normal map tangent",
         {In("Value", ShaderGraphValueType::Color, {0.5f, 0.5f, 1.0f, 1.0f}), Out("Out", ShaderGraphValueType::Vec3)}},
        {"Add", "Add", "Math", "plus", {In("A"), In("B"), Out()}},
        {"Subtract", "Subtract", "Math", "minus", {In("A"), In("B"), Out()}},
        {"Multiply", "Multiply", "Math", "product", {In("A"), In("B"), Out()}},
        {"Divide", "Divide", "Math", "quotient", {In("A"), In("B", ShaderGraphValueType::Any, {1.0f}), Out()}},
        {"Min", "Minimum", "Math", "min", {In("A"), In("B"), Out()}},
        {"Max", "Maximum", "Math", "max", {In("A"), In("B"), Out()}},
        {"Abs", "Absolute", "Math", "absolute", {In("Value"), Out()}},
        {"Negate", "Negate", "Math", "negative", {In("Value"), Out()}},
        {"OneMinus", "One Minus", "Math", "invert", {In("Value"), Out()}},
        {"Power",
         "Power",
         "Math",
         "pow exponent",
         {In("Base"), In("Exponent", ShaderGraphValueType::Any, {1.0f}), Out()}},
        {"Sqrt", "Square Root", "Math", "sqrt", {In("Value"), Out()}},
        {"Lerp",
         "Lerp",
         "Math",
         "interpolate mix",
         {In("A"), In("B", ShaderGraphValueType::Any, {1.0f}), In("T", ShaderGraphValueType::Float, {0.5f}), Out()}},
        {"Clamp",
         "Clamp",
         "Math",
         "range",
         {In("Value"), In("Min", ShaderGraphValueType::Any, {0.0f}), In("Max", ShaderGraphValueType::Any, {1.0f}),
          Out()}},
        {"Saturate", "Saturate", "Math", "clamp 0 1", {In("Value"), Out()}},
        {"Step", "Step", "Math", "threshold", {In("Edge"), In("Value"), Out()}},
        {"Smoothstep",
         "Smooth Step",
         "Math",
         "smooth threshold",
         {In("Min"), In("Max", ShaderGraphValueType::Any, {1.0f}), In("Value"), Out()}},
        {"Remap",
         "Remap",
         "Math",
         "range map",
         {In("Value"), In("InMin"), In("InMax", ShaderGraphValueType::Any, {1.0f}), In("OutMin"),
          In("OutMax", ShaderGraphValueType::Any, {1.0f}), Out()}},
        {"Dot",
         "Dot Product",
         "Vector",
         "dot",
         {In("A", ShaderGraphValueType::Vec3), In("B", ShaderGraphValueType::Vec3),
          Out("Out", ShaderGraphValueType::Float)}},
        {"Cross",
         "Cross Product",
         "Vector",
         "cross",
         {In("A", ShaderGraphValueType::Vec3), In("B", ShaderGraphValueType::Vec3),
          Out("Out", ShaderGraphValueType::Vec3)}},
        {"Length", "Length", "Vector", "magnitude", {In("Value"), Out("Out", ShaderGraphValueType::Float)}},
        {"Distance", "Distance", "Vector", "distance", {In("A"), In("B"), Out("Out", ShaderGraphValueType::Float)}},
        {"Normalize",
         "Normalize",
         "Vector",
         "unit",
         {In("Value", ShaderGraphValueType::Vec3), Out("Out", ShaderGraphValueType::Vec3)}},
        {"Split",
         "Split",
         "Vector",
         "channels components",
         {In("Value"), Out("R", ShaderGraphValueType::Float), Out("G", ShaderGraphValueType::Float),
          Out("B", ShaderGraphValueType::Float), Out("A", ShaderGraphValueType::Float)}},
        {"Combine",
         "Combine",
         "Vector",
         "append components",
         {In("X", ShaderGraphValueType::Float), In("Y", ShaderGraphValueType::Float),
          In("Z", ShaderGraphValueType::Float), In("W", ShaderGraphValueType::Float, {1.0f}),
          Out("Out", ShaderGraphValueType::Vec4)}},
        {"Fresnel",
         "Fresnel",
         "Lighting",
         "rim facing",
         {In("Normal", ShaderGraphValueType::Vec3), In("ViewDirection", ShaderGraphValueType::Vec3),
          In("Power", ShaderGraphValueType::Float, {5.0f}), Out("Out", ShaderGraphValueType::Float)}},
        {"SurfaceOutputLit",
         "Lit Surface",
         "Output",
         "surface material",
         {In("BaseColor", ShaderGraphValueType::Color, {1, 1, 1, 1}),
          In("Normal", ShaderGraphValueType::Vec3, {0, 0, 1}), In("Metallic", ShaderGraphValueType::Float, {0}),
          In("Roughness", ShaderGraphValueType::Float, {0.5f}),
          In("AmbientOcclusion", ShaderGraphValueType::Float, {1}),
          In("Emissive", ShaderGraphValueType::Color, {0, 0, 0, 0}), In("Opacity", ShaderGraphValueType::Float, {1}),
          In("AlphaClip", ShaderGraphValueType::Float, {0.5f})},
         {},
         false,
         false,
         true},
        {"SurfaceOutputUnlit",
         "Unlit Surface",
         "Output",
         "surface material",
         {In("Color", ShaderGraphValueType::Color, {1, 1, 1, 1}),
          In("Emissive", ShaderGraphValueType::Color, {0, 0, 0, 0}), In("Opacity", ShaderGraphValueType::Float, {1}),
          In("AlphaClip", ShaderGraphValueType::Float, {0.5f})},
         {},
         false,
         false,
         true},
    };
    return nodes;
}

const ShaderGraphNodeDefinition* FindShaderGraphNodeDefinition(const std::string& type) {
    const auto& nodes = GetShaderGraphNodeLibrary();
    const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const auto& node) { return type == node.type; });
    return found == nodes.end() ? nullptr : &*found;
}

ShaderGraphNode CreateShaderGraphNode(const std::string& type, uint64_t nodeId, uint64_t& nextPinId) {
    ShaderGraphNode node;
    node.id = nodeId;
    node.type = type;
    if (const auto* definition = FindShaderGraphNodeDefinition(type)) {
        node.value = definition->defaultValue;
        for (const auto& pinDefinition : definition->pins) {
            ShaderGraphNode::Pin pin;
            pin.id = nextPinId++;
            pin.name = pinDefinition.name;
            pin.type = ShaderGraphValueTypeName(pinDefinition.type);
            pin.input = pinDefinition.input;
            pin.defaultValue = pinDefinition.defaultValue;
            node.pins.push_back(std::move(pin));
        }
    }
    return node;
}

bool ShaderGraphCanConnect(const ShaderGraphNode::Pin& from, const ShaderGraphNode::Pin& to) {
    if (from.input || !to.input)
        return false;
    ShaderGraphValueType source = ShaderGraphValueType::Any, destination = ShaderGraphValueType::Any;
    if (!ShaderGraphValueTypeFromString(from.type, source) || !ShaderGraphValueTypeFromString(to.type, destination))
        return false;
    if (source == ShaderGraphValueType::Any || destination == ShaderGraphValueType::Any || source == destination)
        return true;
    if ((source == ShaderGraphValueType::Color && destination == ShaderGraphValueType::Vec4) ||
        (source == ShaderGraphValueType::Vec4 && destination == ShaderGraphValueType::Color))
        return true;
    return source == ShaderGraphValueType::Float && destination != ShaderGraphValueType::Bool;
}

bool ShaderPropertyTypeFromString(const std::string& value, ShaderPropertyType& out) {
    if (value == "Float")
        out = ShaderPropertyType::Float;
    else if (value == "Vec2")
        out = ShaderPropertyType::Vec2;
    else if (value == "Vec3")
        out = ShaderPropertyType::Vec3;
    else if (value == "Color" || value == "Vec4")
        out = ShaderPropertyType::Color;
    else if (value == "Bool")
        out = ShaderPropertyType::Bool;
    else if (value == "Texture2D")
        out = ShaderPropertyType::Texture2D;
    else
        return false;
    return true;
}

bool ParseShaderProperties(const nlohmann::json& value, std::vector<ShaderPropertyDesc>& out,
                           std::vector<ShaderGraphDiagnostic>* diagnostics) {
    out.clear();
    if (!value.is_array()) {
        Error(diagnostics, 0, "shader properties must be an array");
        return false;
    }
    std::unordered_set<std::string> ids;
    uint32_t constantSlot = 0, textureSlot = 0;
    bool ok = true;
    for (const auto& item : value) {
        if (!item.is_object()) {
            Error(diagnostics, 0, "shader property must be an object");
            ok = false;
            continue;
        }
        ShaderPropertyDesc property;
        property.id = item.value("id", std::string{});
        property.name = item.value("name", property.id);
        const std::string type = item.value("type", std::string("Float"));
        if (property.id.empty() || !ids.insert(property.id).second) {
            Error(diagnostics, 0,
                  property.id.empty() ? "shader property id is empty" : "duplicate shader property id: " + property.id);
            ok = false;
            continue;
        }
        if (!ShaderPropertyTypeFromString(type, property.type)) {
            Error(diagnostics, 0, "unsupported shader property type: " + type);
            ok = false;
            continue;
        }
        property.sRGB = item.value("sRGB", property.type == ShaderPropertyType::Color);
        if (item.contains("range")) {
            const auto& range = item["range"];
            if (!range.is_array() || range.size() != 2 || !ReadFiniteNumber(range[0], property.minValue) ||
                !ReadFiniteNumber(range[1], property.maxValue) || property.minValue > property.maxValue) {
                Error(diagnostics, 0, "invalid range for shader property: " + property.id);
                ok = false;
            } else {
                property.hasRange = true;
            }
        }
        if (property.type == ShaderPropertyType::Texture2D) {
            property.defaultTexture = item.value("default", std::string{});
            property.textureSlot = textureSlot++;
        } else {
            const auto defaultIt = item.find("default");
            if (defaultIt != item.end()) {
                if (defaultIt->is_number() || defaultIt->is_boolean()) {
                    property.defaultValue[0] =
                        defaultIt->is_boolean() ? (defaultIt->get<bool>() ? 1.0f : 0.0f) : defaultIt->get<float>();
                } else if (defaultIt->is_array()) {
                    const size_t count = (std::min<size_t>)(4, defaultIt->size());
                    for (size_t index = 0; index < count; ++index) {
                        if (!ReadFiniteNumber((*defaultIt)[index], property.defaultValue[index])) {
                            Error(diagnostics, 0, "invalid default for shader property: " + property.id);
                            ok = false;
                            break;
                        }
                    }
                } else {
                    Error(diagnostics, 0, "invalid default for shader property: " + property.id);
                    ok = false;
                }
            }
            property.constantSlot = constantSlot++;
        }
        out.push_back(std::move(property));
    }
    if (constantSlot > 32) {
        Error(diagnostics, 0, "surface shader supports at most 32 numeric properties");
        ok = false;
    }
    if (textureSlot > 16) {
        Error(diagnostics, 0, "surface shader supports at most 16 texture properties");
        ok = false;
    }
    return ok;
}

bool ParseShaderGraph(const nlohmann::json& value, ShaderGraph& out, std::vector<ShaderGraphDiagnostic>* diagnostics) {
    out = {};
    if (!value.is_object()) {
        Error(diagnostics, 0, "shader graph must be an object");
        return false;
    }
    out.version = value.value("version", 0u);
    if (out.version != ShaderGraph::kLegacyVersion && out.version != ShaderGraph::kVersion) {
        Error(diagnostics, 0, "unsupported shader graph version");
        return false;
    }
    const auto nodes = value.find("nodes"), links = value.find("links");
    if (nodes == value.end() || !nodes->is_array() || links == value.end() || !links->is_array()) {
        Error(diagnostics, 0, "shader graph requires nodes and links arrays");
        return false;
    }
    bool ok = true;
    uint64_t nextGeneratedPinId = 1;
    for (const auto& item : *nodes)
        if (item.is_object() && item.contains("pins") && item["pins"].is_array())
            for (const auto& pinValue : item["pins"])
                if (pinValue.is_object())
                    nextGeneratedPinId =
                        (std::max)(nextGeneratedPinId, pinValue.value("id", uint64_t{0}) + uint64_t{1});
    for (const auto& item : *nodes) {
        if (!item.is_object()) {
            Error(diagnostics, 0, "graph node must be an object");
            ok = false;
            continue;
        }
        ShaderGraphNode node;
        node.id = item.value("id", uint64_t{0});
        node.type = item.value("type", std::string{});
        node.propertyId = item.value("property", std::string{});
        if (item.contains("position") && item["position"].is_array() && item["position"].size() == 2) {
            node.x = item["position"][0].get<float>();
            node.y = item["position"][1].get<float>();
        }
        if (item.contains("value") && item["value"].is_array()) {
            for (const auto& component : item["value"])
                node.value.push_back(component.get<float>());
        }
        if (item.contains("pins") && item["pins"].is_array()) {
            for (const auto& pinValue : item["pins"]) {
                if (!pinValue.is_object())
                    continue;
                ShaderGraphNode::Pin pin;
                pin.id = pinValue.value("id", uint64_t{0});
                pin.name = pinValue.value("name", std::string{});
                pin.type = pinValue.value("type", std::string("Any"));
                pin.input = pinValue.value("direction", std::string("Input")) != "Output";
                if (pinValue.contains("default") && pinValue["default"].is_array())
                    for (const auto& component : pinValue["default"])
                        if (component.is_number())
                            pin.defaultValue.push_back(component.get<float>());
                node.pins.push_back(std::move(pin));
            }
        }
        if (const auto* definition = FindShaderGraphNodeDefinition(node.type)) {
            if (node.pins.empty()) {
                ShaderGraphNode defaults = CreateShaderGraphNode(node.type, node.id, nextGeneratedPinId);
                node.pins = std::move(defaults.pins);
                if (node.value.empty())
                    node.value = std::move(defaults.value);
            } else {
                for (auto& pin : node.pins) {
                    const auto match =
                        std::find_if(definition->pins.begin(), definition->pins.end(), [&](const auto& candidate) {
                            return pin.name == candidate.name && pin.input == candidate.input;
                        });
                    if (match != definition->pins.end()) {
                        if (pin.type == "Any")
                            pin.type = ShaderGraphValueTypeName(match->type);
                        if (pin.input && pin.defaultValue.empty())
                            pin.defaultValue = match->defaultValue;
                    }
                }
                for (const auto& pinDefinition : definition->pins) {
                    const auto existing = std::find_if(node.pins.begin(), node.pins.end(), [&](const auto& pin) {
                        return pin.name == pinDefinition.name && pin.input == pinDefinition.input;
                    });
                    if (existing != node.pins.end())
                        continue;
                    ShaderGraphNode::Pin pin;
                    pin.id = nextGeneratedPinId++;
                    pin.name = pinDefinition.name;
                    pin.type = ShaderGraphValueTypeName(pinDefinition.type);
                    pin.input = pinDefinition.input;
                    pin.defaultValue = pinDefinition.defaultValue;
                    node.pins.push_back(std::move(pin));
                }
            }
        }
        if (node.id == 0 || node.type.empty()) {
            Error(diagnostics, node.id, "graph node requires non-zero id and type");
            ok = false;
        }
        out.nodes.push_back(std::move(node));
    }
    for (const auto& item : *links) {
        if (!item.is_object()) {
            Error(diagnostics, 0, "graph link must be an object");
            ok = false;
            continue;
        }
        ShaderGraphLink link;
        link.id = item.value("id", uint64_t{0});
        link.fromNode = item.value("fromNode", uint64_t{0});
        link.fromPin = item.value("fromPin", std::string("Out"));
        link.fromPinId = item.value("fromPinId", uint64_t{0});
        link.toNode = item.value("toNode", uint64_t{0});
        link.toPin = item.value("toPin", std::string{});
        link.toPinId = item.value("toPinId", uint64_t{0});
        if (link.id == 0 || link.fromNode == 0 || link.toNode == 0 || link.toPin.empty()) {
            Error(diagnostics, link.toNode, "graph link is incomplete");
            ok = false;
        }
        out.links.push_back(std::move(link));
    }
    // Legacy versions are normalized at load time. Version compatibility stays
    // at the asset boundary and every subsequent edit serializes graph v2.
    out.version = ShaderGraph::kVersion;
    return ok;
}

nlohmann::json SerializeShaderProperties(const std::vector<ShaderPropertyDesc>& properties) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& property : properties) {
        nlohmann::json item = {
            {"id", property.id}, {"name", property.name}, {"type", ShaderPropertyTypeName(property.type)}};
        if (property.type == ShaderPropertyType::Texture2D) {
            item["default"] = property.defaultTexture;
        } else {
            size_t count = property.type == ShaderPropertyType::Float || property.type == ShaderPropertyType::Bool ? 1
                           : property.type == ShaderPropertyType::Vec2                                             ? 2
                           : property.type == ShaderPropertyType::Vec3                                             ? 3
                                                                                                                   : 4;
            item["default"] = nlohmann::json::array();
            for (size_t i = 0; i < count; ++i)
                item["default"].push_back(property.defaultValue[i]);
        }
        if (property.hasRange)
            item["range"] = {property.minValue, property.maxValue};
        if (property.sRGB)
            item["sRGB"] = true;
        result.push_back(std::move(item));
    }
    return result;
}

nlohmann::json SerializeShaderGraph(const ShaderGraph& graph) {
    nlohmann::json result = {
        {"version", ShaderGraph::kVersion}, {"nodes", nlohmann::json::array()}, {"links", nlohmann::json::array()}};
    for (const auto& node : graph.nodes) {
        nlohmann::json item = {{"id", node.id}, {"type", node.type}, {"position", {node.x, node.y}}};
        if (!node.propertyId.empty())
            item["property"] = node.propertyId;
        if (!node.value.empty())
            item["value"] = node.value;
        if (!node.pins.empty()) {
            item["pins"] = nlohmann::json::array();
            for (const auto& pin : node.pins) {
                nlohmann::json serializedPin = {{"id", pin.id},
                                                {"name", pin.name},
                                                {"type", pin.type},
                                                {"direction", pin.input ? "Input" : "Output"}};
                if (pin.input && !pin.defaultValue.empty())
                    serializedPin["default"] = pin.defaultValue;
                item["pins"].push_back(std::move(serializedPin));
            }
        }
        result["nodes"].push_back(std::move(item));
    }
    for (const auto& link : graph.links) {
        result["links"].push_back({{"id", link.id},
                                   {"fromNode", link.fromNode},
                                   {"fromPin", link.fromPin},
                                   {"fromPinId", link.fromPinId},
                                   {"toNode", link.toNode},
                                   {"toPin", link.toPin},
                                   {"toPinId", link.toPinId}});
    }
    return result;
}

bool ShaderGraphUsesTime(const ShaderGraph& graph) {
    const ShaderGraphNode* output = nullptr;
    std::unordered_map<uint64_t, const ShaderGraphNode*> nodes;
    nodes.reserve(graph.nodes.size());
    for (const auto& node : graph.nodes) {
        nodes[node.id] = &node;
        if (node.type != "SurfaceOutputLit" && node.type != "SurfaceOutputUnlit")
            continue;
        if (output)
            return false;
        output = &node;
    }
    if (!output)
        return false;

    std::vector<uint64_t> pending{output->id};
    std::unordered_set<uint64_t> visited;
    while (!pending.empty()) {
        const uint64_t nodeId = pending.back();
        pending.pop_back();
        if (!visited.insert(nodeId).second)
            continue;
        const auto node = nodes.find(nodeId);
        if (node == nodes.end())
            continue;
        if (node->second->type == "Time")
            return true;
        for (const auto& link : graph.links)
            if (link.toNode == nodeId)
                pending.push_back(link.fromNode);
    }
    return false;
}
