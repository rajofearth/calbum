#pragma once
// =========================================================================
// types.h — Single header with ALL shared types, inline utilities, and
// cross-file function declarations.
//
// In a unity build this is the only header needed — every .c file is
// #included directly into build.c in dependency order, so visibility is
// controlled by inclusion order, not by header files.
// =========================================================================
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

// Ensure 'interface' is defined for COM headers (required by some language servers)
#ifndef interface
#define interface struct
#endif

#include <shlobj.h>
#include <d3d11.h>
#include <wincodec.h>
#include <stdint.h>
#include <wchar.h>

// -------------------------------------------------------------------------
// Constants
// -------------------------------------------------------------------------
#define MAX_PATH_LEN 1024

// Custom window messages for inter-thread communication
#define WM_CALBUM_LOAD_COMPLETE (WM_APP + 1)
#define WM_CALBUM_FILE_CHANGE (WM_APP + 2)
#define THUMB_SIZE 160
#define THUMB_PADDING 8
#define GALLERY_PADDING 16
#define DEFAULT_CAPACITY 256
#define MAX_INSTANCES 4096
#define NUM_WORKERS 2
#define RING_CAPACITY 4096
#define ARENA_CAPACITY (16ULL * 1024 * 1024) // 16 MB
#define SMOOTH_SCROLL_SPEED 12.0f
#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif
#ifndef APP_VERSION_W
#define APP_VERSION_W L"0.1.0"
#endif

#define MIN_WINDOW_WIDTH 400
#define MIN_WINDOW_HEIGHT 300

// -------------------------------------------------------------------------
// Enums
// -------------------------------------------------------------------------
typedef enum
{
    VIEW_GALLERY,
    VIEW_FULLIMAGE
} ViewMode;

typedef enum
{
    ALIGN_X_LEFT = 0,
    ALIGN_X_RIGHT = 1,
    ALIGN_X_CENTER = 2,
    ALIGN_X_JUSTIFIED = 3
} TextAlignmentX;

typedef enum
{
    ALIGN_Y_TOP = 0,
    ALIGN_Y_BOTTOM = 1,
    ALIGN_Y_CENTER = 2
} TextAlignmentY;

typedef enum
{
    SORT_DATE_CREATED,
    SORT_DATE_MODIFIED,
    SORT_SIZE
} SortMode;

typedef enum
{
    TOKEN_DEFAULT = -1,
    TOKEN_BORDER = -2,
    TOKEN_PANEL = -3,
    TOKEN_SCROLLBAR = -4,
    TOKEN_FULL_IMAGE = -5,
    TOKEN_DROP_SHADOW = -6,
    TOKEN_ACCENT = -7,
    TOKEN_SELECTION_OUTLINE = -8,
    TOKEN_BLUR = -9
} RenderToken;

// -------------------------------------------------------------------------
// Layout Structures
// -------------------------------------------------------------------------
typedef struct
{
    int cols;
    int pad;
    int grid_width;
    int left_margin;
    int scroll_int;
    int first_row;
    int last_row;
    int first_visible;
    int last_visible;
} GridLayout;

// -------------------------------------------------------------------------
// Arena Bump Allocator — zero-fragmentation, O(1) alloc/free
// -------------------------------------------------------------------------
typedef struct
{
    unsigned char *buf;
    size_t capacity;
    size_t offset;
} Arena;

static inline void arena_init(Arena *a, void *buf, size_t cap)
{
    a->buf = (unsigned char *) buf;
    a->capacity = cap;
    a->offset = 0;
}

static inline void *arena_alloc(Arena *a, size_t size)
{
    size_t align = 16;
    size_t mask = align - 1;
    size_t off = (a->offset + mask) & ~mask;
    if (off + size > a->capacity)
        return NULL;
    a->offset = off + size;
    return memset(a->buf + off, 0, size);
}

static inline void arena_reset(Arena *a)
{
    a->offset = 0;
}

#define arena_alloc_type(a, T) (T *) arena_alloc((a), sizeof(T))
#define arena_alloc_array(a, T, n) (T *) arena_alloc((a), sizeof(T) * (n))

// -------------------------------------------------------------------------
// Ring Buffer — bounded SPSC queue with event-based blocking pop
// -------------------------------------------------------------------------
typedef struct
{
    void **slots;
    int capacity;
    int head;
    int tail;
    CRITICAL_SECTION lock;
    HANDLE nonempty; // auto-reset event
} RingBuffer;

