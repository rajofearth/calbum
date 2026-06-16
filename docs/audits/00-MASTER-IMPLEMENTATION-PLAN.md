# calbum — Master Audit Synthesis & Implementation Plan

**Generated:** 2026-06-16
**Source audits:** Security, Architecture, Performance/GPU, UX, Code Quality (5 reports)
**Scope:** ~115 unique findings across 14 source files, compressed and deduplicated below

---

## How To Read This Document

Each item below is a concrete, ordered implementation step. Items are grouped into **passes** — do pass 1 before pass 2, etc. Within each pass, items can be done in any order (or in parallel by different people). The format is:

```
[Tag] Short imperative title
       Files: list of files to change
       What: brief description of the root cause / current behavior
       How: concrete implementation instructions
```

Tags: `BUG` = crash/wrong behavior, `SEC` = security hardening, `PERF` = performance optimization, `UX` = usability improvement, `ARCH` = structural improvement, `CQ` = code quality.

**Status markers:** `✅ DONE` = completed and verified, blank = pending.

---

## Pass 1 — Crash Bugs & Security (Fix Immediately) ✅ DONE

These are the highest priority: they can crash the program, cause data races, or silently corrupt memory.

**Completed:** 2026-06-16 via 6 parallel subagents. All 29 tests pass. Release builds clean.

### 1.1 [BUG] Guard division by zero in thumbnail decoder ✅ DONE

**Files:** `src/image_loader.c:55-72`

**What:** `il_load_and_compress` divides by `w` or `h` without checking for zero. A corrupt image with reported dimensions `0×0` causes `EXCEPTION_INT_DIVIDE_BY_ZERO` (process crash).

**How:**
- After `frame->lpVtbl->GetSize(frame, &w, &h)`, add:
  ```c
  if (w == 0 || h == 0) { frame->lpVtbl->Release(frame); return NULL; }
  ```
- Also add a belt-and-suspenders clamp on `tw`/`th` before allocation: `if (tw > 4096) tw = 4096;`

### 1.2 [BUG] Add instance buffer overflow guard in full-image renderer ✅ DONE

**Files:** `src/gallery_fullimage.c:178`

**What:** The full-image renderer uses `static InstanceData instances[4096]` without checking `inst_count < 4096` before writing. With a wide window / many strip thumbnails, the stack array overflows.

**How:**
- `#define MAX_INSTANCES 4096` added to `types.h` (shared constant).
- Both `gallery_fullimage.c:178` and `gallery.c:156` now use `instances[MAX_INSTANCES]`.
- Guards added before every `inst_count++` and every `ui_*` call: `if (inst_count >= MAX_INSTANCES - 16) return;` at top level, `break;` inside the strip loop.
- Existing guard in `gallery.c:279` updated from `4080` → `MAX_INSTANCES - 16`.

### 1.3 [BUG] Drain WM_CALBUM_FILE_CHANGE messages during folder reload ✅ DONE

**Files:** `src/app.c:92-102`

**What:** `app_load_folder` drains only `WM_CALBUM_LOAD_COMPLETE` messages from the queue but not `WM_CALBUM_FILE_CHANGE`. After `fm_stop_monitor`, queued file-change messages have `LPARAM` pointing to freed `FileChange` structs.

**How:**
- After the existing drain loop (lines 92-102), add a second loop:
  ```c
  while (PeekMessageW(&msg, s->hwnd, WM_CALBUM_FILE_CHANGE, WM_CALBUM_FILE_CHANGE, PM_REMOVE))
  {
      FileChange *fc = (FileChange *)(uintptr_t)msg.lParam;
      free(fc);
  }
  ```

### 1.4 [SEC] Extend worker shutdown timeout to INFINITE ✅ DONE

**Files:** `src/asset_worker.c:139-146`, `src/file_monitor.c:141`

**What:** `WaitForSingleObject(s->worker_threads[i], 2000)` uses a 2-second timeout. If a worker is stuck in WIC I/O, the main thread proceeds to close handles and call `il_shutdown_wic()` while the worker still runs — a use-after-free (see finding 1.5).

**How:**
- Changed `WaitForSingleObject(s->worker_threads[i], 2000)` to `WaitForSingleObject(s->worker_threads[i], INFINITE)` in `asset_worker.c`.
- Changed `WaitForSingleObject(s->monitor_thread, 2000)` to `WaitForSingleObject(s->monitor_thread, INFINITE)` in `file_monitor.c` (done together with 1.5 since both modify `fm_stop_monitor`).

### 1.5 [SEC] Fix fm_stop_monitor handle close ordering ✅ DONE

**Files:** `src/file_monitor.c:117-151`

**What:** `CloseHandle(dir_handle)` is called before waiting for the monitor thread to exit. The thread may still be executing inside `ReadDirectoryChangesW`.

