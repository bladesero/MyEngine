#pragma once

#include "Core/EngineMath.h"

// ==========================================================================
// Transform  –  位置 / 旋转 / 缩放，并能生成本地/世界矩阵
//
// 旋转以 Euler 角（度）存储，顺序 Y→X→Z（Yaw→Pitch→Roll）
// ==========================================================================

struct Transform {
    Vec3 position = Vec3::Zero();
    Vec3 rotation = Vec3::Zero();   // (pitch, yaw, roll) in degrees
    Vec3 scale    = Vec3::One();

    // 生成本地 TRS 矩阵（列表示基向量，行主序存储）
    Mat4 GetLocalMatrix() const {
        Mat4 t = Mat4::Translation(position);
        Mat4 ry = Mat4::RotationY(rotation.y * kDeg2Rad);
        Mat4 rx = Mat4::RotationX(rotation.x * kDeg2Rad);
        Mat4 rz = Mat4::RotationZ(rotation.z * kDeg2Rad);
        Mat4 s  = Mat4::Scale(scale);
        // Row-vector TRS: v * S * Ry * Rx * Rz * T.
        return s * ry * rx * rz * t;
    }

    // 本地前向量
    Vec3 GetForward() const {
        Mat4 r = Mat4::RotationY(rotation.y * kDeg2Rad)
               * Mat4::RotationX(rotation.x * kDeg2Rad);
        return r.TransformDir(Vec3::Forward()).Normalized();
    }

    Vec3 GetRight() const {
        Mat4 r = Mat4::RotationY(rotation.y * kDeg2Rad)
               * Mat4::RotationX(rotation.x * kDeg2Rad);
        return r.TransformDir(Vec3::Right()).Normalized();
    }

    Vec3 GetUp() const {
        Mat4 r = Mat4::RotationY(rotation.y * kDeg2Rad)
               * Mat4::RotationX(rotation.x * kDeg2Rad);
        return r.TransformDir(Vec3::Up()).Normalized();
    }
};
