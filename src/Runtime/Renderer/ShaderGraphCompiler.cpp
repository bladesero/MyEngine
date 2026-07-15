#include "Renderer/ShaderGraphCompiler.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {
using Diagnostic = ShaderGraphDiagnostic;

std::string Sanitize(std::string value) {
    for (char& c : value)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            c = '_';
    if (value.empty() || std::isdigit(static_cast<unsigned char>(value.front())))
        value.insert(value.begin(), '_');
    return value;
}

std::string FloatLiteral(float value) {
    std::ostringstream out;
    out << std::setprecision(9) << value;
    std::string result = out.str();
    if (result.find_first_of(".eE") == std::string::npos)
        result += ".0";
    return result + "f";
}

std::string Literal(const std::vector<float>& values) {
    float v[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < (std::min<size_t>)(values.size(), 4); ++i)
        v[i] = values[i];
    if (values.size() <= 1)
        return "(" + FloatLiteral(v[0]) + ").xxxx";
    return "float4(" + FloatLiteral(v[0]) + ", " + FloatLiteral(v[1]) + ", " + FloatLiteral(v[2]) + ", " +
           FloatLiteral(v[3]) + ")";
}

struct Generator {
    const ShaderGraph& graph;
    const std::vector<ShaderPropertyDesc>& properties;
    std::unordered_map<uint64_t, const ShaderGraphNode*> nodes;
    std::unordered_map<std::string, const ShaderGraphLink*> inputs;
    std::unordered_map<std::string, std::string> cache;
    std::vector<std::string> statements;

    const ShaderPropertyDesc* Property(const std::string& id) const {
        for (const auto& property : properties)
            if (property.id == id)
                return &property;
        return nullptr;
    }

    std::string InputKey(uint64_t node, const std::string& pin) const { return std::to_string(node) + ":" + pin; }

    std::string Input(uint64_t node, const std::string& pin, const std::string& fallback) {
        const auto found = inputs.find(InputKey(node, pin));
        if (found != inputs.end())
            return Expression(found->second->fromNode, found->second->fromPin);
        const auto foundNode = nodes.find(node);
        if (foundNode != nodes.end())
            for (const auto& candidate : foundNode->second->pins)
                if (candidate.input && candidate.name == pin && !candidate.defaultValue.empty())
                    return Literal(candidate.defaultValue);
        return fallback;
    }

