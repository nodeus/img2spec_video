# Changelog

## 5.8 — Export Range, Formats & Timeline Handles

### New Features

- **Export video moved to the File menu** — `File → Export video...` (enabled only with a loaded video); the button is removed from the Image window
- **Export formats: mp4 / mkv / gif** — format combo in the Export window, filename extension follows the format; mkv uses the same encoders without `+faststart`; gif is encoded in a single pass via `palettegen+paletteuse` (no audio track, encoder/quality hidden)
- **Export range (In/Out markers)** — export a part of the video: sliders + exact frame number fields + `Set In/Out to current` / `Full range` buttons in the Export window; decoder limited with `-ss` + `-frames:v`, audio input cut with `-ss`/`-t`, progress measured over the range duration
- **RMB-drag range markers on the timeline** — grab the In (green) / Out (blue) triangle with the right mouse button and drag it, preview follows the marker; LMB keeps scrubbing the current frame everywhere
- **Double-height timeline** with range edge guide lines; timeline parts outside the export range are dimmed

### Notes

- Keyframes outside the export range do not apply to a partial export (pipe frames are numbered from the range start)

---

## 5.7 — Per-Frame Dumps, Single-Pass Audio & fps Fix

### New Features

- **Per-frame dumps on video export** — Export window checkboxes save every processed frame as `temp/scr/frameNNNNNN.<ext>` (raw device dump) and `temp/png/frameNNNNNN.png` (device resolution, numbering matches `--keys` frames); new `--dump-scr <dir>` / `--dump-png <dir>` pipe flags also work standalone (e.g. macOS Terminal pipeline)
- **Per-device dump extensions** — new virtual `Device::dumpext()`: ZX Spectrum / ZXHalfTile → `.scr`, ZX3x64 → `.3sc` (1 pixel set + 2 attribute sets, non-standard), C64Hires → `.h64`, C64Multicolor → `.m64` (raw dumps carry no load address, so not `.prg`)
- **Cleanup temporary files now removes everything** — the 5 service files plus all of `temp/scr` and `temp/png` (directories themselves removed); factored into `export_cleanup_temp_files()`

### Improvements

- **Single-pass audio** — the source video is fed to the encoding ffmpeg as 2nd input (`-map 0:v:0 -map 1:a:0? -c:a aac -shortest`); the post-pass remux (`_tmp.mp4` + `move`, ffprobe audio probe, second ffmpeg run, ~100 lines) is deleted
- **Output framerate = input framerate** — probe prefers `avg_frame_rate` (fallback `r_frame_rate`, then 25 fps), exact num/den passed via `-framerate`; override with `-r` in Extra ffmpeg params; input fps shown in the Export window Settings line

### Bug Fixes

- **CIEDE2000 used radians as degrees** (upstream `robertkist/libdither#10`) — `hp_ave` went raw into `T` / `delta_theta` (constant-ish `T`, dead rotation term): 23/33 Sharma reference pairs failed (max err 6.63), hue sweep off by up to 29%; fixed with `RAD2DEG`, now 33/33 pass (local patch noted in `VENDOR.txt`)
- **CCIR distance assumed 0..255 inputs** — `FloatColor` channels are 0..1, so the luminance term was suppressed ~255x and palette-choice ordering differed from the author's intent; `/255` normalization removed (verified: new 0..1 ordering matches the original byte-scale ordering on ~1.9M grid comparisons)
- Combined ffprobe parser split `avg,r` fields with `%s`, gluing e.g. `0/0,30/1` into one field; now `%[^,]`

### Internal

- Merged PR #1 from `ruguevara/fix/macos-build` (macOS build shims in `nonwin32.h`, `--whole-archive` only for WIN32, portable `PATH_SEP`, macOS docs)

---

## 5.6 — libdither Dithering Engine

### New Features

