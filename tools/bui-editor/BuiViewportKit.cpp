#include "BuiViewportABI.h"

#include <BUI_kit.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#if defined(_WIN32)
#define MBUI_EXPORT __declspec(dllexport)
#else
#define MBUI_EXPORT __attribute__((visibility("default")))
#endif

namespace {
using Root = std::unique_ptr<BKNode, decltype(&BK_node_free)>;
using Context = std::unique_ptr<BKContext, decltype(&BK_context_free)>;
using Raster = std::unique_ptr<BuiRaster, decltype(&BUI_raster_free)>;

struct Kit {
    // Destruction order keeps the raster and tree alive while the context closes.
    Root root{BK_node_new("frame"), BK_node_free};
    Raster raster{BUI_raster_create(1, 1), BUI_raster_free};
    Context context{nullptr, BK_context_free};
    BKNode* view = nullptr;
    BKNode* reset = nullptr;
    int width = 0, height = 0;
    float scale = 0;
    bool resetRequested = false;
    uint64_t revision = 0;
    std::vector<uint8_t> rgba;
};

BKNode* Pack(BKNode* parent, const char* type, int side, bool grow, const char* label = nullptr) {
    auto* node = BK_node_new(type);
    if (!node)
        return nullptr;
    BK_node_add(parent, node, -1);
    BK_geom_set_manager(node, BK_GEOM_PACK);
    BK_geom_set(node, "side", side);
    BK_geom_set(node, "expand", grow);
    BK_geom_set(node, "fill", grow ? 3 : 1);
    if (label)
        BK_set_str(node, "text", label);
    return node;
}

void Emit(void* self, BKNode* source, int kind, void*, double, double) noexcept {
    auto& kit = *static_cast<Kit*>(self);
    if (source == kit.reset && kind == BK_EV_CLICK)
        kit.resetRequested = true;
}

void* Create() noexcept {
    try {
        auto kit = std::make_unique<Kit>();
        if (!kit->root || !kit->raster)
            return nullptr;
        BK_set_num(kit->root.get(), "padding", 8);
        if (!Pack(kit->root.get(), "label", 0, false, "MyEngine  /  Native BUI Scene View"))
            return nullptr;
        kit->reset = Pack(kit->root.get(), "button", 0, false, "Reset Camera");
        if (!Pack(kit->root.get(), "label", 1, false, "RMB: Look    RMB + WASDQE: Fly    Scroll: Dolly"))
            return nullptr;
        kit->view = Pack(kit->root.get(), "frame", 0, true);
        if (!kit->view || !kit->reset)
            return nullptr;
        kit->context.reset(BK_context_new(kit->root.get()));
        if (!kit->context)
            return nullptr;
        BKHost host{};
        host.data = kit.get();
        host.emit = Emit;
        BK_context_set_host(kit->context.get(), &host);
        return kit.release();
    } catch (...) {
        return nullptr;
    }
}

void Destroy(void* object) noexcept {
    delete static_cast<Kit*>(object);
}

int32_t Frame(void* object, int32_t width, int32_t height, float scale, MBUIFrame* frame) noexcept {
    if (!object || !frame || width <= 0 || height <= 0 || width > 8192 || height > 8192 || !std::isfinite(scale) ||
        scale <= 0)
        return 0;
    try {
        auto& kit = *static_cast<Kit*>(object);
        const bool resize = width != kit.width || height != kit.height || scale != kit.scale;
        if (resize) {
            kit.rgba.resize(static_cast<size_t>(width) * height * 4);
            BK_set_scale(scale);
        }
        if (resize || BK_needs_render(kit.context.get())) {
            BK_render(kit.context.get(), kit.raster.get(), width, height, resize);
            const auto* pixels = BUI_raster_pixels(kit.raster.get());
            if (!pixels || BUI_raster_width(kit.raster.get()) != width || BUI_raster_height(kit.raster.get()) != height)
                return 0;
            for (size_t i = 0; i < kit.rgba.size() / 4; ++i) {
                const uint32_t pixel = pixels[i]; // BUI's native 0x00RRGGBB.
                auto* output = &kit.rgba[i * 4];
                output[0] = static_cast<uint8_t>(pixel >> 16);
                output[1] = static_cast<uint8_t>(pixel >> 8);
                output[2] = static_cast<uint8_t>(pixel);
                output[3] = 255;
            }
            ++kit.revision;
            kit.width = width;
            kit.height = height;
            kit.scale = scale;
        }
        int rect[4]{};
        BK_node_rect(kit.view, rect);
        *frame = {kit.rgba.data(), kit.revision, {rect[0], rect[1], rect[2], rect[3]}};
        return 1;
    } catch (...) {
        return 0;
    }
}

void Pointer(void* object, int32_t kind, int32_t x, int32_t y) noexcept {
    if (!object)
        return;
    auto* context = static_cast<Kit*>(object)->context.get();
    switch (kind) {
    case MBUI_MOVE:
        BK_motion(context, x, y, 0);
        break;
    case MBUI_LEFT_DOWN:
        BK_press(context, x, y, 0);
        break;
    case MBUI_LEFT_UP:
        BK_release(context, x, y, 0);
        break;
    case MBUI_CANCEL:
        BK_cancel(context, 0);
        break;
    }
}

int32_t TakeReset(void* object) noexcept {
    if (!object)
        return 0;
    auto& kit = *static_cast<Kit*>(object);
    const auto requested = kit.resetRequested;
    kit.resetRequested = false;
    return requested;
}

const MBUIApi kApi{Create, Destroy, Frame, Pointer, TakeReset};
} // namespace

extern "C" MBUI_EXPORT const MBUIApi* MyEngineBuiGetApi(uint32_t version) {
    return version == MYENGINE_BUI_VIEWPORT_ABI ? &kApi : nullptr;
}
