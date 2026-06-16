# Code Quality & Maintainability Audit Report: calbum

**Audit date:** 2026-06-16
**Version audited:** 0.1.0
**Total files examined:** 21 source files + 5 config/build files

---

## 1. Coding Standards Compliance

### 1.1 Naming Conventions / Module Prefixes

**Severity: Low | Location: scattered**

**Status:** ✅ Partially resolved / Still Relevant (format_size/format_filetime)

Module prefixes are applied correctly throughout (`fs_`, `il_`, `r_`, `gal_`, `aw_`, `fm_`, `ui_`, `app_`). Two original violations:

- `get_pictures_folder` (`src/app.c`) and `get_parent_dir` (`src/app.c`) — **✅ RESOLVED (Master Plan 3.3).** Renamed with `app_` prefix.
- `format_size` / `format_filetime` (`lib/core/utils.c:5,25`) — **Still Relevant.** Declared as `void format_...` but the module prefix convention would suggest `ut_`.

**Resolution (get_pictures_folder/get_parent_dir):** Renamed with `app_` prefix under Master Plan 3.3.

**Recommendation (format_size/format_filetime):** Rename `format_size` → `ut_format_size` and `format_filetime` → `ut_format_filetime`.

---

### 1.2 Allman Brace Style

**Severity: Low | Location: gallery.c:13-14,17-18**

**Status:** Still Relevant (by convention, clang-tidy permits it)

A few single-line `if` bodies without braces:
```c
if (out->pad < 1) out->pad = 1;
```
The clang-tidy config permits this via `ShortStatementLines: 2`.

**Recommendation:** Add braces for consistency, or accept the existing clang-tidy exception as intentional.

---

### 1.3 120-Column Limit

**Severity: Low | Location: gallery_fullimage.c:128-129, gallery.c:509-510**

**Status:** Still Relevant (minor, clang-format catches these)

Most code stays under 120 columns. A few lines exceed it slightly.

**Recommendation:** Run `clang-format` (already available via `make format`) to catch these.

---

### 1.4 Pointer Alignment (Right)

**Severity: Low (positive)**

**Status:** Still compliant ✅

`.clang-format` specifies `PointerAlignment: Right`. The code is consistent. No violations found.

---

### 1.5 Const Correctness

**Severity: Medium | Location: across multiple files, pervasive pattern**

**Status:** ✅ RESOLVED (Master Plan 3.4)

Many functions took `AppState *s` but only read fields:
- `gal_max_scroll` (`layout.c:40`)
- `gal_hit_test` (`layout.c:57`)
- `gal_calc_layout` (`layout.c:5`)

**Resolution (Master Plan 3.4):** `const` qualifier added to 3 `layout.c` functions (`gal_max_scroll`, `gal_hit_test`, `gal_calc_layout`).

---

### 1.6 Static Functions for Module-Internal Functions

**Severity: Low | Location: app.c**

**Status:** ✅ RESOLVED (Master Plan 3.3)

- `get_pictures_folder` (`app.c:62`) — only used in `main.c`, should be `static` or moved.

**Resolution (Master Plan 3.3):** Function renamed with `app_` prefix (`app_get_pictures_folder`).

---

## 2. Static Analysis Readiness

### 2.1 clang-tidy Suppressions

**Severity: Low | Location: .clang-tidy:18**

**Status:** Still acceptable

`-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` is suppressed intentionally. In a C17 Win32 app where buffer sizes are bounded by `MAX_PATH_LEN` and `wcsncpy(...)[...] = L'\0'` ensures termination, this is reasonable.

### 2.2 Signed/Unsigned Mismatches

**Severity: Medium | Location: multiple files**

**Status:** ⚠️ PARTIALLY RESOLVED

- `layout.c` — `out->pad = (int) (thumb_size + thumb_padding)` — implicit narrowing from `float` to `int` (still present).
- `app_append_image_entry` (`app.c:168`) — `int new_cap = s->capacity ? s->capacity * 2 : 256` — mixing `int` with `size_t` operations (still present).
- `image_loader.c` — `UINT stride = thumb_size * 4` — signed `int` to unsigned conversion (still present).
- ~~`renderer.c:238-239` — `tdesc.Width = THUMB_SIZE` — `UINT` assigned from `int` macro.~~ ✅ Resolved by file split.

**Recommendation:** Enable `-Wsign-conversion` or add explicit casts. Add `assert(thumb_size > 0)` at the top of `il_load_and_compress`.

### 2.3 NULL vs nullptr

**Severity: Low (informational)**

**Status:** Still consistent

`NULL` is used consistently. C17 has no `nullptr`. The `.clang-tidy` config correctly maps this.

### 2.4 Unused Parameters via `(void) var`

**Severity: Low | Location: gallery.c:143, gallery_fullimage.c:71, main.c:533-535**

**Status:** Still Relevant

`(void) hdc` still in both render functions.

**Recommendation:** Consider removing the `HDC hdc` parameter from both render functions (always called with `NULL`).

