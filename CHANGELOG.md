# Changelog

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
- Export respects all modifiers (Scale/Position, etc.)
- Cleaner build with MSVC 19.44 (VS 2022 BuildTools)
- `imgui.ini` removed from version control

### Bug Fixes

- Color byte order (R↔B swap) in `get_video_frame()` and `pipe_loop()`
- `ScalePosModifier::process()` now correctly uses `gSourceImageData`
- Infinite file dialog loops in video mode (`gOptTrackFile`/`gDirtyPic` guards)
- Export pipeline: `STARTF_USESTDHANDLES` without handle assignment no longer kills stdin
- `CREATE_NO_WINDOW` vs `DETACHED_PROCESS` selection for background export
- Binary mode (`_setmode`) for stdin/stdout in pipe mode
- Output ffmpeg now uses `-i -` to read from stdin, `-r` for framerate, `-vf scale` for scaling
- Fixed `main10` profile mismatch (changed to `main` for 8-bit yuv420p)

---

## 4.0 — Previous Release

Original release by Jari Komppa. Image-to-spectrum conversion GUI tool with:
- ZX Spectrum, ZX 3x64, C64 HiRes, C64 Multicolor device modes
- Stackable modifiers (quantize, dither, scale, position, etc.)
- PNG/SCR/H/INC export
- Real-time interactive preview
- Workspace save/load
