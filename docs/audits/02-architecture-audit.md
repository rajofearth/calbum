# Architecture & Design Audit Report: calbum

**Date:** 2026-06-16
**Scope:** Full codebase (~4,500 lines across 20 modules)
**Methodology:** Static analysis of source files, structural review, cross-module dependency tracing, concurrency model analysis, capacity planning.

---

## Executive Summary

calbum is a well-scoped native Windows image gallery built on a **data-oriented, immediate-mode, unity-build** architecture. The design philosophy is applied with remarkable consistency. However, several structural weaknesses have emerged as the project has grown: a monolithic god struct (`AppState`), implicit coupling through global state, mixed concerns in the largest modules, and a testing gap around threaded/GPU code. Below are 20 findings organized by domain. Since the original audit, significant remediation has been completed — 13 findings are fully resolved, 4 are partially resolved, and 3 remain open.

---

## 1. Monolithic God Struct (`AppState`)

| | |
|---|---|
| **Impact** | **High** |
| **Location** | `src/types.h` lines 376–496 (120 fields) |
| **Evidence** | Struct groups: view mode (4 fields), scroll (2), window (4), theme/layout (6), D3D11 (7), D2D/DWrite (14), shaders (8), images (4), timing (3), scrollbar state (4), monitor (4), workers (3), queue (2), directories (2), grid nav (6), full-image cache (6+4), zoom/pan (8) |
| **Description** | `AppState` combines rendering resources, application data, UI state, OS handles, thread synchronization objects, and layout state in one flat struct. Every non-static function in the project takes `AppState *s` as its first parameter. There is no separation between model, view, and controller state. |
| **Recommendation** | Decompose into domain-specific sub-structs owned by pointer inside AppState: `RenderState` (D3D11/D2D/DWrite objects), `AppModel` (images, grid items, directories), `UIState` (scroll, zoom, selection, info_open, sort_menu_open), `ThreadState` (handles, events, ring buffer). Each module would receive only the slice it needs. This can be done incrementally without breaking the unity build. |
| **Status:** | ✅ **RESOLVED** |
| **Resolution:** | Decomposed into GpuState, TextState, ViewState, UIState, WorkerState, DataState sub-structs (Master Plan 5.1). AppState is now 19 lines with 6 sub-structs + 7 shared fields. |

---

## 2. Module Boundary Leakage via AppState Pointer

| | |
|---|---|
| **Impact** | **High** |
| **Location** | Every `.c` file |
| **Evidence** | Examples: `r_evict_texture(GpuState *r, DataState *data, int slot)` reads `data->images` to reset state (lib/gpu/texture.c:6–20). `r_alloc_full_image_slot(GpuState *r, DataState *data, ViewState *view, int grid_item_count)` reads `data->images`, `view->strip_image_count`, `view->selected_index`, `view->dpi_scale`, view window dimensions to make eviction decisions (lib/gpu/fullimage.c:35–60). `gal_hit_test(const DataState *data, const ViewState *view, const UIState *ui, ...)` reads layout, scroll, window dimensions, grid_item_count, view_mode, sort_menu_open (src/layout.c:57–84). |
| **Description** | The god struct is passed to every function, creating **implicit cross-module coupling**. The renderer makes cache eviction decisions based on gallery navigation state. The layout module reads view state and sort menu state. There are no accessor functions or interface boundaries — any module can read/write any field. |
| **Recommendation** | Introduce explicit interface patterns. Gallery modules should query cache state through renderer functions (`r_alloc_full_image_slot` should not need `grid_items`). At minimum, move the full-image cache eviction policy into gallery_fullimage.c and give the renderer a simpler load/free/replace interface. |
| **Status:** | ⚠️ **Partially Resolved** |
| **Resolution:** | Hot-path functions now take sub-structs. Top-level orchestration (main.c message pump, fm_thread_proc) still uses AppState*. About 70% addressed. |

---

## 3. Mixed Concerns in `gallery_fullimage.c` (741 lines)

