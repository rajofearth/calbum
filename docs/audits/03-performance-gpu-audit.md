# Performance & GPU Audit Report: calbum

**Date:** 2026-06-16
**Version audited:** 0.1.0
**Scope:** Rendering pipeline, GPU resource management, asset pipeline, threading, memory, message loop

---

## Table of Contents

1. Rendering Pipeline
2. GPU Resource Management
3. D2D / DirectWrite Overhead
4. Asset Pipeline & Threading
5. Memory / Arena
6. Message Loop / Frame Pacing
7. Shader
8. Miscellaneous
9. Summary Matrix

---

## 1. Rendering Pipeline

### 1.1 Per-frame D2D brush allocation in `r_draw_text_ext()`

| | |
|---|---|
| **Severity** | **High** |
| **Location** | `renderer.c:579-598` |

**Description:** `CreateSolidColorBrush` is called every time text is drawn. With ~10-15 text draw calls per frame from the gallery UI (folder names, breadcrumbs, sort menu, buttons) and ~12-14 from full-image view (metadata panel, icons, zoom badge), this creates and destroys 10-15 D2D brushes per frame. D2D brush creation involves GPU resource allocation.

**Remediation:** Pre-create one white brush at init (already exists as `s->d2d_brush`, line 421-422). Use it with `ID2D1RenderTarget::SetTransform` or pre-multiplied color blending via the text itself. For colored text, batch by color or use a small cache of 4-8 brushes keyed by color.

---

### 1.2 Multiple D2D BeginDraw/EndDraw pairs per frame

| | |
|---|---|
| **Severity** | **High** |
| **Location** | `renderer.c:592-595` (in `r_draw_text_ext`) and `renderer.c:627-631` (in `r_draw_text_aligned`) |

**Description:** Each text call opens its own `BeginDraw`/`EndDraw`. D2D internally flushes its command buffer on `EndDraw`, losing batching. Combined with finding 1.1, this is significant overhead.

**Remediation:** Open a single `BeginDraw` at the top of the render function and a single `EndDraw` before `r_present()`. Reuse the same target across all D2D calls in the frame.

---

### 1.3 Per-frame DWrite `CreateTextLayout`

| | |
|---|---|
| **Severity** | **High** |
| **Location** | `renderer.c:614, 916` |

**Description:** `r_draw_text_aligned()` (line 614) and `r_measure_text_width()` (line 916) create a new `IDWriteTextLayout` object every call. Text layout creation performs Unicode shaping, line breaking, and font fallback — all relatively heavy. Gallery view calls this ~10-15 times per frame; full-image view calls it ~12-14 times per frame.

**Remediation:** Cache text layouts for static strings (button labels, icons). For dynamic strings (file paths, metadata), consider reusing a single layout object and calling `SetText()` instead of create/release. At minimum, avoid creation for trivial single-character strings (icons).

---

### 1.4 `GetCursorPos` + `ScreenToClient` called multiple times per frame

| | |
|---|---|
| **Severity** | **Medium** |
| **Location** | `gallery.c:159-161`, `gallery_fullimage.c:217-219`, `gallery_fullimage.c:424-427`, `main.c:784-786` |

**Description:** Called once in `gal_render_gallery`, called twice in `gal_render_fullimage`, called yet again in the main message loop. These are syscalls into `win32k.sys`.

**Remediation:** Call once at the top of each render function and pass the cursor position as a parameter.

---

### 1.5 4096-element `InstanceData` static array with silent truncation

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `gallery.c:156`, `gallery_fullimage.c:178`, `renderer.c:206` |

**Description:** Both renderers declare `static InstanceData instances[4096]`. The gallery caps at 4080 with a silent `break`. If the grid grows (e.g., very small thumbnails on high-DPI with many columns), instances silently stop being emitted.

**Remediation:** Use a dynamic array or assert/cap gracefully with a visible warning in debug builds.

---

### 1.6 Duplicate directory path formatting

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `gallery.c:484-517` and `gallery.c:520-544` |

**Description:** The `parent_formatted` string is built and the `display_parent` path is formatted twice: once for measurement and once for rendering. The second pass is an exact duplicate.

**Remediation:** Cache the formatted string alongside the width.

---

