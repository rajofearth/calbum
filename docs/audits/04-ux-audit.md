# Comprehensive UX Audit Report: calbum

**Audit date:** 2026-06-16
**Codebase:** `P:\Projects\calbum` (C17, Direct3D 11 / Direct2D, immediate-mode UI)
**Scope:** Visual design, interaction design, information architecture, accessibility, usability, and polish

---

## Executive Summary

calbum is an impressively crafted native image gallery with strong visual foundations — the Mica backdrop, dark theme, smooth scrolling, and ghost-style buttons show real attention to detail. The immediate-mode UI achieves a lot with minimal code. However, several issues range from critical blocking bugs (UI-freezing folder scan, empty state showing nothing) to missing essentials for a polished image viewer (no loading indicator, no tooltips, no context menu, no multi-select, no slideshow).

---

## Critical Severity

### C-1. Synchronous directory scan blocks UI indefinitely

| | |
|---|---|
| **Location** | `lib/fs/scanner.c:38-73` (`scan_recursive`) called from `app.c:130` |
| **Description** | `scan_recursive()` uses `FindFirstFileW`/`FindNextFileW` synchronously on the **main thread** during `app_load_folder()`. For folders with thousands of images across many subdirectories, the window becomes unresponsive until the scan completes. There is no progress feedback. The message loop only starts processing after `app_load_folder()` returns. |
| **Recommendation** | Move directory scanning to a background thread. Push results incrementally via custom window messages. Show a \"Scanning...\" overlay or indeterminate progress bar. As a minimum viable improvement, call `DisableProcessWindowsGhosting()` or use `PeekMessage` during long loops to keep the window responsive. |
| **Status** | ✅ RESOLVED |
| **Resolution** | Async directory scan implemented via background thread (Master Plan 2.4). UI no longer freezes. |

---

### C-2. Empty folder shows a blank black window with no guidance

| | |
|---|---|
| **Location** | `src/gallery.c:147-151` |
| **Description** | When `total_items == 0`, `gal_render_gallery` returns early BEFORE the top bar is drawn. The user sees an empty colored window with nothing — no top bar, no breadcrumb, no guidance. |
| **Recommendation** | Draw the top bar + breadcrumb even when there are no items. Display a centered message like \"Drop a folder here\" or \"This folder contains no images\". |
| **Status** | ✅ RESOLVED |
| **Resolution** | Empty state guidance with centered message "No images here — drop a folder to browse" (Master Plan 4.1). |

---

### C-3. No error handling for corrupt images, permission failures, or unsupported formats

| | |
|---|---|
| **Location** | `lib/image/loader.c`, `lib/fs/scanner.c:46-48` |
| **Description** | If `FindFirstFileW` fails (permission denied), `scan_recursive` silently returns zero items. If `il_load_and_compress` fails on a corrupt image, the thumbnail slot stays at `TOKEN_DEFAULT` forever. `IMG_STATE_FAILED` is defined but never used. |
| **Recommendation** | Set `e->state = IMG_STATE_FAILED` when decode fails. Render a broken-image icon over failed thumbnails. Display an inline error message for directory scan failures. |
| **Status** | ✅ RESOLVED |
| **Resolution** | IMG_STATE_FAILED set on decode failure, ⚠ warning icon rendered (Master Plan 4.2). |

---

### C-4. Instance buffer overflow in full-image view (no overflow guard)

| | |
|---|---|
| **Location** | `src/gallery_fullimage.c:178` |
| **Description** | Full-image view declares `static InstanceData instances[4096]` but does NOT check bounds before writing. A long thumbnail strip could overflow the stack array into adjacent memory. |
| **Recommendation** | Add `if (inst_count >= MAX_INSTANCES - 16) break;` guard. Better yet, use a define constant consistently across both renderers. |
| **Status** | ✅ RESOLVED |
| **Resolution** | MAX_INSTANCES constant + bounds guards (Master Plan 1.2). |

---

## High Severity

### H-1. No loading/spinner indicator for thumbnail loading

| | |
|---|---|
| **Location** | `src/gallery.c:229-233`, `gallery_fullimage.c:340-344` |
| **Description** | Until a thumbnail arrives, the cell shows only a dark panel (`TOKEN_DEFAULT`). No pulsing placeholder or spinner indicates that content is loading. |
| **Recommendation** | Add a subtle pulsing animation or small spinning indicator for `IMG_STATE_NEW`/`IMG_STATE_LOADING` states. |
| **Status** | ⚠️ PARTIALLY RESOLVED |
| **Resolution** | Subtle pulse added to non-resident thumbnails. Explicit spinner not implemented (intentionally skipped per Master Plan 4.5 notes). |