    std::string Expression(uint64_t nodeId, const std::string& outputPin = "Out") {
        const std::string cacheKey = std::to_string(nodeId) + ":" + outputPin;
        if (auto found = cache.find(cacheKey); found != cache.end())
            return found->second;
        const auto foundNode = nodes.find(nodeId);
        if (foundNode == nodes.end())
            return "(0.0f).xxxx";
        const ShaderGraphNode& node = *foundNode->second;
        std::string result;
        if (node.type == "Float" || node.type == "Vec2" || node.type == "Vec3" || node.type == "Vec4" ||
            node.type == "Color" || node.type == "Bool") {
            result = Literal(node.value);
        } else if (node.type == "Property") {
            const auto* property = Property(node.propertyId);
            result = property && property->constantSlot != UINT32_MAX
                         ? "g_MaterialValues[" + std::to_string(property->constantSlot) + "]"
                         : "(0.0f).xxxx";
        } else if (node.type == "UV0") {
            result = "float4(input.uv, 0.0f, 0.0f)";
        } else if (node.type == "WorldPosition") {
            result = "float4(input.worldPosition, 1.0f)";
        } else if (node.type == "WorldNormal") {
            result = "float4(input.worldNormal, 0.0f)";
        } else if (node.type == "Tangent") {
            result = "float4(input.worldTangent, 0.0f)";
        } else if (node.type == "ViewDirection") {
            result = "float4(input.viewDirection, 0.0f)";
        } else if (node.type == "Time") {
            result = "g_GraphTime.xxxx";
        } else if (node.type == "TextureSample") {
            const auto* property = Property(node.propertyId);
            const std::string name = property ? Sanitize(property->id) : "Missing";
            result = "g_Texture_" + name + ".Sample(g_Sampler_" + name + ", " +
                     Input(node.id, "UV", "float4(input.uv, 0.0f, 0.0f)") + ".xy)";
            if (outputPin == "R")
                result = "(" + result + ").rrrr";
            else if (outputPin == "G")
                result = "(" + result + ").gggg";
            else if (outputPin == "B")
                result = "(" + result + ").bbbb";
            else if (outputPin == "A")
                result = "(" + result + ").aaaa";
        } else if (node.type == "Add" || node.type == "Subtract" || node.type == "Multiply" || node.type == "Divide") {
            const char* op = node.type == "Add"        ? "+"
                             : node.type == "Subtract" ? "-"
                             : node.type == "Multiply" ? "*"
                                                       : "/";
            result = "(" + Input(node.id, "A", "(0.0f).xxxx") + " " + op + " " +
                     (node.type == "Divide" ? "max(abs(" + Input(node.id, "B", "(1.0f).xxxx") + "), (1e-6f).xxxx)"
                                            : Input(node.id, "B", "(0.0f).xxxx")) +
                     ")";
        } else if (node.type == "Lerp") {
            result = "lerp(" + Input(node.id, "A", "(0.0f).xxxx") + ", " + Input(node.id, "B", "(1.0f).xxxx") + ", " +
                     Input(node.id, "T", "(0.5f).xxxx") + ")";
        } else if (node.type == "Clamp") {
            result = "clamp(" + Input(node.id, "Value", "(0.0f).xxxx") + ", " + Input(node.id, "Min", "(0.0f).xxxx") +
                     ", " + Input(node.id, "Max", "(1.0f).xxxx") + ")";
        } else if (node.type == "Saturate") {
            result = "saturate(" + Input(node.id, "Value", "(0.0f).xxxx") + ")";
        } else if (node.type == "Min" || node.type == "Max") {
            result = (node.type == "Min" ? "min(" : "max(") + Input(node.id, "A", "(0.0f).xxxx") + ", " +
                     Input(node.id, "B", "(0.0f).xxxx") + ")";
        } else if (node.type == "Abs") {
            result = "abs(" + Input(node.id, "Value", "(0.0f).xxxx") + ")";
        } else if (node.type == "Negate") {
            result = "-(" + Input(node.id, "Value", "(0.0f).xxxx") + ")";
        } else if (node.type == "OneMinus") {
            result = "((1.0f).xxxx - " + Input(node.id, "Value", "(0.0f).xxxx") + ")";
        } else if (node.type == "Power") {
            result = "pow(max(" + Input(node.id, "Base", "(0.0f).xxxx") + ", (0.0f).xxxx), " +
                     Input(node.id, "Exponent", "(1.0f).xxxx") + ")";
        } else if (node.type == "Sqrt") {
            result = "sqrt(max(" + Input(node.id, "Value", "(0.0f).xxxx") + ", (0.0f).xxxx))";
        } else if (node.type == "Step") {
            result =
                "step(" + Input(node.id, "Edge", "(0.0f).xxxx") + ", " + Input(node.id, "Value", "(0.0f).xxxx") + ")";
        } else if (node.type == "Smoothstep") {
            result = "smoothstep(" + Input(node.id, "Min", "(0.0f).xxxx") + ", " +
                     Input(node.id, "Max", "(1.0f).xxxx") + ", " + Input(node.id, "Value", "(0.0f).xxxx") + ")";
        } else if (node.type == "Remap") {
            const std::string value = Input(node.id, "Value", "(0.0f).xxxx");
            const std::string inMin = Input(node.id, "InMin", "(0.0f).xxxx");
            const std::string inMax = Input(node.id, "InMax", "(1.0f).xxxx");
            const std::string outMin = Input(node.id, "OutMin", "(0.0f).xxxx");
            const std::string outMax = Input(node.id, "OutMax", "(1.0f).xxxx");
            result = "(" + outMin + " + (" + value + " - " + inMin + ") / max(abs(" + inMax + " - " + inMin +
                     "), (1e-6f).xxxx) * (" + outMax + " - " + outMin + "))";
        } else if (node.type == "Dot") {
            result = "dot(" + Input(node.id, "A", "(0.0f).xxxx") + ".xyz, " + Input(node.id, "B", "(0.0f).xxxx") +
                     ".xyz).xxxx";
        } else if (node.type == "Cross") {
            result = "float4(cross(" + Input(node.id, "A", "float4(1,0,0,0)") + ".xyz, " +
                     Input(node.id, "B", "float4(0,1,0,0)") + ".xyz), 0.0f)";
        } else if (node.type == "Length") {
            result = "length(" + Input(node.id, "Value", "(0.0f).xxxx") + ").xxxx";
        } else if (node.type == "Distance") {
            result =
                "distance(" + Input(node.id, "A", "(0.0f).xxxx") + ", " + Input(node.id, "B", "(0.0f).xxxx") + ").xxxx";
        } else if (node.type == "Normalize") {
            result = "float4(normalize(" + Input(node.id, "Value", "float4(0,0,1,0)") + ".xyz), 0.0f)";
        } else if (node.type == "Split") {
            const char component = outputPin == "G" || outputPin == "Y"   ? 'y'
                                   : outputPin == "B" || outputPin == "Z" ? 'z'
                                   : outputPin == "A" || outputPin == "W" ? 'w'
                                                                          : 'x';
            result = "(" + Input(node.id, "Value", "(0.0f).xxxx") + ")." + std::string(1, component) + ".xxxx";
        } else if (node.type == "Combine") {
            result = "float4(" + Input(node.id, "X", "(0.0f).xxxx") + ".x, " + Input(node.id, "Y", "(0.0f).xxxx") +
                     ".x, " + Input(node.id, "Z", "(0.0f).xxxx") + ".x, " + Input(node.id, "W", "(1.0f).xxxx") + ".x)";
        } else if (node.type == "NormalUnpack") {
            result =
                "float4(normalize(" + Input(node.id, "Value", "float4(0.5f,0.5f,1,1)") + ".xyz * 2.0f - 1.0f), 0.0f)";
        } else if (node.type == "Fresnel") {
            result = "pow(1.0f - saturate(dot(normalize(" + Input(node.id, "Normal", "float4(input.worldNormal,0)") +
                     ".xyz), normalize(" + Input(node.id, "ViewDirection", "float4(input.viewDirection,0)") +
                     ".xyz))), " + Input(node.id, "Power", "(5.0f).xxxx") + ".x).xxxx";
        } else {
            result = "(0.0f).xxxx";
        }
        const std::string variable = "graph_n" + std::to_string(nodeId) + "_" + Sanitize(outputPin);
        statements.push_back("    float4 " + variable + " = " + result + ";\n");
        cache[cacheKey] = variable;
        return variable;
    }
};

