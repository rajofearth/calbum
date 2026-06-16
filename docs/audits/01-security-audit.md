# Security Audit Report: calbum v0.1.0

**Audit Date:** 2026-06-16
**Scope:** All source files in `src/` and `lib/` (unity build, C17, MinGW-w64 gcc)
**Architecture:** Native Windows image gallery, D3D11/D2D/DWrite, background worker threads, ReadDirectoryChangesW file monitoring

---

## Executive Summary

The codebase is well-structured for a single-developer project with careful attention to correctness in most paths. No remotely exploitable vulnerabilities (RCE, arbitrary code execution) were identified from network or file-processing vectors alone. The primary risks are **use-after-free / dangling pointer races** between background threads and the main thread, **integer truncation** in file size handling, and **unchecked divisions** by zero from corrupt image metadata. Several message-queue lifetime management issues could lead to crashes under rapid folder switching or shutdown storms.

**Severity distribution (unresolved):** 0 critical, 1 high, 3 medium, 4 low
**Resolved:** 10 of 18 findings

---

## Critical (0)

None identified.

---

## High

### H-01. Worker-thread COM factory use-after-free via `g_wic_factory`

| | |
|---|---|
| **File** | `lib/image/loader.c` |
| **Lines** | 8–9, 26–27, 38–41 |
| **Severity** | **High** |
| **Status** | **Still Relevant** |

**Description:**
`g_wic_factory` is a global `IWICImagingFactory *` written only during `il_init_wic()` (main thread) and set to `NULL` during `il_shutdown_wic()`. Worker threads call `il_load_and_compress()`, `il_get_image_dimensions()`, and `il_load_full_image()` on the heap pool, all of which dereference `g_wic_factory` without any synchronization. Meanwhile, the main thread calls `il_shutdown_wic()` during `app_load_folder()` (via `aw_stop_workers()`) and during final shutdown. The shutdown sequence in `WinMain` is:

```c
r_shutdown(&g_state);
il_shutdown_wic();              // releases g_wic_factory, sets to NULL
app_shutdown(&g_state);         // joins workers (too late!)
```

Workers are joined AFTER the factory is released. If a worker thread is still inside `il_load_and_compress()` (e.g. blocked on WIC I/O) when `il_shutdown_wic()` runs, the WIC factory is released and the pointer zeroed. The worker thread then continues executing on a released COM object — a use-after-free.

**Remediation:**
Three options, in order of preference:

1. **Extend the shutdown wait.** Increase the 2000 ms timeout to `INFINITE` — worker threads will exit promptly once the stop event is set. The current 2s timeout is a safety measure but creates the UAF window.
2. **Guard `g_wic_factory` with a refcount or SRWLOCK.** Add `InterlockedIncrement/Decrement` refcounting and make the shutdown wait until the refcount drops to 1 (only the main-thread reference).
3. **Ensure `aw_stop_workers` joins fully.** After `WaitForSingleObject` timeout, `TerminateThread` the stragglers — though this is itself dangerous (leaks locks, memory).

**Note:** `aw_stop_workers` and `fm_stop_monitor` now use INFINITE timeouts (see H-03), but the shutdown ordering problem persists: `r_shutdown()` → `il_shutdown_wic()` → `app_shutdown()` (which joins workers). Workers are still joined after the factory is released.

---

### H-02. `PostMessage` heap pointers survive window destruction and partial draining

| | |
|---|---|
| **File** | `src/asset_worker.c`, `src/app.c` |
| **Lines** | 100–108, 92–102 |
| **Severity** | **High** |
| **Status** | **✅ RESOLVED** |

**Description:**
Both `aw_worker_thread` and `fm_thread_proc` allocate heap `LoadResult` / `FileChange` structs and post them to the main thread via `PostMessageW`. This is a standard pattern, but there are two gaps:

