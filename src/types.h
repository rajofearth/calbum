#pragma once
// =========================================================================
// types.h — Single header with ALL shared types, inline utilities, and
// cross-file function declarations.
//
// In a unity build this is the only header needed — every .c file is
// #included directly into build.c in dependency order, so visibility is
// controlled by inclusion order, not by header files.
// =========================================================================
#include <windows.h>
#include <stdint.h>

// -------------------------------------------------------------------------
// Constants
// -------------------------------------------------------------------------
#define MAX_PATH_LEN     1024
#define THUMB_SIZE       160
#define THUMB_PADDING    8
#define GALLERY_PADDING  16
#define DEFAULT_CAPACITY 256
#define NUM_WORKERS      2
#define RING_CAPACITY    4096
#define ARENA_CAPACITY   (16ULL * 1024 * 1024)  // 16 MB
#define SMOOTH_SCROLL_SPEED  12.0f
#define MIN_WINDOW_WIDTH     400
#define MIN_WINDOW_HEIGHT    300

// -------------------------------------------------------------------------
// Enums
// -------------------------------------------------------------------------
typedef enum { VIEW_GALLERY, VIEW_FULLIMAGE } ViewMode;

// -------------------------------------------------------------------------
// Arena Bump Allocator — zero-fragmentation, O(1) alloc/free
// -------------------------------------------------------------------------
typedef struct {
    unsigned char *buf;
    size_t         capacity;
    size_t         offset;
} Arena;

static inline void arena_init(Arena *a, void *buf, size_t cap)
{
    a->buf = (unsigned char *)buf; a->capacity = cap; a->offset = 0;
}

static inline void *arena_alloc(Arena *a, size_t size)
{
    size_t align = 16, mask = align - 1, off = (a->offset + mask) & ~mask;
    if (off + size > a->capacity) return NULL;
    a->offset = off + size;
    return memset(a->buf + off, 0, size);
}

static inline void arena_reset(Arena *a) { a->offset = 0; }

#define arena_alloc_type(a, T)     (T *)arena_alloc((a), sizeof(T))
#define arena_alloc_array(a, T, n) (T *)arena_alloc((a), sizeof(T) * (n))

// -------------------------------------------------------------------------
// Ring Buffer — bounded SPSC queue with event-based blocking pop
// -------------------------------------------------------------------------
typedef struct {
    void   **slots;
    int      capacity;
    int      head;
    int      tail;
    CRITICAL_SECTION lock;
    HANDLE   nonempty;     // auto-reset event
} RingBuffer;

static inline void rb_init(RingBuffer *rb, void *storage, int cap)
{
    rb->slots = (void **)storage; rb->capacity = cap;
    rb->head = rb->tail = 0;
    InitializeCriticalSection(&rb->lock);
    rb->nonempty = CreateEventW(NULL, FALSE, FALSE, NULL);
}

static inline void rb_destroy(RingBuffer *rb)
{
    CloseHandle(rb->nonempty);
    DeleteCriticalSection(&rb->lock);
}

static inline int rb_push(RingBuffer *rb, void *item)
{
    int ok = 0;
    EnterCriticalSection(&rb->lock);
    int next = (rb->tail + 1) % rb->capacity;
    if (next != rb->head) { rb->slots[rb->tail] = item; rb->tail = next; ok = 1; }
    LeaveCriticalSection(&rb->lock);
    if (ok) SetEvent(rb->nonempty);
    return ok;
}

static inline void *rb_try_pop(RingBuffer *rb)
{
    void *item = NULL;
    EnterCriticalSection(&rb->lock);
    if (rb->head != rb->tail) {
        item = rb->slots[rb->head];
        rb->head = (rb->head + 1) % rb->capacity;
    }
    LeaveCriticalSection(&rb->lock);
    return item;
}

static inline void *rb_wait_pop(RingBuffer *rb, DWORD timeout_ms)
{
    for (;;) {
        void *item = rb_try_pop(rb);
        if (item) return item;
        if (WaitForSingleObject(rb->nonempty, timeout_ms) != WAIT_OBJECT_0)
            return NULL;
    }
}

// -------------------------------------------------------------------------
// Math helpers
// -------------------------------------------------------------------------
static inline float lerpf(float a, float b, float t)
{
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    return a + (b - a) * t;
}
static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
static inline int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
static inline int ceil_div(int a, int b) { return (a + b - 1) / b; }
// Frame-rate-independent ease-out factor: 1 - 1/(1 + x + 0.5x²)
static inline float ease_out_factor(float speed, float dt) {
    float x = speed * dt;
    return (x < 0.01f) ? x : (1.0f - 1.0f / (1.0f + x + 0.5f * x * x));
}

// -------------------------------------------------------------------------
// Core Data Structures
// -------------------------------------------------------------------------

