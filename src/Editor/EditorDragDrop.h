#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

// ============================================================
// EditorDragDropSource — base class for drag-drop operators.
// Subclasses provide payload type, data, size, and preview
// label. Draw() wraps the ImGui BeginDragDropSource /
// SetDragDropPayload / preview / EndDragDropSource pattern.
// ============================================================

class EditorDragDropSource {
public:
    virtual ~EditorDragDropSource() = default;

    // Draw the drag source. Call this each frame on the item
    // that should be draggable. Returns true when dragging starts.
    bool Draw();

protected:
    virtual const char* GetPayloadType() const = 0;
    virtual const void* GetPayloadData() const = 0;
    virtual size_t GetPayloadSize() const = 0;
    virtual const char* GetPreviewLabel() const = 0;
    virtual ImGuiDragDropFlags GetFlags() const { return ImGuiDragDropFlags_None; }
};

// ============================================================
// ActorDragDropSource — scene outliner actor drag.
// Payload type: "MYENGINE_ACTOR_ID", data is the actor's
// uint64_t ID, preview label is the actor name.
// ============================================================

class ActorDragDropSource : public EditorDragDropSource {
public:
    ActorDragDropSource(uint64_t actorId, std::string name);

protected:
    const char* GetPayloadType() const override;
    const void* GetPayloadData() const override;
    size_t GetPayloadSize() const override;
    const char* GetPreviewLabel() const override;

private:
    uint64_t m_ActorId;
    std::string m_Name;
};

// ============================================================
// AssetDragDropSource — asset browser asset drag.
// Payload type is the provided type string (e.g.
// "MYENGINE_MODEL_PATH"), data is the asset path string,
// preview label is the display name.
// Uses ImGuiDragDropFlags_SourceAllowNullID for assets that
// may not have stable imgui IDs.
// ============================================================

class AssetDragDropSource : public EditorDragDropSource {
public:
    AssetDragDropSource(const char* payloadType, std::string path, std::string label);

protected:
    const char* GetPayloadType() const override;
    const void* GetPayloadData() const override;
    size_t GetPayloadSize() const override;
    const char* GetPreviewLabel() const override;
    ImGuiDragDropFlags GetFlags() const override { return ImGuiDragDropFlags_SourceAllowNullID; }

private:
    const char* m_PayloadType; // non-owning pointer to string literal
    std::string m_Path;
    std::string m_Label;
};
