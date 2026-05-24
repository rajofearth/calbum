# calbum

High-performance native Windows application — built for speed, minimalism, and direct OS interaction.

## Development Environment

This project uses **[w64devkit](https://github.com/skeeto/w64devkit)** — a lightweight, portable C/C++ toolchain for Windows that requires zero installation. No Visual Studio needed.

### Installation

w64devkit is already installed on this machine at:

```
P:\Applications\w64devkit\w64devkit
```

It has been added to your **User PATH**, so GCC should be available globally in any new terminal. If `gcc` isn't found, open a **fresh** terminal window (Start Menu → PowerShell/CMD).

### Compiling

From the project root, compile a Win32 GUI application:

```bash
gcc main.c -o calbum.exe -mwindows -lgdi32
```

**Flag quick reference:**

| Flag | Purpose |
|---|---|
| `-o <name>.exe` | Output executable name |
| `-mwindows` | Build a Windows GUI app (no console window) |
| `-lgdi32` | Link the GDI library for drawing |
| `-ldwmapi` | Link DWM (Desktop Window Manager) APIs |
| `-luser32` | Link user32 (windows, messages) |
| `-lkernel32` | Link kernel32 (files, processes) |
| `-O2` / `-O3` | Optimization flags for release builds |
| `-Wall -Wextra` | Enable compiler warnings |
| `-g` | Include debug symbols |

### Faster Compilation (Unity Build)

This project follows a **single translation unit** (unity build) approach. Instead of compiling many `.c` files separately, include them all into one master file:

```bash
gcc build.c -o calbum.exe -mwindows -lgdi32 -O2
```

This keeps compile times under 1 second and enables aggressive cross-module optimizations.

### Running the Launcher (Alternative to PATH)

If you prefer not to rely on the PATH, launch the w64devkit environment directly:

```
P:\Applications\w64devkit\w64devkit\w64devkit.exe
```

This opens a pre-configured terminal with GCC, Make, and all tools ready to use.

### Using from VS Code / Zed

Open the integrated terminal in your editor and compile normally — as long as `gcc` is on your PATH, it works everywhere.

## Knowledge Map

See [`agents.md`](agents.md) for the full context map covering:

1. **Win32 API** — application lifecycle, window procedures, message pump
2. **Memory & Data Architecture** — custom allocators, data-oriented design
3. **UI Architecture** — immediate mode GUI, layout, clipping
4. **Graphics & Rendering** — GDI prototyping, DirectX, text rasterization
5. **I/O & Multi-threading** — async I/O, IOCP, worker threads
6. **Development Pipeline** — unity builds, minimal dependencies, profiling

## Tooling

| Tool | Purpose |
|---|---|
| [w64devkit](https://github.com/skeeto/w64devkit) (GCC 16.1.0) | C/C++ compiler |
| [stb libraries](https://github.com/nothings/stb) | Single-file image loading, font parsing |
| Windows SDK (via MinGW-w64) | Win32 API headers |
