#pragma once

#include <cmath>

// ==========================================================================
// MyEngine  –  Core Math
//
// Convention:
//   - Row-major Mat4 stored as m[row][col]
//   - Left-hand coordinate system, Y-up (+Z forward)
//   - Depth range 0..1  (D3D11 NDC)
//   - HLSL mul(vector, matrix) friendly  (vector * matrix)
//
// Named EngineMath.h (not Math.h) so MSVC on Windows does not pick this file
// when the standard library includes <math.h> (case-insensitive match).
//
// Vec* / Ray / Plane / AABB / Color live under namespace Math; this header pulls
// them into the global namespace for existing call sites.
// ==========================================================================

static constexpr float kPi    = 3.14159265358979323846f;
static constexpr float kTwoPi = 6.28318530717958647692f;
static constexpr float kDeg2Rad = kPi / 180.0f;
static constexpr float kRad2Deg = 180.0f / kPi;

#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Math/Simd.h"
#include "Math/Ray.h"
#include "Math/Plane.h"
#include "Math/AABB.h"
#include "Math/Color.h"
#include "Math/Quaternion.h"

using Math::Vec2;
using Math::Vec3;
using Math::Vec4;
using Math::Ray;
using Math::Plane;
using Math::AABB;
using Math::Color;
using Math::Quat;
using Math::Slerp;

// --------------------------------------------------------------------------
// Mat4  –  row-major, stored as m[row][col]
// --------------------------------------------------------------------------
struct Mat4 {
    float m[4][4] = {};

