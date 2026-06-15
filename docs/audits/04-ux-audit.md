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
| **Location** | `src/file_scanner.c:38-73` (`scan_recursive`) called from `app.c:130` |
| **Description** | `scan_recursive()` uses `FindFirstFileW`/`FindNextFileW` synchronously on the **main thread** during `app_load_folder()`. For folders with thousands of images across many subdirectories, the window becomes unresponsive until the scan completes. There is no progress feedback. The message loop only starts processing after `app_load_folder()` returns. |
| **Recommendation** | Move directory scanning to a background thread. Push results incrementally via custom window messages. Show a \"Scanning...\" overlay or indeterminate progress bar. As a minimum viable improvement, call `DisableProcessWindowsGhosting()` or use `PeekMessage` during long loops to keep the window responsive. |

---

### C-2. Empty folder shows a blank black window with no guidance

| | |
|---|---|
| **Location** | `src/gallery.c:147-151` |
| **Description** | When `total_items == 0`, `gal_render_gallery` returns early BEFORE the top bar is drawn. The user sees an empty colored window with nothing — no top bar, no breadcrumb, no guidance. |
| **Recommendation** | Draw the top bar + breadcrumb even when there are no items. Display a centered message like \"Drop a folder here\" or \"This folder contains no images\". |

---

### C-3. No error handling for corrupt images, permission failures, or unsupported formats

| | |
|---|---|
| **Location** | `src/image_loader.c`, `src/file_scanner.c:46-48` |
| **Description** | If `FindFirstFileW` fails (permission denied), `scan_recursive` silently returns zero items. If `il_load_and_compress` fails on a corrupt image, the thumbnail slot stays at `TOKEN_DEFAULT` forever. `IMG_STATE_FAILED` is defined but never used. |
| **Recommendation** | Set `e->state = IMG_STATE_FAILED` when decode fails. Render a broken-image icon over failed thumbnails. Display an inline error message for directory scan failures. |

---

### C-4. Instance buffer overflow in full-image view (no overflow guard)

| | |
|---|---|
| **Location** | `src/gallery_fullimage.c:178` |
| **Description** | Full-image view declares `static InstanceData instances[4096]` but does NOT check bounds before writing. A long thumbnail strip could overflow the stack array into adjacent memory. |
| **Recommendation** | Add `if (inst_count >= MAX_INSTANCES - 16) break;` guard. Better yet, use a define constant consistently across both renderers. |

---

## High Severity

### H-1. No loading/spinner indicator for thumbnail loading

| | |
|---|---|
| **Location** | `src/gallery.c:229-233`, `gallery_fullimage.c:340-344` |
| **Description** | Until a thumbnail arrives, the cell shows only a dark panel (`TOKEN_DEFAULT`). No pulsing placeholder or spinner indicates that content is loading. |
| **Recommendation** | Add a subtle pulsing animation or small spinning indicator for `IMG_STATE_NEW`/`IMG_STATE_LOADING` states. |

### H-2. Scrollbar track click (page-up/page-down) not implemented

| | |
|---|---|
| **Location** | `src/main.c:467-474` |
| **Description** | Clicking on the scrollbar track above or below the thumb does nothing — only thumb drag is supported. |
| **Recommendation** | Implement track-click: if click is on the track but not the thumb, scroll by approximately one viewport height. |

### H-3. Tooltips absent on all interactive elements

| | |
|---|---|
| **Location** | Throughout `gallery.c` and `gallery_fullimage.c` |
| **Description** | Sort button, info button, back button, prev/next arrows, zoom badge, close buttons — all have no tooltip. |
| **Recommendation** | Use `Tooltip_*` common controls or a custom tooltip after 500ms hover delay on all buttons/badges. |

### H-4. No right-click context menu

| | |
|---|---|
| **Location** | `src/main.c` — no `WM_RBUTTONDOWN` handler |
| **Description** | Right-clicking does nothing. No access to Open, Copy, Delete, Properties, or \"Open file location\". |
| **Recommendation** | Implement `TrackPopupMenu` with items: \"Open\", \"Open file location\", \"Copy\", \"Delete\", \"Properties\". |

