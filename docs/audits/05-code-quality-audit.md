# Code Quality & Maintainability Audit Report: calbum

**Audit date:** 2026-06-16
**Version audited:** 0.1.0
**Total files examined:** 16 source files + 5 config/build files

---

## 1. Coding Standards Compliance

### 1.1 Naming Conventions / Module Prefixes

**Severity: Low | Location: scattered**

Module prefixes are applied correctly throughout (`fs_`, `il_`, `r_`, `gal_`, `aw_`, `fm_`, `ui_`, `app_`). Two violations:

- `get_pictures_folder` (`app.c:62`) and `get_parent_dir` (`app.c:210`) are global functions lacking the `app_` prefix.
- `format_size` / `format_filetime` (`utils.c:5,25`) are declared as `void format_...` but the module prefix convention would suggest `ut_`.

**Recommendation:** Rename `get_pictures_folder` → `app_get_pictures_folder` and `get_parent_dir` → `app_get_parent_dir`.

---

### 1.2 Allman Brace Style

**Severity: Low | Location: gallery.c:13-14,17-18**

A few single-line `if` bodies without braces:
```c
if (out->pad < 1) out->pad = 1;
```
The clang-tidy config permits this via `ShortStatementLines: 2`.

**Recommendation:** Add braces for consistency, or accept the existing clang-tidy exception as intentional.

---

### 1.3 120-Column Limit

**Severity: Low | Location: gallery_fullimage.c:128-129, gallery.c:509-510**

Most code stays under 120 columns. A few lines exceed it slightly.

**Recommendation:** Run `clang-format` (already available via `make format`) to catch these.

---

### 1.4 Pointer Alignment (Right)

**Severity: Low (positive)**

`.clang-format` specifies `PointerAlignment: Right`. The code is consistent. No violations found.

---

### 1.5 Const Correctness

**Severity: Medium | Location: across multiple files, pervasive pattern**

Many functions take `AppState *s` but only read fields:
- `gal_max_scroll` (`layout.c:40`)
- `gal_hit_test` (`layout.c:57`)
- `gal_calc_layout` (`layout.c:5`)
- `gal_tick_smooth_scroll` (`gallery.c:97`)

**Recommendation:** Add `const` qualifier to read-only `AppState *` parameters. This touches ~20 function signatures.

---

### 1.6 Static Functions for Module-Internal Functions

**Severity: Low | Location: app.c**

- `get_pictures_folder` (`app.c:62`) — only used in `main.c`, should be `static` or moved.

**Recommendation:** Make `get_pictures_folder` `static` in `main.c` or prefix with `app_`.

---

## 2. Static Analysis Readiness

### 2.1 clang-tidy Suppressions

**Severity: Low | Location: .clang-tidy:18**

`-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` is suppressed intentionally. In a C17 Win32 app where buffer sizes are bounded by `MAX_PATH_LEN` and `wcsncpy(...)[...] = L'\0'` ensures termination, this is reasonable.

### 2.2 Signed/Unsigned Mismatches

**Severity: Medium | Location: multiple files**

- `gallery.c:13` — `out->pad = (int) (thumb_size + thumb_padding)` — implicit narrowing from `float` to `int`.
- `app_append_image_entry` (`app.c:168`) — `int new_cap = s->capacity ? s->capacity * 2 : 256` — mixing `int` with `size_t` operations.
- `image_loader.c:106` — `UINT stride = thumb_size * 4` — signed `int` to unsigned conversion.
- `renderer.c:238-239` — `tdesc.Width = THUMB_SIZE` — `UINT` assigned from `int` macro.

**Recommendation:** Enable `-Wsign-conversion` or add explicit casts. Add `assert(thumb_size > 0)` at the top of `il_load_and_compress`.

### 2.3 NULL vs nullptr

**Severity: Low (informational)**

`NULL` is used consistently. C17 has no `nullptr`. The `.clang-tidy` config correctly maps this.

### 2.4 Unused Parameters via `(void) var`

**Severity: Low | Location: gallery.c:143, gallery_fullimage.c:71, main.c:533-535**

Correctly uses the `(void) hdc` and `(void) wParam` pattern.

**Recommendation:** Consider removing the `HDC hdc` parameter from both render functions (always called with `NULL`).

---

## 3. Type Safety

### 3.1 Enum Consistency

**Severity: Medium | Location: gallery_fullimage.c, gallery.c**

- `GridItem` field `type` is `uint8_t`, not `GridItemType` — deliberate space optimization but confusing.