std::string TextureDeclarations(const std::vector<ShaderPropertyDesc>& properties) {
    std::ostringstream out;
    uint32_t slot = 0;
    for (const auto& property : properties) {
        if (property.type != ShaderPropertyType::Texture2D)
            continue;
        const std::string name = Sanitize(property.id);
        out << "Texture2D g_Texture_" << name << " : register(t" << slot << ");\n";
        out << "SamplerState g_Sampler_" << name << " : register(s" << slot << ");\n";
        ++slot;
    }
    return out.str();
}

const ShaderGraphNode* FindOutput(const ShaderGraph& graph) {
    for (const auto& node : graph.nodes)
        if (node.type == "SurfaceOutputLit" || node.type == "SurfaceOutputUnlit")
            return &node;
    return nullptr;
}

std::string BuildSurfaceEvaluation(Generator& generator, const ShaderGraphNode& output,
                                   ShaderShadingModel shadingModel) {
    const bool unlit = shadingModel == ShaderShadingModel::Unlit || output.type == "SurfaceOutputUnlit";
    const std::string basePin = unlit ? "Color" : "BaseColor";
    const std::string baseColor = generator.Input(output.id, basePin, "(1.0f).xxxx");
    const std::string normal = generator.Input(output.id, "Normal", "float4(0,0,1,0)");
    const std::string metallic = generator.Input(output.id, "Metallic", "(0.0f).xxxx");
    const std::string roughness = generator.Input(output.id, "Roughness", "(0.5f).xxxx");
    const std::string ao = generator.Input(output.id, "AmbientOcclusion", "(1.0f).xxxx");
    const std::string emissive = generator.Input(output.id, "Emissive", "(0.0f).xxxx");
    const std::string opacity = generator.Input(output.id, "Opacity", "(1.0f).xxxx");
    const std::string alphaClip = generator.Input(output.id, "AlphaClip", "(0.5f).xxxx");
    std::ostringstream out;
    out << "SurfaceData EvaluateSurface(GraphInput input)\n{\n";
    for (const auto& statement : generator.statements)
        out << statement;
    out << "    SurfaceData s;\n";
    out << "    s.baseColor = (" << baseColor << ").rgb;\n";
    out << "    s.normalTS = normalize((" << normal << ").xyz);\n";
    out << "    s.metallic = saturate((" << metallic << ").x);\n";
    out << "    s.roughness = clamp((" << roughness << ").x, 0.04f, 1.0f);\n";
    out << "    s.ao = max((" << ao << ").x, 0.0f);\n";
    out << "    s.emissive = (" << emissive << ").rgb;\n";
    out << "    s.opacity = saturate((" << opacity << ").x);\n";
    out << "    s.alphaClip = saturate((" << alphaClip << ").x);\n";
    out << "    return s;\n}\n";
    return out.str();
}

