#pragma once

#include "Editor/EditorCommand.h"
#include "Assets/Asset.h"
#include "Assets/AssetManager.h"
#include "Core/Logger.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

class EditorContext;
class Scene;

// ==========================================================================
// IResourceOperator  鈥? unified interface for resource-modifying editor ops
//
// Implementations wrap the full pipeline:
//   1. Load / inspect the resource
//   2. Apply the modification in-memory
//   3. Create an undo command
//   4. Persist to disk
//   5. Reload / refresh runtime state
// ==========================================================================

class IResourceOperator {
public:
    virtual ~IResourceOperator() = default;

    // Return true if this operator can run in the current context.
    virtual bool CanModify(EditorContext& context) const = 0;

    // Run the operation and push an undo command onto the stack.
    // Returns false if the operation fails (no changes made).
    virtual bool Modify(EditorContext& context) = 0;

    // Human-readable label for undo menu.
    virtual const char* GetLabel() const = 0;
};

// ==========================================================================
// AssetModifier  鈥? base class for operators that modify a disk-backed asset
// ==========================================================================

class AssetModifier : public IResourceOperator {
public:
    explicit AssetModifier(std::string assetPath) : m_AssetPath(std::move(assetPath)) {}

    const std::string& GetAssetPath() const { return m_AssetPath; }

    // Default CanModify: asset must exist and be writable.
    bool CanModify(EditorContext& context) const override {
        (void)context;
        std::error_code ec;
        return std::filesystem::exists(m_AssetPath, ec);
    }

    // Default Modify pipeline:
    //   1. Read current file content
    //   2. Apply in-memory change (subclass overrides ApplyTo)
    //   3. Write back to disk
    //   4. Push undo command (ModifyAssetCommand)
    //   5. Reload in AssetManager
    bool Modify(EditorContext& context) override;

    // Subclass implements: read asset into runtime object, modify it, serialize back.
    // @param asset  The loaded asset handle (already loaded by Modify).
    // @return       True if the asset was modified and needs saving.
    virtual bool ApplyTo(Asset& asset) = 0;

protected:
    std::string m_AssetPath;
};

// ==========================================================================
// MaterialModifier  鈥? modifies a material asset via lambda
// ==========================================================================

class MaterialModifier final : public AssetModifier {
public:
    using ModifyFn = std::function<bool(class MaterialAsset&)>;

    MaterialModifier(std::string path, std::string label, ModifyFn fn)
        : AssetModifier(std::move(path)), m_Label(std::move(label)), m_Fn(std::move(fn)) {}

    const char* GetLabel() const override { return m_Label.c_str(); }
    bool ApplyTo(Asset& asset) override;

private:
    std::string m_Label;
    ModifyFn m_Fn;
};

// ==========================================================================
// SceneModifier  鈥? base class for operators that modify a scene object
// ==========================================================================

class SceneModifier : public IResourceOperator {
public:
    SceneModifier() = default;

    bool CanModify(EditorContext& context) const override;

    // Default: save before snapshot, apply, create undo, mark dirty.
    bool Modify(EditorContext& context) override;

    // Subclass implements: mutate the scene/actor/component in-place.
    virtual bool ApplyTo(EditorContext& context) = 0;

    const char* GetLabel() const override { return "Scene Edit"; }
};