1. **`WM_CALBUM_FILE_CHANGE` is never drained.** `app_load_folder()` (line 94) drains only `WM_CALBUM_LOAD_COMPLETE` messages from the queue. After `fm_stop_monitor()` completes, there may still be queued `FileChange` messages whose `LPARAM` points to freed memory — or if the monitor was stopped mid-operation, a `FileChange` may be allocated but never reach the queue.
2. **Window destruction race.** If the window is destroyed (WM_DESTROY → PostQuitMessage), any still-queued messages with heap pointers become orphaned — no handler will ever `free()` them, causing a leak. Conversely, if a handler runs after the window is destroyed (in the brief window before the message loop exits), it writes to `g_state` which may be partially torn down.

**Remediation:**
- Drain `WM_CALBUM_FILE_CHANGE` messages alongside `WM_CALBUM_LOAD_COMPLETE` in `app_load_folder`.
- Add a sentinel flag `g_state.shutting_down` checked at the top of `on_thumb_complete` and `on_file_changed` to skip processing and free the pointer.
- Consider switching to a lock-free SPSC queue for results instead of the Windows message queue, giving explicit lifetime control.

**Resolution:** A `PeekMessage` drain loop was added in `app_load_folder()` (app.c:96-146) covering both `WM_CALBUM_LOAD_COMPLETE` and `WM_CALBUM_FILE_CHANGE` messages. A `shutting_down` flag in `AppState` prevents processing and ensures heap pointers are freed after shutdown begins.

---

### H-03. Thread pool shutdown can leave threads running after handle close

| | |
|---|---|
| **File** | `src/asset_worker.c` |
| **Lines** | 130–153 |
| **Severity** | **High** |
| **Status** | **✅ RESOLVED** |

**Description:**
`aw_stop_workers` sets the stop event, signals `work_queue.nonempty`, then waits up to 2000 ms per thread with `WaitForSingleObject`. If a worker thread does not exit within the timeout (e.g. it is stuck on a slow disk read or a WIC operation on a corrupt file), the main thread proceeds to `CloseHandle(s->worker_stop_event)` and `CloseHandle(s->worker_threads[i])`. The worker thread continues running with stale references:

- Its next check of `s->worker_stop_event` reads freed memory
- It may call `rb_try_pop` on a ring buffer whose CRITICAL_SECTION was not destroyed (that happens in `rb_destroy`, called from `app_shutdown`)
- It may call `il_load_and_compress` which uses `g_wic_factory` (see H-01)

In `app_shutdown()`, the same pattern occurs redundantly (lines 39–59 of `app.c` call `fm_stop_monitor` and `aw_stop_workers` again, but `aw_stop_workers` skips if `s->worker_threads[0]` is NULL — if it timed out, the thread handle was already closed but the thread is still running).

**Remediation:**
- Either raise the timeout to `INFINITE` (preferred — `SetEvent` + `WaitForMultipleObjects` on all threads at once with `INFINITE`), or
- Add a backstop: after the timeout, call `TerminateThread` on remaining threads (with extreme caution — best to avoid needing this), or
- Track worker thread state with an atomic counter so the shutdown waits for all workers to acknowledge the stop event.

**Resolution:** Both `aw_stop_workers()` and `fm_stop_monitor()` now use `INFINITE` timeouts with correct handle ordering (`CancelIoEx` → wait for thread → close handle), eliminating the timeout-based race window.

---

## Medium

### M-01. Integer truncation of `GetFileSize` return in thumbnail cache load

| | |
|---|---|
| **File** | `src/asset_worker.c` |
| **Line** | 72 |
| **Severity** | **Medium** |
| **Status** | **Still Relevant** |

**Description:**
```c
bc1_size = (int) GetFileSize(hFile, NULL);
```

`GetFileSize` returns `DWORD` (max `0xFFFFFFFF` = ~4 GB). The cast to `int` truncates values > `INT_MAX` (2,147,483,647), producing a negative `bc1_size`. The guard `if (bc1_size > 0)` then skips the block, so the worst outcome is a cache-miss (falling through to re-encode). However, if a future code path removes the guard, `malloc(bc1_size)` with a negative `int` would widen to `SIZE_MAX` (a huge allocation), likely causing an allocation failure or massive OOM.

**Remediation:**
Use `DWORD` for `bc1_size` (or `size_t`), change the type in `LoadResult` and related function signatures from `int` to `DWORD`/`size_t`, and validate against a reasonable max (e.g. `THUMB_SIZE * THUMB_SIZE / 2` for BC1).

