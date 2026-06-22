#pragma once

#include "Core/Event.h"

namespace Rml {
class Context;
}

class RmlInputAdapter {
public:
    bool ProcessEvent(Rml::Context& context, const Event& event) const;

private:
    int GetKeyModifierState() const;
    int ToRmlMouseButton(int button) const;
    int ToRmlKey(int scancode) const;
};