**Recommendation:** Change `uint8_t type` to `GridItemType type` in `GridItem`.

### 3.2 Cast Quality — COM Vtbl Calls

**Severity: Low (positive)**

COM vtbl calls use correct interface types throughout. No incorrect vtbl dispatch found.

### 3.3 Implicit Conversions: int/uint/size_t

**Severity: Medium | Location: image_loader.c, asset_worker.c**

- `image_loader.c:59-72` — `UINT tw = thumb_size` where `thumb_size` is `int`. If negative, promotes to very large unsigned.
- `asset_worker.c:72` — `bc1_size = (int) GetFileSize(hFile, NULL)` — truncation of DWORD to int.
- `app_append_image_entry` (`app.c:168`) — `int new_cap = s->capacity ? s->capacity * 2 : 256` — should use `size_t`.

**Recommendation:** Use `size_t` for capacities. Add `assert(thumb_size > 0)` in `il_load_and_compress`.

---

## 4. Duplication / DRY

### 4.1 Strip Thumbnail Window Calculation (4× duplication)

**Severity: High | Location: gallery_fullimage.c:94-174, 276-376, 697-737 + renderer.c:766-806**

The same strip window layout math appears **four times** with near-identical code. Each block is ~30-50 lines.

**Recommendation:** Extract a function `static void gal_compute_strip_window(AppState *s, int active_index, int total_images, int *out_start, int *out_end, int *out_num_thumbs)` and share it.

### 4.2 Folder Name Formatting in Breadcrumb (duplicated)

**Severity: Medium | Location: gallery.c:484-517 and gallery.c:520-534**

The breadcrumb path formatting is computed identically twice — once for width memoization, once for rendering.

**Recommendation:** Cache `display_parent` alongside the width, or refactor into a helper function.

### 4.3 Hit-Test Constants Duplicated

**Severity: Medium | Location: gallery_fullimage.c, main.c**

Hardcoded floats like `20.0F * dpi`, `80.0F * dpi`, `130.0F * dpi` appear repeatedly across hit-test and rendering code.

**Recommendation:** Add fields for `strip_height`, `back_button_width`, `scrollbar_hit_width` to `ScaledLayout`.

### 4.4 Arena Alignment Arithmetic Duplicated

**Severity: Low | Location: types.h:132-134 and app.c:170-172**

The alignment round-up computation is duplicated between inline `arena_alloc` and `app_append_image_entry`.

**Recommendation:** Document the duplication or add an `arena_realloc` pattern.

---

## 5. Comments & Documentation

### 5.1 Module Headers

**Severity: Low (positive)**

Every `.c` file has a clear header comment. README module map is thorough.

### 5.2 Inline Comments

**Severity: Low**

Well-balanced overall. The HLSL shader (renderer.c:12-118) has **zero comments** — especially the 25-tap blur kernel has no explanation.

### 5.3 Complex Logic Without Explanation

**Severity: Medium | Location: gallery_fullimage.c:88-176**

The preloading nesting in `gal_render_fullimage` spans ~90 lines with multiple nested `if` blocks and a `break` for staggering.

**Recommendation:** Refactor the dual-branch logic (grid_items vs. fallback) into a shared helper.

### 5.4 Inline HLSL Shader

**Severity: Low | Location: renderer.c:12-118**

The 107-line HLSL shader embedded as a C string literal is hard to debug. No syntax highlighting.

**Recommendation:** Extract to `src/shader.hlsl` and compile at build time, or add a build step to convert to a C string header.

---

## 6. Test Quality

### 6.1 Coverage Gaps

**Severity: High | Location: tests/test_main.c**

29 tests exist covering layout, hit-testing, sort, state transitions, and folder population.

**Untested:** `renderer.c`, `file_monitor.c`, `asset_worker.c`, `image_loader.c`, `ui.c`, `gallery.c` render functions, startup/shutdown.

### 6.2 Test Framework

**Severity: Low | Location: tests/test_main.c:39-66**

Custom test framework with `TEST`/`CHECK`/`PASS`/`FAIL` macros is minimal but sufficient.

**Recommendation:** If test count grows beyond 50, migrate to `greatest.h` or `minctest`.

### 6.3 Unity Include Approach

**Severity: Medium | Location: tests/test_main.c:21-34**

Test file `#include`s `.c` files directly. The entire renderer is compiled even though most tests don't touch GPU code.

