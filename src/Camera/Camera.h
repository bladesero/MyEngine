#pragma once

#include <array>
#include <cmath>

// --------------------------------------------------------------------------
// Vec3 / Mat4  –  minimal math types (column-major, right-hand, NDC y-up).
// --------------------------------------------------------------------------
struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3 operator*(float s)       const { return {x*s, y*s, z*s}; }

    float Dot(const Vec3& o) const { return x*o.x + y*o.y + z*o.z; }
    Vec3  Cross(const Vec3& o) const {
        return { y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x };
    }
    float Length() const { return std::sqrt(x*x + y*y + z*z); }
    Vec3  Normalized() const {
        float l = Length();
        return l > 1e-8f ? Vec3{x/l, y/l, z/l} : Vec3{};
    }
};

// Row-major 4×4 (matches HLSL row_major / mul(vec, mat) convention).
// Stored as m[row][col].
struct Mat4 {
    float m[4][4] = {};

    static Mat4 Identity() {
        Mat4 r;
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                for (int k = 0; k < 4; ++k)
                    r.m[row][col] += m[row][k] * o.m[k][col];
        return r;
    }

    // Build a look-at view matrix (right-hand, y-up).
    static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) {
        Vec3 f = (target - eye).Normalized();          // forward
        Vec3 r = f.Cross(up).Normalized();             // right
        Vec3 u = r.Cross(f);                           // recomputed up

        Mat4 v;
        v.m[0][0] =  r.x; v.m[0][1] =  r.y; v.m[0][2] =  r.z; v.m[0][3] = -r.Dot(eye);
        v.m[1][0] =  u.x; v.m[1][1] =  u.y; v.m[1][2] =  u.z; v.m[1][3] = -u.Dot(eye);
        v.m[2][0] = -f.x; v.m[2][1] = -f.y; v.m[2][2] = -f.z; v.m[2][3] =  f.Dot(eye);
        v.m[3][3] = 1.0f;
        return v;
    }

    // Perspective projection (right-hand, depth 0..1, matches D3D11 NDC).
    static Mat4 Perspective(float fovYRad, float aspect, float zNear, float zFar) {
        const float tanHalfFov = std::tan(fovYRad * 0.5f);
        Mat4 p;
        p.m[0][0] = 1.0f / (aspect * tanHalfFov);
        p.m[1][1] = 1.0f / tanHalfFov;
        p.m[2][2] = zFar / (zNear - zFar);
        p.m[2][3] = -1.0f;
        p.m[3][2] = -(zFar * zNear) / (zFar - zNear);
        return p;
    }

    // Orthographic projection (right-hand, depth 0..1).
    static Mat4 Ortho(float l, float r, float b, float t, float zn, float zf) {
        Mat4 o;
        o.m[0][0] =  2.0f / (r - l);
        o.m[1][1] =  2.0f / (t - b);
        o.m[2][2] = -1.0f / (zf - zn);
        o.m[0][3] = -(r + l) / (r - l);
        o.m[1][3] = -(t + b) / (t - b);
        o.m[2][3] = -zn / (zf - zn);
        o.m[3][3] =  1.0f;
        return o;
    }
};

// --------------------------------------------------------------------------
// Camera
// --------------------------------------------------------------------------
class Camera {
public:
    // ---- Position / orientation --------------------------------------------
    void SetPosition(Vec3 pos)    { m_Position = pos;    m_Dirty = true; }
    void SetTarget  (Vec3 target) { m_Target   = target; m_Dirty = true; }
    void SetUp      (Vec3 up)     { m_Up       = up;     m_Dirty = true; }

    Vec3 GetPosition() const { return m_Position; }
    Vec3 GetTarget()   const { return m_Target; }

    // ---- Projection --------------------------------------------------------
    void SetPerspective(float fovYDeg, float aspect, float zNear = 0.1f, float zFar = 1000.0f) {
        m_Proj  = Mat4::Perspective(fovYDeg * (3.14159265f / 180.0f), aspect, zNear, zFar);
        m_Dirty = true;
    }

    void SetOrtho(float width, float height, float zNear = -1.0f, float zFar = 1.0f) {
        m_Proj  = Mat4::Ortho(-width*0.5f, width*0.5f, -height*0.5f, height*0.5f, zNear, zFar);
        m_Dirty = true;
    }

    // ---- Matrices ----------------------------------------------------------
    const Mat4& GetView() const {
        if (m_Dirty) Rebuild();
        return m_View;
    }
    const Mat4& GetProj() const { return m_Proj; }

    Mat4 GetViewProj() const {
        if (m_Dirty) Rebuild();
        return m_View * m_Proj;
    }

    // ---- Simple FPS-style orbit --------------------------------------------
    // Rotate around target: yawDelta (left/right), pitchDelta (up/down) in degrees.
    void Orbit(float yawDeg, float pitchDeg) {
        const float yaw   = yawDeg   * (3.14159265f / 180.0f);
        const float pitch = pitchDeg * (3.14159265f / 180.0f);

        Vec3 dir = (m_Position - m_Target);
        float radius = dir.Length();
        if (radius < 1e-6f) radius = 1.0f;
        dir = dir.Normalized();

        // Spherical coordinates.
        float theta = std::atan2(dir.x, dir.z) + yaw;
        float phi   = std::asin(std::max(-0.99f, std::min(0.99f, dir.y))) + pitch;

        m_Position = m_Target + Vec3{
            radius * std::cos(phi) * std::sin(theta),
            radius * std::sin(phi),
            radius * std::cos(phi) * std::cos(theta)
        };
        m_Dirty = true;
    }

    // Dolly (move camera closer / farther from target).
    void Dolly(float delta) {
        Vec3 dir = (m_Position - m_Target).Normalized();
        m_Position = m_Position + dir * delta;
        m_Dirty = true;
    }

private:
    void Rebuild() const {
        m_View  = Mat4::LookAt(m_Position, m_Target, m_Up);
        m_Dirty = false;
    }

    Vec3 m_Position = { 0.0f, 0.0f, 3.0f };
    Vec3 m_Target   = { 0.0f, 0.0f, 0.0f };
    Vec3 m_Up       = { 0.0f, 1.0f, 0.0f };

    Mat4 m_Proj;
    mutable Mat4  m_View;
    mutable bool  m_Dirty = true;
};
