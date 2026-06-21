#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class EditorContext;

class EditorAction {
public:
    virtual ~EditorAction() = default;
    virtual const char* GetID() const = 0;
    virtual const char* GetLabel() const = 0;
    virtual bool CanExecute(EditorContext& context) const
    {
        (void)context;
        return true;
    }
    virtual void Execute(EditorContext& context) = 0;
};

class LambdaEditorAction final : public EditorAction {
public:
    using Predicate = std::function<bool(EditorContext&)>;
    using Function = std::function<void(EditorContext&)>;

    LambdaEditorAction(std::string id, std::string label, Function execute,
                       Predicate canExecute = {});
    const char* GetID() const override { return m_ID.c_str(); }
    const char* GetLabel() const override { return m_Label.c_str(); }
    bool CanExecute(EditorContext& context) const override;
    void Execute(EditorContext& context) override;

private:
    std::string m_ID;
    std::string m_Label;
    Function m_Execute;
    Predicate m_CanExecute;
};

class EditorActionRegistry {
public:
    bool Register(std::unique_ptr<EditorAction> action);
    EditorAction* Find(const std::string& id) const;
    bool CanExecute(const std::string& id, EditorContext& context) const;
    bool Execute(const std::string& id, EditorContext& context) const;
    const std::vector<EditorAction*>& GetOrderedActions() const { return m_Order; }
    void Clear() { m_Actions.clear(); m_Order.clear(); }

private:
    std::unordered_map<std::string, std::unique_ptr<EditorAction>> m_Actions;
    std::vector<EditorAction*> m_Order;
};