std::string CommonTypes() {
    return R"(
struct GraphInput { float2 uv; float3 worldPosition; float3 worldNormal; float3 worldTangent; float3 viewDirection; };
struct SurfaceData { float3 baseColor; float3 normalTS; float metallic; float roughness; float ao; float3 emissive; float opacity; float alphaClip; };
)";
}

std::string MeshVertexTypes() {
    return R"(
struct VSIn { float3 pos : POSITION; float3 normal : NORMAL; float3 tangent : TANGENT; float2 uv : TEXCOORD0; float4 joints : BLENDINDICES; float4 weights : BLENDWEIGHT; float4 color : COLOR0; };
struct VSOut { float4 pos : SV_POSITION; float3 normalW : NORMAL; float3 tangentW : TANGENT; float2 uv : TEXCOORD0; float3 worldPos : TEXCOORD1; float4 color : COLOR0; };
VSOut VSMain(VSIn v) {
    VSOut o; float4 localPosition=float4(v.pos,1); float3 localNormal=v.normal; float3 localTangent=v.tangent;
    if (g_SkinInfo.x > 0.5f) { row_major float4x4 skin=g_BoneMatrices[(uint)v.joints.x]*v.weights.x+g_BoneMatrices[(uint)v.joints.y]*v.weights.y+g_BoneMatrices[(uint)v.joints.z]*v.weights.z+g_BoneMatrices[(uint)v.joints.w]*v.weights.w; localPosition=mul(localPosition,skin); localNormal=mul(float4(localNormal,0),skin).xyz; localTangent=mul(float4(localTangent,0),skin).xyz; }
    float4 worldPos=mul(localPosition,g_World); o.pos=mul(worldPos,g_ViewProj); o.normalW=normalize(mul(float4(localNormal,0),g_NormalMatrix).xyz); o.tangentW=normalize(mul(float4(localTangent,0),g_NormalMatrix).xyz); o.uv=v.uv; o.worldPos=worldPos.xyz; o.color=v.color; return o;
}
)";
}

std::string GenerateGBuffer(Generator& generator, const ShaderGraphNode& output, ShaderShadingModel model,
                            ShaderSurfaceType surface) {
    std::ostringstream hlsl;
    hlsl
        << R"(cbuffer GraphPerDraw : register(b0) { row_major float4x4 g_ViewProj; row_major float4x4 g_World; row_major float4x4 g_BoneMatrices[128]; float4 g_SkinInfo; row_major float4x4 g_NormalMatrix; float4 g_MaterialValues[32]; float4 g_GraphTime; };
)";
    hlsl << TextureDeclarations(generator.properties) << CommonTypes() << MeshVertexTypes();
    hlsl << BuildSurfaceEvaluation(generator, output, model);
    hlsl << R"(
