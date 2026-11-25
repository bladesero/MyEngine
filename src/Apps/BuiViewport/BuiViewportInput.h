#pragma once

#include "BuiViewportABI.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace BuiViewport {

inline int ToPixel(float position, int windowExtent, int pixelExtent) {
    if (windowExtent <= 0 || pixelExtent <= 0 || !std::isfinite(position))
        return -1;
    // Don't clamp off-window motion into the camera region.
    const double value = std::floor(static_cast<double>(position) * pixelExtent / windowExtent);
    return static_cast<int>(std::clamp(value, -1.0, static_cast<double>(pixelExtent)));
}

inline bool Contains(const MBUIRect& r, int x, int y) {
    return r.width > 0 && r.height > 0 && x >= r.x && y >= r.y &&
           static_cast<int64_t>(x) < static_cast<int64_t>(r.x) + r.width &&
           static_cast<int64_t>(y) < static_cast<int64_t>(r.y) + r.height;
}

inline bool Fits(const MBUIRect& r, int width, int height) {
    return r.x >= 0 && r.y >= 0 && r.width > 0 && r.height > 0 && static_cast<int64_t>(r.x) + r.width <= width &&
           static_cast<int64_t>(r.y) + r.height <= height;
}

enum class Key { Forward, Back, Left, Right, Up, Down, Count };

class Input {
public:
    bool BeginLook(bool inside) {
        if (!inside)
            return false;
        look_ = true;
        return true;
    }
    bool Looking() const { return look_; }
    void EndLook() {
        look_ = false;
        keys_.fill(false);
    }
    void Cancel() {
        EndLook();
        dx_ = dy_ = wheel_ = 0;
    }
    void Motion(float x, float y) {
        if (look_) {
            dx_ += x;
            dy_ += y;
        }
    }
    // Key-up always clears; key-down is accepted only during a viewport gesture.
    void SetKey(Key key, bool down) {
        if (down && !look_)
            return;
        keys_[static_cast<unsigned>(key)] = down;
    }
    void Wheel(bool inside, float value) {
        if (inside)
            wheel_ += value;
    }
    struct Delta {
        float yaw, pitch, dolly, forward, right, up;
    };
    Delta Take() {
        const auto on = [this](Key k) { return static_cast<float>(keys_[static_cast<unsigned>(k)]); };
        Delta result{dx_,
                     dy_,
                     wheel_,
                     on(Key::Forward) - on(Key::Back),
                     on(Key::Right) - on(Key::Left),
                     on(Key::Up) - on(Key::Down)};
        dx_ = dy_ = wheel_ = 0;
        return result;
    }

private:
    std::array<bool, static_cast<unsigned>(Key::Count)> keys_{};
    bool look_ = false;
    float dx_ = 0, dy_ = 0, wheel_ = 0;
};

// Called only after the scene owner has opened/recorded the frame. The UI
// overlay owns the one close, including early-return/error paths.
template <class End> class FinishFrame {
public:
    explicit FinishFrame(End end) : end_(end) {}
    ~FinishFrame() { end_(); }
    FinishFrame(const FinishFrame&) = delete;
    FinishFrame& operator=(const FinishFrame&) = delete;

private:
    End end_;
};

} // namespace BuiViewport