### H-2. Scrollbar track click (page-up/page-down) not implemented

| | |
|---|---|
| **Location** | `src/main.c:467-474` |
| **Description** | Clicking on the scrollbar track above or below the thumb does nothing — only thumb drag is supported. |
| **Recommendation** | Implement track-click: if click is on the track but not the thumb, scroll by approximately one viewport height. |
| **Status** | ✅ RESOLVED |
| **Resolution** | Implemented in on_lbutton_down (Master Plan 4.4). |

### H-3. Tooltips absent on all interactive elements

| | |
|---|---|
| **Location** | Throughout `gallery.c` and `gallery_fullimage.c` |
| **Description** | Sort button, info button, back button, prev/next arrows, zoom badge, close buttons — all have no tooltip. |
| **Recommendation** | Use `Tooltip_*` common controls or a custom tooltip after 500ms hover delay on all buttons/badges. |
| **Status** | 🔴 STILL RELEVANT |
| **Resolution** | No tooltip implementation. |

### H-4. No right-click context menu

| | |
|---|---|
| **Location** | `src/main.c` — no `WM_RBUTTONDOWN` handler |
| **Description** | Right-clicking does nothing. No access to Open, Copy, Delete, Properties, or \"Open file location\". |
| **Recommendation** | Implement `TrackPopupMenu` with items: \"Open\", \"Open file location\", \"Copy\", \"Delete\", \"Properties\". |
| **Status** | 🔴 STILL RELEVANT |
| **Resolution** | No WM_RBUTTONDOWN handler. |

### H-5. Home/End keyboard navigation does not scroll to make selection visible

| | |
|---|---|
| **Location** | `src/main.c:420-432` |
| **Description** | Pressing Home/End moves `selected_index` but does not adjust `scroll_target_y`. The selection moves off-screen. |
| **Recommendation** | After changing `selected_index`, recompute scroll position so the selected item is visible. |
| **Status** | 🔴 STILL RELEVANT |
| **Resolution** | VK_HOME/VK_END not handled (Master Plan 4.3 — NOT DONE). |

---

## Medium Severity

### M-1. No search or filter

| | |
|---|---|
| **Location** | Feature gap |
| **Description** | No search or filter functionality is implemented. |
| **Recommendation** | Add text field in top bar; filter grid items by filename substring. |
| **Status** | Feature Gap (v0.1.0 scope) |
| **Resolution** | Deferred to a future release. |

---

### M-2. No image deletion / file operations

| | |
|---|---|
| **Location** | Feature gap |
| **Description** | No delete or file operations are implemented. |
| **Recommendation** | Implement Delete key + SHFileOperationW for recycle-bin integration. |
| **Status** | Feature Gap (v0.1.0 scope) |
| **Resolution** | Deferred to a future release. |

---

### M-3. No multi-select or batch operations

| | |
|---|---|
| **Location** | Feature gap |
| **Description** | No multi-select or batch operations are implemented. |
| **Recommendation** | Bitmap/dynamic array for selection; Ctrl+click, Shift+click. |
| **Status** | Feature Gap (v0.1.0 scope) |
| **Resolution** | Deferred to a future release. |

---

### M-4. No slideshow mode

| | |
|---|---|
| **Location** | Feature gap |
| **Description** | No slideshow mode is implemented. |
| **Recommendation** | Auto-advance with configurable interval (F5 trigger). |
| **Status** | Feature Gap (v0.1.0 scope) |
| **Resolution** | Deferred to a future release. |

---

### M-5. Sort menu: click-outside behavior lacks visual cue

| | |
|---|---|
| **Location** | `gallery.c:66-68` |
| **Description** | Sort menu click-outside lacks a visual cue. |
| **Recommendation** | Add subtle backdrop overlay when menu is open. |
| **Status** | 🔴 STILL RELEVANT |
| **Resolution** | Not yet implemented. |

---

### M-6. Zoom badge timer resets while hovered

| | |
|---|---|
| **Location** | `main.c:794-796` |
| **Description** | Zoom badge timer resets while hovered, preventing it from fading away. |
| **Recommendation** | Allow badge to fade after 5s even when hovered. |
| **Status** | 🔴 STILL RELEVANT |
| **Resolution** | Not yet implemented. |

---

### M-7. No touch support

| | |
|---|---|
| **Location** | Feature gap |
| **Description** | No touch support is implemented. |
| **Recommendation** | Register for WM_TOUCH; map pinch-to-zoom, swipe. |
| **Status** | Feature Gap (v0.1.0 scope) |
| **Resolution** | Deferred to a future release. |