struct PSOut { float4 albedo:SV_Target0; float4 normal:SV_Target1; float4 material:SV_Target2; float4 emissive:SV_Target3; };
PSOut PSMain(VSOut p) { GraphInput i; i.uv=p.uv; i.worldPosition=p.worldPos; i.worldNormal=normalize(p.normalW); i.worldTangent=normalize(p.tangentW); i.viewDirection=normalize(g_GraphTime.yzw-p.worldPos); SurfaceData s=EvaluateSurface(i);
)";
    if (surface == ShaderSurfaceType::Masked)
        hlsl << "    if (s.opacity < s.alphaClip) discard;\n";
    hlsl
        << R"(    float3 N=normalize(p.normalW); float3 T=normalize(p.tangentW-N*dot(p.tangentW,N)); float3 B=normalize(cross(N,T)); N=normalize(s.normalTS.x*T+s.normalTS.y*B+s.normalTS.z*N);
    PSOut o; o.albedo=float4(max(s.baseColor*p.color.rgb,0),s.opacity*p.color.a); o.normal=float4(N*0.5f+0.5f,1); o.material=float4(s.metallic,s.roughness,s.ao,0); o.emissive=float4(s.emissive,0); return o; }
)";
    return hlsl.str();
}

std::string GenerateForward(Generator& generator, const ShaderGraphNode& output, ShaderShadingModel model,
                            ShaderSurfaceType surface) {
    std::ostringstream hlsl;
    hlsl
        << R"(cbuffer GraphPerDraw : register(b0) { row_major float4x4 g_ViewProj; row_major float4x4 g_World; float4 g_LightDirection; float4 g_LightColor; float4 g_CameraPosition; row_major float4x4 g_BoneMatrices[128]; float4 g_SkinInfo; row_major float4x4 g_NormalMatrix; float4 g_MaterialValues[32]; float4 g_GraphTime; };
)";
    hlsl << TextureDeclarations(generator.properties) << CommonTypes() << MeshVertexTypes();
    hlsl << BuildSurfaceEvaluation(generator, output, model);
    hlsl << R"(
float4 PSMain(VSOut p):SV_TARGET { GraphInput i; i.uv=p.uv; i.worldPosition=p.worldPos; i.worldNormal=normalize(p.normalW); i.worldTangent=normalize(p.tangentW); i.viewDirection=normalize(g_CameraPosition.xyz-p.worldPos); SurfaceData s=EvaluateSurface(i);
)";
    if (surface == ShaderSurfaceType::Masked)
        hlsl << "    if (s.opacity < s.alphaClip) discard;\n";
    if (model == ShaderShadingModel::Unlit)
        hlsl << "    float3 color=s.baseColor+s.emissive;\n";
    else
        hlsl << "    float3 N=normalize(p.normalW); float3 T=normalize(p.tangentW-N*dot(p.tangentW,N)); float3 "
                "B=normalize(cross(N,T)); N=normalize(s.normalTS.x*T+s.normalTS.y*B+s.normalTS.z*N); float "
                "ndl=saturate(dot(N,normalize(-g_LightDirection.xyz))); float3 "
                "color=s.baseColor*(0.08f+ndl*g_LightColor.rgb*max(g_LightDirection.w,0.0f))+s.emissive;\n";
    hlsl << "    return float4(max(color,0.0f),s.opacity*p.color.a); }\n";
    return hlsl.str();
}

