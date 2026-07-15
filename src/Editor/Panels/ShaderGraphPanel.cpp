#include "Editor/Panels/ShaderGraphPanel.h"

#include "Assets/AssetManager.h"
#include "Assets/ShaderAsset.h"
#include "Editor/EditorCommand.h"
#include "Editor/EditorContext.h"
#include "Editor/EditorImGuiBackend.h"
#include "Game/SceneRenderLayer.h"
#include "Game/SceneViewportController.h"
#include "Renderer/RHI/GpuTextureView.h"
#include "Renderer/MeshShader.h"
#include "Renderer/ShaderGraphCompiler.h"
#include "Renderer/ShaderCooker.h"
#include "Renderer/ShaderManager.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#include <imnodes.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <cctype>
#include <functional>
#include <cstdio>

namespace {
ShaderGraph g_Clipboard;

void PopulatePins(ShaderGraphNode& node, uint64_t& next) {
    if (!node.pins.empty())
        return;
    ShaderGraphNode defaults = CreateShaderGraphNode(node.type, node.id, next);
    node.pins = std::move(defaults.pins);
    if (node.value.empty())
        node.value = std::move(defaults.value);
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool MatchesNodeSearch(const ShaderGraphNodeDefinition& node, const char* search) {
    const std::string needle = Lower(search ? search : "");
    if (needle.empty())
        return true;
    const std::string haystack =
        Lower(std::string(node.displayName) + " " + node.type + " " + node.category + " " + node.keywords);
    size_t start = 0;
    while (start < needle.size()) {
        const size_t end = needle.find(' ', start);
        const std::string token = needle.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!token.empty() && haystack.find(token) == std::string::npos)
            return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}

std::filesystem::path FindContentRoot(std::filesystem::path path) {
    path = std::filesystem::absolute(path).lexically_normal();
    for (auto current = path.parent_path(); !current.empty(); current = current.parent_path()) {
        const std::string name = Lower(current.filename().string());
        if (name == "content" || name == "enginecontent")
            return current;
        if (current == current.root_path())
            break;
    }
    return AssetManager::Get().GetProjectRoot();
}

const ShaderGraphNode::Pin* FindPin(const ShaderGraph& graph, uint64_t id, const ShaderGraphNode** owner = nullptr) {
    for (const auto& node : graph.nodes) {
        for (const auto& pin : node.pins) {
            if (pin.id == id) {
                if (owner)
                    *owner = &node;
                return &pin;
            }
        }
    }
    return nullptr;
}

const char* SurfaceName(ShaderSurfaceType type) {
    return type == ShaderSurfaceType::Masked        ? "Masked"
           : type == ShaderSurfaceType::Transparent ? "Transparent"
                                                    : "Opaque";
}
} // namespace

ShaderGraphPanel::ShaderGraphPanel() : EditorPanel("shaderGraph", "Shader Graph") {
}

void ShaderGraphPanel::OnAttach(EditorContext& context) {
    EditorPanel::OnAttach(context);
#if defined(MYENGINE_ENABLE_IMGUI)
    ImNodes::SetImGuiContext(ImGui::GetCurrentContext());
    ImNodes::CreateContext();
    m_ContextCreated = true;
#endif
}

void ShaderGraphPanel::OnDetach() {
    if (GetContext() && GetContext()->GetSceneLayer())
        GetContext()->GetSceneLayer()->SetMaterialPreviewActive(false);
#if defined(MYENGINE_ENABLE_IMGUI)
    if (m_ContextCreated)
        ImNodes::DestroyContext();
#endif
    m_ContextCreated = false;
    if (m_CompileFuture.valid())
        m_CompileFuture.wait();
    EditorPanel::OnDetach();
}

bool ShaderGraphPanel::LoadAsset(const std::string& path) {
    auto shader = LoadShaderAssetFromFile(path);
    if (!shader || !shader->IsGraph())
        return false;
    m_Path = path;
    m_Name = shader->GetName();
    m_Graph = shader->GetGraph();
    m_Properties = shader->GetProperties();
    m_ShadingModel = shader->GetShadingModel();
    m_SurfaceType = shader->GetSurfaceType();
    m_PassMask = shader->GetPassMask();
    m_Diagnostics = shader->GetDiagnostics();
    uint64_t next = NextID();
    for (auto& node : m_Graph.nodes)
        PopulatePins(node, next);
    for (auto& link : m_Graph.links) {
        if (link.fromPinId == 0) {
            const auto owner = std::find_if(m_Graph.nodes.begin(), m_Graph.nodes.end(),
                                            [&](const auto& node) { return node.id == link.fromNode; });
            if (owner != m_Graph.nodes.end())
                for (const auto& pin : owner->pins)
                    if (!pin.input && pin.name == link.fromPin)
                        link.fromPinId = pin.id;
        }
        if (link.toPinId == 0) {
            const auto owner = std::find_if(m_Graph.nodes.begin(), m_Graph.nodes.end(),
                                            [&](const auto& node) { return node.id == link.toNode; });
            if (owner != m_Graph.nodes.end())
                for (const auto& pin : owner->pins)
                    if (pin.input && pin.name == link.toPin)
                        link.toPinId = pin.id;
        }
    }
    m_NodePositionsApplied = false;
    ++m_DocumentRevision;
    m_CompileCountdown = -1.0f;
    m_RecompileRequested = false;
    m_Status = "Ready";
    m_CompileSucceeded = m_Diagnostics.empty();
    m_PreviewRealtime = m_CompileSucceeded && ShaderGraphUsesTime(m_Graph);
    return true;
}

bool ShaderGraphPanel::CommitDocument(const char* label, bool logicalChange) {
    if (m_Path.empty() || !GetContext())
        return false;
    std::ifstream input(m_Path, std::ios::binary);
    if (!input)
        return false;
    const std::string before((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(before);
    } catch (...) {
        return false;
    }
    document["graph"] = SerializeShaderGraph(m_Graph);
    document["properties"] = SerializeShaderProperties(m_Properties);
    const std::string after = document.dump(2);
    if (after == before)
        return false;
    auto* stack = GetContext()->GetCommandStack();
    if (!stack || !stack->ExecuteCommand(std::make_unique<ModifyAssetCommand>(m_Path, before, after), *GetContext()))
        return false;
    ++m_DocumentRevision;
    if (logicalChange)
        ScheduleCompile();
    return true;
}

void ShaderGraphPanel::ScheduleCompile() {
    m_CompileCountdown = 0.3f;
    if (m_CompileRunning)
        m_RecompileRequested = true;
    m_Status = "Pending compile";
}

void ShaderGraphPanel::StartCompile() {
    if (m_Path.empty())
        return;
    if (m_CompileRunning) {
        m_RecompileRequested = true;
        return;
    }
    const ShaderGraph graph = m_Graph;
    const auto properties = m_Properties;
    const auto model = m_ShadingModel;
    const auto surface = m_SurfaceType;
    const uint32_t passMask = m_PassMask;
    const uint64_t revision = m_DocumentRevision;
    const std::string path = m_Path;
    const ShaderBackend backend = ShaderManager::Get().GetActiveShaderBackend();
    const std::filesystem::path allowedRoot = FindContentRoot(path);
    const std::filesystem::path artifact =
        AssetManager::Get().GetProjectRoot() / "Library" / "ShaderGraphPreview" /
        (std::to_string(std::hash<std::string>{}(path)) + "." + ShaderCooker::BackendName(backend) + ".shader");
    m_CompileRunning = true;
    m_RecompileRequested = false;
    m_Status = "Compiling...";
    m_CompileFuture = std::async(std::launch::async, [graph, properties, model, surface, passMask, revision, path,
                                                      backend, allowedRoot, artifact]() {
        CompileJobResult result;
        result.revision = revision;
        result.usesTime = ShaderGraphUsesTime(graph);
        if (!ShaderGraphCompiler::Validate(graph, properties, result.diagnostics))
            return result;
        for (uint32_t value = static_cast<uint32_t>(ShaderPass::GBuffer);
             value <= static_cast<uint32_t>(ShaderPass::Shadow); ++value) {
            if ((passMask & (1u << value)) == 0)
                continue;
            ShaderGraphCompileRequest request;
            request.graph = &graph;
            request.properties = &properties;
            request.shadingModel = model;
            request.surfaceType = surface;
            request.pass = static_cast<ShaderPass>(value);
            auto compiled = ShaderGraphCompiler::Compile(request);
            result.diagnostics.insert(result.diagnostics.end(), compiled.diagnostics.begin(),
                                      compiled.diagnostics.end());
            if (!compiled.succeeded)
                return result;
        }
        ShaderCookRequest cookRequest;
        cookRequest.sourcePath = path;
        cookRequest.artifactPath = artifact;
        cookRequest.allowedRoot = allowedRoot;
        cookRequest.backends = {backend};
        std::string cookError;
        const ShaderCookResult cooked = ShaderCooker::Cook(cookRequest, &cookError);
        if (!cooked.succeeded) {
            for (const auto& diagnostic : cooked.diagnostics)
                result.diagnostics.push_back({diagnostic.severity == "warning"
                                                  ? ShaderGraphDiagnostic::Severity::Warning
                                                  : ShaderGraphDiagnostic::Severity::Error,
                                              0, diagnostic.message, 0, 0, "backend_compile", ShaderPass::Default,
                                              ShaderCooker::BackendName(backend)});
            if (cooked.diagnostics.empty())
                result.diagnostics.push_back({ShaderGraphDiagnostic::Severity::Error, 0,
                                              cookError.empty() ? "backend shader compile failed" : cookError, 0, 0,
                                              "backend_compile", ShaderPass::Default,
                                              ShaderCooker::BackendName(backend)});
            return result;
        }
        result.artifactPath = cooked.artifactPath.string();
        result.succeeded = true;
        return result;
    });
}

void ShaderGraphPanel::CompleteCompile(CompileJobResult result) {
    m_CompileRunning = false;
    if (result.revision != m_DocumentRevision) {
        m_Status = "Compile superseded";
        m_RecompileRequested = true;
        m_CompileCountdown = 0.0f;
        return;
    }
    m_Diagnostics = std::move(result.diagnostics);
    m_CompileSucceeded = result.succeeded && result.revision == m_DocumentRevision;
    if (!m_CompileSucceeded) {
        m_Status = "Compile failed";
        if (m_RecompileRequested) {
            m_CompileCountdown = 0.0f;
            m_Status = "Pending compile";
        }
        return;
    }
    std::string applyError;
    const bool gpuSucceeded = ShaderManager::Get().ApplyCompiledArtifact(m_Path, result.artifactPath, &applyError);
    if (gpuSucceeded) {
        m_PreviewRealtime = result.usesTime;
        m_Status = "Compile succeeded";
        if (GetContext() && GetContext()->GetSceneLayer())
            GetContext()->GetSceneLayer()->InvalidateMaterialPreview();
    } else {
        m_CompileSucceeded = false;
        m_Status = "Backend compile failed; previous shader retained";
        m_Diagnostics.push_back(
            {ShaderGraphDiagnostic::Severity::Error, 0, applyError.empty() ? m_Status : applyError, 0, 0, "gpu_apply"});
    }
    if (m_RecompileRequested) {
        m_CompileCountdown = 0.0f;
        m_Status = "Pending compile";
    }
}

void ShaderGraphPanel::OnUpdate(float deltaSeconds) {
    EditorContext* context = GetContext();
    if (context && context->GetSelection().HasAsset()) {
        const std::string selected = context->GetSelection().GetAssetPath();
        if (selected != m_Path && std::filesystem::path(selected).extension() == ".shader")
            LoadAsset(selected);
    }
    if (m_CompileCountdown >= 0.0f) {
        m_CompileCountdown -= deltaSeconds;
        if (m_CompileCountdown <= 0.0f) {
            m_CompileCountdown = -1.0f;
            StartCompile();
        }
    }
    if (m_CompileRunning && m_CompileFuture.valid() &&
        m_CompileFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        CompleteCompile(m_CompileFuture.get());
}

uint64_t ShaderGraphPanel::NextID() const {
    uint64_t next = 1;
    for (const auto& node : m_Graph.nodes) {
        next = (std::max)(next, node.id + 1);
        for (const auto& pin : node.pins)
            next = (std::max)(next, pin.id + 1);
    }
    for (const auto& link : m_Graph.links)
        next = (std::max)(next, link.id + 1);
    return next;
}

void ShaderGraphPanel::AddNode(const std::string& type, float x, float y) {
    uint64_t next = NextID();
    const uint64_t nodeId = next++;
    ShaderGraphNode node = CreateShaderGraphNode(type, nodeId, next);
    node.x = x;
    node.y = y;
    if (type == "Property") {
        const auto found = std::find_if(m_Properties.begin(), m_Properties.end(), [](const auto& property) {
            return property.type != ShaderPropertyType::Texture2D;
        });
        if (found != m_Properties.end())
            node.propertyId = found->id;
    } else if (type == "TextureSample") {
        const auto found = std::find_if(m_Properties.begin(), m_Properties.end(), [](const auto& property) {
            return property.type == ShaderPropertyType::Texture2D;
        });
        if (found != m_Properties.end())
            node.propertyId = found->id;
    }
    m_Graph.nodes.push_back(std::move(node));
    CommitDocument("Add Shader Graph Node", true);
}

void ShaderGraphPanel::ApplyCanvasZoom(float zoom, float canvasOriginX, float canvasOriginY) {
#if defined(MYENGINE_ENABLE_IMGUI)
    zoom = (std::clamp)(zoom, 0.25f, 2.0f);
    if (std::fabs(zoom - m_CanvasZoom) < 0.001f)
        return;
    const ImVec2 mouse = ImGui::GetMousePos();
    const ImVec2 pan = ImNodes::EditorContextGetPanning();
    const ImVec2 logical{(mouse.x - canvasOriginX - pan.x) / m_CanvasZoom,
                         (mouse.y - canvasOriginY - pan.y) / m_CanvasZoom};
    ImNodes::EditorContextResetPanning(
        {mouse.x - canvasOriginX - logical.x * zoom, mouse.y - canvasOriginY - logical.y * zoom});
    m_CanvasZoom = zoom;
    m_NodePositionsApplied = false;
#else
    (void)zoom;
    (void)canvasOriginX;
    (void)canvasOriginY;
#endif
}

void ShaderGraphPanel::DrawNodePalette(bool popup, float createX, float createY) {
#if defined(MYENGINE_ENABLE_IMGUI)
    auto& search = popup ? m_NodeSearch : m_LibrarySearch;
    ImGui::SetNextItemWidth(popup ? 300.0f : -1.0f);
    if (popup && ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint(popup ? "##NodeSearch" : "##LibrarySearch", "Search nodes...", search.data(),
                             search.size());
    std::string lastCategory;
    bool foundAny = false;
    for (const auto& definition : GetShaderGraphNodeLibrary()) {
        if (definition.surfaceOutput || !MatchesNodeSearch(definition, search.data()))
            continue;
        if (lastCategory != definition.category) {
            lastCategory = definition.category;
            ImGui::SeparatorText(lastCategory.c_str());
        }
        foundAny = true;
        ImGui::PushID(definition.type);
        const bool selected =
            popup ? ImGui::Selectable(definition.displayName) : ImGui::Button(definition.displayName, {-1, 0});
        if (ImGui::IsItemHovered() && definition.keywords && definition.keywords[0])
            ImGui::SetTooltip("%s\n%s", definition.category, definition.keywords);
        if (selected) {
            AddNode(definition.type, createX, createY);
            if (popup) {
                search.fill('\0');
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::PopID();
    }
    if (!foundAny)
        ImGui::TextDisabled("No matching nodes");
#else
    (void)popup;
    (void)createX;
    (void)createY;
#endif
}

void ShaderGraphPanel::DeleteSelection() {
#if defined(MYENGINE_ENABLE_IMGUI)
    std::vector<int> selectedNodes(static_cast<size_t>(ImNodes::NumSelectedNodes()));
    std::vector<int> selectedLinks(static_cast<size_t>(ImNodes::NumSelectedLinks()));
    if (!selectedNodes.empty())
        ImNodes::GetSelectedNodes(selectedNodes.data());
    if (!selectedLinks.empty())
        ImNodes::GetSelectedLinks(selectedLinks.data());
    std::unordered_set<uint64_t> nodes(selectedNodes.begin(), selectedNodes.end());
    std::unordered_set<uint64_t> links(selectedLinks.begin(), selectedLinks.end());
    for (const auto& node : m_Graph.nodes)
        if (nodes.count(node.id) && (node.type == "SurfaceOutputLit" || node.type == "SurfaceOutputUnlit"))
            nodes.erase(node.id);
    if (nodes.empty() && links.empty())
        return;
    m_Graph.links.erase(std::remove_if(m_Graph.links.begin(), m_Graph.links.end(),
                                       [&](const auto& link) {
                                           return links.count(link.id) || nodes.count(link.fromNode) ||
                                                  nodes.count(link.toNode);
                                       }),
                        m_Graph.links.end());
    m_Graph.nodes.erase(std::remove_if(m_Graph.nodes.begin(), m_Graph.nodes.end(),
                                       [&](const auto& node) { return nodes.count(node.id); }),
                        m_Graph.nodes.end());
    ImNodes::ClearNodeSelection();
    ImNodes::ClearLinkSelection();
    CommitDocument("Delete Shader Graph Selection", true);
#endif
}

void ShaderGraphPanel::CopySelection() {
#if defined(MYENGINE_ENABLE_IMGUI)
    std::vector<int> selected(static_cast<size_t>(ImNodes::NumSelectedNodes()));
    if (!selected.empty())
        ImNodes::GetSelectedNodes(selected.data());
    std::unordered_set<uint64_t> ids(selected.begin(), selected.end());
    g_Clipboard = {};
    for (const auto& node : m_Graph.nodes)
        if (ids.count(node.id))
            g_Clipboard.nodes.push_back(node);
    for (const auto& link : m_Graph.links)
        if (ids.count(link.fromNode) && ids.count(link.toNode))
            g_Clipboard.links.push_back(link);
#endif
}

void ShaderGraphPanel::PasteClipboard() {
    if (g_Clipboard.nodes.empty())
        return;
    uint64_t next = NextID();
    std::unordered_map<uint64_t, uint64_t> remap;
    std::unordered_map<uint64_t, uint64_t> pinRemap;
    for (auto node : g_Clipboard.nodes) {
        const uint64_t old = node.id;
        node.id = next++;
        remap[old] = node.id;
        node.x += 32.0f;
        node.y += 32.0f;
        for (auto& pin : node.pins) {
            const uint64_t oldPin = pin.id;
            pin.id = next++;
            pinRemap[oldPin] = pin.id;
        }
        m_Graph.nodes.push_back(std::move(node));
    }
    for (auto link : g_Clipboard.links) {
        link.id = next++;
        link.fromNode = remap[link.fromNode];
        link.toNode = remap[link.toNode];
        link.fromPinId = pinRemap[link.fromPinId];
        link.toPinId = pinRemap[link.toPinId];
        m_Graph.links.push_back(std::move(link));
    }
    m_NodePositionsApplied = false;
    CommitDocument("Paste Shader Graph Nodes", true);
}

void ShaderGraphPanel::DrawContent() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (m_Path.empty()) {
        ImGui::TextDisabled("Double-click a Graph mode .shader asset to open it here.");
        return;
    }
    ImGui::TextUnformatted(m_Name.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Save / Compile")) {
        CommitDocument("Save Shader Graph", false);
        ScheduleCompile();
    }
    ImGui::SameLine();
    if (ImGui::Button("Compile Now")) {
        m_CompileCountdown = -1.0f;
        StartCompile();
    }
    ImGui::SameLine();
    ImGui::TextColored(m_CompileSucceeded ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f) : ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "%s",
                       m_Status.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("Zoom %.0f%%", m_CanvasZoom * 100.0f);

    ImGui::BeginChild("ShaderBlackboard", ImVec2(250.0f, 0.0f), true);
    if (ImGui::BeginTabBar("ShaderGraphSidebarTabs")) {
        if (ImGui::BeginTabItem("Library")) {
            DrawNodePalette(false);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Blackboard")) {
            constexpr const char* propertyTypes[] = {"Float", "Vec2", "Vec3", "Color", "Bool", "Texture2D"};
            ImGui::SetNextItemWidth(110.0f);
            ImGui::Combo("##NewPropertyType", &m_NewPropertyType, propertyTypes, 6);
            ImGui::SameLine();
            if (ImGui::Button("+ Property", {-1.0f, 0.0f})) {
                ShaderPropertyDesc property;
                uint64_t suffix = NextID() + m_Properties.size();
                do {
                    property.id = "surface.property." + std::to_string(suffix++);
                } while (std::any_of(m_Properties.begin(), m_Properties.end(),
                                     [&](const auto& existing) { return existing.id == property.id; }) ||
                         std::any_of(m_Graph.nodes.begin(), m_Graph.nodes.end(),
                                     [&](const auto& node) { return node.propertyId == property.id; }));
                property.type = static_cast<ShaderPropertyType>(m_NewPropertyType);
                property.name = std::string("New ") + ShaderPropertyTypeName(property.type);
                if (property.type == ShaderPropertyType::Texture2D) {
                    uint32_t slot = 0;
                    for (const auto& existing : m_Properties)
                        if (existing.textureSlot != UINT32_MAX)
                            slot = (std::max)(slot, existing.textureSlot + 1);
                    property.textureSlot = slot;
                    property.constantSlot = UINT32_MAX;
                    property.sRGB = true;
                } else {
                    uint32_t slot = 0;
                    for (const auto& existing : m_Properties)
                        if (existing.constantSlot != UINT32_MAX)
                            slot = (std::max)(slot, existing.constantSlot + 1);
                    property.constantSlot = slot;
                    property.textureSlot = UINT32_MAX;
                    if (property.type == ShaderPropertyType::Color) {
                        std::fill(property.defaultValue, property.defaultValue + 4, 1.0f);
                        property.sRGB = true;
                    }
                }
                m_Properties.push_back(std::move(property));
                CommitDocument("Add Shader Property", true);
            }
            int removeProperty = -1;
            for (size_t propertyIndex = 0; propertyIndex < m_Properties.size(); ++propertyIndex) {
                auto& property = m_Properties[propertyIndex];
                ImGui::PushID(property.id.c_str());
                char displayName[128] = {};
                std::snprintf(displayName, sizeof(displayName), "%s", property.name.c_str());
                ImGui::SetNextItemWidth(-28.0f);
                if (ImGui::InputText("##PropertyName", displayName, sizeof(displayName)) && displayName[0])
                    property.name = displayName;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    CommitDocument("Rename Shader Property", false);
                ImGui::SameLine();
                if (ImGui::SmallButton("x"))
                    removeProperty = static_cast<int>(propertyIndex);
                ImGui::TextDisabled("%s  [%s]", property.id.c_str(), ShaderPropertyTypeName(property.type));
                if (property.type == ShaderPropertyType::Float) {
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::DragFloat("##Default", &property.defaultValue[0], 0.01f,
                                     property.hasRange ? property.minValue : 0.0f,
                                     property.hasRange ? property.maxValue : 0.0f);
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        CommitDocument("Edit Shader Property Default", true);
                } else if (property.type == ShaderPropertyType::Color) {
                    ImGui::ColorEdit4("##Default", property.defaultValue);
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        CommitDocument("Edit Shader Property Default", true);
                } else if (property.type == ShaderPropertyType::Vec2) {
                    ImGui::DragFloat2("##Default", property.defaultValue, 0.01f);
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        CommitDocument("Edit Shader Property Default", true);
                } else if (property.type == ShaderPropertyType::Vec3) {
                    ImGui::DragFloat3("##Default", property.defaultValue, 0.01f);
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        CommitDocument("Edit Shader Property Default", true);
                } else if (property.type == ShaderPropertyType::Bool) {
                    bool enabled = property.defaultValue[0] > 0.5f;
                    if (ImGui::Checkbox("Default", &enabled)) {
                        property.defaultValue[0] = enabled ? 1.0f : 0.0f;
                        CommitDocument("Edit Shader Property Default", true);
                    }
                } else if (property.type == ShaderPropertyType::Texture2D) {
                    char texturePath[256] = {};
                    std::snprintf(texturePath, sizeof(texturePath), "%s", property.defaultTexture.c_str());
                    if (ImGui::InputTextWithHint("##DefaultTexture", "Default texture asset...", texturePath,
                                                 sizeof(texturePath)))
                        property.defaultTexture = texturePath;
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        CommitDocument("Edit Shader Property Default", true);
                }
                if (property.type == ShaderPropertyType::Color || property.type == ShaderPropertyType::Texture2D) {
                    if (ImGui::Checkbox("sRGB", &property.sRGB))
                        CommitDocument("Edit Shader Property Color Space", true);
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            if (removeProperty >= 0) {
                m_Properties.erase(m_Properties.begin() + removeProperty);
                CommitDocument("Delete Shader Property", true);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::SeparatorText("Surface");
    ImGui::Text("%s / %s", m_ShadingModel == ShaderShadingModel::Lit ? "Lit" : "Unlit", SurfaceName(m_SurfaceType));
    ImGui::SeparatorText("Preview");
    const char* meshes[] = {"Cube", "Quad"};
    ImGui::Combo("Mesh", &m_PreviewMesh, meshes, 2);
    constexpr float previewSize = 188.0f;
    bool drewPreview = false;
    if (EditorContext* context = GetContext()) {
        if (SceneRenderLayer* layer = context->GetSceneLayer()) {
            layer->ConfigureMaterialPreview(m_Path, m_PreviewMesh == 1);
            layer->SetMaterialPreviewRealtime(m_PreviewRealtime);
            if (SceneViewport* viewport = layer->GetMaterialPreviewViewport()) {
                int oldX = 0, oldY = 0, oldWidth = 0, oldHeight = 0;
                viewport->GetViewportRect(oldX, oldY, oldWidth, oldHeight);
                viewport->SetViewportRect(0, 0, static_cast<int>(previewSize), static_cast<int>(previewSize));
                if (oldWidth != static_cast<int>(previewSize) || oldHeight != static_cast<int>(previewSize))
                    layer->InvalidateMaterialPreview();
                if (GpuTextureView* view = viewport->GetOutputView()) {
                    if (EditorImGuiBackend* backend = context->GetImGuiBackend()) {
                        if (void* texture = backend->GetTextureId(view)) {
                            ImGui::Image(reinterpret_cast<ImTextureID>(texture), {previewSize, previewSize});
                            drewPreview = true;
                            if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                                const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                                viewport->OrbitAroundFocus(Vec3::Zero(), -drag.x * 0.2f, -drag.y * 0.2f);
                                layer->InvalidateMaterialPreview();
                                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                            }
                        }
                    }
                }
            }
        }
    }
    if (!drewPreview)
        ImGui::Dummy({previewSize, previewSize});
    ImGui::TextDisabled("Drag to orbit; Runtime offscreen renderer");
    ImGui::SeparatorText("Diagnostics");
    for (size_t diagnosticIndex = 0; diagnosticIndex < m_Diagnostics.size(); ++diagnosticIndex) {
        const auto& diagnostic = m_Diagnostics[diagnosticIndex];
        const ImVec4 color = diagnostic.severity == ShaderGraphDiagnostic::Severity::Error
                                 ? ImVec4(1.0f, 0.35f, 0.25f, 1.0f)
                                 : ImVec4(1.0f, 0.8f, 0.25f, 1.0f);
        std::string label = diagnostic.nodeId ? "[Node " + std::to_string(diagnostic.nodeId) + "] " : std::string{};
        label += diagnostic.message;
        if (!diagnostic.backend.empty())
            label += " (" + diagnostic.backend + ")";
        ImGui::PushID(static_cast<int>(diagnosticIndex));
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && diagnostic.nodeId)
            m_FocusNodeId = diagnostic.nodeId;
        ImGui::PopStyleColor();
        if (!diagnostic.code.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%s%s", diagnostic.code.c_str(), diagnostic.stage.empty() ? "" : " / ",
                              diagnostic.stage.c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("ShaderGraphCanvas", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NoScrollbar);
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    if (canvasHovered && std::fabs(ImGui::GetIO().MouseWheel) > 0.01f)
        ApplyCanvasZoom(m_CanvasZoom * (ImGui::GetIO().MouseWheel > 0.0f ? 1.1f : (1.0f / 1.1f)), canvasOrigin.x,
                        canvasOrigin.y);
    if (canvasHovered && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_0)) {
        ApplyCanvasZoom(1.0f, canvasOrigin.x, canvasOrigin.y);
        ImNodes::EditorContextResetPanning({0.0f, 0.0f});
    }
    if (canvasHovered && ImGui::IsKeyPressed(ImGuiKey_Home) && !m_Graph.nodes.empty()) {
        float minX = m_Graph.nodes.front().x, maxX = minX, minY = m_Graph.nodes.front().y, maxY = minY;
        for (const auto& node : m_Graph.nodes) {
            minX = (std::min)(minX, node.x);
            maxX = (std::max)(maxX, node.x);
            minY = (std::min)(minY, node.y);
            maxY = (std::max)(maxY, node.y);
        }
        ImNodes::EditorContextResetPanning({canvasSize.x * 0.5f - (minX + maxX) * 0.5f * m_CanvasZoom,
                                            canvasSize.y * 0.5f - (minY + maxY) * 0.5f * m_CanvasZoom});
    }
    if (canvasHovered && ImGui::IsKeyPressed(ImGuiKey_F) && ImNodes::NumSelectedNodes() > 0) {
        int selectedNode = 0;
        ImNodes::GetSelectedNodes(&selectedNode);
        ImNodes::EditorContextMoveToNode(selectedNode);
    }
    const ImNodesStyle baseStyle = ImNodes::GetStyle();
    ImNodes::PushStyleVar(ImNodesStyleVar_GridSpacing, baseStyle.GridSpacing * m_CanvasZoom);
    ImNodes::PushStyleVar(ImNodesStyleVar_NodeCornerRounding, baseStyle.NodeCornerRounding * m_CanvasZoom);
    ImNodes::PushStyleVar(ImNodesStyleVar_NodePadding,
                          ImVec2(baseStyle.NodePadding.x * m_CanvasZoom, baseStyle.NodePadding.y * m_CanvasZoom));
    ImNodes::PushStyleVar(ImNodesStyleVar_NodeBorderThickness, baseStyle.NodeBorderThickness * m_CanvasZoom);
    ImNodes::PushStyleVar(ImNodesStyleVar_LinkThickness, baseStyle.LinkThickness * m_CanvasZoom);
    ImNodes::PushStyleVar(ImNodesStyleVar_LinkHoverDistance, baseStyle.LinkHoverDistance * m_CanvasZoom);
    ImNodes::PushStyleVar(ImNodesStyleVar_PinCircleRadius, baseStyle.PinCircleRadius * m_CanvasZoom);
    ImNodes::PushStyleVar(ImNodesStyleVar_PinQuadSideLength, baseStyle.PinQuadSideLength * m_CanvasZoom);
    ImNodes::PushStyleVar(ImNodesStyleVar_PinTriangleSideLength, baseStyle.PinTriangleSideLength * m_CanvasZoom);
    ImNodes::PushStyleVar(ImNodesStyleVar_PinLineThickness, baseStyle.PinLineThickness * m_CanvasZoom);
    ImNodes::PushStyleVar(ImNodesStyleVar_PinHoverRadius, baseStyle.PinHoverRadius * m_CanvasZoom);
    ImNodes::PushStyleVar(ImNodesStyleVar_PinOffset, baseStyle.PinOffset * m_CanvasZoom);
    ImGui::SetWindowFontScale(m_CanvasZoom);
    ImNodes::BeginNodeEditor();
    if (!m_NodePositionsApplied) {
        for (const auto& node : m_Graph.nodes)
            ImNodes::SetNodeEditorSpacePos(static_cast<int>(node.id), {node.x * m_CanvasZoom, node.y * m_CanvasZoom});
        m_NodePositionsApplied = true;
    }
    if (m_FocusNodeId != 0) {
        const auto found = std::find_if(m_Graph.nodes.begin(), m_Graph.nodes.end(),
                                        [&](const auto& node) { return node.id == m_FocusNodeId; });
        if (found != m_Graph.nodes.end()) {
            ImNodes::ClearNodeSelection();
            ImNodes::SelectNode(static_cast<int>(m_FocusNodeId));
            ImNodes::EditorContextMoveToNode(static_cast<int>(m_FocusNodeId));
        }
        m_FocusNodeId = 0;
    }
    for (auto& node : m_Graph.nodes) {
        ImNodes::BeginNode(static_cast<int>(node.id));
        ImNodes::BeginNodeTitleBar();
        const auto* definition = FindShaderGraphNodeDefinition(node.type);
        ImGui::TextUnformatted(definition ? definition->displayName : node.type.c_str());
        ImNodes::EndNodeTitleBar();
        for (const auto& pin : node.pins) {
            if (pin.input)
                ImNodes::BeginInputAttribute(static_cast<int>(pin.id));
            else
                ImNodes::BeginOutputAttribute(static_cast<int>(pin.id));
            ImGui::TextUnformatted(pin.name.c_str());
            if (!pin.input)
                ImGui::SameLine();
            if (pin.input)
                ImNodes::EndInputAttribute();
            else
                ImNodes::EndOutputAttribute();
        }
        if ((node.type == "Float" || node.type == "Vec2" || node.type == "Vec3" || node.type == "Vec4" ||
             node.type == "Color" || node.type == "Bool") &&
            !node.value.empty()) {
            ImGui::SetNextItemWidth(110.0f);
            if (node.type == "Bool") {
                bool value = node.value[0] > 0.5f;
                if (ImGui::Checkbox("Value", &value))
                    node.value[0] = value ? 1.0f : 0.0f;
            } else if (node.type == "Vec2")
                ImGui::DragFloat2("Value", node.value.data(), 0.01f);
            else if (node.type == "Vec3")
                ImGui::DragFloat3("Value", node.value.data(), 0.01f);
            else if (node.type == "Vec4")
                ImGui::DragFloat4("Value", node.value.data(), 0.01f);
            else if (node.type == "Color")
                ImGui::ColorEdit4("Value", node.value.data());
            else
                ImGui::DragFloat("Value", &node.value[0], 0.01f);
            if (ImGui::IsItemDeactivatedAfterEdit())
                CommitDocument("Edit Shader Graph Constant", true);
        }
        if ((node.type == "Property" || node.type == "TextureSample") && !m_Properties.empty()) {
            if (ImGui::BeginCombo("Property", node.propertyId.c_str())) {
                for (const auto& property : m_Properties) {
                    if ((node.type == "TextureSample") != (property.type == ShaderPropertyType::Texture2D))
                        continue;
                    if (ImGui::Selectable(property.name.c_str(), property.id == node.propertyId)) {
                        node.propertyId = property.id;
                        CommitDocument("Set Shader Graph Property", true);
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImNodes::EndNode();
    }
    for (const auto& link : m_Graph.links)
        ImNodes::Link(static_cast<int>(link.id), static_cast<int>(link.fromPinId), static_cast<int>(link.toPinId));
    ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();
    ImGui::SetWindowFontScale(1.0f);
    ImNodes::PopStyleVar(12);

    int startPin = 0, endPin = 0;
    if (ImNodes::IsLinkCreated(&startPin, &endPin)) {
        const ShaderGraphNode* startOwner = nullptr;
        const ShaderGraphNode* endOwner = nullptr;
        const auto* start = FindPin(m_Graph, static_cast<uint64_t>(startPin), &startOwner);
        const auto* end = FindPin(m_Graph, static_cast<uint64_t>(endPin), &endOwner);
        if (start && end && startOwner && endOwner && start->input != end->input) {
            if (start->input) {
                std::swap(start, end);
                std::swap(startOwner, endOwner);
            }
            if (!ShaderGraphCanConnect(*start, *end)) {
                m_Diagnostics.push_back({ShaderGraphDiagnostic::Severity::Error, endOwner->id,
                                         "Cannot connect " + start->type + " to " + end->type, end->id, 0,
                                         "type_mismatch"});
            } else {
                ShaderGraphLink link;
                link.id = NextID();
                link.fromNode = startOwner->id;
                link.fromPin = start->name;
                link.fromPinId = start->id;
                link.toNode = endOwner->id;
                link.toPin = end->name;
                link.toPinId = end->id;
                m_Graph.links.erase(std::remove_if(m_Graph.links.begin(), m_Graph.links.end(),
                                                   [&](const auto& old) { return old.toPinId == link.toPinId; }),
                                    m_Graph.links.end());
                m_Graph.links.push_back(std::move(link));
                CommitDocument("Connect Shader Graph Pins", true);
            }
        }
    }
    int destroyedLink = 0;
    if (ImNodes::IsLinkDestroyed(&destroyedLink)) {
        m_Graph.links.erase(
            std::remove_if(m_Graph.links.begin(), m_Graph.links.end(),
                           [&](const auto& link) { return link.id == static_cast<uint64_t>(destroyedLink); }),
            m_Graph.links.end());
        CommitDocument("Disconnect Shader Graph Pins", true);
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete))
        DeleteSelection();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C))
        CopySelection();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V))
        PasteClipboard();
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        bool moved = false;
        for (auto& node : m_Graph.nodes) {
            const ImVec2 position = ImNodes::GetNodeEditorSpacePos(static_cast<int>(node.id));
            const float logicalX = position.x / m_CanvasZoom;
            const float logicalY = position.y / m_CanvasZoom;
            if (std::fabs(logicalX - node.x) > 0.1f || std::fabs(logicalY - node.y) > 0.1f) {
                node.x = logicalX;
                node.y = logicalY;
                moved = true;
            }
        }
        if (moved)
            CommitDocument("Move Shader Graph Nodes", false);
    }
    if (ImNodes::IsEditorHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const ImVec2 pan = ImNodes::EditorContextGetPanning();
        m_NodeCreateX = (mouse.x - canvasOrigin.x - pan.x) / m_CanvasZoom;
        m_NodeCreateY = (mouse.y - canvasOrigin.y - pan.y) / m_CanvasZoom;
        m_NodeSearch.fill('\0');
        ImGui::OpenPopup("AddShaderNode");
    }
    if (ImGui::BeginPopup("AddShaderNode")) {
        DrawNodePalette(true, m_NodeCreateX, m_NodeCreateY);
        ImGui::EndPopup();
    }
    ImGui::EndChild();
#endif
}