### 1.7 Dummy-character text width measurement hack

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `gallery.c:510-516` |

**Description:** Appends `\"x\"` to measure trailing space, then subtracts the width of `\"x\"`. This calls `CreateTextLayout` twice.

**Remediation:** Use `DWRITE_TEXT_METRICS::widthIncludingTrailingWhitespace` from the `GetMetrics` call directly.

---

## 2. GPU Resource Management

### 2.1 `r_evict_texture` O(N) scan over all images

| | |
|---|---|
| **Severity** | **Medium** |
| **Location** | `renderer.c:476-485` |

**Description:** To find which image owns a GPU slot, the function linearly scans every image entry (potentially thousands).

**Remediation:** Store a reverse mapping: `slot_owners[MAX_GPU_TEXTURES]` array directly mapping slot → image_index. Update on allocation/eviction.

---

### 2.2 `r_alloc_full_image_slot` nested loop O(FULL_CACHE_SIZE × strip_count)

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `renderer.c:808-829` |

**Description:** For each of the 3 full-image slots, the code scans the visible strip thumbnails. With FULL_CACHE_SIZE=3, this is fast, but overly complex.

**Remediation:** Simplify to a simple FIFO eviction (round-robin), or keep current logic with an early-exit.

---

### 2.3 Full images loaded with no mipmaps

| | |
|---|---|
| **Severity** | **Medium** |
| **Location** | `renderer.c:875` |

**Description:** `desc.MipLevels = 1`. When the main image is zoomed out, the GPU minifies without mipmapping, causing aliasing/scintillation.

**Remediation:** Generate a full mip chain when creating full-image textures. Either generate at load time (CPU) or use `D3D11_USAGE_DEFAULT` with `D3D11_BIND_RENDER_TARGET` and `GenerateMips` (GPU).

---

### 2.4 `UpdateSubresource` for thumbnail upload via immediate context

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `renderer.c:527-528` |

**Description:** Using the immediate context for `UpdateSubresource` serializes the upload with rendering.

**Remediation:** Consider using a staging texture + `CopySubresourceRegion`, or batch uploads.

---

### 2.5 100 texture slots may be insufficient for large thumbnail grids

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `types.h:335` (MAX_GPU_TEXTURES=100) |

**Description:** On a grid with many columns visible (e.g., 6 columns × 20 rows = 120 visible thumbnails), visible thumbnails exceed the pool, causing LRU thrashing.

**Remediation:** Increase `MAX_GPU_TEXTURES` to at least 256-512. At 256 (1.6 MB) the memory impact is negligible on any modern GPU.

---

### 2.6 Blur effect: full-resolution copy + mipmap generation per frame

| | |
|---|---|
| **Severity** | **Medium** |
| **Location** | `renderer.c:531-542` |

**Description:** `r_copy_backbuffer_for_blur()` copies the entire backbuffer and calls `GenerateMips`. The blur reads 25 samples per pixel. On a 1920×1080 window this is ~52M texture reads per blur panel draw.

**Remediation:** Consider: (a) only re-blur when underlying content changes; (b) reduce blur resolution by copying at half-res; (c) implement a two-pass separable blur (5+5=10 samples instead of 25).

---

## 3. D2D / DirectWrite Overhead

### 3.1 Per-frame `CreateSolidColorBrush`
**Severity: High** | **Location:** `renderer.c:587`

Pre-create a white brush at init (already done, `s->d2d_brush`). Use `SetColor` rather than creating new brushes.

### 3.2 Per-text-call BeginDraw/EndDraw
**Severity: High** | **Location:** `renderer.c:592,595,627,631`

Restructure to single BeginDraw/EndDraw per frame.

### 3.3 Per-call `CreateTextLayout`
**Severity: High** | **Location:** `renderer.c:614,916`

Cache layouts for static text; reuse layout objects for dynamic text via `SetText`.

---

## 4. Asset Pipeline & Threading

### 4.1 Synchronous WIC full-image decode on main thread

| | |
|---|---|
| **Severity** | **Critical** |
| **Location** | `renderer.c:865` → `image_loader.c:205-305` |

**Description:** `r_load_full_image()` calls `il_load_full_image()` which does WIC decode (including JPEG decompression, scaling, format conversion) synchronously on the render thread. For large files (especially 20+ MP RAW camera images), this can stall the frame for 100-500+ ms, causing visible freezing and DWM timeout.

