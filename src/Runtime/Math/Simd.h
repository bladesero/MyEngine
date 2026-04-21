#pragma once

// ==========================================================================
// MyEngine – SIMD helpers (SSE on x86/x64)
//
// Row-vector * matrix and Vec4 layout match EngineMath.h (x,y,z,w).
// Define MYENGINE_DISABLE_SSE to force scalar fallbacks on x86.
// ==========================================================================

#include <cstddef>

#include "Math/Vector3.h"
#include "Math/Vector4.h"

#if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)) && !defined(MYENGINE_DISABLE_SSE)
#define MYENGINE_HAS_SSE 1
#include <immintrin.h>
#else
#undef MYENGINE_HAS_SSE
#endif

namespace Math {
namespace Simd {

#if MYENGINE_HAS_SSE

using Packed4f = __m128;

inline Packed4f Set4(float x, float y, float z, float w) { return _mm_set_ps(w, z, y, x); }

inline Packed4f Load4Unaligned(const float* p) { return _mm_loadu_ps(p); }
inline Packed4f Load4(const Vec4& v) { return _mm_loadu_ps(&v.x); }

inline void Store4Unaligned(float* p, Packed4f v) { _mm_storeu_ps(p, v); }

inline Vec4 ToVec4(Packed4f v) {
    Vec4 r;
    _mm_storeu_ps(&r.x, v);
    return r;
}

inline float HorizontalSum(Packed4f v) {
    __m128 shuf = _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 3, 0, 1));
    __m128 sums = _mm_add_ps(v, shuf);
    shuf = _mm_movehl_ps(shuf, sums);
    sums = _mm_add_ss(sums, shuf);
    return _mm_cvtss_f32(sums);
}

inline float Dot4(Packed4f a, Packed4f b) { return HorizontalSum(_mm_mul_ps(a, b)); }

inline float Dot3(Packed4f a, Packed4f b) {
    const __m128 mask = _mm_castsi128_ps(_mm_set_epi32(0, -1, -1, -1));
    return HorizontalSum(_mm_and_ps(_mm_mul_ps(a, b), mask));
}

inline Packed4f Add(Packed4f a, Packed4f b) { return _mm_add_ps(a, b); }
inline Packed4f Sub(Packed4f a, Packed4f b) { return _mm_sub_ps(a, b); }
inline Packed4f Mul(Packed4f a, Packed4f b) { return _mm_mul_ps(a, b); }
inline Packed4f Div(Packed4f a, Packed4f b) { return _mm_div_ps(a, b); }

inline Packed4f MulScalar(Packed4f a, float s) { return _mm_mul_ps(a, _mm_set1_ps(s)); }

inline Packed4f Min(Packed4f a, Packed4f b) { return _mm_min_ps(a, b); }
inline Packed4f Max(Packed4f a, Packed4f b) { return _mm_max_ps(a, b); }

// |a×b| with w = 0 (lanes xyz).
inline Packed4f Cross3(Packed4f a, Packed4f b) {
    __m128 a_yzx = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));
    __m128 a_zxy = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 1, 0, 2));
    __m128 b_yzx = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 0, 2, 1));
    __m128 b_zxy = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 1, 0, 2));
    __m128 c = _mm_sub_ps(_mm_mul_ps(a_yzx, b_zxy), _mm_mul_ps(a_zxy, b_yzx));
    const __m128 mask = _mm_castsi128_ps(_mm_set_epi32(0, -1, -1, -1));
    return _mm_and_ps(c, mask);
}

inline float LengthSq3(Packed4f a) { return Dot3(a, a); }
inline float LengthSq4(Packed4f a) { return Dot4(a, a); }

inline Vec3 ToVec3XYZ(Packed4f a) {
    Vec4 t = ToVec4(a);
    return {t.x, t.y, t.z};
}

// Linear interpolation (per component).
inline Packed4f Lerp(Packed4f a, Packed4f b, float t) {
    return _mm_add_ps(a, _mm_mul_ps(_mm_sub_ps(b, a), _mm_set1_ps(t)));
}

#else // ---------- Scalar fallback (non-x86 or MYENGINE_DISABLE_SSE) ----------

using Packed4f = Vec4;

inline Packed4f Set4(float x, float y, float z, float w) { return Vec4(x, y, z, w); }

inline Packed4f Load4Unaligned(const float* p) { return Vec4(p[0], p[1], p[2], p[3]); }
inline Packed4f Load4(const Vec4& v) { return v; }

inline void Store4Unaligned(float* p, Packed4f v) {
    p[0] = v.x;
    p[1] = v.y;
    p[2] = v.z;
    p[3] = v.w;
}

inline Vec4 ToVec4(Packed4f v) { return v; }

inline float HorizontalSum(Packed4f v) { return v.x + v.y + v.z + v.w; }

inline float Dot4(Packed4f a, Packed4f b) { return a.Dot(b); }

inline float Dot3(Packed4f a, Packed4f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Packed4f Add(Packed4f a, Packed4f b) { return a + b; }
inline Packed4f Sub(Packed4f a, Packed4f b) { return a - b; }
inline Packed4f Mul(Packed4f a, Packed4f b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w};
}
inline Packed4f Div(Packed4f a, Packed4f b) {
    return {a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w};
}

inline Packed4f MulScalar(Packed4f a, float s) { return a * s; }

inline Packed4f Min(Packed4f a, Packed4f b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z), std::min(a.w, b.w)};
}
inline Packed4f Max(Packed4f a, Packed4f b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z), std::max(a.w, b.w)};
}

inline Packed4f Cross3(Packed4f a, Packed4f b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, 0.0f};
}

inline float LengthSq3(Packed4f a) { return Dot3(a, a); }
inline float LengthSq4(Packed4f a) { return Dot4(a, a); }

inline Vec3 ToVec3XYZ(Packed4f a) { return {a.x, a.y, a.z}; }

inline Packed4f Lerp(Packed4f a, Packed4f b, float t) {
    return Add(a, MulScalar(Sub(b, a), t));
}

#endif

} // namespace Simd
} // namespace Math