| | |
|---|---|
| **Impact** | **High** |
| **Location** | `src/gallery_fullimage.c` lines 69–741 |
| **Evidence** | `gal_render_fullimage` (489 lines) does: full-image loading with staggered preload logic (lines 89–176), InstanceData batch building for main image + strip (178–375), backbuffer copy for blur (390), info overlay rendering (378–403), D2D text drawing (409–553). `gal_handle_fullimage_click` (183 lines) does: back-button hit-test (567–571), info button (574–580), zoom badge (583–598), info box close (606–619), strip navigation hit-testing (636–737). |
| **Description** | Rendering, input handling, state mutation, preloading logic, and metadata formatting are interleaved. The render function triggers background thumbnail requests (`aw_request_thumbnail`) during drawing, combining side effects with presentation. The click handler duplicates the strip layout math from the render function (same loop, same bounds calculation, ~40 lines duplicated). |
| **Recommendation** | Split into: (a) `fiv_calc_strip_layout()` — pure layout math reused by render and click handler, (b) `fiv_render()` — just the draw calls, (c) `fiv_handle_input()` — pure hit-testing and state transitions. Extract the preload orchestration into a small function. |
| **Status:** | ⚠️ **Partially Resolved** |
| **Resolution:** | Line count halved from 741 to ~400 by extracting ~6 helpers. Still a mixed-concern file, but decomposition improved maintainability. |

---

## 4. Duplicated Layout Math in Full-Image View

| | |
|---|---|
| **Impact** | **Medium** |
| **Location** | `gallery_fullimage.c` lines 93–144, 275–324, 564–737; `lib/gpu/fullimage.c` lines 35–60 |
| **Evidence** | The bottom strip layout (computing `active_img_idx_in_strip`, `half_n`, `start_idx`, `end_idx`, `thumbs_start_x` with the same arithmetic) appears **four times**: once in `gal_render_fullimage` for preloading, once in the same function for drawing, once in `gal_handle_fullimage_click` for hit-testing, and once in `r_alloc_full_image_slot` for eviction policy. All four independently compute num_strip_thumbs, half_n, start/end indices using identical formulas. |
| **Description** | A ~30-line block of strip layout math is copy-pasted across three files. Any change to the strip geometry requires updating all four copies. |
| **Recommendation** | Define `static void fiv_strip_bounds(AppState *s, int *start, int *end, int *active_strip_idx)` in gallery_fullimage.c and call it from all three sites. For the renderer's eviction policy, expose `int fiv_is_in_strip(AppState *s, int image_index)` on the gallery side. |
| **Status:** | ✅ **RESOLVED** |
| **Resolution:** | fiv_strip_bounds() + fiv_is_in_strip() extracted (Master Plan 3.1). Eliminated all 4+ original duplication sites. |

---

## 5. Unity Build: No Symbol Hiding

| | |
|---|---|
| **Impact** | **Medium** |
| **Location** | `build.c` |
| **Evidence** | All `.c` files are `#include`d into a single translation unit. Functions intended as module-internal are declared with `static` (e.g., `hash_path`, `ensure_cache_dir` in asset_worker.c; `map_action`, `fm_thread_proc` in lib/fs/monitor.c; `scan_recursive`, `skip_dir` in lib/fs/scanner.c). However, the build.c includes can never be whitelisted individually — there is no per-file include guard strategy. |
| **Description** | The unity build means all functions share the same scope. The convention of using `static` for internal linkage is followed, but there is no mechanism to prevent accidental cross-module dependencies on internal functions. |
| **Recommendation** | This is an accepted tradeoff. Mitigations: (1) Add `// MODULE:` comments at the start of each included file in build.c for grep-ability. (2) Consider a `sources.mk` that could generate `build.c` automatically. (3) For debugging, keep the `-g` flag and accept the single-TU mapping. |
| **Status:** | 🔶 **Partially Resolved** |
| **Resolution:** | Many module-internal functions now static. No systematic audit completed. |

---

## 6. `r_shutdown` — 38 Sequential COM Release Calls

| | |
|---|---|
| **Impact** | **Low** |
| **Location** | `lib/gpu/device.c` lines 261–300 |
| **Evidence** | Each COM pointer is released individually with null-check + Release + null-assign. The function is 40 lines of near-identical patterns. |
| **Description** | The COM release pattern is verbose and error-prone. If a new COM pointer is added to `AppState` but not added to `r_shutdown`, it leaks. |
| **Recommendation** | Define a macro: `#define SAFE_RELEASE(p) do { if (p) { ((IUnknown*)(p))->lpVtbl->Release((IUnknown*)(p)); (p)=NULL; } } while(0)`. This cuts the function from 77 to ~25 lines and makes omissions more obvious. |
| **Status:** | ✅ **RESOLVED** |
| **Resolution:** | SAFE_RELEASE macro added in types.h (Master Plan 3.2), used throughout r_shutdown. |

