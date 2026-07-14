#include "Editor/EditorOperatorShared.h"

bool EditorDragDropOperator::ApplyActorDrop(EditorContext& context, uint64_t actorID, uint64_t afterParentID,
                                            uint64_t afterNextSiblingID) const {
    if (EditorOperators* operators = context.GetOperators()) {
        return operators->Commands().MoveActor(context, actorID, afterParentID, afterNextSiblingID);
    }
    EditorCommandOperator commands;
    return commands.MoveActor(context, actorID, afterParentID, afterNextSiblingID);
}

bool EditorDragDropOperator::ApplyAssetDrop(EditorContext& context, const std::string& assetPath,
                                            const std::string& targetPath) const {
    (void)context;
    return !assetPath.empty() && !targetPath.empty();
}
