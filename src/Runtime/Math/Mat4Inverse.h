#pragma once

#include "Core/EngineMath.h"

// Full 4x4 matrix inverse (general); returns false if singular.
bool Mat4Invert(const Mat4& src, Mat4& dst);