    // ---- Arithmetic --------------------------------------------------------
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                for (int k   = 0; k   < 4; ++k)
                    r.m[row][col] += m[row][k] * o.m[k][col];
        return r;
    }

    Mat4& operator*=(const Mat4& o) { *this = *this * o; return *this; }

    // Transform a Vec4 (row-vector * matrix).
    Vec4 Transform(const Vec4& v) const {
        return {
            v.x*m[0][0] + v.y*m[1][0] + v.z*m[2][0] + v.w*m[3][0],
            v.x*m[0][1] + v.y*m[1][1] + v.z*m[2][1] + v.w*m[3][1],
            v.x*m[0][2] + v.y*m[1][2] + v.z*m[2][2] + v.w*m[3][2],
            v.x*m[0][3] + v.y*m[1][3] + v.z*m[2][3] + v.w*m[3][3],
        };
    }

    // Same as Transform; uses SSE packed math on x86/x64 when MYENGINE_HAS_SSE.
    Vec4 TransformSimd(const Vec4& v) const;

    // Transform a direction (w=0).
    Vec3 TransformDir(const Vec3& v) const {
        return Transform(Vec4::FromVec3(v, 0.0f)).XYZ();
    }

    // Transform a point (w=1).
    Vec3 TransformPoint(const Vec3& v) const {
        return Transform(Vec4::FromVec3(v, 1.0f)).XYZ();
    }

    Vec3 TransformDirSimd(const Vec3& v) const { return TransformSimd(Vec4::FromVec3(v, 0.0f)).XYZ(); }
    Vec3 TransformPointSimd(const Vec3& v) const { return TransformSimd(Vec4::FromVec3(v, 1.0f)).XYZ(); }

    Mat4 Transposed() const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                r.m[i][j] = m[j][i];
        return r;
    }

    // Pointer to first element (for uploading to GPU as flat float[16]).
    const float* Data() const { return &m[0][0]; }

    // ---- Factories ---------------------------------------------------------
    static Mat4 Identity() {
        Mat4 r;
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }

    static Mat4 Translation(float tx, float ty, float tz) {
        // Row-vector convention: translation lives in the last row.
        Mat4 r = Identity();
        r.m[3][0] = tx; r.m[3][1] = ty; r.m[3][2] = tz;
        return r;
    }
    static Mat4 Translation(const Vec3& t) {
        return Translation(t.x, t.y, t.z);
    }

    static Mat4 Scale(float sx, float sy, float sz) {
        Mat4 r;
        r.m[0][0] = sx; r.m[1][1] = sy; r.m[2][2] = sz; r.m[3][3] = 1.0f;
        return r;
    }
    static Mat4 Scale(float s) { return Scale(s, s, s); }
    static Mat4 Scale(const Vec3& s) { return Scale(s.x, s.y, s.z); }

    // Rotation around an arbitrary axis (Rodrigues) – row-vector form.
    static Mat4 Rotation(const Vec3& axis, float rad) {
        Vec3 a = axis.Normalized();
        const float c = std::cos(rad), s = std::sin(rad), t = 1.0f - c;
        // This is the transpose of the common column-vector Rodrigues matrix,
        // i.e. directly usable for v * R.
        Mat4 r = Identity();
        r.m[0][0] = t*a.x*a.x + c;       r.m[0][1] = t*a.x*a.y + s*a.z; r.m[0][2] = t*a.x*a.z - s*a.y;
        r.m[1][0] = t*a.x*a.y - s*a.z;   r.m[1][1] = t*a.y*a.y + c;     r.m[1][2] = t*a.y*a.z + s*a.x;
        r.m[2][0] = t*a.x*a.z + s*a.y;   r.m[2][1] = t*a.y*a.z - s*a.x; r.m[2][2] = t*a.z*a.z + c;
        return r;
    }
    // Left-handed, row-vector rotations.
    static Mat4 RotationX(float rad) {
        const float c = std::cos(rad), s = std::sin(rad);
        Mat4 r = Identity();
        // v' = v * Rx
        r.m[1][1] =  c; r.m[1][2] =  s;
        r.m[2][1] = -s; r.m[2][2] =  c;
        return r;
    }
    static Mat4 RotationY(float rad) {
        const float c = std::cos(rad), s = std::sin(rad);
        Mat4 r = Identity();
        // v' = v * Ry
        r.m[0][0] =  c; r.m[0][2] = -s;
        r.m[2][0] =  s; r.m[2][2] =  c;
        return r;
    }
    static Mat4 RotationZ(float rad) {
        const float c = std::cos(rad), s = std::sin(rad);
        Mat4 r = Identity();
        // v' = v * Rz
        r.m[0][0] =  c; r.m[0][1] =  s;
        r.m[1][0] = -s; r.m[1][1] =  c;
        return r;
    }

    // Look-at view matrix (left-hand, Y-up). Row-vector convention.
    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        // Row-vector view matrix. Basis vectors are stored in the first 3 rows,
        // translation is stored in the last row.
        Vec3 f = (target - eye).Normalized();      // forward (+Z)
        Vec3 r = up.Cross(f).Normalized();         // right (+X)
        Vec3 u = f.Cross(r);                       // up (+Y)

        Mat4 v = Identity();
        v.m[0][0] = r.x; v.m[0][1] = u.x; v.m[0][2] = f.x;
        v.m[1][0] = r.y; v.m[1][1] = u.y; v.m[1][2] = f.y;
        v.m[2][0] = r.z; v.m[2][1] = u.z; v.m[2][2] = f.z;
        v.m[3][0] = -eye.Dot(r);
        v.m[3][1] = -eye.Dot(u);
        v.m[3][2] = -eye.Dot(f);
         v.m[3][3] = 1.0f;
        return v;
    }

    // Perspective (left-hand, depth 0..1, D3D11). Row-vector convention.
    static Mat4 Perspective(float fovYRad, float aspect, float zNear, float zFar) {
        const float f = 1.0f / std::tan(fovYRad * 0.5f);
        // Row-vector LH perspective (0..1). This is the transpose of the
        // common column-vector D3D matrix.
        Mat4 p = {};
        p.m[0][0] = f / aspect;
        p.m[1][1] = f;
        p.m[2][2] = zFar / (zFar - zNear);
        p.m[3][2] = (-zNear * zFar) / (zFar - zNear);
        p.m[2][3] = 1.0f;
        p.m[3][3] = 0.0f;// by default
        return p;
    }

    // Orthographic (left-hand, depth 0..1). Row-vector convention.
    static Mat4 Ortho(float l, float r, float b, float t, float zn, float zf) {
        // Row-vector LH ortho (0..1).
        Mat4 o = Identity();
        o.m[0][0] =  2.0f / (r - l);
        o.m[1][1] =  2.0f / (t - b);
        o.m[2][2] =  1.0f / (zf - zn);
        o.m[3][0] = -(r + l) / (r - l);
        o.m[3][1] = -(t + b) / (t - b);
        o.m[3][2] = -zn / (zf - zn);
        return o;
    }
};

// Quat::ToMat4 — R_c is the column-vector rotation matrix (v' = R_c * v as column);
// row-vector multiply uses M = R_c^T so that v' = v * M matches Transform().
inline Mat4 Quat::ToMat4() const
{
    const Quat q = Normalized();
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;

    const float rc00 = 1.0f - 2.0f * (yy + zz);
    const float rc01 = 2.0f * (xy - wz);
    const float rc02 = 2.0f * (xz + wy);
    const float rc10 = 2.0f * (xy + wz);
    const float rc11 = 1.0f - 2.0f * (xx + zz);
    const float rc12 = 2.0f * (yz - wx);
    const float rc20 = 2.0f * (xz - wy);
    const float rc21 = 2.0f * (yz + wx);
    const float rc22 = 1.0f - 2.0f * (xx + yy);

    Mat4 r = Mat4::Identity();
    r.m[0][0] = rc00;
    r.m[0][1] = rc10;
    r.m[0][2] = rc20;
    r.m[1][0] = rc01;
    r.m[1][1] = rc11;
    r.m[1][2] = rc21;
    r.m[2][0] = rc02;
    r.m[2][1] = rc12;
    r.m[2][2] = rc22;
    return r;
}

