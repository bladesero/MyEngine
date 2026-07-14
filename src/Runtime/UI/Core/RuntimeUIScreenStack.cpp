#include "UI/Core/RuntimeUIScreenStack.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <unordered_set>

namespace {
const std::string k_Empty;
void SetError(std::string* error, std::string value) { if (error) *error = std::move(value); }
}

bool RuntimeUIScreenStack::Register(RuntimeUIScreenDescriptor descriptor, std::string* error)
{
    if (descriptor.stableName.empty() || descriptor.title.empty()) {
        SetError(error, "runtime screen name and title must not be empty");
        return false;
    }
    if (m_Descriptors.count(descriptor.stableName)) {
        SetError(error, "duplicate runtime screen: " + descriptor.stableName);
        return false;
    }
    std::unordered_set<std::string> actions;
    for (const auto& action : descriptor.actions) {
        if (action.stableName.empty() || action.label.empty() ||
            !actions.insert(action.stableName).second) {
            SetError(error, "invalid or duplicate action on runtime screen: " + descriptor.stableName);
            return false;
        }
    }
    m_Descriptors.emplace(descriptor.stableName, std::move(descriptor));
    if (error) error->clear();
    return true;
}

bool RuntimeUIScreenStack::SetActionLabel(const std::string& screen,
                                          const std::string& action,
                                          std::string label)
{
    if (label.empty()) return false;
    const auto found = m_Descriptors.find(screen);
    if (found == m_Descriptors.end()) return false;
    for (auto& descriptor : found->second.actions) {
        if (descriptor.stableName != action) continue;
        descriptor.label = std::move(label);
        return true;
    }
    return false;
}

bool RuntimeUIScreenStack::ApplyProjectOverride(const std::string& screen,
    std::string title,std::string documentPath,
    const std::unordered_map<std::string,std::string>& actionLabels,std::string* error)
{
    const auto found=m_Descriptors.find(screen);
    if(found==m_Descriptors.end()){SetError(error,"unknown standard runtime screen: "+screen);return false;}
    if(title.empty()||documentPath.empty()){SetError(error,"screen title and document path must not be empty: "+screen);return false;}
    auto descriptor=found->second;
    descriptor.title=std::move(title);descriptor.documentPath=std::move(documentPath);
    for(const auto& [action,label]:actionLabels){
        auto item=std::find_if(descriptor.actions.begin(),descriptor.actions.end(),
            [&](const RuntimeUIActionDescriptor& value){return value.stableName==action;});
        if(item==descriptor.actions.end()||label.empty()){
            SetError(error,"unknown action or empty label on runtime screen: "+screen+"."+action);return false;
        }
        item->label=label;
    }
    found->second=std::move(descriptor);if(error)error->clear();return true;
}

RuntimeUIScreenStack RuntimeUIScreenStack::CreateStandard()
{
    RuntimeUIScreenStack stack;
    stack.Register({"mainMenu", "Main Menu", true, false,
                    {{"play", "Play"}, {"settings", "Settings"}}});
    stack.Register({"pause", "Paused", true, true,
                    {{"resume", "Resume"}, {"settings", "Settings"},
                     {"mainMenu", "Main Menu"}}});
    stack.Register({"settings", "Settings", true, true,
                    {{"masterDown", "Master -"}, {"masterUp", "Master +"},
                     {"musicDown", "Music -"}, {"musicUp", "Music +"},
                     {"effectsDown", "Effects -"}, {"effectsUp", "Effects +"},
                     {"voiceDown", "Voice -"}, {"voiceUp", "Voice +"},
                     {"uiScaleDown", "UI Scale -"}, {"uiScaleUp", "UI Scale +"},
                     {"subtitles", "Subtitles: On"},
                     {"highContrast", "High Contrast: Off"},
                     {"back", "Back"}}});
    stack.Register({"gameOver", "Game Over", true, false,
                    {{"retry", "Retry"}, {"mainMenu", "Main Menu"}}});
    return stack;
}

bool RuntimeUIScreenStack::Push(const std::string& stableName)
{
    if (!m_Descriptors.count(stableName)) return false;
    if (!m_Stack.empty() && m_Stack.back().screen == stableName) return true;
    m_Stack.push_back({stableName, 0});
    return true;
}

bool RuntimeUIScreenStack::ReplaceRoot(const std::string& stableName)
{
    if (!m_Descriptors.count(stableName)) return false;
    m_Stack.clear();
    m_Stack.push_back({stableName, 0});
    return true;
}

