#pragma once

#include "Core/Event.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

struct RuntimeUIActionDescriptor {
    std::string stableName;
    std::string label;
};

struct RuntimeUIScreenDescriptor {
    std::string stableName;
    std::string title;
    bool modal = true;
    bool dismissible = true;
    std::vector<RuntimeUIActionDescriptor> actions;
    std::string documentPath;
};

struct RuntimeUIScreenView {
    std::string stableName;
    std::string title;
    bool modal = true;
    std::vector<RuntimeUIActionDescriptor> actions;
    size_t focusedIndex = 0;
    std::string documentPath;
};

enum class RuntimeUIStackEventType { None, FocusChanged, Activated, Dismissed };

struct RuntimeUIStackEvent {
    RuntimeUIStackEventType type = RuntimeUIStackEventType::None;
    std::string screen;
    std::string action;
};

class RuntimeUIScreenStack {
public:
    bool Register(RuntimeUIScreenDescriptor descriptor, std::string* error = nullptr);
    bool SetActionLabel(const std::string& screen, const std::string& action,
                        std::string label);
    bool ApplyProjectOverride(const std::string& screen,std::string title,
        std::string documentPath,
        const std::unordered_map<std::string,std::string>& actionLabels,
        std::string* error=nullptr);
    static RuntimeUIScreenStack CreateStandard();

    bool Push(const std::string& stableName);
    bool ReplaceRoot(const std::string& stableName);
    bool Pop();
    void Clear();
    bool Empty() const { return m_Stack.empty(); }
    size_t Depth() const { return m_Stack.size(); }
    bool IsModal() const;
    const std::string& TopName() const;
    const std::string& RootName() const;
    RuntimeUIScreenView GetView() const;

    bool SetFocusedIndex(size_t index);
    RuntimeUIStackEvent MoveFocus(int delta);
    RuntimeUIStackEvent ActivateFocused() const;
    RuntimeUIStackEvent Activate(size_t index);
    RuntimeUIStackEvent Dismiss();
    RuntimeUIStackEvent ProcessEvent(const Event& event);

private:
    struct Entry { std::string screen; size_t focusedIndex = 0; };
    const RuntimeUIScreenDescriptor* TopDescriptor() const;

    std::unordered_map<std::string, RuntimeUIScreenDescriptor> m_Descriptors;
    std::vector<Entry> m_Stack;
};