std::string GenerateShadow(Generator& generator, const ShaderGraphNode& output, ShaderShadingModel model,
                           ShaderSurfaceType surface) {
    std::ostringstream hlsl;
    hlsl
        << R"(cbuffer GraphPerDraw : register(b0) { row_major float4x4 g_LightMVP; row_major float4x4 g_World; row_major float4x4 g_BoneMatrices[128]; float4 g_SkinInfo; row_major float4x4 g_NormalMatrix; float4 g_MaterialValues[32]; float4 g_GraphTime; };
)";
    hlsl << TextureDeclarations(generator.properties) << CommonTypes();
    hlsl
        << R"(struct VSIn { float3 pos:POSITION; float3 normal:NORMAL; float3 tangent:TANGENT; float2 uv:TEXCOORD0; float4 joints:BLENDINDICES; float4 weights:BLENDWEIGHT; float4 color:COLOR0; }; struct VSOut { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float3 worldPos:TEXCOORD1; float3 normalW:NORMAL; };
VSOut VSMain(VSIn v) { VSOut o; float4 local=float4(v.pos,1); if(g_SkinInfo.x>0.5f){row_major float4x4 skin=g_BoneMatrices[(uint)v.joints.x]*v.weights.x+g_BoneMatrices[(uint)v.joints.y]*v.weights.y+g_BoneMatrices[(uint)v.joints.z]*v.weights.z+g_BoneMatrices[(uint)v.joints.w]*v.weights.w; local=mul(local,skin);} float4 world=mul(local,g_World); o.pos=mul(local,g_LightMVP); o.uv=v.uv; o.worldPos=world.xyz; o.normalW=normalize(mul(float4(v.normal,0),g_NormalMatrix).xyz); return o; }
)";
    hlsl << BuildSurfaceEvaluation(generator, output, model);
    hlsl << "float4 PSMain(VSOut p):SV_TARGET {";
    if (surface == ShaderSurfaceType::Masked) {
        hlsl << " GraphInput i; i.uv=p.uv; i.worldPosition=p.worldPos; i.worldNormal=p.normalW; "
                "i.worldTangent=float3(1,0,0); i.viewDirection=float3(0,0,1); SurfaceData s=EvaluateSurface(i); "
                "if(s.opacity<s.alphaClip) discard;";
    }
    hlsl << " return (1.0f).xxxx; }\n";
    return hlsl.str();
}
} // namespace

