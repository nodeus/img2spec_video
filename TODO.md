# TODO — Performance Audit & Improvement Plan

## Audit Summary

Full code audit of `src/main.cpp` (~3200 lines), modifier headers, and device headers.
Date: 2026-09-15. Version: 5.4.

---

## Phase 1: Critical Bugs (HIGH — fix immediately)

### 1.1 BlurModifier memory leak
- **File**: `src/blurmodifier.h:72`
- **Problem**: `float *buf = new float[...]` — no `delete[] buf`. Leaks ~6MB per frame with blur enabled.
- **Fix**: Add `delete[] buf` before end of `process()`, or better — pre-allocate as class member.

### 1.2 Redundant bitmap_to_float() call
- **File**: `src/main.cpp:352`
- **Problem**: `bitmap_to_float(gBitmapOrig)` is called unconditionally, then ScalePosModifier calls it again at `scaleposmodifier.h:121`, overwriting the result. 524K iterations wasted every frame.
- **Fix**: Move `bitmap_to_float()` into ScalePosModifier exclusively, or skip it in `process_image()` when ScalePos is in the stack.

### 1.3 Button callbacks block UI
- **File**: `src/main.cpp:2874-2889`
- **Problem**: Navigation buttons (`|<`, `<`, `-10`, `+10`, `>`, `>|`) call `get_video_frame()` directly in ImGui callbacks, freezing UI.
- **Fix**: All buttons should set `gVideoPendingFrame` instead. Frame loads after `ImGui::Render()`.

---

## Phase 2: Performance — Hot Path Optimizations (MEDIUM)

### 2.1 Pre-allocate modifier buffers
- **Files**: `errordiffusiondithermodifier.h:81`, `edgemodifier.h:111-112`, `blurmodifier.h:72`, `minmaxmodifier.h:101`, `scaleposmodifier.h:84`
- **Problem**: Heap allocation (`new float[]`) on every `process()` call. Edge allocates 2x 6MB, Blur allocates 18MB per call.
- **Fix**: Add buffer members to each modifier class. Resize only when resolution changes (check `gDevice->mXRes * gDevice->mYRes`).

### 2.2 Replace floor() in float_to_color()
- **File**: `src/main.cpp:256-258`
- **Problem**: `floor()` is expensive library call per pixel. Called for every pixel in `float_to_bitmap()` (up to 500K times).
- **Fix**: Replace with `(int)(value * 255.0f + 0.5f)` or simply `(int)(value * 255.0f)`.

### 2.3 Debounce sidecar file writes
- **File**: `src/main.cpp:1361, 1372` (calls `keyframe_save_sidecar()`)
- **Problem**: Full JSON serialize + disk write on every slider drag in video mode.
- **Fix**: Debounce — only save after 500ms of no changes. Use a timer flag.

### 2.4 Combine ffprobe calls in load_video()
- **File**: `src/main.cpp:1107-1138`
- **Problem**: 3 separate `run_pipe()` calls to ffprobe, each spawning a process.
- **Fix**: Single call: `ffprobe -v error -select_streams v:0 -show_entries stream=width,height,r_frame_rate -show_entries format=duration -of csv=p=0`

### 2.5 Cache histograms
- **File**: `src/main.cpp:2709-2720`
- **Problem**: `calc_histogram()` called 3 times per frame (on gBitmapOrig, gBitmapProc, gBitmapSpec) while histogram window is open.
- **Fix**: Compute only when `gDirty` is set. Cache results.

---

## Phase 3: Architecture — Separation of Concerns (MEDIUM)

### 3.1 Extract KeyframeManager
- **Lines**: `main.cpp:1157-1533`
- **What**: Move `VideoKeyframe` struct, `gKeyframes[]`, `gKeyframeCount`, and all `keyframe_*()` functions into `src/keyframemanager.h`.
- **Benefit**: Isolates ~380 lines of keyframe logic. Testable independently.

### 3.2 Extract VideoPipeline
- **Lines**: `main.cpp:1008-1155`
- **What**: Move `get_video_frame()`, `load_video()`, `run_pipe()`, video globals into `src/videopipeline.h`.
- **Benefit**: Video decode/export logic separated from UI.

### 3.3 Extract ExportManager
- **Lines**: `main.cpp:1578-2018`
- **What**: Move `start_video_export()`, `poll_video_export()`, `cancel_video_export()` into `src/exportmanager.h`.
- **Benefit**: ~440 lines of export logic isolated.

### 3.4 Remove ImGui coupling from Modifier base
- **File**: `src/modifier.h:23-86`
- **Problem**: `Modifier` base class directly calls ImGui. Impossible to use without GUI.
- **Fix**: Move `complexsliderfloat()`/`complexsliderint()` to a separate utility. Keep `Modifier::ui()` as virtual but accept a render context.

---

## Phase 4: Performance — Advanced (LOW, long-term)

### 4.1 Async video frame loading
- **Problem**: `get_video_frame()` blocks UI. Currently deferred via `gVideoPendingFrame` only for slider.
- **Fix**: Background thread for ffmpeg decode. Queue results. Show loading indicator.

### 4.2 Direct modifier interpolation (eliminate JSON round-trip)
- **File**: `main.cpp:1250` (`keyframe_build_interpolated`)
- **Problem**: JSON serialize→deep_copy→modify→deserialize→free per interpolated frame.
- **Fix**: Interpolate directly between live modifier objects. Add `lerp(Modifier* a, Modifier* b, float t)` to Modifier interface.

### 4.3 SIMD for simple modifiers
- **Files**: `rgbmodifier.h`, `contrastmodifier.h`, `superblackmodifier.h`, `noisemodifier.h`
- **Problem**: Per-pixel sequential loops for trivial operations.
- **Fix**: SSE2/AVX intrinsics for batch processing (load 8 floats, add/multiply, store).

### 4.4 Background thread for image processing
- **Problem**: `process_image()` + `gDevice->filter()` runs synchronously on main thread.
- **Fix**: Double-buffer: process into back buffer, swap on completion.

### 4.5 Binary keyframe format
- **Problem**: JSON snapshots are large and slow to parse.
- **Fix**: Binary format for keyframe storage (faster load/save, less memory).

---

## Execution Order

```
Phase 1 (bugs)     → DONE (commit caeb09b, branch develop/feature/perf-phase1-2)
Phase 2 (hot path) → DONE (commit caeb09b)
Phase 3 (arch)     → 3.1 DONE (keyframemanager.h, 661e301),
                      3.2 DONE (videopipeline.h, 5b0ec29),
                      3.3 DONE (exportmanager.h, 5acedf1),
                      3.4 OPEN (deferred: touches all 14 modifiers, no test coverage)
Phase 4 (advanced) → DEFERRED (measured 2026-09-17: 30 frames pipe test,
                      0.34s w/o keys vs 0.35s with JSON interpolation —
                      round-trip ~0.3ms/frame vs ~11ms processing, no ROI)
```

## Verification

- Release build (VS2022, `build/Release/img2spec_video.exe`): 0 errors.
- Pipe test (`--pipe`, 5-modifier stack incl. Blur/Edge/MinMax/ErrorDiffusion/ScalePos,
  2 frames 256x192): output bit-identical to pre-change baseline.
- Pipe test with `--keys` + `--interpolate`: output bit-identical to baseline.

## Notes

- No tests exist. All changes must be verified by manual build + run.
- Build: CMake → VS2022 solution in `build/`. Binary: `build/Release/img2spec_video.exe`.
- Commit messages: `type: описание` (mix of English conventional + Russian).
- Remote: push to `img2spec_video` (GitHub), NOT `origin` (jarikomppa, read-only).