**How (applied together with 1.4):**
  1. SetEvent(monitor_stop_event)
  2. CancelIoEx(dir_handle, NULL)
  3. WaitForSingleObject(monitor_thread, INFINITE)  ← changed from 2000ms
  4. CloseHandle(monitor_thread)                     ← moved before dir_handle
  5. CloseHandle(dir_handle)                         ← moved after thread exit
  6. CloseHandle(monitor_stop_event)

### 1.6 [BUG] Fix wcsncpy fragility in file_scanner.c ✅ DONE

**Files:** `src/file_scanner.c:41, 55, 78`

**What:** The chained `wcsncpy(...)[...] = L'\0'` pattern is correct but fragile.

**How:**
- The plan originally cited only line 41, but **3 identical patterns** exist in this file (lines 41, 55, 78). All three were fixed:
  ```c
  // Before:
  wcsncpy(search, dir, MAX_PATH_LEN - 3)[MAX_PATH_LEN - 3] = L'\0';

  // After:
  wcsncpy(search, dir, MAX_PATH_LEN - 3);
  search[MAX_PATH_LEN - 3] = L'\0';
  ```
- **Note:** The same pattern also exists in `asset_worker.c:160`. Not in Pass 1 scope — consider fixing in a later pass.

### 1.7 [BUG] Add NULL check for shader compile blobs ✅ DONE

**Files:** `src/renderer.c:168-202`

**What:** `vs_blob` and `ps_blob` can be NULL if `D3DCompile` fails. The code calls `Release` on them without NULL checks.

**How:**
- After each `D3DCompile` call, check for NULL before calling `lpVtbl->Release`:
  ```c
  if (vs_blob) vs_blob->lpVtbl->Release(vs_blob);
  if (ps_blob) ps_blob->lpVtbl->Release(ps_blob);
  ```

---

## Pass 1 Completion Notes

**Date:** 2026-06-16
**Execution:** 6 parallel subagents, disjoint write sets
**Validation:** `make release` — clean compile, `make test` — 29/29 passed, `make format` — clean

### Scope Changes (vs. original plan)
| Item | Original Scope | Actual Scope | Reason |
|---|---|---|---|
| 1.2 | `gallery_fullimage.c` only | `gallery_fullimage.c`, `gallery.c`, `types.h` | Moved `MAX_INSTANCES` to shared header; updated existing `gallery.c` guard to use it |
| 1.4 | `asset_worker.c` only | `asset_worker.c` + `file_monitor.c` | Same pattern in both files; 1.4 and 1.5 were combined into one subagent |
| 1.6 | 1 line (`file_scanner.c:41`) | 3 lines (41, 55, 78) | Same chained pattern exists at 3 sites in the file — fixed all for consistency |

### Cross-Cutting Observations
1. **Same `wcsncpy` chained pattern in `asset_worker.c:160`** — not in scope but could be fixed in a future pass.
2. **`D2D brush leak` (audit L-08)** in `renderer.c:587-589,622-632` remains untouched, deferred from Pass 1.
3. **Item 1.4 and 1.5 must always be paired** — they both modify `fm_stop_monitor` in `file_monitor.c`. Any future work on that function should account for both timeout and handle ordering.
4. **All changes are correctness-only** — no functional additions, no new features. Pass 1 strictly fixes bugs and security holes.

---

## Pass 2 — UI Freezes & Performance Bottlenecks

These are the biggest impacts on user experience: frame freezes, unresponsive window, and GPU thrashing.

### 2.1 [UX/PERF] Move full-image WIC decode off main thread ✅ DONE

**Files:** `src/types.h`, `src/asset_worker.c`, `src/gallery_fullimage.c:8-51,69-557`, `src/main.c`, `src/image_loader.c` (noted: static decode buffer)

**What:** `r_load_full_image()` calls `il_load_full_image()` synchronously on the main thread, blocking rendering for 100-500+ms while JPEG/RAW decompression happens. This is the single biggest performance issue.

**How:**
1. Add a new custom message in `types.h`:
   ```c
   #define WM_CALBUM_FULL_LOAD_COMPLETE (WM_APP + 3)
   ```
2. Create `aw_request_full_image(AppState *s, const wchar_t *path)` that:
   - Allocates a `LoadResult` with path
   - Pushes to a new ring buffer (or reuses work_queue with a type flag)
   - Worker thread calls `il_load_full_image`, sets bc1_data to the RGBA result
   - Posts `WM_CALBUM_FULL_LOAD_COMPLETE` to main thread
3. Create `on_full_load_complete(HWND hwnd, WPARAM, LPARAM)`:
   - Calls `CreateTexture2D` with IMMUTABLE on the received RGBA data (fast, GPU-only)
   - Sets `active_full_srv` to the new SRV
   - Frees the RGBA buffer
4. In `gal_select_full_image`, instead of calling `r_load_full_image` directly:
   - If file < 2MB, load immediately (already fast)
   - Otherwise, call `aw_request_full_image` + set a pending flag
