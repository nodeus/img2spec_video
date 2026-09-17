# Image Spectrumizer 5.5

![Screenshot](img2spec2.jpg)

GUI tool for converting images to ZX Spectrum and retro-platform formats, with **video processing**, **keyframe interpolation**, and **CLI pipe mode**.

Originally by [Jari Komppa](https://github.com/jarikomppa/img2spec). Extended with video mode, keyframes, CLI batch/pipe, and export pipeline by [nodeus](https://nodeus.ru).

**[Описание на русском языке / Russian description](README_ru.md)**

### Screenshots

| Main window | Keyframes UI | Export |
|-------------|-------------|--------|
| ![Main](img/main-window.png) | ![Keyframes](img/keyframes-ui.png) | ![Export](img/export-window.png) |

---

## Features

### Image Conversion

Convert images to retro-platform bitmap formats with real-time interactive preview:

| Device | Resolution | Description |
|--------|-----------|-------------|
| **ZX Spectrum** | 256x192 | Standard Speccy screen (8x8 cells, 2 colors per cell) |
| **ZX 3x64** | 256x192 | Three attribute sets, flicker-blended for ~192 colors on CRT |
| **C64 HiRes** | 320x200 | Commodore 64 high-resolution bitmap mode |
| **C64 Multicolor** | 160x200 | C64 multicolor mode (4 colors per 8x8 cell) |

### Modifiers (14 stackable, real-time)

ScalePos, Quantize, Ordered Dither, Error Diffusion Dither, Edge, Blur, Min/Max, HSV, YIQ, RGB, Contrast, Curve, Noise, SuperBlack

### Export Formats

PNG, raw binary SCR (`.scr`), C header (`.h`), assembler include (`.inc`)

### Video Mode

- Load video files (MP4, MOV, AVI, etc.) via ffmpeg
- Timeline slider with frame-by-frame navigation
- Play/pause with forward/backward skip buttons
- All modifiers apply to every frame in real time
- Export with NVIDIA NVENC (HEVC), AMD AMF (HEVC), or software x264 (H.264)
- Configurable quality (CRF/QP) and scale multiplier (1x-32x)
- Audio re-muxed from source after export

### Video Keyframes

- Save full modifier + device snapshots at specific frames
- Auto-capture: any parameter change on current frame creates/updates a keyframe
- Hold semantics: settings apply from keyframe until the next one
- **Interpolation**: smooth parameter transitions between keyframes (checkbox + `--interpolate` CLI flag)
- Timeline markers (red diamonds) show keyframe positions
- Navigation buttons: `|< key`, `< key`, `> key`, `>| key`
- Sidecar storage: `<video>.keyframes.json` next to the video file
- Full snapshots: modifier stack, device type, and all options

### CLI & Pipe Mode

```
img2spec input.png workspace.isw -p output.png
```

| Flag | Description |
|------|-------------|
| `-p <file>` | Save PNG output |
| `-h <file>` | Save C header output |
| `-i <file>` | Save assembler include output |
| `-s <file>` | Save SCR output |
| `--pipe --width W --height H` | Process raw RGB24 frames via stdin/stdout |
| `--interpolate` | Enable keyframe interpolation in pipe mode |
| `--keys <file>` | Load keyframes for per-frame parameter switching |
| `--batch-stdin` | Read batch jobs as JSON lines from stdin |

**Batch format** (one JSON object per line):
```json
{"src":"input.png","workspace":"file.isw","dst":"output.png"}
```

**Pipe mode** (ffmpeg integration):
```bash
ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 - |
  img2spec workspace.isw --pipe --width 1920 --height 1080 |
  ffmpeg -f rawvideo -pix_fmt rgba -s 256x384 -i - -c:v libx264 output.mp4
```

### Video Export Pipeline

```
ffmpeg (decode) -> img2spec --pipe (process) -> ffmpeg (encode + scale)
```

- Frames pass through anonymous pipes (no disk I/O)
- img2spec processes at device resolution (e.g. 256x384)
- Final ffmpeg scales output to `device_resolution x scale_multiplier`
- Audio re-muxed from source after video encoding completes
- Requires ffmpeg in PATH or in the program folder

---

## Building

### CMake (cross-platform)

```bash
mkdir build && cd build
cmake ..
make
```

Dependencies: SDL2, OpenGL. On Linux: GTK3. On macOS: AppKit.

### Visual Studio

Open `img2spectrum.vcxproj`. v120 toolset (VS2013). Win32 and x64 configs. SDL2 expected at `\libraries\sdl2\`.

### MinGW cross-compile (Linux to Windows)

```bash
./build_w32.sh   # i686, static
./build_w64.sh   # x86_64, static
```

---

## Usage

```bash
# Image conversion
img2spec cat.png mush.isw -h cat.h

# Video export
img2spec video.mp4 workspace.isw -p output.mp4

# Pipe mode
ffmpeg -i input.mp4 -f rawvideo -pix_fmt rgb24 - | \
  img2spec workspace.isw --pipe --width 1920 --height 1080 | \
  ffmpeg -f rawvideo -pix_fmt rgba -s 256x384 -i - -c:v libx264 output.mp4

# Batch mode
echo '{"src":"img.png","workspace":"conv.isw","dst":"out.png"}' | img2spec --batch-stdin
```

---

## Libraries & Licenses

| Library | License | URL |
|---------|---------|-----|
| **img2spec** | zlib/libpng | https://github.com/jarikomppa/img2spec |
| **SDL2** | zlib | https://www.libsdl.org/ |
| **Dear ImGui** | MIT | https://github.com/ocornut/imgui |
| **Parson** | MIT | https://github.com/kgabis/parson |
| **stb libraries** | Public Domain | https://github.com/nothings/stb |
| **ffmpeg** | GPL/LGPL | https://ffmpeg.org/ |

---

## Links

- Original project: https://github.com/jarikomppa/img2spec
- Fork (video + keyframes + CLI): https://github.com/nodeus/img2spec_video
- Author: https://nodeus.ru
