#include "Math/Mat4Inverse.h"

#include <cmath>
#include <cstring>

bool Mat4Invert(const Mat4& src, Mat4& dst) {
    // Augmented matrix [A | I], row-major storage.
    double a[4][8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            a[r][c] = static_cast<double>(src.m[r][c]);
        }
        for (int c = 0; c < 4; ++c) {
            a[r][4 + c] = (r == c) ? 1.0 : 0.0;
        }
    }

    for (int col = 0; col < 4; ++col) {
        // Pivot
        int pivot = col;
        double best = std::fabs(a[col][col]);
        for (int r = col + 1; r < 4; ++r) {
            const double v = std::fabs(a[r][col]);
            if (v > best) {
                best = v;
                pivot = r;
            }
        }
        if (best < 1e-12) {
            return false;
        }
        if (pivot != col) {
            for (int c = 0; c < 8; ++c) {
                std::swap(a[col][c], a[pivot][c]);
            }
        }

        // Normalize pivot row
        const double invPivot = 1.0 / a[col][col];
        for (int c = 0; c < 8; ++c) {
            a[col][c] *= invPivot;
        }

        // Eliminate other rows
        for (int r = 0; r < 4; ++r) {
            if (r == col) {
                continue;
            }
            const double f = a[r][col];
            if (std::fabs(f) < 1e-15) {
                continue;
            }
            for (int c = 0; c < 8; ++c) {
                a[r][c] -= f * a[col][c];
            }
        }
    }

    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            dst.m[r][c] = static_cast<float>(a[r][4 + c]);
        }
    }
    return true;
}