5. In `gal_render_fullimage`, if the full image is not loaded yet, render a "Loading..." placeholder

**⚠️ Learning:** `il_load_full_image` returns a pointer to a **global static decode buffer** (`g_decode_buffer`), NOT malloc'd memory. The `on_full_load_complete` handler must NOT call `free()` on this buffer. The pointer is set to NULL after texture creation to avoid double-free risk.

### 2.2 [PERF] Cache D2D brush and use single BeginDraw/EndDraw per frame ✅ DONE

**Files:** `src/renderer.c:579-604, 606-636`, `src/gallery.c`, `src/gallery_fullimage.c`

**What:** `CreateSolidColorBrush` is called 10-15 times per frame. Each text call opens its own `BeginDraw`/`EndDraw`, flushing the D2D command buffer.

**How:**
1. Pre-create the white brush at init (already done as `s->d2d_brush`). For colored text, use `s->d2d_brush->SetColor(...)` instead of creating a new brush.
2. Remove the `CreateSolidColorBrush` call from `r_draw_text_ext` and `r_draw_text_aligned`. Use `s->d2d_brush` directly.
3. Move `BeginDraw` / `EndDraw` to the top/bottom of each render function:
   - In `gal_render_gallery`: BeginDraw at ~line 557 (after all D3D instances are submitted), EndDraw at ~line 580 (before present).
   - In `gal_render_fullimage`: BeginDraw at ~line 405 (after blur-panel D3D draw), EndDraw at ~line 553 (before present).
4. Remove individual BeginDraw/EndDraw from both `r_draw_text_ext` and `r_draw_text_aligned`.

**⚠️ Learning:** All D2D text calls must be inside the BeginDraw/EndDraw pair. In `gal_render_gallery`, folder icon/text D2D calls (for `ITEM_FOLDER` grid items) happened before the original BeginDraw point. BeginDraw was moved earlier to wrap all D2D output.

### 2.3 [PERF] Cache IDWriteTextLayout for static strings ✅ DONE

**Files:** `src/renderer.c:610-636, 910-930`, `src/gallery.c`, `src/gallery_fullimage.c`

**What:** A new `IDWriteTextLayout` is created for every text draw call. This involves Unicode shaping, line breaking, and font fallback.

**How:**
1. In `AppState` (or `ScaledLayout`), add a small cache of `IDWriteTextLayout *` for static strings:
   - Icon glyphs (single character): cache once at init
   - Button labels ("Sort", back arrow, info icon): cache once at init
   - Zoom badge text: cache per zoom level
2. For `r_draw_text_aligned`:
   - Single-character strings: use a pre-cached layout
   - Dynamic strings (file paths, metadata): skip caching for now (low use frequency)
3. For `r_measure_text_width`:
   - Cache the breadcrumb text layout alongside the width (already partially cached)

**⚠️ Learning:** Cached layouts are created with 9999×9999 virtual dimensions at init. When drawing, `SetMaxWidth`/`SetMaxHeight` must be called to resize the layout to the actual button dimensions — otherwise center alignment positions the icon at the center of the huge virtual box (off-screen).

### 2.4 [UX/PERF] Make directory scan asynchronous with progress feedback ✅ DONE

**Files:** `src/file_scanner.c`, `src/app.c:130`, `src/main.c`, `src/types.h`

**What:** The recursive `FindFirstFileW`/`FindNextFileW` scan runs synchronously on the main thread. For folders with thousands of images, the window freezes.

**How:**
1. Create a new background thread function in `file_scanner.c`:
   ```c
   DWORD WINAPI fs_scan_thread(LPVOID param)
   ```
   - Scans recursively using FindFirstFileW/FindNextFileW
   - Posts batched results (e.g., 50 images at a time) via PostMessage
   - Posts a "scan complete" message at the end
2. Add a scanning flag in `AppState`:
   ```c
   int scanning;        // non-zero while scan is in progress
   int scan_progress;   // estimated % (images found so far)
   HANDLE scan_thread;  // thread handle
   ```
3. In `gal_render_gallery` (or a new overlay function):
   - If `s->scanning` is set, draw a "Scanning..." overlay with an indeterminate progress bar
4. In `app_load_folder`:
   - Instead of calling `fs_scan_directory` directly, start the scan thread
   - Return immediately (the message loop handles scan results)
5. Handle WM_CALBUM_SCAN_PROGRESS and WM_CALBUM_SCAN_COMPLETE messages in the window proc

### 2.5 [PERF] Add reverse mapping for texture slot eviction ✅ DONE

**Files:** `src/renderer.c:471-488`

**What:** `r_evict_texture` scans all images linearly to find which one owns a given slot — O(N) per eviction.

**How:**
- Add a field to `GPUTexturePool`:
  ```c
  int slot_owner[MAX_GPU_TEXTURES];  // image_index for each slot, or -1
  ```