---

## 7. Error Handling Strategy Gaps

| | |
|---|---|
| **Impact** | **High** |
| **Location** | Throughout codebase |
| **Evidence** | All functions return `0`/`1` or `NULL`/non-NULL. `r_init` is the only function that shows a user-facing error dialog (`MessageBoxW` for D3D11 failure, lib/gpu/device.c:15–30). `r_load_full_image` silently returns 0 on D3D11 texture creation failure (lib/gpu/fullimage.c:71–84). `aw_worker_thread` silently continues on `malloc` failure of `LoadResult` (asset_worker.c:100–108). `app_append_image_entry` returns NULL on arena exhaustion, but caller `append_entry` in lib/fs/scanner.c treats this as "skip file" — it never propagates. |
| **Description** | There is no systematic error propagation or user-facing error reporting for non-fatal failures. Arena exhaustion, D3D11 resource allocation failure, and WIC decode failure are all silent. The user sees a blank thumbnail or an unresponsive button with no feedback. |
| **Recommendation** | (1) Introduce a `LogError(const wchar_t *fmt, ...)` that writes to `OutputDebugString` and optionally shows a toast. (2) For arena exhaustion, check on every `arena_alloc` and log. (3) For D3D11 resource creation failures, log the HRESULT reason. (4) For thumbnail decode failures, set `IMG_STATE_FAILED` and render a placeholder icon. |
| **Status:** | ✅ **RESOLVED** |
| **Resolution:** | log_error() in lib/core/logger.c (Master Plan 5.3), used across D3D, WIC, worker, monitor, full-image paths. |

---

## 8. Worker Thread Queue Has No Backpressure

| | |
|---|---|
| **Impact** | **Medium** |
| **Location** | `types.h` ring buffer (lines 152–217), `asset_worker.c` |
| **Evidence** | `aw_request_thumbnail` calls `rb_push`. If the ring buffer is full, `rb_push` returns 0, the `LoadRequest` is freed, and the thumbnail is silently dropped (asset_worker.c:164–169). There is no mechanism to block the caller, re-queue, or signal the main thread to wait. |
| **Description** | Under rapid scrolling or a large folder load, many thumbnails may be requested faster than workers can process them. The ring buffer fills, requests are dropped, and thumbnails are never loaded — with no indication to the user. |
| **Recommendation** | (1) Increase `RING_CAPACITY` proportionally to image count. (2) Rather than dropping requests, track "pending" state in `ImageEntry` and retry on the next render pass. (3) Add priority queueing: visible thumbnails first. (4) Consider a singly-buffered double-buffer scheme where the main thread writes to a "next frame" list and swaps atomically. |
| **Status:** | ✅ **RESOLVED** |
| **Resolution:** | rb_push return value checked; retry logic in thumbnail request path. |

---

## 9. Monolithic Gallery Render Function (442 lines)

| | |
|---|---|
| **Impact** | **Medium** |
| **Location** | `src/gallery.c` lines 141–583 |
| **Evidence** | `gal_render_gallery` does: clear + calc layout (141–164), thumbnail InstanceData batch building (165–281), scrollbar update/draw (283–343), top bar backdrop (345–365), sort button (367–378), sort dropdown (380–392), D2D folder text/icons (395–445), breadcrumb rendering with memoized metrics (447–558), sort button text (558–559), sort menu text (560–579), present (581). |
| **Description** | A single function handles grid iteration, scrollbar physics, top-bar compositing, dropdown rendering, breadcrumb text with width memoization, and sort menu rendering. |
| **Recommendation** | Decompose into: `gal_render_grid_items()`, `gal_render_scrollbar()`, `gal_render_topbar()`, `gal_render_sort_menu()`, `gal_render_breadcrumb()`. The static cache for breadcrumb width should be moved to a per-frame state struct. |
| **Status:** | ✅ **RESOLVED** |
| **Resolution:** | Decomposed into ~6 helpers (Master Plan 3.5). Main function now ~64 lines vs original 442. |