namespace ShaderGraphCompiler {
bool Validate(const ShaderGraph& graph, const std::vector<ShaderPropertyDesc>& properties,
              std::vector<ShaderGraphDiagnostic>& diagnostics) {
    diagnostics.clear();
    std::unordered_map<uint64_t, const ShaderGraphNode*> nodes;
    std::unordered_map<uint64_t, const ShaderGraphNode::Pin*> pins;
    std::unordered_map<uint64_t, uint64_t> pinOwners;
    std::unordered_set<uint64_t> linkIds;
    std::unordered_set<std::string> propertyIds;
    for (const auto& property : properties)
        propertyIds.insert(property.id);
    size_t outputCount = 0;
    for (const auto& node : graph.nodes) {
        if (!nodes.emplace(node.id, &node).second)
            diagnostics.push_back({Diagnostic::Severity::Error, node.id, "duplicate graph node id"});
        const ShaderGraphNodeDefinition* definition = FindShaderGraphNodeDefinition(node.type);
        if (!definition)
            diagnostics.push_back({Diagnostic::Severity::Error, node.id, "unknown graph node type: " + node.type});
        if (node.type == "SurfaceOutputLit" || node.type == "SurfaceOutputUnlit")
            ++outputCount;
        if ((node.type == "Property" || node.type == "TextureSample") && !propertyIds.count(node.propertyId))
            diagnostics.push_back(
                {Diagnostic::Severity::Error, node.id, "unknown shader property: " + node.propertyId});
        if (node.type == "TextureSample" || node.type == "Property") {
            const auto found = std::find_if(properties.begin(), properties.end(),
                                            [&](const auto& property) { return property.id == node.propertyId; });
            if (found != properties.end() && found->type != ShaderPropertyType::Texture2D)
                if (node.type == "TextureSample")
                    diagnostics.push_back(
                        {Diagnostic::Severity::Error, node.id, "TextureSample requires Texture2D property"});
            if (found != properties.end() && found->type == ShaderPropertyType::Texture2D && node.type == "Property")
                diagnostics.push_back(
                    {Diagnostic::Severity::Error, node.id, "Texture2D properties require a TextureSample node"});
        }
        for (const auto& pin : node.pins) {
            if (pin.id == 0 || !pins.emplace(pin.id, &pin).second)
                diagnostics.push_back({Diagnostic::Severity::Error, node.id, "duplicate or zero graph pin id"});
            else
                pinOwners[pin.id] = node.id;
            if (definition) {
                const auto schema =
                    std::find_if(definition->pins.begin(), definition->pins.end(), [&](const auto& expected) {
                        return pin.name == expected.name && pin.input == expected.input;
                    });
                if (schema == definition->pins.end()) {
                    diagnostics.push_back({Diagnostic::Severity::Error, node.id,
                                           "pin is not declared by the node schema: " + pin.name, pin.id, 0,
                                           "pin_schema"});
                } else if (schema->type != ShaderGraphValueType::Any &&
                           pin.type != ShaderGraphValueTypeName(schema->type)) {
                    diagnostics.push_back({Diagnostic::Severity::Error, node.id,
                                           "pin type does not match the node schema: " + pin.name, pin.id, 0,
                                           "pin_schema_type"});
                }
            }
        }
        if (definition)
            for (const auto& expected : definition->pins) {
                const auto pin = std::find_if(node.pins.begin(), node.pins.end(), [&](const auto& candidate) {
                    return candidate.name == expected.name && candidate.input == expected.input;
                });
                if (pin == node.pins.end())
                    diagnostics.push_back({Diagnostic::Severity::Error, node.id,
                                           "node is missing required pin: " + std::string(expected.name), 0, 0,
                                           "missing_pin"});
            }
    }
    if (outputCount != 1)
        diagnostics.push_back({Diagnostic::Severity::Error, 0, "shader graph requires exactly one surface output"});

    std::unordered_map<uint64_t, std::vector<uint64_t>> edges;
    std::unordered_set<std::string> inputPins;
    for (const auto& link : graph.links) {
        if (!linkIds.insert(link.id).second)
            diagnostics.push_back({Diagnostic::Severity::Error, link.toNode, "duplicate graph link id"});
        if (!nodes.count(link.fromNode) || !nodes.count(link.toNode))
            diagnostics.push_back({Diagnostic::Severity::Error, link.toNode, "graph link references missing node"});
        if (link.fromNode == link.toNode)
            diagnostics.push_back(
                {Diagnostic::Severity::Error, link.toNode, "graph link cannot connect a node to itself"});
        if (link.fromPinId != 0 || link.toPinId != 0) {
            const auto from = pins.find(link.fromPinId);
            const auto to = pins.find(link.toPinId);
            if (from == pins.end() || to == pins.end() || pinOwners[link.fromPinId] != link.fromNode ||
                pinOwners[link.toPinId] != link.toNode) {
                diagnostics.push_back({Diagnostic::Severity::Error, link.toNode, "graph link references missing pin"});
            } else {
                if (from->second->input || !to->second->input)
                    diagnostics.push_back(
                        {Diagnostic::Severity::Error, link.toNode, "graph link pin direction is invalid"});
                if (!ShaderGraphCanConnect(*from->second, *to->second))
                    diagnostics.push_back(
                        {Diagnostic::Severity::Error, link.toNode, "graph link pin types do not match"});
            }
        }
        const std::string inputKey = std::to_string(link.toNode) + ":" + link.toPin;
        if (!inputPins.insert(inputKey).second)
            diagnostics.push_back(
                {Diagnostic::Severity::Error, link.toNode, "graph input pin has multiple links: " + link.toPin});
        edges[link.fromNode].push_back(link.toNode);
    }
    std::unordered_map<uint64_t, uint8_t> marks;
    std::function<void(uint64_t)> visit = [&](uint64_t node) {
        if (marks[node] == 1) {
            diagnostics.push_back({Diagnostic::Severity::Error, node, "shader graph contains a cycle"});
            return;
        }
        if (marks[node] == 2)
            return;
        marks[node] = 1;
        for (uint64_t next : edges[node])
            visit(next);
        marks[node] = 2;
    };
    for (const auto& [id, node] : nodes) {
        (void)node;
        visit(id);
    }
    if (const ShaderGraphNode* output = FindOutput(graph)) {
        std::unordered_set<uint64_t> reachable{output->id};
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& link : graph.links)
                if (reachable.count(link.toNode) && reachable.insert(link.fromNode).second)
                    changed = true;
        }
        for (const auto& node : graph.nodes)
            if (!reachable.count(node.id))
                diagnostics.push_back(
                    {Diagnostic::Severity::Warning, node.id, "graph node is unreachable from the surface output"});
    }
    return std::none_of(diagnostics.begin(), diagnostics.end(),
                        [](const auto& diagnostic) { return diagnostic.severity == Diagnostic::Severity::Error; });
}