// M stores row-vector rotation; R_c = M^T for column-vector extraction.
inline Quat Quat::FromMat4(const Mat4& m)
{
    const float r00 = m.m[0][0], r01 = m.m[0][1], r02 = m.m[0][2];
    const float r10 = m.m[1][0], r11 = m.m[1][1], r12 = m.m[1][2];
    const float r20 = m.m[2][0], r21 = m.m[2][1], r22 = m.m[2][2];

    const float rc00 = r00, rc01 = r10, rc02 = r20;
    const float rc10 = r01, rc11 = r11, rc12 = r21;
    const float rc20 = r02, rc21 = r12, rc22 = r22;

    const float tr = rc00 + rc11 + rc22;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        return Quat{
            (rc21 - rc12) / s,
            (rc02 - rc20) / s,
            (rc10 - rc01) / s,
            0.25f * s,
        }.Normalized();
    }
    if (rc00 > rc11 && rc00 > rc22) {
        const float s = std::sqrt(1.0f + rc00 - rc11 - rc22) * 2.0f;
        return Quat{
            0.25f * s,
            (rc01 + rc10) / s,
            (rc02 + rc20) / s,
            (rc21 - rc12) / s,
        }.Normalized();
    }
    if (rc11 > rc22) {
        const float s = std::sqrt(1.0f + rc11 - rc00 - rc22) * 2.0f;
        return Quat{
            (rc01 + rc10) / s,
            0.25f * s,
            (rc12 + rc21) / s,
            (rc02 - rc20) / s,
        }.Normalized();
    }
    const float s = std::sqrt(1.0f + rc22 - rc00 - rc11) * 2.0f;
    return Quat{
        (rc02 + rc20) / s,
        (rc12 + rc21) / s,
        0.25f * s,
        (rc10 - rc01) / s,
    }.Normalized();
}

inline Vec4 Mat4::TransformSimd(const Vec4& v) const
{
#if MYENGINE_HAS_SSE
    const __m128 vx = _mm_loadu_ps(&v.x);
    const __m128 c0 = _mm_set_ps(m[3][0], m[2][0], m[1][0], m[0][0]);
    const __m128 c1 = _mm_set_ps(m[3][1], m[2][1], m[1][1], m[0][1]);
    const __m128 c2 = _mm_set_ps(m[3][2], m[2][2], m[1][2], m[0][2]);
    const __m128 c3 = _mm_set_ps(m[3][3], m[2][3], m[1][3], m[0][3]);
    const __m128 r = _mm_add_ps(
        _mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(vx, vx, _MM_SHUFFLE(0, 0, 0, 0)), c0),
                   _mm_mul_ps(_mm_shuffle_ps(vx, vx, _MM_SHUFFLE(1, 1, 1, 1)), c1)),
        _mm_add_ps(_mm_mul_ps(_mm_shuffle_ps(vx, vx, _MM_SHUFFLE(2, 2, 2, 2)), c2),
                   _mm_mul_ps(_mm_shuffle_ps(vx, vx, _MM_SHUFFLE(3, 3, 3, 3)), c3)));
    return Math::Simd::ToVec4(r);
#else
    return Transform(v);
#endif
}

// World-space AABB from a model-space AABB and object matrix (eight corners).
inline AABB TransformAABB(const AABB& local, const Mat4& world)
{
    const Vec3 corners[8] = {
        { local.min.x, local.min.y, local.min.z },
        { local.max.x, local.min.y, local.min.z },
        { local.min.x, local.max.y, local.min.z },
        { local.max.x, local.max.y, local.min.z },
        { local.min.x, local.min.y, local.max.z },
        { local.max.x, local.min.y, local.max.z },
        { local.min.x, local.max.y, local.max.z },
        { local.max.x, local.max.y, local.max.z },
    };
    AABB r;
    bool first = true;
    for (const Vec3& c : corners) {
        const Vec3 w = world.TransformPoint(c);
        if (first) {
            r.min = r.max = w;
            first = false;
        } else {
            r.Expand(w);
        }
    }
    return r;
}
