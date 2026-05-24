# calbum

> High-performance native Windows image gallery — built for speed, minimalism, and direct OS interaction.

![CI](https://github.com/yashraj/calbum/workflows/CI/badge.svg)
![Platform](https://img.shields.io/badge/platform-Windows-blue)
![Language](https://img.shields.io/badge/language-C17-555555)
![License](https://img.shields.io/badge/license-MIT-green)

---

## Overview

**calbum** is a lightweight, native Windows image gallery application that loads instantly and uses minimal memory. It scans your Pictures folder (or any dropped directory), displays images in a scrollable gallery grid, and lets you view them full-screen — all with zero framework overhead.

### Features

- **Auto-scan** — Opens your `%USERPROFILE%\Pictures` folder on startup
- **Recursive directory scanning** — Finds images in subfolders
- **Gallery view** — Lazy-loaded thumbnails in a responsive grid (scrollable, clickable)
- **Full-image view** — Zoom-to-fit display with aspect ratio preservation
- **Drag-and-drop** — Drop any folder to browse its images
- **Keyboard navigation** — Arrow keys, Home/End, Space/Enter, Escape
- **Double-buffered rendering** — No flicker during paint

### Supported Image Formats

JPEG, PNG, BMP, GIF, WebP, TGA, TIFF, PSD

---

## Build

### Prerequisites

[w64devkit](https://github.com/skeeto/w64devkit) — a lightweight, portable C/C++ toolchain for Windows (no Visual Studio needed). Already installed at:

```
P:\Applications\w64devkit\w64devkit
```

### Quick Start

```bash
make release   # optimized build
make run       # build and launch
make debug     # debug build with symbols
make clean     # remove artifacts
```

Or compile directly with GCC:

```bash
gcc build.c -o calbum.exe -mwindows -lgdi32 -lshell32 -O2
```

### Build Targets

| Target      | Description                                          |
|-------------|------------------------------------------------------|
| `release`   | Unity build, `-O2`, GUI binary                       |
| `debug`     | Unity build, `-O0 -g -Wall -Wextra -Wpedantic`       |
| `dev`       | Standard (non-unity) build, faster recompilation      |
| `run`       | Build release and launch                             |
| `clean`     | Delete build artifacts                               |
| `test`      | Build and run unit tests                             |
| `format`    | Run `clang-format` on all source                     |
| `lint`      | Run `clang-tidy` static analysis                     |
| `size`      | Show binary size and source stats                    |

---

## Testing

```bash
make test
```

The test runner compiles a console executable that exercises pure functions:

- Grid layout calculation (`gal_calc_layout`)
- Hit-test targeting (`gal_hit_test` with dynamic padding calculation)
- Selection boundary logic
- Scroll bounds clamping
- Data structure invariants

**Note:** Tests are automatically run as a strict gatekeeper when invoking `make all`.

All tests are in [`tests/test_main.c`](tests/test_main.c).

---

## Architecture

### Design Philosophy

The project follows **data-oriented design** principles:

- **Single translation unit (unity build)** — All `.c` files are `#include`d into `build.c`, enabling aggressive cross-module optimization and sub-second compile times
- **Immediate-mode rendering** — UI is drawn fresh every frame; no retained widget tree
- **Lazy loading** — Thumbnails and full images are loaded on demand, not at startup
- **Minimal dependencies** — Only the Win32 API and [stb_image.h](https://github.com/nothings/stb) (single-file header library)

### Module Map

| Module               | Responsibility                                |
|----------------------|----------------------------------------------|
| `calbum.c`           | Entry point (`WinMain`), window procedure, event dispatch |
| `calbum.h`           | Shared types (`AppState`, `ImageEntry`), function declarations |
| `file_scanner.c`     | Recursive directory scan, image extension filtering |
| `image_loader.c`     | Thumbnail/full-image loading via stb_image, HBITMAP conversion |
| `gallery.c`          | Gallery grid layout, full-image viewer, hit testing, scrolling |
| `build.c`            | Unity build master — includes `stb_image.h` implementation + all sources |

### Directory Layout

```
calbum/
├── build.c            # Unity build entry point
├── calbum.c           # Application main
├── calbum.h           # Header / types
├── file_scanner.c     # File system scanning
├── gallery.c          # Gallery rendering
├── image_loader.c     # Image loading
├── stb_image.h        # stb_image (single-file library)
├── Makefile           # Build system
├── .clang-format      # Code formatting rules
├── .clang-tidy        # Static analysis configuration
├── .github/
│   └── workflows/
│       └── ci.yml     # GitHub Actions CI
├── tests/
│   └── test_main.c    # Unit tests
├── agents.md          # Context map for AI coding assistants
├── README.md          # This file
├── LICENSE            # MIT License
└── .gitignore
```

---

## Code Quality

This project maintains a high quality bar:

- **Static analysis** via `clang-tidy` (configured in `.clang-tidy`)
- **Code formatting** via `clang-format` (configured in `.clang-format`)
- **No file exceeds 1,000 lines** — each module has a focused responsibility
- **No dynamic dispatch** — direct function calls, no v-tables
- **No C runtime dependencies in main loop** — only Win32 API calls

### Style Guide

- C17 (C11 with GNU extensions via MinGW-w64)
- Allman brace style
- 4-space indentation (no tabs)
- 120-character column limit
- `snake_case` for functions and variables
- Prefixes: `fs_` (file scanner), `il_` (image loader), `gal_` (gallery)
- Hungarian notation avoided — types are explicit

---

## License

[MIT](LICENSE)
