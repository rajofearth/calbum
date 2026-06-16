// =========================================================================
// build.c — THE ONLY FILE passed to the compiler.
//
// Every subsystem is #included in strict dependency order so the compiler
// sees the entire application as a single translation unit.
//
// Compile:
//   gcc build.c -o calbum.exe -mwindows -lgdi32 -lshell32 -lole32 -luuid
//     -ld3d11 -ldxguid -lwindowscodecs -ld3dcompiler -ldwmapi -ld2d1 -ldwrite
//     -O2
//   gcc build.c -o calbum.exe -mwindows ... -O0 -g -Wall -Wextra
// =========================================================================

// ── 1. System Headers ───────────────────────────────────────────────────
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <initguid.h>
#include <knownfolders.h>
#include <stdint.h>

// ── 2. Third-Party Libraries (implementations) ──────────────────────────
#define STB_DXT_IMPLEMENTATION
#include "lib/stb_dxt.h"

// ── 3. Application Constants ───────────────────────────────────────────
#include "src/constants.h"

// ── 4. Shared Types & App Manifest ──────────────────────────────────────
// types.h provides all type definitions and forward declarations.
// Each library .c file includes it via `#include "src/types.h"`.
#include "src/types.h"

// ── 5. Core / Utility Libraries ────────────────────────────────────────
#include "lib/core/utils.c"
#include "lib/core/logger.c"

// ── 6. GPU Subsystem ────────────────────────────────────────────────────
#include "lib/gpu/device.c"
#include "lib/gpu/shader.c"
#include "lib/gpu/texture.c"
#include "lib/gpu/d2d.c"
#include "lib/gpu/fullimage.c"

// ── 7. UI Widgets ───────────────────────────────────────────────────────
#include "lib/ui/ui.c"

// ── 8. OS & Data Subsystems ────────────────────────────────────────────
#include "lib/fs/scanner.c"
#include "lib/image/loader.c"
#include "lib/fs/monitor.c"

// ── 9. App Logic ───────────────────────────────────────────────────────
#include "src/layout.c"
#include "src/gallery_sort.c"
#include "src/gallery_fullimage.c"
#include "src/gallery.c"
#include "src/asset_worker.c"
#include "src/app.c"

// ── 10. Entry Point ─────────────────────────────────────────────────────
#include "src/main.c"

// ── 11. Event Handlers ────────────────────────────────────────────────
#include "src/events.c"
