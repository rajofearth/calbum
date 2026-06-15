# calbum

> High-performance native Windows image gallery — built for speed, minimalism, and direct OS interaction.

![Platform](https://img.shields.io/badge/platform-Windows-blue)
![Language](https://img.shields.io/badge/language-C17-555555)
![License](https://img.shields.io/badge/license-GPL--2.0-blue)

---

## Overview

**calbum** is a lightweight, native Windows image gallery application that loads instantly and uses minimal memory. It scans your Pictures folder (or any dropped directory), displays images in a scrollable gallery grid, and lets you view them full-screen — all with zero framework overhead.

### Features

- **Auto-scan** — Opens your `%USERPROFILE%\Pictures` folder on startup
- **Recursive directory scanning** — Finds images in subfolders with live file monitoring via `ReadDirectoryChangesW`
- **Gallery view** — Lazy-loaded thumbnails in a responsive grid with folder grouping and subfolder drill-down
- **Full-image viewer** — Zoom-to-fit display with zoom (1x–8x, mouse wheel or +/- keys), mouse-drag pan, and an info overlay
- **Thumbnail strip** — Bottom strip in full-image view with smart preloading and LRU eviction
- **Sort & filter** — Sort images by Date Created, Date Modified, or Size (ascending/descending)
- **File monitoring** — Live directory change detection: files added, removed, or modified update the gallery in real time
- **Drag-and-drop** — Drop any folder to browse its images
- **Keyboard navigation** — Arrow keys, Home/End, Space/Enter, Escape, Back
- **Immersive dark mode** — Mica backdrop with matching title bar; reacts to OS accent changes
- **High-DPI aware** — Scales correctly across displays; responds to `WM_DPICHANGED`
- **Hardware-accelerated rendering** — Direct3D 11 instanced drawing with Direct2D text, double-buffered at up to 60 FPS

### Background Infrastructure

- **Asset worker thread pool** (4 threads) — Decodes and compresses thumbnails via WIC in the background
- **Disk thumbnail cache** — BC1-compressed thumbnails cached to `%LOCALAPPDATA%\calbum\Cache`; FNV-1a hashed paths
- **GPU texture pool** — Up to 100 BC1-compressed textures in a `Texture2DArray` with LRU eviction
- **Blur effect** — 25-tap Gaussian blur shader for UI panels
- **Arena allocator** — 16 MB main arena + 2 MB nav arena, zero fragmentation, reset on folder load

### Supported Image Formats

JPEG (`.jpg`, `.jpeg`), PNG, BMP, GIF, WebP, TGA, TIFF (`.tiff`, `.tif`), PSD

---

## Build

### Prerequisites

[w64devkit](https://github.com/skeeto/w64devkit) — a lightweight, portable C/C++ toolchain for Windows (no Visual Studio needed).

### Quick Start

```bash
make release   # optimized build (-O2, GUI subsystem)
make run       # build and launch
make debug     # debug build with symbols
make clean     # remove artifacts
```

Or compile directly with GCC:

```bash
gcc build.c -o calbum.exe -mwindows -std=c17 -O2 -Wall -Wextra \
    -lgdi32 -lshell32 -luser32 -lkernel32 -lole32 -luuid -ld3d11 -ldxguid \
    -lwindowscodecs -ld3dcompiler -ldwmapi -ld2d1 -ldwrite
```

Cross-compilation (x64 / arm64) is supported via llvm-mingw:

```bash
make release CC=x86_64-w64-mingw32-gcc TARGET=calbum-x64.exe
make release CC=aarch64-w64-mingw32-gcc TARGET=calbum-arm64.exe
```

### Build Targets

| Target      | Description                                          |
|-------------|------------------------------------------------------|
| `release`   | Unity build, `-O2`, GUI binary                       |
| `debug`     | Unity build, `-O0 -g -Wall -Wextra -Wpedantic`       |
| `run`       | Build release and launch                             |
| `clean`     | Delete build artifacts                               |
| `test`      | Build and run unit tests                             |
| `format`    | Run `clang-format` on all source                     |
| `lint`      | Run `clang-tidy` static analysis (summary)           |
| `lint-full` | Full `clang-tidy` output                             |
| `lint-fix`  | Auto-fix mechanical warnings                         |
| `size`      | Show binary size and source stats                    |

---

## Testing

```bash
make test
```

The test runner compiles a console executable that exercises core logic:

- Grid layout calculation (`gal_calc_layout`)
- Hit-test targeting (`gal_hit_test`)
- Selection boundary and scroll clamping
- Zoom/pan clamping
- View mode enum and image extension recognition
- Full-image UI interactions (back, info toggle, prev/next arrows)
- Zoom badge click-reset and level clamping
- Texture eviction state transitions (`NEW → LOADING → READY → RESIDENT_GPU → FAILED`)
- Adaptive load sizing and full-image cache eviction
- Folder grid population with `..` parent entry and subfolder counts
- Data structure invariants (arena, ring buffer, image entry)

**Note:** Tests are automatically run as a strict gatekeeper when invoking `make all`.

All tests are in [`tests/test_main.c`](tests/test_main.c).

---

## Architecture

### Design Philosophy

The project follows **data-oriented design** principles:

- **Single translation unit (unity build)** — All `.c` files are `#include`d into `build.c`, enabling aggressive cross-module optimization and sub-second compile times
- **Immediate-mode rendering** — UI is drawn fresh every frame; no retained widget tree
- **Lazy loading** — Thumbnails and full images are loaded on demand, not at startup
- **Minimal dependencies** — Primarily Win32/COM APIs (Direct2D, DirectWrite, Direct3D 11, WIC) and `stb_dxt.h` for BC1 compression
- **Function pointers used where appropriate** — Sort mode selection via `qsort` comparator, COM vtables for DirectX interfaces

### Module Map

| Module               | Responsibility                                |
|----------------------|----------------------------------------------|
| `main.c`             | Entry point (`WinMain`), window procedure, event dispatch, message loop |
| `types.h`            | Shared types (`AppState`, `ImageEntry`, `GridItem`), macros, inline utilities (arena, ring buffer, math) |
| `app.c`              | Application state management, folder loading, grid population, title updates |
| `file_scanner.c`     | Recursive directory scan, image extension filtering |
| `file_monitor.c`     | Background thread for real-time directory change notification via `ReadDirectoryChangesW` |
| `asset_worker.c`     | Background thread pool for decoding and BC1-compressing thumbnails via WIC |
| `image_loader.c`     | WIC-based image decoding, BC1 compression, full-image loading (capped at 2048px) |
| `renderer.c`         | Direct3D 11 / Direct2D / DirectWrite rendering context, HLSL shaders, GPU texture pool, blur |
| `gallery.c`          | Gallery grid layout, rendering, click handling, smooth scrolling, folder navigation |
| `gallery_sort.c`     | Sort comparators (date created, date modified, size) |
| `gallery_fullimage.c`| Full-image viewer with zoom, pan, info overlay, bottom thumbnail strip |
| `layout.c` / `layout.h` | Grid layout calculation, hit-testing, scroll clamping |
| `ui.c` / `ui.h`      | Reusable IMGUI component library (panels, buttons, badges, blur panels) |
| `utils.c` / `utils.h`| Format helpers: `format_size()` (human-readable bytes), `format_filetime()` (date string) |
| `build.c`            | Unity build master — includes all sources in dependency order |

---

## Code Quality

This project maintains a high quality bar:

- **Static analysis** via `clang-tidy` (configured in `.clang-tidy`)
- **Code formatting** via `clang-format` (configured in `.clang-format`)
- **No file exceeds 1,000 lines** — each module has a focused responsibility
- **Enforced C17 standard** via `-std=c17` in the Makefile
- **29 unit tests** covering layout, hit-testing, state machines, folder navigation, and cache eviction

### Style Guide

- C17 (C11 with GNU extensions via MinGW-w64)
- Allman brace style
- 4-space indentation (no tabs)
- 120-character column limit
- `snake_case` for functions and variables
- Module prefixes: `fs_` (file scanner), `il_` (image loader), `gal_` (gallery), `r_` (renderer), `aw_` (asset worker), `fm_` (file monitor), `ui_` (UI widgets), `app_` (app state)

---

## License

[GNU General Public License v2.0](LICENSE)