// A single image entry — packed into a flat, cache-friendly array.
typedef struct {
    wchar_t path[MAX_PATH_LEN];
    wchar_t filename[MAX_PATH_LEN];
    HBITMAP thumbnail;
    HBITMAP full_image;
    int     full_width;
    int     full_height;
    int     loaded_thumb;
    int     loaded_full;
} ImageEntry;

// Work item for background thumbnail loading
typedef struct {
    int  image_index;
    int  thumb_size;
    HWND target_hwnd;
} LoadRequest;

// Result posted back to main thread on thumbnail load completion
typedef struct {
    int      image_index;
    HBITMAP  bitmap;
    int      succeeded;
} LoadResult;

// File change notification from monitor thread
typedef struct {
    enum { CHANGE_ADDED, CHANGE_REMOVED, CHANGE_MODIFIED } type;
    wchar_t path[MAX_PATH_LEN];
    wchar_t filename[MAX_PATH_LEN];
} FileChange;

// ─────────────────────────────────────────────────────────────────────
// Application State — single global struct, zero indirection
// ─────────────────────────────────────────────────────────────────────
typedef struct AppState {
    // View mode
    ViewMode    view_mode;
    int         selected_index;

    // Smooth scrolling (target vs visual)
    float       scroll_target_y;
    float       scroll_current_y;

    // Window
    int         window_width;
    int         window_height;
    int         needs_redraw;
    HWND        hwnd;

    // Flat array of images (arena-allocated)
    ImageEntry *images;
    int         count;
    int         capacity;
    Arena       arena;

    // Frame timing via QueryPerformanceCounter
    double      delta_time;
    int64_t     perf_counter_freq;
    int64_t     last_tick;

    // File monitor thread
    HANDLE      monitor_thread;
    HANDLE      monitor_stop_event;
    HANDLE      dir_handle;
    int         monitoring_active;

    // Asset worker thread pool
    HANDLE      worker_threads[NUM_WORKERS];
    HANDLE      worker_stop_event;
    CRITICAL_SECTION work_lock;
    void       *ring_slots[RING_CAPACITY];
    int         ring_head;
    int         ring_tail;
    HANDLE      ring_nonempty;

    // Current directory
    wchar_t     current_dir[MAX_PATH_LEN];
} AppState;

// ─────────────────────────────────────────────────────────────────────
// Cross-file function declarations
// Every non-static function in any .c file must be declared here.
// ─────────────────────────────────────────────────────────────────────

// app.c
void  app_init(AppState *s);
void  app_shutdown(AppState *s);
void  app_load_folder(AppState *s, const wchar_t *path);
void  app_update_title(AppState *s);
void  get_pictures_folder(wchar_t *buf, int len);

// file_scanner.c
int   fs_scan_directory(const wchar_t *path, AppState *s);
int   fs_has_image_extension(const wchar_t *name);

// image_loader.c
HBITMAP il_load_thumbnail(const wchar_t *path, int thumb_size);
HBITMAP il_load_full(const wchar_t *path, int *out_w, int *out_h);
void    il_free_bitmap(HBITMAP bmp);
HBITMAP il_create_hbitmap(unsigned char *data, int width, int height, int channels);

// file_monitor.c
int   fm_start_monitor(AppState *s, const wchar_t *directory);
void  fm_stop_monitor(AppState *s);
DWORD WINAPI fm_monitor_thread(LPVOID param);

// asset_worker.c
int   aw_start_workers(AppState *s);
void  aw_stop_workers(AppState *s);
int   aw_request_thumbnail(AppState *s, int image_index, int thumb_size, HWND hwnd);
DWORD WINAPI aw_worker_thread(LPVOID param);

// gallery.c
int   gal_get_columns(AppState *s);
void  gal_render_gallery(HDC hdc, AppState *s);
void  gal_render_fullimage(HDC hdc, AppState *s);
int   gal_hit_test(AppState *s, int x, int y, int *out_index);
void  gal_scroll(AppState *s, float delta);
void  gal_update_layout(AppState *s);
void  gal_open_full(AppState *s, int index);
void  gal_close_full(AppState *s);
void  gal_tick_smooth_scroll(AppState *s);

// renderer.c
void r_clear(HDC hdc, int w, int h, COLORREF color);
void r_fill_rect(HDC hdc, int x, int y, int w, int h, COLORREF color);
void r_round_rect(HDC hdc, int x, int y, int w, int h, int r, COLORREF color);
void r_draw_bitmap(HDC dst, HDC src_dc, HBITMAP bmp, int x, int y);
void r_draw_text(HDC hdc, const wchar_t *text, int x, int y, int w, int h,
                 COLORREF color, int font_size, int flags);
void r_stretch_bitmap(HDC dst, int dx, int dy, int dw, int dh,
                      HDC src_dc, HBITMAP bmp, int sw, int sh);
