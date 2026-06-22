#include "UI/UIEventBridge.h"

void UIEventBridge::Subscribe(UICanvas* canvas, const std::string& elementId,
                              const std::string& eventName, Callback callback)
{
    if (!canvas || !callback) return;
    m_Subscriptions.push_back({canvas, elementId, eventName, std::move(callback)});
}

void UIEventBridge::Emit(UICanvas* canvas, const UIEvent& event) const
{
    for (const auto& subscription : m_Subscriptions) {
        if (subscription.canvas != canvas) continue;
        if (!subscription.elementId.empty() && subscription.elementId != event.elementId) continue;
        if (!subscription.eventName.empty() && subscription.eventName != event.eventName) continue;
        subscription.callback(event);
    }
}