bool RuntimeUIScreenStack::Pop()
{
    if (m_Stack.empty()) return false;
    m_Stack.pop_back();
    return true;
}

void RuntimeUIScreenStack::Clear() { m_Stack.clear(); }

const RuntimeUIScreenDescriptor* RuntimeUIScreenStack::TopDescriptor() const
{
    if (m_Stack.empty()) return nullptr;
    const auto found = m_Descriptors.find(m_Stack.back().screen);
    return found == m_Descriptors.end() ? nullptr : &found->second;
}

bool RuntimeUIScreenStack::IsModal() const
{
    const auto* descriptor = TopDescriptor();
    return descriptor && descriptor->modal;
}

const std::string& RuntimeUIScreenStack::TopName() const
{
    return m_Stack.empty() ? k_Empty : m_Stack.back().screen;
}

const std::string& RuntimeUIScreenStack::RootName() const
{
    return m_Stack.empty() ? k_Empty : m_Stack.front().screen;
}

RuntimeUIScreenView RuntimeUIScreenStack::GetView() const
{
    RuntimeUIScreenView view;
    const auto* descriptor = TopDescriptor();
    if (!descriptor) return view;
    view.stableName = descriptor->stableName;
    view.title = descriptor->title;
    view.modal = descriptor->modal;
    view.actions = descriptor->actions;
    view.focusedIndex = m_Stack.back().focusedIndex;
    view.documentPath = descriptor->documentPath;
    return view;
}

bool RuntimeUIScreenStack::SetFocusedIndex(size_t index)
{
    const auto* descriptor = TopDescriptor();
    if (!descriptor || index >= descriptor->actions.size()) return false;
    m_Stack.back().focusedIndex = index;
    return true;
}

RuntimeUIStackEvent RuntimeUIScreenStack::MoveFocus(int delta)
{
    const auto* descriptor = TopDescriptor();
    if (!descriptor || descriptor->actions.empty() || delta == 0) return {};
    const int count = static_cast<int>(descriptor->actions.size());
    int index = static_cast<int>(m_Stack.back().focusedIndex);
    index = (index + (delta > 0 ? 1 : -1) + count) % count;
    m_Stack.back().focusedIndex = static_cast<size_t>(index);
    return {RuntimeUIStackEventType::FocusChanged, descriptor->stableName,
            descriptor->actions[static_cast<size_t>(index)].stableName};
}

RuntimeUIStackEvent RuntimeUIScreenStack::ActivateFocused() const
{
    const auto* descriptor = TopDescriptor();
    if (!descriptor || descriptor->actions.empty()) return {};
    const size_t index = (std::min)(m_Stack.back().focusedIndex,
                                    descriptor->actions.size() - 1);
    return {RuntimeUIStackEventType::Activated, descriptor->stableName,
            descriptor->actions[index].stableName};
}

RuntimeUIStackEvent RuntimeUIScreenStack::Activate(size_t index)
{
    if (!SetFocusedIndex(index)) return {};
    return ActivateFocused();
}

RuntimeUIStackEvent RuntimeUIScreenStack::Dismiss()
{
    const auto* descriptor = TopDescriptor();
    if (!descriptor || !descriptor->dismissible) return {};
    const std::string name = descriptor->stableName;
    Pop();
    return {RuntimeUIStackEventType::Dismissed, name, "back"};
}

RuntimeUIStackEvent RuntimeUIScreenStack::ProcessEvent(const Event& event)
{
    if (m_Stack.empty()) return {};
    if (event.type == EventType::KeyDown && !event.key.repeat) {
        if (event.key.scancode == SDL_SCANCODE_UP || event.key.scancode == SDL_SCANCODE_W)
            return MoveFocus(-1);
        if (event.key.scancode == SDL_SCANCODE_DOWN || event.key.scancode == SDL_SCANCODE_S)
            return MoveFocus(1);
        if (event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_SPACE)
            return ActivateFocused();
        if (event.key.scancode == SDL_SCANCODE_ESCAPE) return Dismiss();
    }
    if (event.type == EventType::GamepadButtonDown) {
        const auto button = static_cast<SDL_GamepadButton>(event.gamepadButton.button);
        if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) return MoveFocus(-1);
        if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) return MoveFocus(1);
        if (button == SDL_GAMEPAD_BUTTON_SOUTH) return ActivateFocused();
        if (button == SDL_GAMEPAD_BUTTON_EAST) return Dismiss();
    }
    return {};
}