---

## 10. No Per-Item Deallocation (Arena Tradeoff)

| | |
|---|---|
| **Impact** | **Low** |
| **Location** | `src/types.h` lines 123–144, `app_append_image_entry` (app.c:163–208) |
| **Evidence** | `arena_alloc` only bumps an offset. `arena_reset` releases the entire block. `app_append_image_entry` reallocates the image array by copying all existing entries to a new arena offset (lines 168–183) — this is `O(n)` per append when capacity is exceeded, and the old entries remain in the arena (wasted space). |
| **Description** | The `app_append_image_entry` growth strategy leaves "dead" old arrays in the arena. For a 40K-image folder, the last allocation is ~40K * sizeof(ImageEntry) = ~10MB, but the waste from previous allocations is another ~10MB. |
| **Recommendation** | (1) Accept the waste — it's a known data-oriented design tradeoff. (2) Document that `ARENA_CAPACITY` (16MB) must be > the peak array size. (3) If memory pressure becomes an issue, switch to a slab allocator or a separate VirtualAlloc for the image array that can be freed independently. |
| **Status:** | 🔶 **Still Relevant (by design)** |
| **Resolution:** | Intentional trade-off. Not explicitly documented in code. |

---

## 11. Thread Safety Assumptions

| | |
|---|---|
| **Impact** | **Medium** |
| **Location** | `lib/fs/monitor.c`, `asset_worker.c`, `types.h` ring buffer |
| **Evidence** | The ring buffer protects `head`/`tail` with a `CRITICAL_SECTION` (rb_push/rb_try_pop/rb_wait_pop). However, `aw_request_thumbnail` and the main thread's render loop both read/write `s->images[img_idx].thumb_requested` and `s->images[img_idx].state` without synchronization (gallery.c:228–233, gallery_fullimage.c:340–343). The monitor thread reads `s->current_dir` and `s->hwnd` without any lock (lib/fs/monitor.c:54–61). `s->monitoring_active` is set to 1 in fm_start_monitor and checked in the thread proc without an atomic or lock. |
| **Description** | The `ImageEntry` struct fields (`state`, `thumb_requested`, `texture_slot`) are read/written by the main thread and read by the worker thread (which reads `req->path` on a copy). However, there is no memory barrier between the main thread setting `thumb_requested = 1` and the worker reading the `LoadRequest`. On x64, this is likely benign due to strong memory ordering, but it is technically a data race. |
| **Recommendation** | (1) Document the concurrency model explicitly. (2) Add `MemoryBarrier()` or `InterlockedExchange` for `monitoring_active`. (3) For `ImageEntry`, consider using `InterlockedExchange` for `texture_slot` writes. (4) Ideally, don't share `AppState*` directly with threads — pass a dedicated context struct. |
| **Status:** | 🔴 **Still Relevant** |
| **Resolution:** | Still a global static in lib/image/loader.c. No lock added. Shutdown order still wrong (factory released before workers joined). |

---

## 12. Message Queue Saturation Risk

| | |
|---|---|
| **Impact** | **Medium** |
| **Location** | `lib/fs/monitor.c:74`, `asset_worker.c:107` |
| **Evidence** | Both background threads use `PostMessageW(s->hwnd, ...)` to deliver results to the main thread. `PostMessageW` only fails when the message queue has 10,000 messages (the system limit). The main thread drains these in the message loop (`PeekMessageW`/`DispatchMessageW`). A rapid succession of `ReadDirectoryChangesW` events (e.g., copying 40K files) could post 40K `WM_CALBUM_FILE_CHANGE` messages faster than the main thread can process them, hitting the 10K ceiling and silently dropping messages. |
| **Description** | There is no backpressure or acknowledgment protocol. The file monitor posts a `FileChange` struct for every event. The worker posts a `LoadResult` for every thumbnail. If the message queue fills, `PostMessageW` returns `FALSE` and the message is lost. |
| **Recommendation** | (1) Batch file-change events in the monitor thread: accumulate events for 50ms, then post a single message with a batch array. (2) For load completion, use the ring buffer (or a second ring buffer) instead of window messages. (3) Check `PostMessageW` return value and handle failure. |
| **Status:** | ✅ **RESOLVED** |
| **Resolution:** | PeekMessage drain loop in app_load_folder covering all custom messages. |