**Note:** Practically harmless for `.bc1` files (~32 KB max) but the pattern remains unchanged.

---

### M-02. Division by zero from corrupt image dimensions in `il_load_and_compress`

| | |
|---|---|
| **File** | `lib/image/loader.c` |
| **Lines** | 61, 67 |
| **Severity** | **Medium** |
| **Status** | **✅ RESOLVED** |

**Description:**
```c
if (w > h)
    th = (h * thumb_size) / w;   // division by w=0
else
    tw = (w * thumb_size) / h;   // division by h=0
```

`IWICBitmapFrameDecode::GetSize()` can legitimately return `w=0, h=0` for a corrupt or empty image frame. No zero check precedes the division. On x64, integer division by zero raises an `EXCEPTION_INT_DIVIDE_BY_ZERO` hardware exception, crashing the worker thread (and taking down the process on MinGW-w64, since SEH exceptions are fatal by default).

**Remediation:**
Add a guard immediately after `GetSize`:
```c
if (w == 0 || h == 0) { /* release frame; return NULL */ }
```

**Resolution:** Zero-dimension guard added in `lib/image/loader.c:68-72` exactly matching the recommended remediation.

---

### M-03. Stack instance buffer overflow risk in gallery renderers

| | |
|---|---|
| **File** | `src/gallery.c`, `src/gallery_fullimage.c` |
| **Lines** | 156, 279–280; 178 |
| **Severity** | **Medium** |
| **Status** | **✅ RESOLVED** |

**Description:**
Both gallery renderers declare a fixed-size `static InstanceData instances[4096]` (163,840 bytes on the stack). `gallery.c` has a safety valve:
```c
if (inst_count >= 4080) break;   // line 279
```
but `gallery_fullimage.c` has **no such guard**. The full-image renderer adds instances for the letterbox bar, back button, info button, zoom badge, bottom strip panel, prev/next arrows, and up to `num_strip_thumbs` thumbnails (each adding 3+ instances). With a sufficiently large number of strip thumbnails (e.g. a very wide window on a high-DPI display), `inst_count` could exceed 4096, overflowing the stack array. The static storage class means corruption persists across calls.

**Remediation:**
Add an overflow guard to `gal_render_fullimage()` similar to gallery.c's check. Better: allocate the instance buffer dynamically with a fallback, or use a tracked capacity check before every `inst_count++`.

**Resolution:** `MAX_INSTANCES` constant defined in `types.h` with bounds guards added in both `gallery.c` and `gallery_fullimage.c`.

---

### M-04. Arena allocator no alignment guarantee beyond 16-byte

| | |
|---|---|
| **File** | `src/types.h` |
| **Lines** | 130–139 |
| **Severity** | **Medium** |
| **Status** | **Still Relevant** |

**Description:**
The arena aligns to 16 bytes. `ImageEntry` contains `uint64_t` fields (8-byte aligned — fine), but `wchar_t *` pointers (8 bytes on x64) need only 8-byte alignment, also fine. The real concern is that the arena starts at an arbitrary `VirtualAlloc` base, which is page-aligned (4K / 64K), so the 16-byte alignment is sufficient for all current types. However, if any type with `__m128` / `__declspec(align(32))` were added, the allocator would silently return an under-aligned pointer, causing a fault on SIMD stores.

**Remediation:**
Document the 16-byte alignment guarantee. If SIMD types are ever needed, make alignment a parameter or use `max_align_t`. Consider adding a compile-time `static_assert` that the arena alignment meets all type requirements.

**Note:** Accepted design constraint — no changes made.

---

### M-05. `app_load_folder` integer overflow in capacity calculation

| | |
|---|---|
| **File** | `src/app.c` |
| **Line** | 133 |
| **Severity** | **Medium** |
| **Status** | **Still Relevant** |

**Description:**
```c
s->grid_item_capacity = (s->count * 2) + 256;
```