---

## 3. Type Safety

### 3.1 Enum Consistency

**Severity: Medium | Location: gallery_fullimage.c, gallery.c**

**Status:** Still Relevant

- `GridItem` field `type` is `uint8_t`, not `GridItemType` — deliberate space optimization but confusing.

**Recommendation:** Change `uint8_t type` to `GridItemType type` in `GridItem`.

### 3.2 Cast Quality — COM Vtbl Calls

**Severity: Low (positive)**

**Status:** Still correct

COM vtbl calls use correct interface types throughout. No incorrect vtbl dispatch found.

### 3.3 Implicit Conversions: int/uint/size_t

**Severity: Medium | Location: image_loader.c, asset_worker.c**

**Status:** Still Relevant. Same three sites.

- `image_loader.c:59-72` — `UINT tw = thumb_size` where `thumb_size` is `int`. If negative, promotes to very large unsigned.
- `asset_worker.c:72` — `bc1_size = (int) GetFileSize(hFile, NULL)` — truncation of DWORD to int.
- `app_append_image_entry` (`app.c:168`) — `int new_cap = s->capacity ? s->capacity * 2 : 256` — should use `size_t`.

**Recommendation:** Use `size_t` for capacities. Add `assert(thumb_size > 0)` in `il_load_and_compress`.

---

## 4. Duplication / DRY

### 4.1 Strip Thumbnail Window Calculation (4× duplication)

**Severity: High | Location: gallery_fullimage.c:94-174, 276-376, 697-737 + renderer.c:766-806**

**Status:** ✅ RESOLVED (Master Plan 3.1)

**Resolution (Master Plan 3.1):** `fiv_strip_bounds()` extracted as shared helper. All four call sites unified.

### 4.2 Folder Name Formatting in Breadcrumb (duplicated)

**Severity: Medium | Location: gallery.c:484-517 and gallery.c:520-534**

**Status:** ✅ RESOLVED (Master Plan 4.7)

**Resolution (Master Plan 4.7):** `cached_display_parent` used to cache the formatted breadcrumb string.

### 4.3 Hit-Test Constants Duplicated

**Severity: Medium | Location: gallery_fullimage.c, main.c**

**Status:** Still Relevant

Hardcoded floats like `20.0F * dpi`, `80.0F * dpi`, `130.0F * dpi` appear repeatedly across hit-test and rendering code.

**Recommendation:** Add fields for `strip_height`, `back_button_width`, `scrollbar_hit_width` to `ScaledLayout`.

### 4.4 Arena Alignment Arithmetic Duplicated

**Severity: Low | Location: types.h:132-134 and app.c:170-172**

**Status:** Still Relevant

The alignment round-up computation is duplicated between inline `arena_alloc` and `app_append_image_entry`.

**Recommendation:** Document the duplication or add an `arena_realloc` pattern.

---

## 5. Comments & Documentation

### 5.1 Module Headers

**Severity: Low (positive)**

**Status:** Still good

Every `.c` file has a clear header comment. README module map is thorough.

### 5.2 Inline Comments

**Severity: Low**

**Status:** Improved

Well-balanced overall. The HLSL shader (formerly in renderer.c, now in `lib/gpu/shader.c`) has been **improved with inline comments**.

### 5.3 Complex Logic Without Explanation

**Severity: Medium | Location: gallery_fullimage.c:88-176**

**Status:** ✅ RESOLVED (Master Plan 3.5)

**Resolution (Master Plan 3.5):** Preloading logic refactored into `fiv_update_preloading` helper.

### 5.4 Inline HLSL Shader

**Severity: Low | Location: renderer.c:12-118 → lib/gpu/shader.c**

**Status:** ⚠️ PARTIALLY RESOLVED

The HLSL shader has been moved from `renderer.c` to `lib/gpu/shader.c` with inline comments added. However, it remains a C string literal rather than a separate `.hlsl` file.

**Recommendation:** Extract to `src/shader.hlsl` and compile at build time, or add a build step to convert to a C string header.

---

## 6. Test Quality

### 6.1 Coverage Gaps

**Severity: High | Location: tests/test_main.c**

**Status:** Still Relevant (High)

29 tests exist covering layout, hit-testing, sort, state transitions, and folder population.

**Untested:** `lib/gpu/device.c`, `lib/gpu/d2d.c`, `lib/gpu/texture.c`, `lib/gpu/shader.c`, `lib/gpu/fullimage.c`, `lib/fs/monitor.c`, `src/asset_worker.c`, `lib/image/loader.c`, `lib/ui/ui.c`, `src/gallery.c` render functions, startup/shutdown.

### 6.2 Test Framework

**Severity: Low | Location: tests/test_main.c:39-66**

**Status:** Still acceptable (29 tests, under 50 threshold)

Custom test framework with `TEST`/`CHECK`/`PASS`/`FAIL` macros is minimal but sufficient.

**Recommendation:** If test count grows beyond 50, migrate to `greatest.h` or `minctest`.