static inline void rb_init(RingBuffer *rb, void *storage, int cap)
{
    rb->slots = (void **) storage;
    rb->capacity = cap;
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
    if (next != rb->head)
    {
        rb->slots[rb->tail] = item;
        rb->tail = next;
        ok = 1;
    }
    LeaveCriticalSection(&rb->lock);
    if (ok != 0)
        SetEvent(rb->nonempty);
    return ok;
}

static inline void *rb_try_pop(RingBuffer *rb)
{
    void *item = NULL;
    EnterCriticalSection(&rb->lock);
    if (rb->head != rb->tail)
    {
        item = rb->slots[rb->head];
        rb->head = (rb->head + 1) % rb->capacity;
    }
    LeaveCriticalSection(&rb->lock);
    return item;
}

static inline void *rb_wait_pop(RingBuffer *rb, DWORD timeout_ms)
{
    for (;;)
    {
        void *item = rb_try_pop(rb);
        if (item != NULL)
            return item;
        if (WaitForSingleObject(rb->nonempty, timeout_ms) != WAIT_OBJECT_0)
            return NULL;
    }
}

// -------------------------------------------------------------------------
// Math helpers
// -------------------------------------------------------------------------
static inline float lerpf(float a, float b, float t)
{
    if (t <= 0.0F)
        return a;
    if (t >= 1.0F)
        return b;
    return a + ((b - a) * t);
}
static inline float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}
static inline int clamp_int(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}
static inline int ceil_div(int a, int b)
{
    return (a + b - 1) / b;
}
// Frame-rate-independent ease-out factor: 1 - 1/(1 + x + 0.5x²)
static inline float ease_out_factor(float speed, float dt)
{
    float x = speed * dt;
    return (x < 0.01F) ? x : (1.0F - (1.0F / (1.0F + x + (0.5F * x * x))));
}

// -------------------------------------------------------------------------
// Core Data Structures
// -------------------------------------------------------------------------

typedef enum
{
    IMG_STATE_NEW = 0,
    IMG_STATE_LOADING = 1,
    IMG_STATE_READY = 2,
    IMG_STATE_RESIDENT_GPU = 3,
    IMG_STATE_FAILED = 4
} ImageState;

typedef enum
{
    ITEM_FOLDER,
    ITEM_IMAGE
} GridItemType;

typedef struct
{
    uint8_t type;
    int image_index;            // Index into s->images if ITEM_IMAGE
    const wchar_t *folder_name; // Allocated in nav_arena if ITEM_FOLDER
    const wchar_t *folder_path; // Allocated in nav_arena if ITEM_FOLDER
    int image_count;
    int folder_count;
} GridItem;

// A single image entry — packed into a flat, cache-friendly array.
typedef struct
{
    wchar_t *path;
    wchar_t *filename;
    uint64_t file_size;
    uint64_t last_modified;
    uint64_t created_time;
    uint16_t full_width;
    uint16_t full_height;
    int16_t texture_slot; // Index into GPU texture pool, -1 if none
    uint8_t state;
    uint8_t thumb_requested;
} ImageEntry;

// Work item for background thumbnail loading
typedef struct
{
    wchar_t path[MAX_PATH_LEN];
    int thumb_size;
    HWND target_hwnd;
} LoadRequest;

// Result posted back to main thread on thumbnail load completion
typedef struct
{
    wchar_t path[MAX_PATH_LEN];
    void *bc1_data;
    int bc1_size;
    int succeeded;
} LoadResult;

// File change notification from monitor thread
typedef struct
{
    enum
    {
        CHANGE_ADDED,
        CHANGE_REMOVED,
        CHANGE_MODIFIED
    } type;
    wchar_t path[MAX_PATH_LEN];
    wchar_t filename[MAX_PATH_LEN];
} FileChange;

// ─────────────────────────────────────────────────────────────────────
// Application State — single global struct, zero indirection
// ─────────────────────────────────────────────────────────────────────

#define MAX_GPU_TEXTURES 1024

typedef struct
{
    float bg[4];
    float panel[4];
    float border[4];
    float text_main[4];
    float text_muted[4];
    float accent[4];
    float scrollbar[4];
} Theme;

