#pragma once

#include <cmath>
#include <algorithm>

// ==========================================================================
// MyEngine  –  Core Math
//
// Convention:
//   - Row-major Mat4 stored as m[row][col]
//   - Right-hand coordinate system, Y-up
//   - Depth range 0..1  (D3D11 NDC)
//   - HLSL mul(vector, matrix) friendly  (vector * matrix)
// ==========================================================================

static constexpr float kPi    = 3.14159265358979323846f;
static constexpr float kTwoPi = 6.28318530717958647692f;
static constexpr float kDeg2Rad = kPi / 180.0f;
static constexpr float kRad2Deg = 180.0f / kPi;

// --------------------------------------------------------------------------
// Vec2
// --------------------------------------------------------------------------
struct Vec2 {
    float x = 0.0f, y = 0.0f;

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s)       const { return {x * s, y * s}; }
    Vec2 operator/(float s)       const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x+=o.x; y+=o.y; return *this; }

    float Dot(const Vec2& o)  const { return x*o.x + y*o.y; }
    float LengthSq()          const { return x*x + y*y; }
    float Length()            const { return std::sqrt(LengthSq()); }
    Vec2  Normalized()        const {
        float l = Length();
        return l > 1e-8f ? Vec2{x/l, y/l} : Vec2{};
    }
};

// --------------------------------------------------------------------------
// Vec3
// --------------------------------------------------------------------------
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3 operator+(const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3 operator*(float s)       const { return {x*s, y*s, z*s}; }
    Vec3 operator/(float s)       const { return {x/s, y/s, z/s}; }
    Vec3 operator-()              const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x+=o.x; y+=o.y; z+=o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x-=o.x; y-=o.y; z-=o.z; return *this; }
    Vec3& operator*=(float s)       { x*=s; y*=s; z*=s; return *this; }
    bool operator==(const Vec3& o) const { return x==o.x && y==o.y && z==o.z; }

    float Dot(const Vec3& o)  const { return x*o.x + y*o.y + z*o.z; }
    float LengthSq()          const { return x*x + y*y + z*z; }
    float Length()            const { return std::sqrt(LengthSq()); }
    Vec3  Normalized()        const {
        float l = Length();
        return l > 1e-8f ? Vec3{x/l, y/l, z/l} : Vec3{};
    }
    Vec3 Cross(const Vec3& o) const {
        return { y*o.z - z*o.y,
                 z*o.x - x*o.z,
                 x*o.y - y*o.x };
    }

    static Vec3 Lerp(const Vec3& a, const Vec3& b, float t) {
        return a + (b - a) * t;
    }
    static Vec3 Zero()    { return {0,0,0}; }
    static Vec3 One()     { return {1,1,1}; }
    static Vec3 Up()      { return {0,1,0}; }
    static Vec3 Forward() { return {0,0,-1}; }
    static Vec3 Right()   { return {1,0,0}; }
};

// --------------------------------------------------------------------------
// Vec4
// --------------------------------------------------------------------------
struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

    Vec4 operator+(const Vec4& o) const { return {x+o.x, y+o.y, z+o.z, w+o.w}; }
    Vec4 operator*(float s)       const { return {x*s, y*s, z*s, w*s}; }

    Vec3 XYZ() const { return {x, y, z}; }

    static Vec4 FromVec3(const Vec3& v, float w = 1.0f) { return {v.x, v.y, v.z, w}; }
};

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

    // Transform a direction (w=0).
    Vec3 TransformDir(const Vec3& v) const {
        return Transform(Vec4::FromVec3(v, 0.0f)).XYZ();
    }

    // Transform a point (w=1).
    Vec3 TransformPoint(const Vec3& v) const {
        return Transform(Vec4::FromVec3(v, 1.0f)).XYZ();
    }

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
        Mat4 r = Identity();
        r.m[0][3] = tx; r.m[1][3] = ty; r.m[2][3] = tz;
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

    // Rotation around an arbitrary axis (Rodrigues).
    static Mat4 Rotation(const Vec3& axis, float rad) {
        Vec3 a = axis.Normalized();
        const float c = std::cos(rad), s = std::sin(rad), t = 1.0f - c;
        Mat4 r = Identity();
        r.m[0][0] = t*a.x*a.x + c;       r.m[0][1] = t*a.x*a.y - s*a.z; r.m[0][2] = t*a.x*a.z + s*a.y;
        r.m[1][0] = t*a.x*a.y + s*a.z;   r.m[1][1] = t*a.y*a.y + c;     r.m[1][2] = t*a.y*a.z - s*a.x;
        r.m[2][0] = t*a.x*a.z - s*a.y;   r.m[2][1] = t*a.y*a.z + s*a.x; r.m[2][2] = t*a.z*a.z + c;
        return r;
    }
    static Mat4 RotationX(float rad) { return Rotation({1,0,0}, rad); }
    static Mat4 RotationY(float rad) { return Rotation({0,1,0}, rad); }
    static Mat4 RotationZ(float rad) { return Rotation({0,0,1}, rad); }

    // Look-at view matrix (right-hand, Y-up).
    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 f = (target - eye).Normalized();   // forward
        Vec3 r = f.Cross(up).Normalized();       // right
        Vec3 u = r.Cross(f);                     // recomputed up

        Mat4 v;
        v.m[0][0] =  r.x; v.m[0][1] =  r.y; v.m[0][2] =  r.z; v.m[0][3] = -r.Dot(eye);
        v.m[1][0] =  u.x; v.m[1][1] =  u.y; v.m[1][2] =  u.z; v.m[1][3] = -u.Dot(eye);
        v.m[2][0] = -f.x; v.m[2][1] = -f.y; v.m[2][2] = -f.z; v.m[2][3] =  f.Dot(eye);
        v.m[3][3] = 1.0f;
        return v;
    }

    // Perspective (right-hand, depth 0..1, D3D11).
    static Mat4 Perspective(float fovYRad, float aspect, float zNear, float zFar) {
        const float f = 1.0f / std::tan(fovYRad * 0.5f);
        Mat4 p;
        p.m[0][0] = f / aspect;
        p.m[1][1] = f;
        p.m[2][2] = zFar / (zNear - zFar);
        p.m[2][3] = -1.0f;
        p.m[3][2] = -(zFar * zNear) / (zFar - zNear);
        return p;
    }

    // Orthographic (right-hand, depth 0..1).
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