- **Dithering powered by vendored libdither** (`src/libdither/`, upstream `7bf49c5`, see `VENDOR.txt`)
- **Error Diffusion Dither: 19 kernels** (was 7) — added Diagonal, ShiauFan 1/2/3, Diffusion 1D/2D, Fake Floyd-Steinberg, Atkinson, Steve Pigeon, Robert Kist, Stevenson-Arce, Xot; generic kernel sizes replace the fixed 3x5 grid
- **Ordered Dither: 43 matrices** (was 5) — Bayer up to 32x32, Blue Noise 128x128, Dispersed/Void dots, Non-Rectangular, Ulichney, Clustered Dot 1–11, Central/Balanced/Diagonal points, Magic Circle/45-degree/standard, Variable 2x2/4x4 with step, Interleaved Gradient with size/A/B/C
- **New Mono Dither modifier** — 11 luminance families: Threshold (+Auto via `auto_threshold`), Grid, Pattern, Dot Diffusion (3 diffusions x 9 class matrices), Dot Lippens, Variable Error Diffusion (Ostromoukhov/Zhou Fang), DBS (0–7), Kacker-Allebach, Riemersma (8 space-filling curves, original/improved), mono Error Diffusion, mono Ordered; mask apply switch Modulate (keeps hue) / Replace B/W + Invert + linear-gamma luma
- **Color distance: 10 modes** for color ditherers — Luminance, sRGB, Linear, HSV, LAB76, LAB94, LAB2000, sRGB CCIR, Linear CCIR, Tetrapal (device palettes exposed via new `Device::palette_count()/palette_entry()`)
- **Jitter (sigma + deterministic seed)** on color ditherers (upstream color path has no sigma; applied as pre-jitter in the bridge)

### Bug Fixes

- **Ordered dither was multiplicative with DC bias** — `Float += (M/div-0.5)*Float*mV` gave almost no dither in shadows and brightened the image (`1..N/N` ranges are not centered); replaced by libdither's additive recipe with `(M+0.5)/N-0.5` normalization
- **Removed non-standard `mErrorClamp`** — old `.isw` files load fine, the key is ignored

### Internal