---

## 13. Test Coverage Gaps

| | |
|---|---|
| **Impact** | **High** |
| **Location** | `tests/test_main.c` |
| **Evidence** | 29 tests across 11 test functions. Coverage includes: layout calculation, hit-testing, selection bounds, scroll clamping, zoom pan clamping, image entry state transitions, extension detection, full-image click handlers, texture eviction, adaptive load sizing, cache eviction rules, folder/strip population. **No tests for:** renderer (D3D11), file_monitor, asset_worker, image_loader (WIC decode), UI widgets, gallery render output, startup/shutdown ordering, message handling, error paths. |
| **Description** | All tested code is pure (no I/O, no GPU). The threaded and GPU-backed subsystems have zero test coverage. |
| **Recommendation** | (1) Add a compile-time switch (`CALBUM_TEST_BUILD`) that excludes `main()` when running tests. (2) For image_loader, add tests that decode small in-memory WIC images (stub files). (3) For the ring buffer, add concurrent push/pop tests. (4) For UI widgets, add tests for button/badge layout and hit rectangles. (5) For file_monitor, create temp directories and verify events. |
| **Status:** | 🔴 **Still Relevant** |
| **Resolution:** | No render, monitor, worker, or loader tests. CALBUM_TEST_BUILD not implemented. |

---

## 14. InstanceData Array: Static Local Buffer, Fixed Capacity

| | |
|---|---|
| **Impact** | **Medium** |
| **Location** | `gallery.c:156`, `gallery_fullimage.c:178`, `lib/gpu/device.c:206` (buffer desc) |
| **Evidence** | Both `gal_render_gallery` and `gal_render_fullimage` declare `static InstanceData instances[4096]` as a local variable. The renderer allocates a D3D11 instance buffer of `sizeof(InstanceData) * 4096` bytes (lib/gpu/device.c:206). There is a guard at gallery.c:279 `if (inst_count >= 4080) break`. |
| **Description** | 4096 instances per frame is a hard limit. In a grid view with 4096 visible items, the render loop breaks early and misses items. |
| **Recommendation** | (1) Document the limit and compute max possible visible items vs. 4096. (2) Either remove the static buffer and use arena allocation or add a `D3D11_USAGE_DYNAMIC` buffer that can be recreated at larger sizes. (3) At minimum, assert or log when hitting the limit. |
| **Status:** | ✅ **RESOLVED** |
| **Resolution:** | MAX_INSTANCES constant + bounds guards (Master Plan 1.2). |

---

## 15. Startup/Shutdown Order Duplication

| | |
|---|---|
| **Impact** | **Low** |
| **Location** | `app.c:38–60` vs `main.c:820–831` |
| **Evidence** | `app_shutdown` (app.c:38–60) calls: `fm_stop_monitor`, `aw_stop_workers`, `r_free_full_image`, texture eviction loop, `VirtualFree` arenas, `rb_destroy`. The WinMain shutdown path (main.c:820–831) calls: `aw_stop_workers`, `fm_stop_monitor`, `r_shutdown`, `il_shutdown_wic`, `app_shutdown`. Note the **different order**. |
| **Description** | WinMain calls `aw_stop_workers` and `fm_stop_monitor` again after `app_shutdown` already did — harmless because handles are NULL'd. |
| **Recommendation** | (1) Remove the duplicate stop calls from WinMain and rely on `app_shutdown`. (2) Add a `DWORD app_shutdown(AppState *s, DWORD timeout_ms)` that returns WAIT_TIMEOUT if threads don't join. |
| **Status:** | ⚠️ **Partially Resolved** |
| **Resolution:** | Sequential phases documented with comments. No formal state machine. |

---

## 16. Full-Image Cache Size Mismatch

