#pragma once

#include <string>

namespace Rml {
class Context;
}

class RmlContextManager {
public:
    bool Create(const std::string& name, int width, int height);
    void Destroy();
    void Resize(int width, int height);

    Rml::Context* GetContext() const { return m_Context; }
    static bool IsContextAlive(Rml::Context* context);

private:
    Rml::Context* m_Context = nullptr;
    std::string m_Name;
};