**Remediation:** Move full-image loading to a worker thread. Offload the WIC decode + RGBA buffer to the asset worker pool (or a dedicated high-priority thread). Post a `WM_CALBUM_FULL_LOAD_COMPLETE` message back. The main thread receives a pre-decoded RGBA buffer and only does the D3D upload.

---

### 4.2 Preloading adjacent images with synchronous `r_load_full_image`

| | |
|---|---|
| **Severity** | **Medium** |
| **Location** | `gallery_fullimage.c:131-174` |

**Description:** The preloading loop loads one adjacent image per frame for staggering. But each call still does a synchronous WIC decode on the main thread.

**Remediation:** With the async approach from 4.1, the preloading loop becomes: `aw_request_full_image(...)` and returns immediately.

---

### 4.3 Shared `g_wic_factory` accessed from multiple threads

| | |
|---|---|
| **Severity** | **Medium** |
| **Location** | `image_loader.c:8,44,90,266` |

**Description:** `g_wic_factory` is a single `IWICImagingFactory` pointer. All 4 worker threads call methods on it concurrently. WIC may serialize on internal locks.

**Remediation:** Create one `IWICImagingFactory` per worker thread using `CoInitializeEx(COINIT_MULTITHREADED)`.

---

### 4.4 Per-thumbnail `malloc`/`free` for disk cache reads

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `asset_worker.c:75` |

**Description:** Each cache hit allocates a ~6 KB buffer, then frees it. On a first-load of 10,000 images, this creates 10,000 short-lived allocations.

**Remediation:** Use a small-object pool or a thread-local reusable buffer. Or memory-map the cache file.

---

### 4.5 No rate limiting on thumbnail requests

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `gallery.c:229-233` |

**Description:** The gallery loop requests thumbnails for all visible items immediately. The ring buffer fills quickly.

**Remediation:** Add a per-frame cap (e.g., request at most 20 new thumbnails per frame). Use a priority queue.

---

### 4.6 Serial thumb completion processing

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `main.c:152-186` |

**Description:** Each completed thumbnail posts a message. During bulk loading, the main thread spends significant time doing texture uploads and evictions per message.

**Remediation:** Batch process multiple results before entering the render path.

---

## 5. Memory / Arena

### 5.1 Arena reallocation on growth wastes memory

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `app.c:166-183` |

**Description:** When `app_append_image_entry` exceeds capacity, it allocates a new block (2× size) and memcpy's old entries. The old arena space is never reclaimed. With exponential growth, the waste for 10,000 images is ~5.5 MB.

**Remediation:** Pre-scan the directory to count files first, then allocate exactly once.

---

### 5.2 `nav_arena` 2MB reset per `app_populate_grid_items`

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `app.c:316` |

**Description:** The 2MB arena could theoretically be exhausted on folders with thousands of unique subdirectory names.

**Remediation:** Ensure the 2MB nav arena is sufficient for worst-case folder hierarchies.

---

### 5.3 16MB decode buffer edge case

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `main.c:12` |

**Description:** `g_decode_buffer` is a fixed 16 MB heap allocation. If a full image exceeds 16 MB uncompressed (e.g., 2048×2048 RGBA = 16 MB exactly), the check could fail on edge cases.

**Remediation:** Make the buffer slightly larger (e.g., 17 MB) or switch to dynamic allocation per image.

---

## 6. Message Loop / Frame Pacing

### 6.1 PeekMessage busy-poll + WaitMessage idle pattern

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `main.c:749-819` |

**Description:** When the app is redrawing every frame, it calls `PeekMessage` + `UpdateWindow` synchronously. When idle, `WaitMessage()` correctly yields CPU.

**Remediation:** This pattern works but couples rendering rate to message processing rate. Consider timer-based rendering for smoother frame pacing.

---

### 6.2 Full-image load debounce timer

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `main.c:770-778` |

**Description:** `full_load_timer` is decremented by `delta_time` each iteration of the message loop. Correct.

---

## 7. Shader

### 7.1 25-tap box blur in single pass

| | |
|---|---|
| **Severity** | **Medium** |
| **Location** | `renderer.c:81-111` |