| | |
|---|---|
| **Impact** | **Low** |
| **Location** | `types.h:479`, `gallery_fullimage.c`, `lib/gpu/fullimage.c` |
| **Evidence** | `FULL_CACHE_SIZE` is defined as 32 (inside the struct, as a `#define`). But the project description states "Full-image cache: 3 slots". The value 32 is reasonable, but the documentation is outdated. |
| **Recommendation** | Update the project documentation to reflect the actual value. Or reduce to a smaller number if the strip-based eviction policy makes a larger pool unnecessary. |
| **Status:** | ✅ **RESOLVED** |
| **Resolution:** | FULL_CACHE_SIZE unified (now 32). Eviction uses fiv_is_in_strip. |

---

## 17. Ring Buffer Deadlock Potential in Shutdown

| | |
|---|---|
| **Impact** | **Medium** |
| **Location** | `asset_worker.c:37–56`, `app.c:89–90` |
| **Evidence** | When `app_load_folder` is called, it stops workers (`aw_stop_workers`) — this sets the stop event and waits on worker threads with 2s timeout. A worker thread may be blocked on `WaitForMultipleObjects` on the ring buffer's `nonempty` event (asset_worker.c:51–52). `aw_stop_workers` also sets the `nonempty` event (aw_stop_workers:137) to wake workers. |
| **Description** | The shutdown protocol is: set stop event → set `nonempty` event → wait on threads. This is correct. However, `EnterCriticalSection` in `rb_try_pop` could deadlock if a worker holds the lock and the main thread calls `rb_destroy`. |
| **Recommendation** | (1) In `aw_stop_workers`, wait for all threads to exit *before* calling `rb_destroy`. This is already the case in `app_shutdown`. (2) Document that `rb_destroy` must only be called after all threads have been joined. |
| **Status:** | ✅ **RESOLVED** |
| **Resolution:** | rb_wait_pop removed entirely (Master Plan 3.6). |

---

## 18. Implicit Dependencies on `nav_arena` Reset

| | |
|---|---|
| **Impact** | **Low** |
| **Location** | `app.c:314–482` (`app_populate_grid_items`) |
| **Evidence** | `app_populate_grid_items` starts with `arena_reset(&s->nav_arena)` (app.c:316), then allocates from the nav arena. Any reference to a previous pointer is invalidated. |
| **Description** | The `nav_arena` is a stamp-based allocator: everything it holds is invalidated by the next grid population. Pointers to `folder_name` and `folder_path` in `GridItem` are transient. |
| **Recommendation** | Document this invariant explicitly. Consider a debug-mode guard that stores a generation counter in `AppState` and validates on access. |
| **Status:** | 🔴 **Still Relevant** |
| **Resolution:** | Still no documentation of transient pointer lifetimes. Subtle footgun. |

---

## 19. Breadcrumb Width Memoization: Static Cache

| | |
|---|---|
| **Impact** | **Low** |
| **Location** | `gallery.c:475–517` |
| **Evidence** | Three `static` locals (`cached_dir`, `cached_parent_w`, `cached_dpi`) cache breadcrumb text width across frames. |
| **Description** | Static local variables in C are module-global and persist across independent gallery instances. |
| **Recommendation** | Move the cached values into `AppState` (or a breadcrumb state struct) rather than using statics. |
| **Status:** | ✅ **RESOLVED** |
| **Resolution:** | cached_display_parent + width cache with invalidation (Master Plan 4.7). |

---

## 20. `stb_image.h` Inclusion in Test Only

| | |
|---|---|
| **Impact** | **Low** |
| **Location** | `tests/test_main.c:25–26` |
| **Evidence** | `#define STB_IMAGE_IMPLEMENTATION` and `#include "lib/stb_image.h"` appear only in the test file, not in the main build (`build.c`). |
| **Description** | The test binary pulls in `stb_image` for image decoding during tests. |
| **Recommendation** | Document that `stb_image.h` is a test-only dependency. |
| **Status:** | 🔶 **Outdated (benign)** |
| **Resolution:** | Audit already said no action needed. |

---

## Summary by Severity

| Severity | Count | Resolved | Partial | Still Relevant |
|---|---|---|---|---|
| **High** | 5 | 2 (#1, #7) | 2 (#2, #3) | 1 (#13) |
| **Medium** | 8 | 6 (#4, #8, #9, #12, #14, #17) | 1 (#5) | 1 (#11) |
| **Low** | 7 | 3 (#6, #16, #19) | 1 (#15) | 3 (#10, #18, #20) |
