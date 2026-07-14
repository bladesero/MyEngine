#include "Editor/EditorAction.h"

#include "Editor/EditorContext.h"

LambdaEditorAction::LambdaEditorAction(std::string id, std::string label, Function execute, Predicate canExecute)
    : m_ID(std::move(id)), m_Label(std::move(label)), m_Execute(std::move(execute)),
      m_CanExecute(std::move(canExecute)) {
}

bool LambdaEditorAction::CanExecute(EditorContext& context) const {
    return !m_CanExecute || m_CanExecute(context);
}

void LambdaEditorAction::Execute(EditorContext& context) {
    if (m_Execute)
        m_Execute(context);
}

bool EditorActionRegistry::Register(std::unique_ptr<EditorAction> action) {
    if (!action || !action->GetID() || !*action->GetID())
        return false;
    const std::string id = action->GetID();
    EditorAction* raw = action.get();
    const bool inserted = m_Actions.emplace(id, std::move(action)).second;
    if (inserted)
        m_Order.push_back(raw);
    return inserted;
}

EditorAction* EditorActionRegistry::Find(const std::string& id) const {
    const auto it = m_Actions.find(id);
    return it == m_Actions.end() ? nullptr : it->second.get();
}

bool EditorActionRegistry::CanExecute(const std::string& id, EditorContext& context) const {
    EditorAction* action = Find(id);
    return action && action->CanExecute(context);
}

bool EditorActionRegistry::Execute(const std::string& id, EditorContext& context) const {
    EditorAction* action = Find(id);
    if (!action || !action->CanExecute(context))
        return false;
    action->Execute(context);
    return true;
}
