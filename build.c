// =========================================================================
// build.c — THE ONLY FILE passed to the compiler.
//
// Every subsystem is #included in strict dependency order so the compiler
// sees the entire application as a single translation unit.
//
// Compile:
//   gcc build.c -o calbum.exe -mwindows -lgdi32 -lshell32 -lole32 -luuid -ld3d11 -ldxguid -lwindowscodecs -ld3dcompiler -O2
//   gcc build.c -o calbum.exe -mwindows -lgdi32 -lshell32 -lole32 -luuid -ld3d11 -ldxguid -lwindowscodecs -ld3dcompiler -O0 -g -Wall -Wextra
// =========================================================================

// ── 1. System Headers ───────────────────────────────────────────────────
#include <windows.h>
#include <initguid.h>
#include <knownfolders.h>
#include <stdint.h>

// ── 2. Third-Party Libraries (implementations) ──────────────────────────
#define STB_DXT_IMPLEMENTATION
#include "lib/stb_dxt.h"

// ── 3. Shared Types & Inline Utilities ──────────────────────────────────
#include "src/types.h"

// ── 4. Low-Level Subsystems ────────────────────────────────────────────
#include "src/renderer.c"

// ── 5. OS & Data Subsystems ────────────────────────────────────────────
#include "src/file_scanner.c"
#include "src/image_loader.c"

// ── 6. Background Thread Subsystems ─────────────────────────────────────
#include "src/file_monitor.c"
#include "src/asset_worker.c"

// ── 7. Application Logic ───────────────────────────────────────────────
#include "src/gallery.c"
#include "src/app.c"

// ── 8. Entry Point ─────────────────────────────────────────────────────
#include "src/main.c"
