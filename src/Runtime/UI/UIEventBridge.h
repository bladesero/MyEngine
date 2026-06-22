#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class UICanvas;

struct UIEvent {
    std::string elementId;
    std::string eventName;
};

class UIEventBridge {
public:
    using Callback = std::function<void(const UIEvent&)>;

    void Subscribe(UICanvas* canvas, const std::string& elementId,
                   const std::string& eventName, Callback callback);
    void Emit(UICanvas* canvas, const UIEvent& event) const;

private:
    struct Subscription {
        UICanvas* canvas = nullptr;
        std::string elementId;
        std::string eventName;
        Callback callback;
    };
    std::vector<Subscription> m_Subscriptions;
};
