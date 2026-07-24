#pragma once

#include "API/RuntimeApi.h"

#include "API/RuntimeApi.h"

#include "Math/EngineMath.h"

// Full 4x4 matrix inverse (general); returns false if singular.
MYENGINE_RUNTIME_API bool Mat4Invert(const Mat4& src, Mat4& dst);