### 6.3 Unity Include Approach

**Severity: Medium | Location: tests/test_main.c:21-34**

**Status:** Still Relevant

Test file `#include`s `.c` files directly. `CALBUM_TEST_BUILD` not implemented.

**Recommendation:** Add a compile-time switch (`CALBUM_TEST_BUILD`) that excludes `main()` and GPU-dependent initialization.

---

## 7. Maintainability Concerns

### 7.1 AppState God Struct

**Severity: High | Location: types.h:376-496**

**Status:** ✅ RESOLVED (Master Plan 5.1)

**Resolution (Master Plan 5.1):** `AppState` decomposed into `RenderState`, `TextState`, `GPUState`, `WorkerState`, `ViewportState` sub-structs.

### 7.2 renderer.c Approaching 1,000-Line Limit

**Severity: Medium | Location: renderer.c (930 lines)**

**Status:** ✅ RESOLVED

**Resolution:** `renderer.c` split into 5 files under `lib/gpu/`:
- `lib/gpu/device.c`
- `lib/gpu/d2d.c`
- `lib/gpu/texture.c`
- `lib/gpu/shader.c`
- `lib/gpu/fullimage.c`

### 7.3 Shader String Literal

**Severity: Low | Location: lib/gpu/shader.c**

**Status:** See 5.4

### 7.4 Functions Exceeding 300 Lines

**Severity: High | Location: gallery.c, gallery_fullimage.c, renderer.c**

**Status:** ✅ RESOLVED (Master Plan 3.5)

**Resolution (Master Plan 3.5):** All original functions now well under 300 lines after extraction of helpers (`fiv_update_preloading`, `fiv_strip_bounds`, etc.).

### 7.5 goto for Exit (main.c:820)

**Severity: Low | Location: main.c:820**

**Status:** Still acceptable

The single `goto exit_loop` follows the well-established C pattern for centralized cleanup. **Acceptable as-is.**

### 7.6 Arena Allocator Pattern

**Severity: Low (positive)**

**Status:** Still good

The arena bump allocator is well-documented and correctly implemented. **Acceptable as-is.**

### 7.7 Ring Buffer Implementation

**Severity: Low | Location: types.h:162-217**

**Status:** ✅ RESOLVED (Master Plan 3.6)

`rb_wait_pop` at line 207 was **never called** — all asset workers use `rb_try_pop` + `WaitForMultipleObjects`.

**Resolution (Master Plan 3.6):** Removed unused `rb_wait_pop`.

---

## 8. Build System

### 8.1 Makefile Cleanliness

**Severity: Low (positive)**

**Status:** Still clean

Clean, well-commented Makefile with `release`, `debug`, `test`, `format`, `lint`, `lint-full`, `lint-fix`, `clean`, `size`, `run` targets.

### 8.2 No Package Manager

**Severity: Low (informational)**

**Status:** Still appropriate

Vendored `stb_dxt.h` in `lib/`. Appropriate for minimal dependencies.

### 8.3 Unity Build Performance

**Severity: Low (informational)**

**Status:** Still good (source tree grown but builds fast)

~4,500 lines compiled in a single TU. Build times sub-second at -O0, ~3-5 seconds at -O2.

---

## 9. Minor Additional Findings

| ID | Severity | Location | Description | Status | Recommendation |
|---|---|---|---|---|---|
| 9.1 | Low | `types.h:207-217` | `rb_wait_pop` defined but never called | ✅ RESOLVED (same as 7.7) | Removed dead code |
| 9.2 | **Medium** | `lib/gpu/device.c:87-90` | `vs_blob`/`ps_blob` used without NULL checks after `D3DCompile` | 🔴 STILL RELEVANT | Add NULL checks before Release |
| 9.3 | Low | `lib/fs/scanner.c`, `lib/image/loader.c` | Missing `assert` for invariants | Still Relevant | Add `assert(path != NULL)` |
| 9.4 | **Medium** | `lib/image/loader.c:8` | `g_wic_factory` thread safety fragile | ⚠️ PARTIALLY RESOLVED | Workers joined before shutdown, but factory still global static |
| 9.5 | **Medium** | `types.h:294-295` | `full_width`/`full_height` are `uint16_t` (65535 limit) | Still Relevant | Change to `uint32_t` |
| 9.6 | Low | `main.c:490-515` | Division by zero guard `if (scrollable_track > 0.0F)` present | Still correct | Correct as-is |

---

## Summary of Findings by Severity

| Severity | Fixed | Still Relevant / Partially |
|---|---|---|
| **High** | 3 (4.1, 7.1, 7.4) | 1 (6.1) |
| **Medium** | 1 (1.5) | 8 (2.2, 3.3, 4.3, 5.4, 6.3, 9.2, 9.4, 9.5) |
| **Low** | 4 (1.1, 1.6, 4.2, 7.7/9.1) | 6 (1.2, 1.3, 2.4, 4.4, 5.2, 9.3) |
