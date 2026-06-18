#pragma once

#include "Editor/EditorCommand.h"

class Actor;
class EditorContext;

class EditorSceneTransaction {
public:
    void Begin(const char* name, std::string beforeJson, uint64_t selection);
    bool Commit(EditorContext& context);
    void Cancel();
    bool IsActive() const { return m_Active; }

private:
    std::string m_Name;
    std::string m_BeforeJson;
    uint64_t m_Selection = 0;
    bool m_Active = false;
};

class EditorUndoUtil {
public:
    static std::unique_ptr<IEditorCommand> MakeSceneSnapshotCommand(
        const char* name, const std::string& beforeJson, const std::string& afterJson,
        uint64_t beforeSelection, uint64_t afterSelection);
    static std::unique_ptr<IEditorCommand> MakeTransformCommand(
        const Actor& actor, const Transform& before, const Transform& after);
};