### H-5. Home/End keyboard navigation does not scroll to make selection visible

| | |
|---|---|
| **Location** | `src/main.c:420-432` |
| **Description** | Pressing Home/End moves `selected_index` but does not adjust `scroll_target_y`. The selection moves off-screen. |
| **Recommendation** | After changing `selected_index`, recompute scroll position so the selected item is visible. |

---

## Medium Severity

| ID | Finding | Location | Recommendation |
|---|---|---|---|
| M-1 | No search or filter | Feature gap | Add text field in top bar; filter grid items by filename substring |
| M-2 | No image deletion/file operations | Feature gap | Implement Delete key + SHFileOperationW for recycle-bin integration |
| M-3 | No multi-select or batch operations | Feature gap | Bitmap/dynamic array for selection; Ctrl+click, Shift+click |
| M-4 | No slideshow mode | Feature gap | Auto-advance with configurable interval (F5 trigger) |
| M-5 | Sort menu: click-outside behavior lacks visual cue | `gallery.c:66-68` | Add subtle backdrop overlay when menu is open |
| M-6 | Zoom badge timer resets while hovered | `main.c:794-796` | Allow badge to fade after 5s even when hovered |
| M-7 | No touch support | Feature gap | Register for WM_TOUCH; map pinch-to-zoom, swipe |
| M-8 | GPU texture pool may thrash during fast scrolling | `types.h:335` | Increase pool size or implement priority-based eviction |
| M-9 | Full-image cache limited with simple eviction | `types.h:479` | Increase FULL_CACHE_SIZE or use proper LRU |

---

## Low Severity

| ID | Finding | Location | Recommendation |
|---|---|---|---|
| L-1 | Breadcrumb measurement is cached but child width computed each frame | `gallery.c:475-517` | Minor — no change needed |
| L-2 | No gamma-correct rendering (sRGB) | `renderer.c:145,877` | Use `_SRGB` swap chain and texture formats |
| L-3 | Window title exposes full path | `app.c:156-160` | Show only current folder name |
| L-4 | Folder icons use hardcoded Unicode codepoints | `gallery.c:414` | Consider `SHGetStockIconInfo` for system folder icon |
| L-5 | Info panel truncates paths/filenames | `gallery_fullimage.c:494-512` | Show full path on hover (tooltip) or expand panel width |
| L-6 | Double-click in full-image does nothing | `main.c:549-559` | Toggle between fit-to-window and actual pixels |
| L-7 | `IMG_STATE_FAILED` defined but never used | `types.h:267` | Set on decode failure; render error icon |
| L-8 | `selected_index` starts at -1 but used as array index | Throughout | Audit all uses; add debug asserts |

---

## Accessibility

| ID | Finding | Severity | Recommendation |
|---|---|---|---|
| A-1 | No UIA (UI Automation) support | High | Implement `IAccessible` or `IRawElementProviderSimple` |
| A-2 | No keyboard focus ring beyond selection | Medium | Implement visible focus ring; make buttons Tab-reachable |
| A-3 | Selection uses only accent color | Medium | Add secondary indicator (thicker outline, checkmark) |
| A-4 | High-DPI support needs testing at 250%+ | Low | Verify button sizes and scrollbar usability at extreme scales |

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

| ID | Finding | Location | Recommendation |
|---|---|---|---|
| E-1 | Drag-drop only accepts first file/folder | `main.c:580-591` | Accept image files dropped directly; multi-folder support |
| E-2 | Window ghosting after `app_load_folder` | `main.c:742` | Call `DisableProcessWindowsGhosting()` or defer loading |
| E-3 | No re-entrant `app_load_folder` protection | `app.c:86-154` | Add `loading` flag when scan becomes async |

---

## Summary Count

| Severity | Count |
|---|---|
| **Critical** | 4 |
| **High** | 5 |
| **Medium** | 9 |
| **Low** | 8 |
| **Accessibility** | 4 |
| **Edge Cases** | 3 |
| **Total** | **33 findings** |
