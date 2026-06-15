# calbum v0.1.0 — Initial Release

> **High-performance native Windows image gallery** — built for speed, minimalism, and zero framework overhead.

### Downloads
| File | Platform |
|------|----------|
| calbum-x64.exe | 64-bit Intel/AMD Windows |
| calbum-arm64.exe | 64-bit ARM Windows |

> **Note:** This is a portable, unsigned executable. Windows SmartScreen / Defender may display an "unrecognized publisher" warning. To dismiss it, click **More info** → **Run anyway**. This warning exists because the binary is not code-signed. We plan to address this in a future release.

---

## Features

### Gallery View
- **Auto-scan** — Opens your `%USERPROFILE%\Pictures` folder at startup
- **Recursive directory scanning** — Automatically finds images in subfolders
- **Responsive grid** — Lazy-loaded thumbnails with smooth scrolling
- **Sort images** — By Date Created, Date Modified, or Size (ascending/descending)
- **Folder navigation** — Parent folder navigation and subfolder drill-down
- **Drag-and-drop** — Drop any folder to browse its images

### Full-Image Viewer
- **Zoom-to-fit display** with aspect ratio preservation
- **Zoom in/out** (+ / - keys, or mouse wheel)
- **Pan** while zoomed (click and drag)
- **Keyboard navigation** — Arrow keys, Home/End, Space/Enter
- **Info overlay** — File name, dimensions, file size, date

### Performance
- **Direct3D 11 with Direct2D** — Hardware-accelerated, tear-free 60+ FPS rendering
- **BC1 texture compression** — GPU-friendly thumbnail storage
- **Background asset workers** — Non-blocking thumbnail decoding on 2 worker threads
- **Disk cache** — Thumbnails cached to `%LOCALAPPDATA%\calbum\Cache`
- **Arena allocation** — Zero-fragmentation memory model
- **Unity build** — Single translation unit for aggressive cross-module optimization

### UI & Quality of Life
- **Immersive dark mode** — Title bar matches the dark UI (Mica backdrop on Windows 11)
- **File monitoring** — Live directory change detection (add/remove/modify files)
- **DPI-aware** — Scales correctly across displays
- **Responsive layout** — Minimum window size, adaptive column count

### Supported Formats
JPEG, PNG, BMP, GIF, WebP, TGA, TIFF, PSD

---

## Known Issues

1. **Unsigned executable** — See note above about SmartScreen

---

## Changelog

### v0.1.0 (2026-06-16)

- Initial public release
- Gallery browsing with lazy-loaded thumbnails
- Full-image viewer with zoom and pan
- Recursive directory scanning with file-change monitoring
- Sort by date created, date modified, and file size
- Immersive dark mode with Mica backdrop
- Drag-and-drop folder support
- Keyboard navigation throughout
- Background asset worker pool (2 threads)
- BC1-compressed thumbnail disk cache
- Direct3D 11 / Direct2D rendering pipeline

---