**Description:** The TOKEN_BLUR path samples `blur_texture` 25 times per pixel with fixed weights. On integrated GPUs (Intel UHD, AMD Vega mobile), this can be significant. Additionally, `SampleLevel` with mip level 2.5 means the GPU performs trilinear filtering on each of those 25 reads.

**Remediation:** Switch to two-pass separable Gaussian blur: horizontal pass (9 taps) → vertical pass (9 taps). Total samples: 18 instead of 25. Each pass is cache-friendly. Also, consider using a lower mip level directly as the blur source.

---

### 7.2 Sampler state uses trilinear filtering for all textures

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `renderer.c:220-224` |

**Description:** `D3D11_FILTER_MIN_MAG_MIP_LINEAR` enables trilinear filtering for all textures, including BC1 thumbnails that are always rendered at 1:1.

**Remediation:** Consider using `D3D11_FILTER_MIN_MAG_MIP_POINT` for the thumbnail texture array and `LINEAR` for full-image and blur textures.

---

## 8. Miscellaneous

### 8.1 Scrollbar animation runs when scrollbar is hidden

| | |
|---|---|
| **Severity** | **Low** |
| **Location** | `gallery.c:297-298,306,309, etc.` |

**Description:** The timer decrement and hover interpolation run even when the scrollbar is invisible.

**Remediation:** Gate scrollbar animation on whether the scrollbar is actually visible.

---

## 9. Summary Matrix

| # | Finding | Category | Severity | Performance Impact | Fix Difficulty |
|---|---|---|---|---|---|
| 1.1 | Per-frame D2D brush creation | Rendering | **High** | High | Easy |
| 1.2 | Multiple BeginDraw/EndDraw pairs | Rendering | **High** | Medium | Easy |
| 1.3 | Per-call CreateTextLayout | Rendering | **High** | Medium-High | Medium |
| 1.4 | Redundant GetCursorPos | Rendering | **Medium** | Low | Easy |
| 1.5 | 4096 instance cap + silent truncation | Rendering | **Low** | Low | Easy |
| 1.6 | Duplicate path formatting | Rendering | **Low** | Low | Easy |
| 1.7 | Dummy-character text width measurement | Rendering | **Low** | Low | Easy |
| 2.1 | O(N) texture eviction scan | GPU Resources | **Medium** | Medium | Easy |
| 2.2 | Overengineered full-slot allocation | GPU Resources | **Low** | Negligible | Easy |
| 2.3 | No mipmaps for full images | GPU Resources | **Medium** | Medium | Medium |
| 2.4 | Immediate-context UpdateSubresource | GPU Resources | **Low** | Low-Medium | Medium |
| 2.5 | Small texture pool (100 slots) | GPU Resources | **Low** | Medium | Easy |
| 2.6 | GPU cost of blur effect | GPU Resources | **Medium** | Medium | Medium |
| 4.1 | Synchronous WIC decode on main thread | Threading | **Critical** | Critical | Hard (architectural) |
| 4.2 | Synchronous preloading of adjacent images | Threading | **Medium** | Medium | Hard (depends on 4.1) |
| 4.3 | Shared WIC factory across threads | Threading | **Medium** | Medium | Easy |
| 4.4 | malloc/free per cache read | Memory | **Low** | Low | Easy |
| 4.5 | No rate limiting on thumbnail requests | Threading | **Low** | Low | Easy |
| 4.6 | Serial thumb completion processing | Threading | **Low** | Low | Medium |
| 5.1 | Arena waste on progressive resize | Memory | **Low** | Low | Easy |
| 5.2 | 2MB nav_arena potential exhaustion | Memory | **Low** | Very low | Easy |
| 5.3 | 16MB decode buffer edge case | Memory | **Low** | Very low | Easy |
| 6.1 | Synchronous UpdateWindow paint pattern | Frame Pacing | **Low** | Low | Medium |
| 7.1 | 25-tap single-pass blur shader | Shader | **Medium** | Medium | Medium |
| 7.2 | Unnecessary trilinear on thumbnails | Shader | **Low** | Low | Easy |
| 8.1 | Scrollbar timer runs when hidden | Misc | **Low** | Negligible | Easy |