- **Vendored `src/libdither/`** (28 C sources: ditherers, color models, kdtree, tetrapal, uthash) + `src/dither_bridge.h` adapter (registries, palette cache with per-frame hash drop, BGR float layout handling, R-L mirror trick)
- **CMake `C_STANDARD 11`** for the target (GCC 14+ defaults to C23 which breaks tetrapal's `bool` typedef; legacy v120 toolset cannot build C99 sources — use CMake-generated solution)
- New `MOD_MONODITHER` modifier type with full serialize/deserialize/keyframe-interpolation support

---
## 5.5 — Pipe Leak Fix, Pre-allocated Buffers & Code Extraction

### Bug Fixes

- **Video decoding died after ~500 frames** — `_pclose()` on the custom `_open_osfhandle`+`_fdopen` pipe leaked one CRT fd per decoded frame; fd table (512) exhausted, frame loads and export silently failed. Fixed with `fclose()` in `get_video_frame()`, `run_pipe()` and export audio probe. Added DIAG logging on pipe failures
- **BlurModifier memory leak** — per-frame `new float[]` without `delete[]` fixed via pre-allocated member buffer
- Child ffmpeg processes now get NUL stdin plus `-nostdin` flag instead of inheriting the console

### Improvements

- **Pre-allocated modifier buffers** — Blur, Edge, ErrorDiffusion, MinMax and ScalePos reuse member buffers, reallocated only on resolution change
- **Redundant `bitmap_to_float()` skipped** when an enabled ScalePos overwrites the buffer anyway
- **`floor()` removed from `float_to_color()`** hot path
- **Debounced keyframe sidecar writes** — disk write 500ms after last change instead of every slider drag
- **Single combined ffprobe call** in `load_video()` with legacy 3-call fallback
- **Cached histograms** — recomputed once per processed image
- **Navigation buttons use pending-frame mechanism** — no UI blocking

### Internal

- **Extracted `src/videopipeline.h`** — `get_video_frame()`, `load_video()`, ffprobe parsing
- **Extracted `src/keyframemanager.h`** — keyframe struct, globals and all `keyframe_*()` functions
- **Extracted `src/exportmanager.h`** — `start/poll/cancel_video_export()` and export runtime state
- **Extracted `src/imgui_utils.h`** — slider helpers moved out of `Modifier` base class
- Virtual destructor added to `Modifier` base so member buffers free correctly

---

## 5.4 — Keyframe Interpolation & Timeline Fix

### New Features

- **Keyframe interpolation** — smooth modifier parameter transitions between keyframes, enabled via checkbox in keyframe controls or `--interpolate` CLI flag
- Interpolation uses JSON numeric lerp; automatically falls back to step mode when device types or modifier stacks differ between keyframes

### Bug Fixes

- **Timeline frame display** — "Frame 0" was always shown during drag and playback due to undefined behavior in `ImFormatString` (`%d` format specifier used with float argument inside `SliderInt`); fixed by using `%.0f`

### Improvements

- `get_video_frame()` moved out of SliderInt callback to after `ImGui::Render()` to prevent UI blocking during drag
- Pending frame mechanism: frame loads after ImGui render for smoother timeline interaction

---

## 5.3 — Video Keyframes

### New Features

- **Video keyframes** — save full modifier + device snapshots at specific frames on the video timeline
- **Auto-capture** — parameter changes on the current frame automatically create/update a keyframe
- **Hold semantics** — keyframe settings apply from their frame until the next keyframe (no interpolation)
- **Timeline markers** — red diamond markers show keyframe positions on the timeline slider
- **Keyframe navigation** — jump between keyframes with `|< key`, `< key`, `> key`, `>| key` buttons
- **Sidecar storage** — keyframes saved as `<video>.keyframes.json` alongside the video file
- **Export with keyframes** — keyframes are passed to pipe mode via `--keys` flag during video export
- **Full snapshots** — each keyframe captures the entire state: modifier stack composition, device type, and all device/modifier options

### Internal

- **Refactored serialization** — extracted `serialize_snapshot_to_json()` and `deserialize_snapshot_from_json()` helpers, used by workspace save/load, video export, and keyframe system
- **`--keys <file>` flag** — new pipe mode argument to load keyframes for per-frame parameter switching during export

---

## 5.2 — Export Progress & UX Improvements

### New Features

- **Real-time export progress** — progress bar now reads `out_time=` from ffmpeg's `-progress` file, showing actual encoding progress (works at any loglevel, even `error` or `quiet`)
- **Configurable ffmpeg loglevel** — dropdown selector in Export panel lets you choose between `info`, `error`, `warning`, `verbose`, `debug` to control log verbosity
- **Auto-load `conv.isw`** — if `conv.isw` exists in the startup directory, it is automatically loaded on launch (useful for `--pipe` mode workflows)
- **Cleanup temporary files checkbox** — toggle to auto-delete temp files (`.bat`, `.isw`, `.log`, `_progress.txt`) after export completes, enabled by default

### Improvements

- **`-progress` file** — ffmpeg now writes machine-readable progress to `temp/img2spec_export_progress.txt` independently of loglevel
- **Progress fallback** — parses both `HH:MM:SS.xxxxxx` and numeric (seconds) formats from `-progress` output

---

## 5.1 — Export Pipeline Fixes

### Bug Fixes

- **Export first-frame freeze** — removed `-fflags nobuffer` from decoder ffmpeg, which caused frames to be output in decode order (instead of presentation order), leading to 150 duplicate/skipped frames (~2.5s) due to H.264 B-frame reordering
- **Export framerate consistency** — `-framerate` now used for rawvideo input in encoder ffmpeg (replaces `-r`), matching rawvideo demuxer semantics
- **Export decoder output** — removed redundant `-r` and `-vsync` options from decoder ffmpeg, letting it output at native source framerate without vsync interference

### Improvements

- All first-frame diagnostic code (`prev_buf`, `memcmp`, `dup_warn`) removed from `pipe_loop()` after issue resolution

---

## 5.0 — Video Mode & CLI Optimization

### Major Features

- **Video Mode** — load, scrub, and export video files (MP4, MOV, AVI, etc. supported by ffmpeg)
  - Timeline slider with frame-by-frame navigation
  - Play/pause with forward/backward skip buttons
  - Real-time preview of all modifiers applied to video frames
  - Export to HEVC (NVENC/AMF) or H.264 (x264) with configurable quality and scale
  - Audio remux from source video after export
  - Default export filename: `<input>_spmz.mp4`

- **`--pipe` Mode** — process frames via stdin/stdout pipeline
  - `img2spec workspace.isw --pipe --width W --height H`
  - Reads raw RGB24 frames from stdin, outputs processed RGBA frames to stdout
  - Supports ScalePosModifier (full-resolution source preserved)
  - Frame-accurate pipeline for integration with external tools

- **`--batch-stdin` Mode** — batch process multiple images with different workspaces
  - Reads JSON lines from stdin: `{"src":"input.png","workspace":"file.isw","dst":"output.png"}`
  - Each line is a self-contained export job
  - Avoids Windows 32K-char command-line limit via streaming input

- **Headless CLI Mode** — `--headless` flag suppresses all GUI output
  - Combined with `--batch-stdin` for fully automated batch processing
  - Can run on machines without a display

### Improvements

- CLI arguments processed sequentially and independently
- `process_and_save()` helper reduces code duplication

---

## 4.0 — Previous Release

Original release by Jari Komppa. Image-to-spectrum conversion GUI tool with:
- ZX Spectrum, ZX 3x64, C64 HiRes, C64 Multicolor device modes
- Stackable modifiers (quantize, dither, scale, position, etc.)
- PNG/SCR/H/INC export
- Real-time interactive preview
- Workspace save/load
