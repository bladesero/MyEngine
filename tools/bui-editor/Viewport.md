# Native BUI + engine Scene View

`MyEngineBuiViewport` is the experimental renderer-integrated entry point.
It is separate from the read-only `MyEngineBuiEditor` browser and the full
`MyEngineEditor`. It loads a real scene through `SceneRenderLayer` with the
project's render path/device profile and the existing Scene View camera.
It does not save scenes, execute Play mode, or integrate Gizmo/Shader Graph.

## Frame and input contract

- SDL owns the only OS window; the engine RHI owns the only swapchain.
  The dynamically loaded BUI kit paints to a software raster. No BUI
  native-window loop, SDL renderer, or second swapchain is created.
- The BUI layout reports the Scene View rectangle in drawable pixels. The
  existing scene graph renders to its offscreen SceneColor at that size.
  The last graphics pass uses the engine's ScreenUI shader: first draw the
  BUI chrome, then sample SceneColor directly into the reserved rectangle.
  There is **no GPU scene readback**. The BUI texture is updated only when
  its raster revision changes. During shader prewarm, only the chrome is
  drawn.
- The scene layer has presentation disabled. Its existing coordinator opens
  the frame and the BUI layer closes the same coordinator once, after recording
  the composite. Hidden/minimized/no-drawable frames may do no presentation.
- SDL logical coordinates are converted to drawable pixels using each axis'
  actual scale. RMB starts look only inside the viewport, and RMB + WASDQE
  flies. A wheel event zooms only inside the viewport. Releasing RMB or
  losing focus stops capture; focus loss, hide, minimize and resize also clear
  held input. A left click on the BUI toolbar operates the Reset Camera button.
- BUI is built with its Dear ImGui compatibility binding disabled. Only the
  versioned C ABI crosses the kit/engine boundary. As with the previous
  experiment, this does not change the engine's license or publish a binary.

## Build and run (macOS)

From the repository root after checking out the migration branch:

```sh
cmake -S tools/bui-editor -B build/bui-kit -DCMAKE_BUILD_TYPE=Release
cmake --build build/bui-kit --target MyEngineBuiViewportKit

xmake f -y -m release
xmake build MyEngineBuiViewport
KIT="$PWD/build/bui-kit/libMyEngineBuiViewportKit.dylib"
xmake run MyEngineBuiViewport --kit "$KIT" --project "$PWD" --backend metal
xmake run MyEngineBuiViewport --kit "$KIT" --project "$PWD" --backend metal --smoke-frames 120
```

To build and run the headless tests in the same CMake directory, run
`cmake --build build/bui-kit` without a target, then
`ctest --test-dir build/bui-kit --output-on-failure`. On Windows, build
with `--config Release` if using a multi-config generator, point `--kit`
at the absolute path to `MyEngineBuiViewportKit.dll`, and pass
`--backend d3d11` or `--backend d3d12`. Vulkan is selectable only on a
build that has it enabled. The standalone CMake tests can run on Linux;
this branch's xmake engine app is enabled on macOS and Windows.

## Acceptance before porting additional panels

1. Open the repository's startup scene. Confirm the visible floor/objects
   are engine-rendered and confined to the BUI rectangle; there should be
   one window. Hold RMB inside and use WASDQE. Rotate, scroll, and reset
   the camera. Drag over the toolbar, resize, move between displays at
   different scale factors, and Alt-Tab while holding RMB and W. Confirm
   there is no stuck input and that the viewport fills its assigned region.
2. Run the 120-frame smoke. It fails if the scene/composite never becomes
   ready or the requested composites do not complete. Expected on success:
   `scene_composites=120 coordinator_closes_for_scene_frames=120`.
   The separate total includes any earlier chrome-only frames.
   This counter verifies the application frame boundary, **not** the
   driver-level Present call count.
3. Capture a steady frame on the target backend. On D3D12, look for the
   `BUI viewport composite` marker after the offscreen scene passes. Metal
   and D3D11 do not emit this marker. On those backends, identify the final
   backbuffer (drawable on Metal) color attachment by the two draws: the
   full-window BUI chrome quad, then the viewport quad sampling the scene's
   offscreen SceneColor. A missing label alone is not a rendering failure.
   Verify exactly one native presentation is submitted for the same rendered
   frame. If a Metal capture does not expose the count, use a symbolized
   build and log or break at the `presentDrawable:` call in
   `MetalContext::PresentSwapChain`, correlating hits with rendered-frame
   boundaries. This verifies scheduling a drawable, not display scanout.
   Repeat after resize; check for a second swapchain or validation errors.

The portable contract test exercises actual BUI raster drawing and button
input, plus coordinate conversion, input cancellation and the scoped frame
closure, including an early return. It does not create a platform GPU
device or substitute for steps 1–3.