- In `r_alloc_texture_slot`, set `slot_owner[slot] = image_index`
- In `r_evict_texture`, replace the linear scan with:
  ```c
  int img_idx = s->tex_pool.slot_owner[slot];
  if (img_idx >= 0 && img_idx < s->count && s->images[img_idx].texture_slot == slot)
  {
      s->images[img_idx].texture_slot = -1;
      s->images[img_idx].thumb_requested = 0;
      s->images[img_idx].state = IMG_STATE_READY;
  }
  s->tex_pool.slot_owner[slot] = -1;
  s->tex_pool.last_used[slot] = -1;
  ```

### 2.6 [PERF] Increase MAX_GPU_TEXTURES to reduce LRU thrashing ✅ DONE (pre-existing)

**Files:** `src/types.h:335`

**What:** 100 GPU texture slots may be fewer than the number of visible thumbnails (e.g., 6 cols × 20 rows = 120 visible), causing LRU thrashing.

**How:**
- Change `#define MAX_GPU_TEXTURES 100` to `#define MAX_GPU_TEXTURES 512`
- Memory cost: 512 × (160/4) × (160/4) × 8 = 512 × 40 × 40 × 8 = 6.5 MB of GPU memory — negligible

**Note:** Already at `1024` when Pass 2 started — likely bumped during Pass 1. No change needed.

### 2.7 [PERF] Eliminate redundant GetCursorPos calls ✅ DONE

**Files:** `src/gallery_fullimage.c:217-219,424-427`

**What:** `GetCursorPos` + `ScreenToClient` is called twice in the full-image render function. The second call (for zoom badge) can reuse the first result.

**How:**
- Store the cursor position from the first call in a local variable
- Reuse it for the zoom badge hover check at line 424
- Call `GetCursorPos` once at the top of `gal_render_fullimage`

### 2.8 [CQ/PERF] Replace wsprintfW with swprintf ✅ DONE

**Files:** `src/app.c:175`

**What:** `wsprintfW` has no buffer size limit. Replace with `swprintf` for safety.

**How:**
```c
swprintf(title, sizeof(title)/sizeof(wchar_t), L"calbum " APP_VERSION_W L" — %s", s->viewing_dir);
```

---

## Pass 2 Completion Notes

### Scope Changes (vs. original plan)

- **2.1** — Extended `LoadRequest` with `is_full_image` flag and reused the existing `work_queue` ring buffer instead of creating a separate queue. Added `FullLoadResult` struct (analogous to `LoadResult`). Small files (<2MB) still load synchronously via `r_load_full_image` as planned.
- **2.4** — Used `ScanBatch` (128-entry batches) posted via `PostMessage` rather than individual items. Post-scan logic (grid_items allocation, sort, monitor/worker restart) moved to `on_scan_complete` handler. Existing `fs_scan_directory` preserved for test compatibility.
- **2.6** — Already at `1024` (plan said change `100`→`512`). No action needed.

### Cross-Cutting Observations