---

### M-8. GPU texture pool may thrash during fast scrolling

| | |
|---|---|
| **Location** | `types.h:335` |
| **Description** | GPU texture pool may thrash during fast scrolling, causing visible pop-in. |
| **Recommendation** | Increase pool size or implement priority-based eviction. |
| **Status** | ⚠️ PARTIALLY RESOLVED |
| **Resolution** | Pool increased to 1024 (huge improvement). Priority-based eviction not implemented. |

---

### M-9. Full-image cache limited with simple eviction

| | |
|---|---|
| **Location** | `types.h:479` |
| **Description** | Full-image cache is limited and uses a simple eviction strategy. |
| **Recommendation** | Increase FULL_CACHE_SIZE or use proper LRU. |
| **Status** | 🔴 STILL RELEVANT |
| **Resolution** | FULL_CACHE_SIZE=32 with simple first-free-slot eviction. |

---

## Low Severity

### L-1. Breadcrumb measurement is cached but child width computed each frame

| | |
|---|---|
| **Location** | `gallery.c:475-517` |
| **Description** | Breadcrumb measurement is cached but child width computed each frame. |
| **Recommendation** | Minor — no change needed. |
| **Status** | 🔴 STILL RELEVANT (trivial) |
| **Resolution** | Width cached, child segments recomputed per frame. |

---

### L-2. No gamma-correct rendering (sRGB)

| | |
|---|---|
| **Location** | Various files in `lib/gpu/` |
| **Description** | No gamma-correct rendering (sRGB) is implemented. |
| **Recommendation** | Use `_SRGB` swap chain and texture formats. |
| **Status** | 🔴 STILL RELEVANT |
| **Resolution** | Not yet implemented. |

---

### L-3. Window title exposes full path

| | |
|---|---|
| **Location** | `app.c:156-160` |
| **Description** | Window title shows the full directory path instead of just the folder name. |
| **Recommendation** | Show only current folder name. |
| **Status** | ✅ RESOLVED |
| **Resolution** | app_update_title uses leaf name (Master Plan 4.6). |

---

### L-4. Folder icons use hardcoded Unicode codepoints

| | |
|---|---|
| **Location** | `gallery.c:414` |
| **Description** | Folder icons use hardcoded Unicode codepoints instead of the system folder icon. |
| **Recommendation** | Consider `SHGetStockIconInfo` for system folder icon. |
| **Status** | 🔴 STILL RELEVANT |
| **Resolution** | Not yet implemented. |

---

### L-5. Info panel truncates paths/filenames

| | |
|---|---|
| **Location** | `gallery_fullimage.c:494-512` |
| **Description** | Info panel truncates paths and filenames that are too long. |
| **Recommendation** | Show full path on hover (tooltip) or expand panel width. |
| **Status** | 🔴 STILL RELEVANT |
| **Resolution** | Not yet implemented. |

---

### L-6. Double-click in full-image does nothing

| | |
|---|---|
| **Location** | `main.c:549-559` |
| **Description** | Double-clicking in full-image view does nothing. |
| **Recommendation** | Toggle between fit-to-window and actual pixels. |
| **Status** | 🔴 STILL RELEVANT |
| **Resolution** | Not yet implemented. |

---

### L-7. `IMG_STATE_FAILED` defined but never used

| | |
|---|---|
| **Location** | `types.h:267` |
| **Description** | `IMG_STATE_FAILED` is defined in types.h but was never set anywhere in the codebase. |
| **Recommendation** | Set on decode failure; render error icon. |
| **Status** | ✅ RESOLVED |
| **Resolution** | Used in on_thumb_complete + warning icon (Master Plan 4.2). |

---

### L-8. `selected_index` starts at -1 but used as array index

| | |
|---|---|
| **Location** | Throughout |
| **Description** | `selected_index` starts at -1 but is used directly as an array index in several places without a guard. |
| **Recommendation** | Audit all uses; add debug asserts. |
| **Status** | ⚠️ PARTIALLY RESOLVED |
| **Resolution** | Guards added before array access. No debug asserts. |

---

## Accessibility

### A-1. No UIA (UI Automation) support

| | |
|---|---|
| **Location** | Feature gap |
| **Description** | No UI Automation support is implemented. |
| **Recommendation** | Implement `IAccessible` or `IRawElementProviderSimple`. |
| **Status** | Feature Gap (v0.1.0 scope) |
| **Resolution** | Deferred to a future release. |

