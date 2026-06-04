# Image Spectrumizer 5.0

![ScreenShot](https://raw.github.com/jarikomppa/img2spec/master/img2spec2.jpg)

GUI tool for converting images to ZX Spectrum (and similar retro-platform) format, with **video processing support**.

Originally by Jari Komppa — extended with video mode, pipe processing, and CLI batch features.

---

## Features

### Core (original)
- Convert images to ZX Spectrum, ZX 3x64, C64 HiRes, C64 Multicolor formats
- Stackable real-time modifiers: quantize, dither, scale, position, brightness, contrast, etc.
- Interactive preview with instant feedback
- Save as PNG, raw binary, C header (`.h`), assembler include (`.inc`), or SCR (`.scr`)
- Workspace save/load for non-destructive editing

### New in 5.0 — Video Mode
- **Load video files** — MP4, MOV, AVI, etc. via ffmpeg
- **Timeline scrubbing** — slider + frame-by-frame navigation
- **Play/pause** with configurable skip intervals
- **All modifiers apply** to every video frame in real time
- **Export video** — process entire video with modifiers, re-mux original audio:
  - NVIDIA NVENC (HEVC), AMD AMF (HEVC), or software x264 (H.264)
  - Configurable quality (CRF/QP) and scale multiplier (1×–32×)
  - Default output: `<input>_spmz.mp4` in current directory

### New in 5.0 — CLI & Pipe Processing

```
img2spec input.png workspace.isw -p output.png
```

| Flag | Description |
|------|-------------|
| `-p <file>` | Save PNG output |
| `-h <file>` | Save C header output |
| `-i <file>` | Save assembler output |
| `-s <file>` | Save SCR output |
| `--headless` | Suppress GUI (for server/automation) |
| `--batch-stdin` | Read batch jobs as JSON lines from stdin |
| `--pipe --width W --height H` | Process raw video frames via stdin/stdout |

**`--batch-stdin` format** — one JSON object per line:
```json
{"src":"input.png","workspace":"file.isw","dst":"output.png"}
```

**`--pipe` mode** — designed for ffmpeg integration:
```bash
ffmpeg -i video.mp4 -f rawvideo -pix_fmt rgb24 - |
  img2spec workspace.isw --pipe --width 1920 --height 1080 |
  ffmpeg -f rawvideo -pix_fmt rgba -s 256x384 -i - -c:v libx264 output.mp4
```

---

## Video Export Pipeline

The export pipeline uses three-stage chaining without intermediate disk I/O:

```
ffmpeg (extract frames) -> img2spec --pipe (apply modifiers) -> ffmpeg (encode + scale)
```

- Frames pass through anonymous pipes (stdin -> stdout)
- img2spec processes at device resolution (e.g., 256x384)
- Final ffmpeg scales output to `device_resolution x scale_multiplier`
- Audio is re-muxed from the source after video encoding completes

---

## Building

### Requirements
- **SDL2** — tested with v2.30.x (choco: `choco install sdl2`)
- **CMake** 3.20+ (choco: `choco install cmake.install`)
- **MSVC BuildTools 2022** or Visual Studio 2022
- **ffmpeg** in PATH (for video mode at runtime)

### Build commands

```bash
# Configure
cmake -B build -G "Visual Studio 17 2022" -DSDL2_DIR="C:\libraries\sdl2\cmake"

# Build
cmake --build build --config Release
```

Output: `build\Release\img2spec.exe`

---

## Device Modes

| Device | Resolution | Description |
|--------|-----------|-------------|
| **ZX Spectrum** | 256x192 | Standard Speccy screen (8x8 cells, 2 colors per cell) |
| **ZX 3x64** | 256x192 | Three sets of attributes, flicker-blended for ~192 colors |
| **C64 HiRes** | 320x200 | Commodore 64 high-resolution bitmap mode |
| **C64 Multicolor** | 160x200 | C64 multicolor mode (4 colors per 8x8 cell) |

---

## License

Copyright (c) 2015-2016 Jari Komppa — zlib/libpng license

```
This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from
the use of this software.
```

---

*For the original project: https://github.com/jarikomppa/img2spec*
