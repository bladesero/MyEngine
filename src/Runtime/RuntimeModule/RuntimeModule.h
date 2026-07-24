#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include <cstdint>

// Explicit composition entry point. Registration remains callable and idempotent
// so static-library initialization order never defines Runtime behavior.
MYENGINE_RUNTIME_API void InitializeMyEngineRuntimeModules();
MYENGINE_RUNTIME_API uint32_t GetMyEngineRuntimeInitializationCount();