typedef struct
{
    float grid_gap;
    float panel_padding;
    float thumb_radius;
    float card_radius;
    float button_height;
    float topbar_height;
    float scrollbar_w;
} ScaledLayout;

typedef struct
{
    ID3D11Texture2D *texture_array;
    ID3D11ShaderResourceView *texture_array_srv;
    int last_used[MAX_GPU_TEXTURES];
    int frame_counter;
} GPUTexturePool;

typedef struct
{
    ID3D11Texture2D *texture;
    ID3D11ShaderResourceView *srv;
    wchar_t path[MAX_PATH_LEN];
    int w;
    int h;
} FullImageSlot;

typedef struct AppState
{
    // View mode
    ViewMode view_mode;
    int selected_index;
    SortMode sort_mode;
    int sort_descending;

    // Smooth scrolling (target vs visual)
    float scroll_target_y;
    float scroll_current_y;

    // Window
    int window_width;
    int window_height;
    int needs_redraw;
    HWND hwnd;

    // UI & Layout
    Theme theme;
    ScaledLayout layout;
    float dpi_scale;
    float scrollbar_opacity;
    float scrollbar_fade_timer;
    float scrollbar_hover_t;

    // D3D11 Resources
    ID3D11Device *d3d_device;
    ID3D11DeviceContext *d3d_context;
    IDXGISwapChain *swap_chain;
    ID3D11RenderTargetView *rtv;

    // D2D1 / DirectWrite Resources
    struct ID2D1Factory *d2d_factory;
    struct ID2D1RenderTarget *d2d_rtv;
    struct IDWriteFactory *dwrite_factory;
    struct IDWriteTextFormat *dwrite_format;
    struct IDWriteTextFormat *dwrite_format_semibold;
    struct IDWriteTextFormat *dwrite_format_regular;
    struct IDWriteTextFormat *dwrite_format_small;
    struct IDWriteTextFormat *dwrite_format_mono;
    struct IDWriteTextFormat *dwrite_format_mono_small;
    struct IDWriteTextFormat *dwrite_format_small_semibold;
    struct IDWriteTextFormat *dwrite_format_icons;
    struct IDWriteTextFormat *dwrite_format_icons_large;
    struct ID2D1SolidColorBrush *d2d_brush;

    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *input_layout;
    ID3D11Buffer *instance_buffer;
    ID3D11SamplerState *sampler;
    ID3D11BlendState *blend_state;
    ID3D11Buffer *constant_buffer;
    ID3D11Buffer *theme_buffer;
    ID3D11Texture2D *blur_tex;
    ID3D11ShaderResourceView *blur_srv;
    ID3D11Texture2D *back_buffer;

    GPUTexturePool tex_pool;

    // Flat array of images (arena-allocated)
    ImageEntry *images;
    int count;
    int capacity;
    Arena arena;

    // Frame timing via QueryPerformanceCounter
    double delta_time;
    int64_t perf_counter_freq;
    int64_t last_tick;

    // Interactive State
    int is_dragging_scrollbar;
    float drag_start_y;
    float drag_start_scroll_y;
    int sort_menu_open;
    int info_open;

    // File monitor thread
    HANDLE monitor_thread;
    HANDLE monitor_stop_event;
    HANDLE dir_handle;
    int monitoring_active;

    // Asset worker thread pool
    HANDLE worker_threads[NUM_WORKERS];
    HANDLE worker_stop_event;
    RingBuffer work_queue;
    void *ring_slots[RING_CAPACITY];

    // Current directory
    wchar_t current_dir[MAX_PATH_LEN];

    // Folder navigation state
    wchar_t viewing_dir[MAX_PATH_LEN];
    GridItem *grid_items;
    int grid_item_count;
    int grid_item_capacity;
    int *strip_image_grid_indices;
    int strip_image_count;
    Arena nav_arena;

#define FULL_CACHE_SIZE 32

    // High-Resolution Cache Pool & preloading Pipeline
    FullImageSlot full_slots[FULL_CACHE_SIZE];
    ID3D11ShaderResourceView *active_full_srv;
    double full_load_timer;
    float zoom_level;
    float zoom_ui_timer;

    // Centered Zoom Panning
    float zoom_pan_x;
    float zoom_pan_y;
    int is_panning;
    float pan_start_x;
    float pan_start_y;
    float pan_orig_x;
    float pan_orig_y;
} AppState;

