#pragma once

#include "Editor/EditorContext.h"

#include <functional>
#include <vector>

class EditorService {
public:
    virtual ~EditorService() = default;
    virtual void OnAttach(EditorContext& context) { m_Context = &context; }
    virtual void OnDetach() { m_Context = nullptr; }
    virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }

protected:
    EditorContext* GetContext() const { return m_Context; }

private:
    EditorContext* m_Context = nullptr;
};

class EditorServiceCollection {
public:
    template <typename T>
    void Add(T& service)
    {
        Entry entry;
        entry.service = &service;
        entry.registerService = [&service](EditorContext& context) {
            context.RegisterService(service);
        };
        m_Entries.push_back(std::move(entry));
    }

    void AttachAll(EditorContext& context);
    void UpdateAll(float deltaSeconds);
    void DetachAll(EditorContext& context);
    size_t Size() const { return m_Entries.size(); }

private:
    struct Entry {
        EditorService* service = nullptr;
        std::function<void(EditorContext&)> registerService;
    };
    std::vector<Entry> m_Entries;
};
