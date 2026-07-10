#include "UI/UIEventBridge.h"

#include <algorithm>

void UIEventBridge::Subscribe(UICanvas* canvas, const std::string& elementId,
                              const std::string& eventName, Callback callback)
{
    SubscribeForOwner(nullptr, canvas, elementId, eventName, std::move(callback));
}

void UIEventBridge::SubscribeForOwner(void* owner, UICanvas* canvas,
                                      const std::string& elementId,
                                      const std::string& eventName,
                                      Callback callback)
{
    if (!callback) return;
    m_Subscriptions.push_back({owner, canvas, elementId, eventName, std::move(callback)});
}

void UIEventBridge::Unsubscribe(void* owner, const std::string& elementId,
                                const std::string& eventName)
{
    m_Subscriptions.erase(
        std::remove_if(m_Subscriptions.begin(), m_Subscriptions.end(),
            [&](const Subscription& subscription) {
                return subscription.owner == owner &&
                    subscription.elementId == elementId &&
                    subscription.eventName == eventName;
            }),
        m_Subscriptions.end());
}

void UIEventBridge::ClearOwner(void* owner)
{
    m_Subscriptions.erase(
        std::remove_if(m_Subscriptions.begin(), m_Subscriptions.end(),
            [&](const Subscription& subscription) {
                return subscription.owner == owner;
            }),
        m_Subscriptions.end());
}

void UIEventBridge::Emit(UICanvas* canvas, const UIEvent& event) const
{
    for (const auto& subscription : m_Subscriptions) {
        if (subscription.canvas && subscription.canvas != canvas) continue;
        if (!subscription.elementId.empty() && subscription.elementId != event.elementId) continue;
        if (!subscription.eventName.empty() && subscription.eventName != event.eventName) continue;
        subscription.callback(event);
    }
}