ShaderGraphCompileResult Compile(const ShaderGraphCompileRequest& request) {
    ShaderGraphCompileResult result;
    if (!request.graph || !request.properties) {
        result.diagnostics.push_back({Diagnostic::Severity::Error, 0, "shader graph compile request is incomplete"});
        return result;
    }
    if (!Validate(*request.graph, *request.properties, result.diagnostics))
        return result;
    if (request.pass == ShaderPass::Default) {
        result.diagnostics.push_back(
            {Diagnostic::Severity::Error, 0, "surface graph cannot compile the Default pass", 0, 0, "invalid_pass"});
        return result;
    }
    Generator generator{*request.graph, *request.properties};
    for (const auto& node : request.graph->nodes)
        generator.nodes[node.id] = &node;
    for (const auto& link : request.graph->links)
        generator.inputs[generator.InputKey(link.toNode, link.toPin)] = &link;
    const ShaderGraphNode* output = FindOutput(*request.graph);
    if (!output)
        return result;
    if ((request.shadingModel == ShaderShadingModel::Lit && output->type != "SurfaceOutputLit") ||
        (request.shadingModel == ShaderShadingModel::Unlit && output->type != "SurfaceOutputUnlit")) {
        result.diagnostics.push_back(
            {Diagnostic::Severity::Error, output->id, "surface output does not match the shader shading model"});
        return result;
    }
    if (request.pass == ShaderPass::GBuffer)
        result.hlsl = GenerateGBuffer(generator, *output, request.shadingModel, request.surfaceType);
    else if (request.pass == ShaderPass::Forward)
        result.hlsl = GenerateForward(generator, *output, request.shadingModel, request.surfaceType);
    else
        result.hlsl = GenerateShadow(generator, *output, request.shadingModel, request.surfaceType);
    result.succeeded = !result.hlsl.empty();
    return result;
}

std::string BuildCanonicalKey(const ShaderGraph& graph, const std::vector<ShaderPropertyDesc>& properties,
                              ShaderShadingModel shadingModel, ShaderSurfaceType surfaceType) {
    ShaderGraph normalizedGraph = graph;
    for (auto& node : normalizedGraph.nodes) {
        node.x = 0.0f;
        node.y = 0.0f;
        std::sort(node.pins.begin(), node.pins.end(),
                  [](const auto& left, const auto& right) { return left.id < right.id; });
    }
    std::sort(normalizedGraph.nodes.begin(), normalizedGraph.nodes.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    std::sort(normalizedGraph.links.begin(), normalizedGraph.links.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    auto normalizedProperties = properties;
    std::sort(normalizedProperties.begin(), normalizedProperties.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    nlohmann::json canonical = {{"compiler", 2},
                                {"nodeLibrary", 1},
                                {"shadingModel", static_cast<uint32_t>(shadingModel)},
                                {"surfaceType", static_cast<uint32_t>(surfaceType)},
                                {"properties", SerializeShaderProperties(normalizedProperties)},
                                {"graph", SerializeShaderGraph(normalizedGraph)}};
    return canonical.dump();
}
} // namespace ShaderGraphCompiler
