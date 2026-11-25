#include "BuiViewportABI.h"
#include "BuiViewportInput.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>

extern "C" const MBUIApi* MyEngineBuiGetApi(uint32_t version);

namespace {
void Check(bool good, const char* diagnostic) {
    if (!good)
        throw std::runtime_error(diagnostic);
}

void CheckRaster() {
    Check(!MyEngineBuiGetApi(MYENGINE_BUI_VIEWPORT_ABI + 1), "ABI must reject incompatible hosts");
    const auto* api = MyEngineBuiGetApi(MYENGINE_BUI_VIEWPORT_ABI);
    Check(api != nullptr, "No kit API");
    std::unique_ptr<void, decltype(api->destroy)> kit(api->create(), api->destroy);
    Check(kit != nullptr, "No context");

    constexpr int width = 1280, height = 800;
    MBUIFrame first{};
    Check(api->frame(kit.get(), width, height, 1.f, &first) && first.rgba, "No BUI pixels");
    Check(BuiViewport::Fits(first.viewport, width, height) && first.viewport.y > 0 && first.viewport.height < height,
          "Missing reserved viewport area");
    const auto* bytes = first.rgba;
    bool varies = false;
    for (int i = 1; i < width * height && !varies; ++i)
        varies = !std::equal(bytes, bytes + 4, bytes + i * 4);
    Check(varies && bytes[3] == 255, "Expected opaque non-uniform BUI chrome");
    MBUIFrame idle{};
    Check(api->frame(kit.get(), width, height, 1.f, &idle) && first.revision == idle.revision,
          "Unchanged chrome should not upload again");

    // Press actual BUI widget through its host input path. The toolbar lies
    // above the viewport; scanning within it avoids fixed DPI-dependent points.
    bool reset = false;
    for (int y = 0; y < first.viewport.y && !reset; ++y) {
        api->pointer(kit.get(), MBUI_MOVE, width / 2, y);
        api->pointer(kit.get(), MBUI_LEFT_DOWN, width / 2, y);
        api->pointer(kit.get(), MBUI_LEFT_UP, width / 2, y);
        reset = api->take_reset_camera(kit.get()) != 0;
    }
    Check(reset && !api->take_reset_camera(kit.get()), "Reset Camera widget must emit once and then drain");

    MBUIFrame resized{};
    Check(api->frame(kit.get(), 1920, 1200, 1.5f, &resized) && resized.revision > first.revision,
          "Resize/DPI must update the raster");
    Check(BuiViewport::Fits(resized.viewport, 1920, 1200) && resized.viewport.width > first.viewport.width &&
              resized.viewport.y > first.viewport.y,
          "Resize/DPI must update physical viewport geometry");
}

void CheckInput() {
    using namespace BuiViewport;
    const MBUIRect view{10, 100, 600, 500};
    Check(ToPixel(50.f, 800, 1600) == 100 && ToPixel(-0.1f, 800, 1600) == -1 && ToPixel(800.f, 800, 1600) == 1600,
          "Bad logical-to-pixel mapping");
    Check(Contains(view, 10, 100) && !Contains(view, 610, 100) && !Contains(view, 10, 600),
          "Bad half-open viewport edges");
    Input input;
    Check(!input.BeginLook(false), "Mouse down over chrome must not start camera");
    input.SetKey(Key::Forward, true);
    input.Motion(20.f, 10.f);
    input.Wheel(false, 2.f);
    auto d = input.Take();
    Check(!d.forward && !d.yaw && !d.dolly, "Chrome input leaked to camera");
    Check(input.BeginLook(true), "Mouse down inside the viewport must start camera");
    input.SetKey(Key::Forward, true);
    input.Motion(3.f, -4.f);
    input.Wheel(true, 2.f);
    d = input.Take();
    Check(d.forward == 1 && d.yaw == 3 && d.pitch == -4 && d.dolly == 2, "Viewport input missing");
    d = input.Take();
    Check(d.forward == 1 && !d.yaw && !d.dolly, "Held keys and per-frame deltas must have different lifetimes");
    input.Motion(5.f, -3.f);
    input.EndLook();
    d = input.Take();
    Check(!d.forward && d.yaw == 5 && d.pitch == -3, "Releasing RMB must end flight, not drop prior drag");
    input.BeginLook(true);
    input.SetKey(Key::Forward, true);
    input.Motion(3.f, 4.f);
    input.Wheel(true, 1.f);
    input.Cancel();
    d = input.Take();
    Check(!input.Looking() && !d.forward && !d.yaw && !d.dolly, "Focus loss must cancel all queued camera input");
}

void CheckFrameScope() {
    int scene = 0, composite = 0, closes = 0;
    auto frame = [&](bool success) {
        BuiViewport::FinishFrame end([&] {
            Check(scene == closes + 1, "Coordinator close must follow scene recording");
            ++closes;
        });
        ++scene;
        if (!success)
            return;
        Check(scene == closes + 1, "Composite must record before coordinator close");
        ++composite;
    };
    frame(true);
    frame(false);
    frame(true);
    Check(scene == 3 && composite == 2 && closes == 3, "Success and early return must close exactly once");
}
} // namespace

int main() {
    try {
        CheckRaster();
        CheckInput();
        CheckFrameScope();
        std::cout << "BUI raster, input, DPI, and frame scope contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