`s->count` is `int`. If `s->count` exceeds `(INT_MAX - 256) / 2` (~1,073,741,503), the multiplication wraps to a negative (or small positive) value. In practice, the 16 MB arena and `ImageEntry` size (~48 bytes) limit `s->count` to ~349,525 entries — far below the overflow threshold. However, if the arena size were increased, this becomes a real risk: a negative `grid_item_capacity` is cast to `size_t` for `arena_alloc_array`, becoming a huge positive value, which would immediately exceed the arena capacity and return NULL, causing silent failure.

**Remediation:**
Guard with a saturation check:
```c
if (s->count > (INT_MAX - 256) / 2) { /* handle error */ }
```
Or use `size_t` for counts.

**Note:** Practically unreachable with current arena size but technically unchanged.

---

### M-06. Unvalidated `ReadDirectoryChangesW` filename length

| | |
|---|---|
| **File** | `lib/fs/monitor.c` |
| **Line** | 51 |
| **Severity** | **Medium** |
| **Status** | **✅ RESOLVED** |

**Description:**
```c
wcsncpy(fname, fni->FileName, min(fni->FileNameLength / sizeof(wchar_t), MAX_PATH_LEN - 1));
```

`fni->FileNameLength` is in bytes. The division by `sizeof(wchar_t)` truncates if the byte count is not a multiple of 2. The NT kernel guarantees that `FileNameLength` is a multiple of `sizeof(wchar_t)` for well-formed notifications, but a malicious or buggy filesystem filter driver could produce irregular values. Also, `FileNameLength` can be zero (rename with empty new name? unlikely but theoretically possible via FS filter). The buffer `fname` is zero-initialized on the previous line, so an empty copy is safe. But `fni->FileName` is NOT null-terminated — `wcsncpy` copies exactly `n` characters without appending a null if the source has length >= n. The `min` expression caps at `MAX_PATH_LEN - 1`, and `fname` was initialized to `{0}`, so it is always null-terminated at position `min(...)`. This is correct but fragile.

**Remediation:**
Add an explicit null-termination after the copy (already implicit from `{0}` init, but making it explicit after `wcsncpy` would improve readability and harden against future edits):
```c
fname[MAX_PATH_LEN - 1] = L'\0';
```

**Resolution:** Explicit null-termination added in `lib/fs/monitor.c` after the `wcsncpy` call, matching the recommended remediation.

---

### M-07. `file_monitor.c` handle close races with `ReadDirectoryChangesW`

| | |
|---|---|
| **File** | `lib/fs/monitor.c` |
| **Lines** | 117–151 |
| **Severity** | **Medium** |
| **Status** | **✅ RESOLVED** |

**Description:**
`fm_stop_monitor` issues `CancelIoEx(s->dir_handle, NULL)`, then immediately `CloseHandle(s->dir_handle)`, then waits for the thread to exit. After `CancelIoEx` returns, `ReadDirectoryChangesW` may still be in the process of completing. The subsequent `CloseHandle` could abort the pending operation in the kernel, but the monitor thread may wake from an error return from `ReadDirectoryChangesW` after `dir_handle` is already closed. However, in this code, the thread just calls `Sleep(100)` and loops back to check `monitor_stop_event`, which is set. So the thread exits safely. The real issue is that the close-during-I/O pattern is technically undefined per the Windows documentation for some driver stacks.

**Remediation:**
Close `dir_handle` only *after* the monitor thread has exited (swap the order: wait for thread first, then close handle). However, the thread may be stuck *in* `ReadDirectoryChangesW`, so you need `CancelIoEx` to unblock it first. Correct ordering:
1. `SetEvent(monitor_stop_event)`
2. `CancelIoEx(dir_handle, NULL)`
3. `WaitForSingleObject(monitor_thread, timeout)`
4. `CloseHandle(dir_handle)`
5. `CloseHandle(monitor_thread)`
6. `CloseHandle(monitor_stop_event)`

**Resolution:** Correct shutdown ordering implemented in `lib/fs/monitor.c`: `CancelIoEx` → `WaitForSingleObject` on monitor thread → `CloseHandle(dir_handle)`, matching the recommended ordering.

---

## Low

### L-01. Window title exposes full directory path

| | |
|---|---|
| **File** | `src/app.c` |
| **Line** | 156–159 |
| **Severity** | **Low** |
| **Status** | **✅ RESOLVED** |

