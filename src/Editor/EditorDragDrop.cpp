#include "Editor/EditorDragDrop.h"

#if defined(MYENGINE_ENABLE_IMGUI)
#include <imgui.h>
#endif

// ============================================================
// EditorDragDropSource
// ============================================================

bool EditorDragDropSource::Draw() {
#if defined(MYENGINE_ENABLE_IMGUI)
    if (!ImGui::BeginDragDropSource(GetFlags()))
        return false;

    ImGui::SetDragDropPayload(GetPayloadType(), GetPayloadData(), GetPayloadSize());

    ImGui::TextUnformatted(GetPreviewLabel());
    ImGui::EndDragDropSource();
    return true;
#else
    return false;
#endif
}

// ============================================================
// ActorDragDropSource
// ============================================================

namespace {
constexpr const char kActorPayload[] = "MYENGINE_ACTOR_ID";
}

ActorDragDropSource::ActorDragDropSource(uint64_t actorId, std::string name)
    : m_ActorId(actorId), m_Name(std::move(name)) {
}

const char* ActorDragDropSource::GetPayloadType() const {
    return kActorPayload;
}

const void* ActorDragDropSource::GetPayloadData() const {
    return &m_ActorId;
}

size_t ActorDragDropSource::GetPayloadSize() const {
    return sizeof(m_ActorId);
}

const char* ActorDragDropSource::GetPreviewLabel() const {
    return m_Name.c_str();
}

// ============================================================
// AssetDragDropSource
// ============================================================

AssetDragDropSource::AssetDragDropSource(const char* payloadType, std::string path, std::string label)
    : m_PayloadType(payloadType), m_Path(std::move(path)), m_Label(std::move(label)) {
}

const char* AssetDragDropSource::GetPayloadType() const {
    return m_PayloadType;
}

const void* AssetDragDropSource::GetPayloadData() const {
    return m_Path.c_str();
}

size_t AssetDragDropSource::GetPayloadSize() const {
    return m_Path.size() + 1; // include null terminator
}

const char* AssetDragDropSource::GetPreviewLabel() const {
    return m_Label.c_str();
}