// ─────────────────────────────────────────────────────────────────────
// Cross-file function declarations
// Every non-static function in any .c file must be declared here.
// ─────────────────────────────────────────────────────────────────────

// utils.c
void format_size(uint64_t bytes, wchar_t *buf, int len);
void format_filetime(uint64_t filetime, wchar_t *buf, int len);

// layout.c
void gal_calc_layout(AppState *s, GridLayout *out);
int gal_max_scroll(AppState *s);

// app.c
void app_init(AppState *s);
void app_shutdown(AppState *s);
void app_load_folder(AppState *s, const wchar_t *path);
void app_update_title(AppState *s);
void get_pictures_folder(wchar_t *buf, int len);
void get_parent_dir(const wchar_t *path, wchar_t *out, int max_len);
ImageEntry *app_append_image_entry(AppState *s, const wchar_t *path, const wchar_t *filename, uint64_t file_size,
                                   uint64_t last_modified, uint64_t created_time);
void app_populate_grid_items(AppState *s);

// file_scanner.c
int fs_scan_directory(const wchar_t *path, AppState *s);
int fs_has_image_extension(const wchar_t *name);

// image_loader.c
int il_init_wic(void);
void il_shutdown_wic(void);
void *il_load_and_compress(const wchar_t *path, int thumb_size, int *out_size);
void il_free_bc1_data(void *data);
int il_get_image_dimensions(const wchar_t *path, int *out_w, int *out_h);
void *il_load_full_image(const wchar_t *path, int *out_w, int *out_h);

// file_monitor.c
int fm_start_monitor(AppState *s, const wchar_t *directory);
void fm_stop_monitor(AppState *s);

// asset_worker.c
int aw_start_workers(AppState *s);
void aw_stop_workers(AppState *s);
int aw_request_thumbnail(AppState *s, const wchar_t *path, int thumb_size, HWND hwnd);
DWORD WINAPI aw_worker_thread(LPVOID param);

// gallery.c
void gal_render_gallery(HDC hdc, AppState *s);
void gal_render_fullimage(HDC hdc, AppState *s);
void gal_clamp_zoom_pan(AppState *s);
int gal_hit_test(AppState *s, int x, int y, int *out_index);
int gal_handle_ui_click(AppState *s, int x, int y);
int gal_handle_fullimage_click(AppState *s, int x, int y);
void gal_apply_sort(AppState *s);
void gal_scroll(AppState *s, float delta);
void gal_update_layout(AppState *s);
void gal_update_layout_scales(AppState *s);
void gal_open_full(AppState *s, int index);
void gal_close_full(AppState *s);
void gal_select_full_image(AppState *s, int index);
void gal_tick_smooth_scroll(AppState *s);
void gal_activate_item(AppState *s, int idx);

// renderer.c
typedef struct
{
    float x;
    float y;
    float w;
    float h;
    int tex_index;
    float opacity;
    float corner_radius;
    float _pad;
} InstanceData;

int r_init(AppState *s);
void r_shutdown(AppState *s);
void r_resize(AppState *s);
void r_clear(AppState *s, float r, float g, float b);
void r_clear_theme(AppState *s);
void r_present(AppState *s);
int r_alloc_texture_slot(AppState *s, int image_index);
void r_upload_texture(AppState *s, int slot, void *bc1_data);
void r_evict_texture(AppState *s, int slot);
void r_draw_instances(AppState *s, void *instances, int count);
void r_copy_backbuffer_for_blur(AppState *s);
void r_draw_text(AppState *s, const wchar_t *text, float x, float y, float w, float h);
void r_draw_text_aligned(AppState *s, const wchar_t *text, float x, float y, float w, float h, int align_x, int align_y,
                         struct IDWriteTextFormat *format, float color[4]);
float r_measure_text_width(AppState *s, const wchar_t *text, struct IDWriteTextFormat *format);
void r_draw_text_ext(AppState *s, const wchar_t *text, float x, float y, float w, float h,
                     struct IDWriteTextFormat *format, float color[4]);
int r_load_full_image(AppState *s, const wchar_t *path);
void r_free_full_image(AppState *s);
FullImageSlot *r_get_full_image_slot(AppState *s, const wchar_t *path);
void r_free_full_image_slot(AppState *s, int i);
int r_alloc_full_image_slot(AppState *s);
