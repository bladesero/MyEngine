#pragma once

#include <cmath>
#include <algorithm>

namespace Math
{

    struct Vec3 {
        float x = 0.0f, y = 0.0f, z = 0.0f;

        // 构造
        constexpr Vec3() = default;
        constexpr Vec3(float v) : x(v), y(v), z(v) {}
        constexpr Vec3(float vx, float vy, float vz) : x(vx), y(vy), z(vz) {}
        constexpr Vec3(float vx, float vy) : x(vx), y(vy), z(0.0f) {}

        // 静态常量
        static Vec3 Zero()    { return {0,0,0}; }
        static Vec3 One()     { return {1,1,1}; }
        static Vec3 Up()      { return {0,1,0}; }
        static Vec3 Forward() { return {0,0,1}; }
        static Vec3 Right()   { return {1,0,0}; }

        // 运算符重载
        Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
        Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
        Vec3 operator*(float s)       const { return {x * s, y * s, z * s}; }
        Vec3 operator/(float s)       const { return {x / s, y / s, z / s}; }
        Vec3 operator+(float s)       const { return {x + s, y + s, z + s}; }
        Vec3 operator-(float s)       const { return {x - s, y - s, z - s}; }
        Vec3& operator+=(const Vec3& o) { x+=o.x; y+=o.y; z+=o.z; return *this; }
        Vec3& operator-=(const Vec3& o) { x-=o.x; y-=o.y; z-=o.z; return *this; }
        Vec3& operator*=(float s)     { x*=s; y*=s; z*=s; return *this; }
        Vec3& operator/=(float s)     { x/=s; y/=s; z/=s; return *this; }

        // 数学方法
        float Dot(const Vec3& o)  const { return x*o.x + y*o.y + z*o.z; }
        float CrossZ(const Vec3& o) const { return x*o.y - y*o.x; }
        float LengthSq()          const { return x*x + y*y + z*z; }
        float Length()            const { return std::sqrt(LengthSq()); }
        Vec3  Normalized()        const {
            float l = Length();
            return l > 1e-8f ? Vec3{x/l, y/l, z/l} : Vec3{};
        }

    };

    
}