**Recommendation:** Add a compile-time switch (`CALBUM_TEST_BUILD`) that excludes `main()` and GPU-dependent initialization.

---

## 7. Maintainability Concerns

### 7.1 AppState God Struct

**Severity: High | Location: types.h:376-496**

`AppState` is **121 lines** and **~50 fields** spanning rendering, application state, UI state, OS handles, and thread synchronization.

**Recommendation:** Decompose into `RenderState`, `TextState`, `GPUState`, `WorkerState`, `ViewportState` sub-structs.

### 7.2 renderer.c Approaching 1,000-Line Limit

**Severity: Medium | Location: renderer.c (930 lines)**

Close to the claimed 1000-line limit. `main.c` is at 831 lines.

**Recommendation:** Extract full-image cache management (`r_alloc_full_image_slot`, etc.) into `renderer_cache.c`.

### 7.3 Shader String Literal

**Severity: Low | Location: renderer.c:12-118**

See 5.4 above.

### 7.4 Functions Exceeding 300 Lines

**Severity: High | Location: gallery.c, gallery_fullimage.c, renderer.c**

| Function | Lines | File |
|---|---|---|
| `gal_render_gallery` | ~442 | gallery.c:141-583 |
| `gal_render_fullimage` | ~488 | gallery_fullimage.c:69-557 |
| `r_init` | ~215 | renderer.c:120-335 |
| `app_populate_grid_items` | ~168 | app.c:314-482 |
| `gal_handle_fullimage_click` | ~182 | gallery_fullimage.c:559-741 |

**Recommendation:** Split both large render functions into focused helpers.

### 7.5 goto for Exit (main.c:820)

**Severity: Low | Location: main.c:820**

The single `goto exit_loop` follows the well-established C pattern for centralized cleanup. **Acceptable as-is.**

### 7.6 Arena Allocator Pattern

**Severity: Low (positive)**

The arena bump allocator is well-documented and correctly implemented. **Acceptable as-is.**

### 7.7 Ring Buffer Implementation

**Severity: Low | Location: types.h:162-217**

`rb_wait_pop` at line 207 is **never called** — all asset workers use `rb_try_pop` + `WaitForMultipleObjects`.

**Recommendation:** Remove unused `rb_wait_pop`.

---

## 8. Build System

### 8.1 Makefile Cleanliness

**Severity: Low (positive)**

Clean, well-commented Makefile with `release`, `debug`, `test`, `format`, `lint`, `lint-full`, `lint-fix`, `clean`, `size`, `run` targets.

### 8.2 No Package Manager

**Severity: Low (informational)**

Vendored `stb_dxt.h` in `lib/`. Appropriate for minimal dependencies.

### 8.3 Unity Build Performance

**Severity: Low (informational)**

~4,500 lines compiled in a single TU. Build times sub-second at -O0, ~3-5 seconds at -O2.

---

## 9. Minor Additional Findings

| ID | Severity | Location | Description | Recommendation |
|---|---|---|---|---|
| 9.1 | Low | `types.h:207-217` | `rb_wait_pop` defined but never called | Remove dead code |
| 9.2 | **Medium** | `renderer.c:168-201` | `vs_blob`/`ps_blob` used without NULL checks after `D3DCompile` | Add NULL checks before Release |
| 9.3 | Low | `file_scanner.c`, `image_loader.c` | Missing `assert` for invariants | Add `assert(path != NULL)` |
| 9.4 | **Medium** | `image_loader.c:8` | `g_wic_factory` thread safety fragile | Move into `AppState` or document more prominently |
| 9.5 | **Medium** | `types.h:294-295` | `full_width`/`full_height` are `uint16_t` (65535 limit) | Change to `uint32_t` |
| 9.6 | Low | `main.c:490-515` | Division by zero guard `if (scrollable_track > 0.0F)` present | Correct as-is |

---

## Summary of Findings by Severity

| Severity | Count | Key Issues |
|---|---|---|
| **High** | 4 | Strip window duplication (4×), AppState god struct, functions >400 lines, test coverage gaps |
| **Medium** | 8 | Const correctness, signed/unsigned mismatches, breadcrumb duplication, hit-test constants, `g_wic_factory` thread safety, NULL pointer deref in shader compile, `uint16_t` truncation, 1000-line limit proximity |
| **Low** | 15 | Naming prefix violations, missing braces, inline HLSL comments, unused `rb_wait_pop`, arena alignment duplication, `(void)hdc` parameter, etc. |