1. **Static decode buffer pitfall:** `il_load_full_image` returns a pointer to `g_decode_buffer` — a single 16MB buffer allocated at init. This is NOT per-call malloc'd memory. Async handlers must not call `free()` on it. This caused a crash that only manifests when async full-image loads complete.
2. **D2D BeginDraw boundary creep:** Removing per-call BeginDraw/EndDraw from text functions requires all call sites to be audited. Missed D2D calls outside the wrapper silently produce no output — no compiler warning.
3. **Cached layout dimensions:** `IDWriteTextLayout` cached-at-init objects need `SetMaxWidth`/`SetMaxHeight` before each use when the drawing dimensions vary per-frame.
4. **Test isolation:** The `gal_select_full_image` test needed ring buffer initialization (slots array, event handle) to not crash when `aw_request_full_image` tries to push to the work queue.
5. **Scan thread recursion:** The background scan thread cannot use the arena (it's reset in `app_load_folder` before the thread starts). All results are batched and posted to the main thread which does arena allocation.

---

## Pass 3 — Structural Code Improvements

These are mechanical refactors with no behavior change but big readability/maintainability wins.

### 3.1 [ARCH] Extract strip window layout math into shared function ✅ DONE

**Files touched:** `src/gallery_fullimage.c`, `src/renderer.c`, `src/types.h`

**What:** The same 30-line block of strip thumbnail bounds computation appeared 5 times across gallery_fullimage.c and renderer.c (one undocumented duplication in `gal_handle_fullimage_click`).

**How:**
1. Added `fiv_strip_bounds()` in `gallery_fullimage.c` — computes visible strip thumbnail range from active index and total count.
2. Added `fiv_is_in_strip()` in `gallery_fullimage.c` — checks if a given image path is in the visible strip. Placed in `gallery_fullimage.c` (not `renderer.c` as originally planned) because it needs access to `strip_image_grid_indices`.
3. Replaced all 5 duplication sites with shared function calls, removing ~180 lines of duplicated code.
4. **Bonus:** `r_alloc_full_image_slot` in `renderer.c` was simplified from a 70-line nested-loop scan to an 8-line `fiv_is_in_strip` call.

### 3.2 [ARCH] Add SAFE_RELEASE macro for COM cleanup ✅ DONE

**Files touched:** `src/types.h`, `src/renderer.c`

**What:** `r_shutdown` was 82 lines with ~38 identical `if (p) { p->lpVtbl->Release(p); p = NULL; }` triplets.

**How:**
- Added `SAFE_RELEASE` macro in `types.h` — handles NULL check, `IUnknown*` cast for D2D/DWrite objects, Release call, and pointer nullification.
- Rewrote `r_shutdown` body to ~39 lines with 31 `SAFE_RELEASE` calls.
- Macro is also reusable for `r_free_full_image_slot` and any future COM cleanup.

### 3.3 [CQ] Rename unprefixed global functions ✅ DONE

**Files touched:** `src/app.c`, `src/types.h`, `src/main.c`

**What:** `get_pictures_folder` and `get_parent_dir` lacked the `app_` prefix.

**How:**
- `get_pictures_folder` → `app_get_pictures_folder` (definition in app.c, declaration in types.h, call site in main.c)
- `get_parent_dir` → `app_get_parent_dir` (definition in app.c, declaration in types.h, call sites in app.c and main.c)
- **Scope note:** `gallery_fullimage.c` was listed as affected in the original plan but doesn't call either function. `src/utils.c/h` was also listed but not relevant.

### 3.4 [CQ] Add const to read-only AppState* parameters ✅ DONE

**Files touched:** `src/layout.c`, `src/types.h`

**What:** `gal_calc_layout`, `gal_hit_test`, and `gal_max_scroll` read but never write through `AppState *s`.

**How:**
- Changed each signature from `AppState *s` to `const AppState *s` in both `layout.c` definitions and `types.h` declarations.
- **Correction to original plan:** Only 3 of the 6 candidate functions were actually read-only. `gal_tick_smooth_scroll` writes `scroll_current_y`/`needs_redraw`, `gal_apply_sort` calls `qsort` on the images array, and `gal_render_gallery` writes scrollbar state — all 3 cannot be made const. The plan was over-broad.

### 3.5 [ARCH] Decompose monolithic render functions ✅ DONE

**Files touched:** `src/gallery.c`, `src/gallery_fullimage.c`

**What:** Both `gal_render_gallery` (456 lines) and `gal_render_fullimage` (477 lines) mixed geometry, physics, text, and UI state.

**How:**
- Split `gal_render_gallery` into 5 static helpers:
  - `gal_render_grid_thumbnails()` — InstanceData for visible thumbnails/folders
  - `gal_render_scrollbar()` — scrollbar fade animation + track/thumb rendering
  - `gal_render_topbar()` — top bar backdrop + border
  - `gal_render_sort_menu()` — sort button + blur dropdown
  - `gal_render_folder_text()` — D2D pass for folder labels/icons
  - **+ breadcrumb + sort text stays inline in `gal_render_gallery`** (shared local state)
- Split `gal_render_fullimage` into 6 static helpers:
  - `fiv_update_preloading()` — debounced staggered preloading
  - `fiv_render_main_image()` — main image with zoom/pan
  - `fiv_render_top_controls()` — letterbox mask, back/info/zoom buttons
  - `fiv_render_bottom_strip()` — bottom thumbnail strip with lazy loading
  - `fiv_render_info_panel()` — info overlay blur panel + close button
  - `fiv_render_d2d_text()` — all D2D text (icons, loading, zoom, metadata)

### 3.6 [ARCH] Remove unused rb_wait_pop dead code ✅ DONE

**Files touched:** `src/types.h`

**What:** `rb_wait_pop` was defined but never called. All asset workers use `rb_try_pop + WaitForMultipleObjects`.

**How:**
- Deleted the entire `rb_wait_pop` function body.

## Pass 3 Completion Notes

**Date:** 2026-06-16
**Execution:** 4 subagents (3 parallel + 1 sequential)
**Validation:** `make test` — 29/29 passed, `make format` — clean, zero new diagnostics

### Scope Changes (vs. original plan)
| Item | Original Scope | Actual Scope | Reason |
|---|---|---|---|
| 3.1 | 4 duplication sites | 5 sites (4 documented + 1 in `gal_handle_fullimage_click`) | Click handler had the same bounds math undocumented |
| 3.1 | `fiv_is_in_strip` in `renderer.c` | `fiv_is_in_strip` in `gallery_fullimage.c` | Needs access to `strip_image_grid_indices` — data lives in gallery_fullimage.c |
| 3.3 | `src/utils.c/h`, `gallery_fullimage.c` affected | Neither file was actually affected | Neither function is called from those files |
| 3.4 | 6 functions, ~20 signatures | 3 functions (layout.c only) | Only `gal_calc_layout`, `gal_max_scroll`, `gal_hit_test` are truly read-only; the other 3 write through `AppState*` |
| 3.5 | 10 helpers | 11 helpers (6 for fullimage, 5 for gallery) | Multiple small helpers were cleaner than fewer large ones |

### Cross-Cutting Observations
1. **Unity build constraints:** All extracted helpers must be `static` to avoid duplicate symbol errors in the test unity build. This is fine since they're module-internal.
2. **InstanceData buffer ownership:** The `static InstanceData instances[MAX_INSTANCES]` and `int inst_count` must be passed as parameters to every extracted rendering helper. Extracting across the D2D boundary (BeginDraw/EndDraw) requires careful ordering — helpers called between BeginDraw/EndDraw cannot call `r_draw_instances` (which is D3D11).
3. **fiv_strip_bounds availability:** The shared strip bounds function must be declared before `gal_render_fullimage`, which is why subagent 3.1 had to run before 3.5.
4. **const correctness audit:** The plan's aspirational list of const candidates was overly broad. Each function must be individually verified to not write through the pointer. `gal_apply_sort` calls `qsort(s->images, ...)` which modifies the images array through `s`.
5. **SAFE_RELEASE macro generality:** The `IUnknown*` cast works for all COM objects because all D2D/DWrite/D3D interfaces ultimately derive from `IUnknown`. No special-casing needed.

## Pass 4 — UX & Visual Polish

### 4.1 [UX] Render empty state with guidance message

**Files:** `src/gallery.c:147-151`

**What:** Empty folders show a blank window with no top bar or instruction.

**How:**
- Remove the early return at L147-151. Instead, draw the top bar, breadcrumb, and a centered message:
  ```c
  // Inside gal_render_gallery, after r_clear_theme, replace the empty check:
  if (total_items == 0) {
      // Draw top bar
      instances[inst_count] = (InstanceData){ ... top bar ... };
      // Draw centered text
      r_draw_text_aligned(s, L"No images found — drop a folder here",
          (float)s->window_width * 0.5F - 200.0F, (float)s->window_height * 0.5F - 20.0F,
          400.0F, 40.0F, ALIGN_X_CENTER, ALIGN_Y_CENTER,
          s->dwrite_format_semibold, s->theme.text_muted);
      r_present(s);
      return;
  }
  ```

### 4.2 [UX] Use IMG_STATE_FAILED to show broken-image indicator

**Files:** `src/image_loader.c`, `src/gallery.c:249-253`, `src/types.h:267`

**What:** `IMG_STATE_FAILED` is defined but never set. Failed thumbnail decodes leave a blank dark square.

**How:**
- In `on_thumb_complete` (`main.c:152-186`), if `result->succeeded == 0`:
  ```c
  if (!result->succeeded) {
      int found_idx = ...; // find the image in s->images
      if (found_idx != -1) {
          s->images[found_idx].state = IMG_STATE_FAILED;
      }
  }
  ```
- In `gal_render_gallery`, when rendering a thumbnail:
  ```c
  if (s->images[img_idx].state == IMG_STATE_FAILED) {
      instances[inst_count].tex_index = TOKEN_DEFAULT; // dark
      // Also draw a small warning icon via D2D overlay
  }
  ```

### 4.3 [UX] Fix Home/End scroll sync

**Files:** `src/main.c:420-432`

**What:** Home/End change `selected_index` but don't scroll to make it visible.

**How:**
- After `s->selected_index = 0` (Home), add:
  ```c
  s->scroll_target_y = 0.0F;
  ```
- After `s->selected_index = limit - 1` (End), add:
  ```c
  s->scroll_target_y = (float)gal_max_scroll(s);
  ```

### 4.4 [UX] Implement scrollbar track click

**Files:** `src/main.c:467-474`

**What:** Clicking the scrollbar track (above/below thumb) does nothing.

**How:**
- In `on_lbutton_down`, after the scrollbar thumb check, add:
  ```c
  // Check if click is on the scrollbar track but not on the thumb
  float track_x = (float)g_state.window_width - track_w - (4.0F * g_state.dpi_scale);
  if ((float)x >= track_x && (float)x <= track_x + track_w && (float)y >= track_y && (float)y <= track_y + track_h) {
      // Check if it's NOT on the thumb
      if ((float)y < thumb_y) {
          // Click above thumb → page up
          gal_scroll(&g_state, (float)(g_state.window_height * 0.8F));
      } else if ((float)y > thumb_y + thumb_h) {
          // Click below thumb → page down
          gal_scroll(&g_state, (float)(-g_state.window_height * 0.8F));
      }
  }
  ```
  (You'll need to compute `track_x, track_y, track_h, thumb_y, thumb_h` locally from the same formulas used in `gal_render_gallery`.)

### 4.5 [UX] Add thumbnail loading indicator

**Files:** `src/gallery.c:229-233`, `src/gallery_fullimage.c:340-344`

**What:** Thumbnails in `IMG_STATE_NEW` show only a dark panel with no indication of loading.

**How:**
- In `gal_render_gallery`, for images in `IMG_STATE_NEW` or `IMG_STATE_LOADING`:
  - Instead of solid `TOKEN_DEFAULT`, render a subtle pulsing animation using `sin(s->delta_time * accumulated_time)` modulated opacity.
  - Or draw a small circular progress indicator centered in the cell.

### 4.6 [UX] Truncate window title to leaf name only

**Files:** `src/app.c:156-160`

**What:** Window title shows full directory path (privacy leak).

**How:**
- Extract only the leaf folder name from `s->viewing_dir`:
  ```c
  wchar_t *leaf = wcsrchr(s->viewing_dir, L'\\');
  const wchar_t *display_name = leaf ? leaf + 1 : s->viewing_dir;
  swprintf(title, sizeof(title)/sizeof(wchar_t), L"calbum " APP_VERSION_W L" — %s", display_name);
  ```

### 4.7 [UX] Cache breadcrumb formatted string to avoid recomputation

**Files:** `src/gallery.c:475-544`

**What:** The breadcrumb path is formatted twice — once for width measurement, once for rendering.

**How:**
- Store `display_parent` in the same static cache block alongside `cached_parent_w`:
  ```c
  static wchar_t cached_display_parent[MAX_PATH_LEN * 2] = {0};
  ```
- In the memoization block, format it once and store both the string and width.
- Use the cached string directly in the rendering block (lines 547-556).

---

## Pass 5 — Deeper Architecture & Maintainability

### 5.1 [ARCH] Decompose AppState into sub-structs

**Files:** `src/types.h`, then cascading changes to all `.c` files

**What:** The 120-field god struct creates implicit coupling between all modules.

**How:**
This is the largest single refactor. Do it incrementally:

1. Extract `RenderState`:
   ```c
   typedef struct {
       ID3D11Device *d3d_device;
       ID3D11DeviceContext *d3d_context;
       IDXGISwapChain *swap_chain;
       ID3D11RenderTargetView *rtv;
       ID2D1Factory *d2d_factory;
       ID2D1RenderTarget *d2d_rtv;
       // ... all D3D/D2D/DWrite objects ...
   } RenderState;
   ```
2. Embed in `AppState`: `RenderState render;`
3. Pass `RenderState *r` to renderer functions instead of `AppState *s`.
4. Repeat for `AppModel` (images, grid_items, directories), `UIState` (scroll, zoom, selection), `WorkerState` (thread handles).

Each sub-struct extraction can be done as a separate commit. Start with `RenderState` (easiest — well-isolated).

### 5.2 [ARCH] Extract full-image cache management from renderer

**Files:** `src/renderer.c:716-908` → new file `src/renderer_cache.c`

**What:** The full-image cache management (~192 lines) is mixed with D3D11 setup code in renderer.c (930 lines, close to the 1000-line limit).

**How:**
- Create `src/renderer_cache.c` containing:
  - `r_get_full_image_slot`
  - `r_free_full_image_slot`
  - `r_alloc_full_image_slot`
  - `r_free_full_image`
  - `r_load_full_image`
- Add `#include "src/renderer_cache.c"` in `build.c` after `renderer.c`
- Declare one shared function boundary function in `renderer.c` if needed

### 5.3 [ARCH] Add systematic error logging

**Files:** New file `src/logger.c`, plus edits throughout codebase

**What:** Most failures (arena exhaustion, D3D resource creation, WIC decode failure) are silent.

**How:**
- Create `src/logger.c`:
  ```c
  #include "types.h"
  #include <stdio.h>
  void log_error(const wchar_t *fmt, ...) {
      wchar_t buf[1024];
      va_list args;
      va_start(args, fmt);
      vswprintf(buf, 1024, fmt, args);
      va_end(args);
      OutputDebugStringW(buf);
      // Optionally: show a toast via balloon notification
  }
  ```
- Call `log_error` in:
  - `arena_alloc` when allocation fails
  - `r_init` when D3D11/D2D/DWrite creation fails
  - `r_load_full_image` when texture creation fails
  - `aw_worker_thread` when WIC decode fails
  - `fm_thread_proc` when ReadDirectoryChangesW fails

---

## Pass 6 — Testing & CI

### 6.1 [CQ] Add CALBUM_TEST_BUILD compile guard

**Files:** `tests/test_main.c`, `build.c`, `Makefile`

**What:** Tests include all source files directly (unity approach), including GPU-dependent initialization.

**How:**
- Add `#ifndef CALBUM_TEST_BUILD` guards around `main()` in `main.c` and around D3D11-dependent code in `renderer.c`.
- Update the test build command in `Makefile` to pass `-DCALBUM_TEST_BUILD`.

### 6.2 [CQ] Write tests for ring buffer concurrency

**Files:** `tests/test_main.c`

**What:** The ring buffer has no test coverage for concurrent push/pop scenarios.

**How:**
- Create a test that spawns 4 threads pushing and popping from a ring buffer, verifying no lost items and no crashes.
- Test the full/empty boundary conditions.

### 6.3 [CQ] Write tests for image_loader with in-memory WIC stubs

**Files:** `tests/test_main.c`

**What:** `il_load_and_compress` and `il_load_full_image` are untested because they require WIC.

**How:**
- Create small (1×1 pixel) PNG/JPEG buffer in memory
- Use `IWICStream` + `IWICBitmapDecoder` to decode from the memory buffer
- Test that valid images produce BC1 data and invalid images return NULL

---

## Deduplication Reference

Some findings appear in multiple audit reports. Below cross-references each topic to its source reports.

| Topic | Security | Arch | Perf | UX | CQ |
|---|---|---|---|---|---|
| Instance buffer overflow (gallery_fullimage.c) | M-03 | #14 | 1.5 | C-4 | — |
| Division by zero (image_loader.c) | M-02 | — | — | — | — |
| GetFileSize truncation | M-01 | — | 4.4 | — | 3.3 |
| g_wic_factory UAF | H-01 | #11 | 4.3 | — | 9.4 |
| Thread shutdown race | H-01, H-03 | #15, #17 | — | — | — |
| PostMessage heap ptr lifetime | H-02 | #12 | — | — | — |
| AppState god struct | — | #1 | — | — | 7.1 |
| Strip layout math duplicated 4× | — | #4 | — | — | 4.1 |
| Error handling gaps / silent failures | — | #7 | — | C-3 | — |
| IMG_STATE_FAILED not used | — | — | — | L-7 | — |
| Breadcrumb formatting duplicated | — | — | 1.6 | — | 4.2 |
| Test coverage gaps | — | #13 | — | — | 6.1 |
| Ring buffer no backpressure | — | #8 | 4.5 | — | — |
| Synchronous directory scan (UI freeze) | — | — | — | C-1 | — |
| Synchronous full-image WIC decode | — | — | 4.1 | — | — |
| D2D brush / BeginDraw per frame | — | — | 1.1, 1.2 | — | — |
| CreateTextLayout per frame | — | — | 1.3 | — | — |
| O(N) texture eviction scan | — | — | 2.1 | — | — |
| 25-tap blur shader | — | — | 7.1 | — | — |
| MAX_GPU_TEXTURES too small | — | — | 2.5 | M-8 | — |
| Home/End scroll fix | — | — | — | H-5 | — |
| Scrollbar track click | — | — | — | H-2 | — |
| Full-image cache size mismatch | — | #16 | — | M-9 | — |
| Const correctness | — | — | — | — | 1.5 |
| SAFE_RELEASE macro | — | #6 | — | — | — |
| wsprintfW → swprintf | L-04 | — | — | — | — |
| Window title path disclosure | L-01 | — | — | L-3 | — |
| Drag-drop path truncation | L-02 | — | — | — | — |
| FNV-1a hash collision | L-03 | — | — | — | — |
| Arena alignment documentation | M-04 | — | — | — | — |
| Arena resize waste | — | #10 | 5.1 | — | — |
| Integer overflow in capacity calc | M-05 | — | — | — | — |
| Null ptr vs_blob/ps_blob Release | — | — | — | — | 9.2 |
| enum consistency (uint8_t GridItem.type) | — | — | — | — | 3.1 |
| full_width/height uint16_t limit | — | — | — | — | 9.5 |
| Monolithic render functions | — | #3, #9 | — | — | 7.4 |
| Breadcrumb static cache | — | #19 | — | — | — |
| nav_arena transient pointers | — | #18 | — | — | — |
| stb_image.h test-only dep | — | #20 | — | — | — |
| No touch support | — | — | — | M-7 | — |
| No context menu | — | — | — | H-4 | — |
| No tooltips | — | — | — | H-3 | — |
| No search/filter | — | — | — | M-1 | — |
| No slideshow | — | — | — | M-4 | — |

---

## Summary: All Changes at a Glance

| Pass | Count | Type | Description | Status |
|---|---|---|---|---|
| 1 | 7 | BUG/SEC | Crash fixes, security hardening, thread safety | ✅ DONE |
| 2 | 8 | PERF/UX | UI freezes, GPU thrashing, rendering overhead | |
| 3 | 6 | ARCH/CQ | Code deduplication, decomposition, const-correctness | ✅ DONE |
| 4 | 7 | UX | Empty state, error indicators, scroll fixes, polish | |
| 5 | 3 | ARCH | God struct decomposition, error logging, cache extraction | |
| 6 | 3 | CQ | Test infrastructure, ring buffer tests, WIC stubs | |
| **Total** | **34** | | **Deduplicated implementation items** | **3/6 passes done** |