---

### A-2. No keyboard focus ring beyond selection

| | |
|---|---|
| **Location** | Feature gap |
| **Description** | No visible keyboard focus ring exists beyond the selection highlight. |
| **Recommendation** | Implement visible focus ring; make buttons Tab-reachable. |
| **Status** | Feature Gap (v0.1.0 scope) |
| **Resolution** | Deferred to a future release. |

---

### A-3. Selection uses only accent color

| | |
|---|---|
| **Location** | Feature gap |
| **Description** | Selection indication relies solely on the accent color, with no secondary indicator. |
| **Recommendation** | Add secondary indicator (thicker outline, checkmark). |
| **Status** | Feature Gap (v0.1.0 scope) |
| **Resolution** | Deferred to a future release. |

---

### A-4. High-DPI support needs testing at 250%+

| | |
|---|---|
| **Location** | Feature gap |
| **Description** | High-DPI scaling beyond 250% has not been validated. |
| **Recommendation** | Verify button sizes and scrollbar usability at extreme scales. |
| **Status** | Feature Gap (v0.1.0 scope) |
| **Resolution** | Deferred to a future release. |

---

## Information Architecture (Positive)

| Finding | Location | Assessment |
|---|---|---|
| Breadcrumb navigation is clear | `gallery.c:447-556` | Consider making segments clickable for direct ancestor navigation |
| Folder info display is well-designed | `gallery.c:123-139` | Good use of visual hierarchy with count badge + icon |
| Image metadata info panel is comprehensive | `gallery_fullimage.c:449-553` | Consider adding EXIF data as future enhancement |

---

## Visual Design (Positive)

| Finding | Location | Assessment |
|---|---|---|
| Color palette is well-tuned for dark theme | `main.c:83-131` | WCAG AAA contrast ratios; vibrant amber accent |
| Ghost-style buttons use elegant opacity transitions | `ui.c:94-122` | Add subtle background fill animation (lerp) for extra polish |
| Scrollbar fade animation is well-crafted | `gallery.c:284-342` | 2s timer, hover expansion 6dp→10dp, opacity lerp |

---

## Interaction Design (Positive)

| Finding | Location | Assessment |
|---|---|---|
| Zoom and pan interaction is well-designed | `gallery_fullimage.c:86-148`, `main.c:561-577` | Add smooth zoom animation over ~100ms |
| Sort menu with checkbox indicators is clean | `gallery.c:380-392,560-579` | Add separator between sort modes and direction options |

---

## Edge Cases / Robustness

### E-1. Drag-drop only accepts first file/folder

| | |
|---|---|
| **Location** | `main.c:580-591` |
| **Description** | Drag-drop only accepts the first file or folder dropped; subsequent items are ignored. |
| **Recommendation** | Accept image files dropped directly; multi-folder support. |
| **Status** | 🔴 STILL RELEVANT |
| **Resolution** | Not yet implemented. |

---

### E-2. Window ghosting after `app_load_folder`

| | |
|---|---|
| **Location** | `main.c:742` |
| **Description** | Window ghosting occurs after a long `app_load_folder` call because the window cannot respond to the DWM ghosting ping. |
| **Recommendation** | Call `DisableProcessWindowsGhosting()` or defer loading. |
| **Status** | ⚠️ PARTIALLY RESOLVED |
| **Resolution** | Scan now async so no long freeze. No DisableProcessWindowsGhosting() call. |

---

### E-3. No re-entrant `app_load_folder` protection

| | |
|---|---|
| **Location** | `app.c:86-154` |
| **Description** | `app_load_folder` can be re-entered while a scan is already in progress, causing undefined behavior. |
| **Recommendation** | Add `loading` flag when scan becomes async. |
| **Status** | ⚠️ PARTIALLY RESOLVED |
| **Resolution** | scanning flag exists but is not checked at entry of app_load_folder. |

---

## Summary

| Severity | Count | ✅ Resolved | ⚠️ Partial | 🔴 Still Relevant | Feature Gap |
|---|---|---|---|---|---|
| **Critical** | 4 | 4 | 0 | 0 | 0 |
| **High** | 5 | 1 | 1 | 3 | 0 |
| **Medium** | 9 | 0 | 1 | 3 | 5 |
| **Low** | 8 | 2 | 1 | 5 | 0 |
| **Accessibility** | 4 | 0 | 0 | 0 | 4 |
| **Edge Cases** | 3 | 0 | 2 | 1 | 0 |
| **Total** | **33 findings** | **7** | **5** | **12** | **9** |
