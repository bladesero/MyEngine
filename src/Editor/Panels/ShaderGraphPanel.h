#pragma once

#include "Assets/ShaderGraph.h"
#include "Editor/EditorPanel.h"

#include <future>
#include <array>
#include <string>
#include <vector>

class ShaderGraphPanel final : public EditorPanel {
public:
    ShaderGraphPanel();
    void OnAttach(EditorContext& context) override;
    void OnDetach() override;
    void OnUpdate(float deltaSeconds) override;
    bool ShouldUpdateWhenHidden() const override { return true; }
    std::string GetDefaultDockArea() const override { return "bottomCenter"; }

protected:
    void DrawContent() override;

private:
    struct CompileJobResult {
        bool succeeded = false;
        bool usesTime = false;
        uint64_t revision = 0;
        std::string artifactPath;
        std::vector<ShaderGraphDiagnostic> diagnostics;
    };

    bool LoadAsset(const std::string& path);
    bool CommitDocument(const char* label, bool logicalChange);
    void ScheduleCompile();
    void StartCompile();
    void CompleteCompile(CompileJobResult result);
    void AddNode(const std::string& type, float x, float y);
    void DeleteSelection();
    void CopySelection();
    void PasteClipboard();
    void DrawNodePalette(bool popup, float createX = 0.0f, float createY = 0.0f);
    void ApplyCanvasZoom(float zoom, float canvasOriginX, float canvasOriginY);
    uint64_t NextID() const;

    std::string m_Path;
    std::string m_Name;
    ShaderGraph m_Graph;
    std::vector<ShaderPropertyDesc> m_Properties;
    ShaderShadingModel m_ShadingModel = ShaderShadingModel::Lit;
    ShaderSurfaceType m_SurfaceType = ShaderSurfaceType::Opaque;
    uint32_t m_PassMask = 0;
    std::vector<ShaderGraphDiagnostic> m_Diagnostics;
    std::future<CompileJobResult> m_CompileFuture;
    float m_CompileCountdown = -1.0f;
    bool m_NodePositionsApplied = false;
    bool m_ContextCreated = false;
    bool m_CompileSucceeded = false;
    bool m_CompileRunning = false;
    bool m_RecompileRequested = false;
    bool m_PreviewRealtime = false;
    int m_PreviewMesh = 0;
    float m_CanvasZoom = 1.0f;
    float m_NodeCreateX = 0.0f;
    float m_NodeCreateY = 0.0f;
    uint64_t m_DocumentRevision = 0;
    uint64_t m_FocusNodeId = 0;
    int m_NewPropertyType = 0;
    std::array<char, 128> m_NodeSearch{};
    std::array<char, 128> m_LibrarySearch{};
    std::string m_Status = "No graph open";
};