**Description:**
The window title contains the full current directory path (e.g. `C:\Users\Alice\Pictures\Vacation\...`). This information is readable by any process that can enumerate windows (`EnumWindows` / `GetWindowText`), leaking the user's browsing history and folder hierarchy.

**Remediation:**
Truncate the path in the title to just the last 2–3 components, or show only the leaf folder name.

**Resolution:** `app_update_title` now uses `wcsrchr` to extract and display only the leaf folder name in the window title.

---

### L-02. Drag-drop path truncation at `MAX_PATH_LEN`

| | |
|---|---|
| **File** | `src/main.c` |
| **Lines** | 582–583 |
| **Severity** | **Low** |
| **Status** | **Still Relevant** |

**Description:**
Paths longer than 259 characters (`MAX_PATH_LEN - 1`) are silently truncated, potentially causing `app_load_folder` to load a wrong or non-existent path.

**Remediation:**
Use `DragQueryFileW` first with `cch=0` to get required length, then dynamically allocate. Or strip the `\\?\` prefix and check for truncation.

---

### L-03. FNV-1a hash collision in cache file naming

| | |
|---|---|
| **File** | `src/asset_worker.c` |
| **Line** | 66 |
| **Severity** | **Low** |
| **Status** | **Still Relevant** |

**Description:**
The 64-bit FNV-1a hash is not collision-resistant. Two different image file paths could produce the same cache file, causing one image to display the other's thumbnail.

**Remediation:**
Acceptable risk for the use case. Document that cache collisions are theoretically possible. If desired, add a full path comparison when loading from cache and recompute on mismatch.

---

### L-04. `wsprintfW` no length limit in `app_update_title`

| | |
|---|---|
| **File** | `src/app.c` |
| **Line** | 158 |
| **Severity** | **Low** |
| **Status** | **✅ RESOLVED** |

**Description:**
`wsprintfW` is used instead of the safer `swprintf` with explicit buffer size.

**Remediation:**
Replace with `swprintf(title, sizeof(title)/sizeof(wchar_t), ...)`.

**Resolution:** Replaced `wsprintfW` with `swprintf` using explicit buffer size in `app_update_title`.

---

### L-05. `g_cache_dir` unsynchronized write race on first access

| | |
|---|---|
| **File** | `src/asset_worker.c` |
| **Lines** | 8, 21–35 |
| **Severity** | **Low** |
| **Status** | **Still Relevant** |

**Description:**
Multiple worker threads can race on `ensure_cache_dir()` checking `g_cache_dir[0] == 0`. The worst outcome is redundant `CreateDirectoryW` calls (which succeed harmlessly).

**Remediation:**
Initialize `g_cache_dir` once during `aw_start_workers` on the main thread, or use `InitOnceExecuteOnce`.

---

### L-06. Fragile wcsncpy chaining pattern in `file_scanner.c`

| | |
|---|---|
| **File** | `lib/fs/scanner.c` |
| **Line** | 41 |
| **Severity** | **Low** |
| **Status** | **✅ RESOLVED** |

**Description:**
```c
wcsncpy(search, dir, MAX_PATH_LEN - 3)[MAX_PATH_LEN - 3] = L'\0';
```
This is correct but unusual. A future editor might fail to understand the chaining and break the null-termination.

**Remediation:**
Use a more readable two-line pattern.

**Resolution:** The `wcsncpy` chaining was broken into separate statements for readability.

---

### L-07. `r_evict_texture` writes `TOKEN_DEFAULT` (0) as texture slot instead of -1

| | |
|---|---|
| **File** | `lib/gpu/texture.c` |
| **Line** | * |
| **Severity** | **Low** |
| **Status** | **✅ RESOLVED** |

**Description:**
`TOKEN_DEFAULT` is 0, but the "no texture" sentinel used elsewhere is -1. Any code that checks `texture_slot >= 0` to determine if an image has a resident texture would spuriously think slot 0 is active.

**Remediation:**
Use `-1` as the sentinel, or ensure all consumers of `texture_slot` treat 0 as "not resident" consistently.

**Resolution:** `r_evict_texture` now writes `-1` instead of `TOKEN_DEFAULT` (0) as the "no texture" sentinel.

---

### L-08. `CreateSolidColorBrush` return not checked after `EndDraw`

| | |
|---|---|
| **File** | `lib/gpu/d2d.c` |
| **Lines** | * |
| **Severity** | **Low** |
| **Status** | **Still Relevant** |

**Description:**
In `r_draw_text_ext` and `r_draw_text_aligned`, a brush is created, used in BeginDraw/EndDraw, and released. If `CreateSolidColorBrush` fails (e.g. device lost), the function returns early, but if `EndDraw` fails, the D2D render target enters an error state.

**Remediation:**
Check the `HRESULT` from `EndDraw` and handle device loss (recreate render target) or at least log it.

**Note:** The relevant D2D brush allocation path now lives in `r_resize()` — the HRESULT is still unchecked.

---

## Cross-Cutting Observations

### Observation 1: Global state makes testing and isolation hard
`AppState g_state` is a file-static global in `main.c`. Every subsystem reads/writes it directly. This eliminates the possibility of unit testing workers or the renderer in isolation. All threading issues above stem from this shared mutable state.

**Status:** Still applies — no architectural changes made to `g_state`.

### Observation 2: COM refcounting audit — mostly correct
The D3D11/D2D/DWrite COM interfaces follow correct AddRef/Release patterns overall.

**Status:** Still applies — no COM refcounting changes made.

### Observation 3: No validation of image metadata bounds
`il_load_full_image` limits dimensions to 2048px, but `il_load_and_compress` does not limit the source image dimensions before scaling. A belt-and-suspenders clamp on `tw`/`th` before allocating the 16 MB decode buffer would be prudent.

**Status:** Still applies — no additional metadata bounds validation added.

### Observation 4: No cryptographic signing or integrity checks
Cache files (`*.bc1`) are written to disk without any integrity hash. A malicious process that writes to the cache directory could inject crafted BC1 data.

**Status:** Still applies — no integrity checks added.

---

## Summary Table

| ID | Category | File:Line | Severity | Status |
|---|---|---|---|---|
| H-01 | Use-after-free | `lib/image/loader.c:8-9,26-27,38-41` | **High** | Still Relevant |
| H-02 | Message queue lifetime | `src/asset_worker.c:100-108`, `src/app.c:92-102` | **High** | ✅ RESOLVED |
| H-03 | Thread shutdown race | `src/asset_worker.c:130-153` | **High** | ✅ RESOLVED |
| M-01 | Integer truncation | `src/asset_worker.c:72` | Medium | Still Relevant |
| M-02 | Division by zero | `lib/image/loader.c:61,67` | Medium | ✅ RESOLVED |
| M-03 | Stack buffer overflow | `src/gallery_fullimage.c:178` | Medium | ✅ RESOLVED |
| M-04 | Arena alignment | `src/types.h:130-139` | Medium | Still Relevant |
| M-05 | Integer overflow | `src/app.c:133` | Medium | Still Relevant |
| M-06 | Unvalidated filename length | `lib/fs/monitor.c:51` | Medium | ✅ RESOLVED |
| M-07 | Handle close race | `lib/fs/monitor.c:117-151` | Medium | ✅ RESOLVED |
| L-01 | Information disclosure | `src/app.c:156-159` | Low | ✅ RESOLVED |
| L-02 | Path truncation | `src/main.c:582-583` | Low | Still Relevant |
| L-03 | Hash collision | `src/asset_worker.c:66` | Low | Still Relevant |
| L-04 | Unbounded wsprintfW | `src/app.c:158` | Low | ✅ RESOLVED |
| L-05 | Data race | `src/asset_worker.c:8,21-35` | Low | Still Relevant |
| L-06 | Fragile code pattern | `lib/fs/scanner.c:41` | Low | ✅ RESOLVED |
| L-07 | Sentinel confusion | `lib/gpu/texture.c:*` | Low | ✅ RESOLVED |
| L-08 | Unchecked HRESULT | `lib/gpu/d2d.c:*` | Low | Still Relevant |
