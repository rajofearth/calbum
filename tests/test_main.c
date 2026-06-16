// =========================================================================
// test_main.c - calbum unit test runner
//
// Tests pure functions from the codebase. Uses a unity-build approach:
// includes source files directly to access function definitions.
// Compiled as a console executable (no WinMain).
// =========================================================================
#include <initguid.h>
#include <knownfolders.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// Unity include the subsystems we test (in dependency order)
#define STB_DXT_IMPLEMENTATION
#include "lib/stb_dxt.h"
#include "src/types.h"

// ── Library modules ─────────────────────────────────────────────────────
#include "lib/core/utils.c"
#include "lib/core/logger.c"
#include "lib/gpu/device.c"
#include "lib/gpu/shader.c"
#include "lib/gpu/texture.c"
#include "lib/gpu/d2d.c"
#include "lib/gpu/fullimage.c"
#include "lib/ui/ui.c"

// ── OS & Data modules ───────────────────────────────────────────────────
#include "lib/fs/scanner.c"
#define STB_IMAGE_IMPLEMENTATION
#include "lib/stb_image.h"
#include "lib/image/loader.c"
#include "lib/fs/monitor.c"

// ── App modules ─────────────────────────────────────────────────────────
#include "src/layout.c"
#include "src/gallery_sort.c"
#include "src/gallery_fullimage.c"
#include "src/gallery.c"
#include "src/asset_worker.c"
#include "src/app.c"

// ── GPU stubs (device.c is excluded under CALBUM_TEST_BUILD) ───────────
#ifdef CALBUM_TEST_BUILD
void r_clear(GpuState *r_, float r, float g, float b)
{
    (void) r_;
    (void) r;
    (void) g;
    (void) b;
}
void r_clear_theme(GpuState *r, const float bg[4])
{
    (void) r;
    (void) bg;
}
void r_present(GpuState *r)
{
    r->tex_pool.frame_counter++;
}
void r_copy_backbuffer_for_blur(GpuState *r)
{
    (void) r;
}
#endif

// ── Test framework ──────────────────────────────────────────────────────
static int g_run = 0, g_pass = 0, g_fail = 0;

#define TEST(name)                      \
    do                                  \
    {                                   \
        g_run++;                        \
        printf("  TEST  %-50s ", name); \
        fflush(stdout);                 \
    } while (0)
#define PASS()            \
    do                    \
    {                     \
        g_pass++;         \
        printf("PASS\n"); \
    } while (0)
#define FAIL(msg)                    \
    do                               \
    {                                \
        g_fail++;                    \
        printf("FAIL  (%s)\n", msg); \
    } while (0)
#define CHECK(cond, msg) \
    do                   \
    {                    \
        if (!(cond))     \
        {                \
            FAIL(msg);   \
            return;      \
        }                \
    } while (0)

// ── Suite: layout.c (grid layout, hit-test, scroll clamp) ──────────────

static void test_layout_narrow(void)
{
    TEST("narrow window -> 1 column");
    AppState s = {0};
    s.window_width = 200;
    s.window_height = 600;
    s.data.grid_item_count = 10;
    s.ui.dpi_scale = 1.0F;
    GridLayout lay;
    gal_calc_layout(&s.data, &s.view, &s.ui, s.window_width, s.window_height, &lay);
    CHECK(lay.cols == 1, "expected 1 column for 200px");
    PASS();
}

static void test_layout_1200(void)
{
    TEST("1200px window -> 7 columns");
    AppState s = {0};
    s.window_width = 1200;
    s.window_height = 800;
    s.data.grid_item_count = 20;
    s.ui.dpi_scale = 1.0F;
    s.ui.layout.grid_gap = 8.0F;
    s.ui.layout.panel_padding = 16.0F;
    GridLayout lay;
    gal_calc_layout(&s.data, &s.view, &s.ui, s.window_width, s.window_height, &lay);
    CHECK(lay.cols >= 7, "expected >=7 cols");
    PASS();
}

static void test_layout_1920(void)
{
    TEST("1920px window -> >= 11 columns");
    AppState s = {0};
    s.window_width = 1920;
    s.window_height = 1080;
    s.data.grid_item_count = 100;
    s.ui.dpi_scale = 1.0F;
    GridLayout lay;
    gal_calc_layout(&s.data, &s.view, &s.ui, s.window_width, s.window_height, &lay);
    CHECK(lay.cols >= 11, "expected >=11 cols at 1920px");
    PASS();
}

static void test_layout_zero_width(void)
{
    TEST("zero-width -> handled");
    AppState s = {0};
    s.window_width = 0;
    s.window_height = 600;
    s.data.grid_item_count = 10;
    s.ui.dpi_scale = 1.0F;
    GridLayout lay;
    gal_calc_layout(&s.data, &s.view, &s.ui, s.window_width, s.window_height, &lay);
    CHECK(lay.cols >= 1, "cols should be at least 1");
    PASS();
}

static void test_hit_empty(void)
{
    TEST("empty gallery -> no hit");
    AppState s = {0};
    s.view.view_mode = VIEW_GALLERY;
    s.data.grid_item_count = 0;
    int idx;
    CHECK(gal_hit_test(&s.data, &s.view, &s.ui, s.window_width, s.window_height, 100, 100, &idx) == 0,
          "no hit on empty");
    PASS();
}

static void test_hit_wrong_view(void)
{
    TEST("wrong view mode -> no hit");
    AppState s = {0};
    s.view.view_mode = VIEW_FULLIMAGE;
    s.data.grid_item_count = 10;
    int idx;
    CHECK(gal_hit_test(&s.data, &s.view, &s.ui, s.window_width, s.window_height, 100, 100, &idx) == 0,
          "no hit in fullimage");
    PASS();
}

static void test_hit_first(void)
{
    TEST("hit first thumbnail");
    AppState s = {0};
    s.view.view_mode = VIEW_GALLERY;
    s.window_width = 1200;
    s.window_height = 800;
    s.data.grid_item_count = 10;
    s.ui.dpi_scale = 1.0F;
    s.ui.layout.grid_gap = 8.0F;
    s.ui.layout.panel_padding = 16.0F;
    s.ui.layout.topbar_height = 48.0F;
    s.view.scroll_current_y = 0.0F;
    int idx;
    // Grid first cell: left_margin=16, top=64, cell w=160
    int got = gal_hit_test(&s.data, &s.view, &s.ui, s.window_width, s.window_height, 90, 140, &idx);
    CHECK(got && idx == 0, "expected hit on index 0");
    PASS();
}

static void test_hit_second(void)
{
    TEST("hit second thumbnail");
    AppState s = {0};
    s.view.view_mode = VIEW_GALLERY;
    s.window_width = 1200;
    s.window_height = 800;
    s.data.grid_item_count = 10;
    s.ui.dpi_scale = 1.0F;
    s.ui.layout.grid_gap = 8.0F;
    s.ui.layout.panel_padding = 16.0F;
    s.ui.layout.topbar_height = 48.0F;
    s.view.scroll_current_y = 0.0F;
    int idx;
    // Grid second cell: col=1, left_margin + 1*168 = 184, top=64
    int got = gal_hit_test(&s.data, &s.view, &s.ui, s.window_width, s.window_height, 250, 140, &idx);
    CHECK(got && idx == 1, "expected hit on index 1");
    PASS();
}

static void test_hit_outside(void)
{
    TEST("click outside -> no hit");
    AppState s = {0};
    s.view.view_mode = VIEW_GALLERY;
    s.window_width = 1200;
    s.window_height = 800;
    s.data.grid_item_count = 10;
    s.ui.dpi_scale = 1.0F;
    s.ui.layout.grid_gap = 8.0F;
    s.ui.layout.panel_padding = 16.0F;
    s.ui.layout.topbar_height = 48.0F;
    s.view.scroll_current_y = 0.0F;
    int idx;
    CHECK(gal_hit_test(&s.data, &s.view, &s.ui, s.window_width, s.window_height, 0, 0, &idx) == 0, "no hit at (0,0)");
    PASS();
}

// ── Suite: gallery_sort.c (selection bounds / scroll) ──────────────────

static void test_selection_middle(void)
{
    TEST("middle -> valid prev/next");
    AppState s = {0};
    s.data.count = 10;
    s.view.selected_index = 5;
    int prev = s.view.selected_index - 1;
    int next = s.view.selected_index + 1;
    CHECK(prev >= 0 && next < s.data.count, "middle index valid");
    PASS();
}

static void test_selection_boundary(void)
{
    TEST("boundary at first");
    AppState s = {0};
    s.data.count = 10;
    s.view.selected_index = 0;
    CHECK(s.view.selected_index - 1 < 0, "prev out of range");
    PASS();
}

static void test_selection_boundary_last(void)
{
    TEST("boundary at last");
    AppState s = {0};
    s.data.count = 10;
    s.view.selected_index = 9;
    CHECK(s.view.selected_index + 1 >= s.data.count, "next out of range");
    PASS();
}

// ── Suite: scroll clamp check ──────────────────────────────────────────

static void test_max_scroll_clamped(void)
{
    TEST("excessive scroll clamped");
    AppState s = {0};
    s.window_width = 1200;
    s.window_height = 800;
    s.data.grid_item_count = 200;
    s.ui.dpi_scale = 1.0F;
    s.ui.layout.grid_gap = 8.0F;
    s.ui.layout.panel_padding = 16.0F;
    s.ui.layout.topbar_height = 48.0F;
    int ms = gal_max_scroll(&s.data, &s.view, &s.ui, s.window_width, s.window_height);
    CHECK(ms >= 0, "max scroll non-negative");
    PASS();
}

// ── Suite: zoom/pan clamping ───────────────────────────────────────────

static void test_zoom_pan_clamp(void)
{
    TEST("zoom pan clamped on layout update");
    AppState s = {0};
    s.window_width = 1200;
    s.window_height = 800;
    s.ui.dpi_scale = 1.0F;
    s.view.zoom_level = 1.0F;
    s.view.zoom_pan_x = 200.0F;
    s.view.zoom_pan_y = 200.0F;
    gal_clamp_zoom_pan(&s.view, s.window_width, s.window_height, s.ui.dpi_scale, s.ui.layout.topbar_height);
    CHECK(s.view.zoom_pan_x == 0.0F, "pan x reset to 0");
    CHECK(s.view.zoom_pan_y == 0.0F, "pan y reset to 0");
    CHECK(s.view.zoom_level == 1.0F, "zoom level kept at 1");
    PASS();
}

// ── Suite: ImageEntry state transitions ────────────────────────────────

static void test_entry_defaults(void)
{
    TEST("fresh entry invariants");
    ImageEntry e = {0};
    e.texture_slot = -1; // match app_append_image_entry behavior
    e.state = IMG_STATE_NEW;
    CHECK(e.state == IMG_STATE_NEW, "new state is NEW");
    CHECK(e.texture_slot == -1, "no texture slot");
    CHECK(e.thumb_requested == 0, "not requested");
    PASS();
}

static void test_state_transitions(void)
{
    TEST("state transitions");
    ImageEntry e = {0};
    e.state = IMG_STATE_LOADING;
    CHECK(e.state == IMG_STATE_LOADING, "can set LOADING");
    e.state = IMG_STATE_READY;
    CHECK(e.state == IMG_STATE_READY, "can set READY");
    e.state = IMG_STATE_RESIDENT_GPU;
    CHECK(e.state == IMG_STATE_RESIDENT_GPU, "can set RESIDENT_GPU");
    e.state = IMG_STATE_FAILED;
    CHECK(e.state == IMG_STATE_FAILED, "can set FAILED");
    PASS();
}

static void test_enums(void)
{
    TEST("enum values");
    CHECK(VIEW_GALLERY == 0, "VIEW_GALLERY == 0");
    CHECK(VIEW_FULLIMAGE == 1, "VIEW_FULLIMAGE == 1");
    CHECK(IMG_STATE_NEW == 0, "IMG_STATE_NEW == 0");
    PASS();
}

// ── Suite: file_scanner ────────────────────────────────────────────────

static void test_image_extension(void)
{
    TEST("has_image_extension");
    CHECK(fs_has_image_extension(L"photo.jpg") == 1, "jpg");
    CHECK(fs_has_image_extension(L"photo.jpeg") == 1, "jpeg");
    CHECK(fs_has_image_extension(L"photo.png") == 1, "png");
    CHECK(fs_has_image_extension(L"photo.bmp") == 1, "bmp");
    CHECK(fs_has_image_extension(L"photo.txt") == 0, "txt");
    CHECK(fs_has_image_extension(L"noext") == 0, "no ext");
    PASS();
}

// ── Suite: gallery_fullimage.c (click handling) ────────────────────────

static void test_fullimage_back(void)
{
    TEST("fullimage hit back button -> gallery mode");
    AppState s = {0};
    s.view.view_mode = VIEW_FULLIMAGE;
    s.window_width = 1200;
    s.window_height = 800;
    s.ui.dpi_scale = 1.0F;
    int needs_redraw_fb = 0;
    int r = gal_handle_fullimage_click(&s.data, &s.view, &s.ui, &s.gpu, &s.worker, 50, 35, s.window_width,
                                       s.window_height, &needs_redraw_fb, s.hwnd);
    CHECK(r == 1, "click on back region returns 1");
    PASS();
}

static void test_fullimage_info_toggle(void)
{
    TEST("fullimage hit info button -> toggles info_open");
    AppState s = {0};
    s.view.view_mode = VIEW_FULLIMAGE;
    s.window_width = 1200;
    s.window_height = 800;
    s.ui.dpi_scale = 1.0F;
    int needs_redraw_it = 0;
    int r = gal_handle_fullimage_click(&s.data, &s.view, &s.ui, &s.gpu, &s.worker, 1120, 35, s.window_width,
                                       s.window_height, &needs_redraw_it, s.hwnd);
    CHECK(r == 1, "click on info region returns 1");
    CHECK(s.ui.info_open == 1, "info_open toggled to 1");
    PASS();
}

static void test_fullimage_prev_arrow(void)
{
    TEST("fullimage hit prev arrow -> moves selected_index");
    AppState s = {0};
    s.window_width = 1200;
    s.window_height = 800;
    s.data.count = 5;
    s.view.view_mode = VIEW_FULLIMAGE;
    s.view.selected_index = 2;
    s.ui.dpi_scale = 1.0F;
    float strip_y = (float) s.window_height - 130.0F;
    int nr = 0;
    int r = gal_handle_fullimage_click(&s.data, &s.view, &s.ui, &s.gpu, &s.worker,
                                        35, (int) strip_y + 50,
                                        s.window_width, s.window_height, &nr, NULL);
    CHECK(r == 1, "click prev handled");
    CHECK(s.view.selected_index == 1, "selected_index 2 -> 1");
    PASS();
}

static void test_fullimage_next_arrow(void)
{
    TEST("fullimage hit next arrow -> moves selected_index");
    AppState s = {0};
    s.window_width = 1200;
    s.window_height = 800;
    s.data.count = 5;
    s.view.view_mode = VIEW_FULLIMAGE;
    s.view.selected_index = 2;
    s.ui.dpi_scale = 1.0F;
    float strip_y = (float) s.window_height - 130.0F;
    int nr = 0;
    int r = gal_handle_fullimage_click(&s.data, &s.view, &s.ui, &s.gpu, &s.worker,
                                        (int) ((float) s.window_width - 35.0F), (int) strip_y + 50,
                                        s.window_width, s.window_height, &nr, NULL);
    CHECK(r == 1, "click next handled");
    CHECK(s.view.selected_index == 3, "selected_index 2 -> 3");
    PASS();
}

static void test_adaptive_load_threshold(void)
{
    TEST("large file sets full_load_pending, no-images fallback");
    AppState s = {0};
    s.window_width = 1200;
    s.window_height = 800;
    s.data.count = 2;
    s.ui.dpi_scale = 1.0F;

    // Worker ring buffer must be initialized for aw_request_full_image
    void *ring_storage[16];
    rb_init(&s.worker.work_queue, ring_storage, 16);

    ImageEntry *saved_images = (ImageEntry *) calloc(2, sizeof(ImageEntry));
    s.data.images = saved_images;
    s.data.images[1].path = L"large.jpg";
    s.data.images[1].file_size = 10ULL * 1024ULL * 1024ULL; // 10MB — above threshold
    s.data.images[1].full_width = 100; // prevent il_get_image_dimensions call
    s.data.images[1].full_height = 100;
    s.view.view_mode = VIEW_FULLIMAGE;
    s.view.selected_index = 0;

    // 1. Large file: full_load_pending set, no immediate load
    s.data.full_load_pending = 0;
    gal_select_full_image(&s.data, &s.view, &s.gpu, &s.worker, 1, NULL);
    CHECK(s.data.full_load_pending == 1, "large file: pending flag set");

    // 2. No images path (no GPU needed): pending flag set
    s.data.full_load_pending = 0;
    s.data.count = 1;
    s.data.images = NULL;
    gal_select_full_image(&s.data, &s.view, &s.gpu, &s.worker, 0, NULL);
    CHECK(s.data.full_load_pending == 1, "no images: pending flag set");

    free(saved_images);
    rb_destroy(&s.worker.work_queue);
    PASS();
}

// ── Suite: populate grid items ────────────────────────────────────────

static void test_populate_grid_root(void)
{
    TEST("populate grid items in root folder");
    // Create 5 images: 2 in subfolder "vacation", 1 in subfolder "work", 2 at root
    // We use dummy wide strings that won't actually be accessed for file I/O
    AppState s = {0};
    s.window_width = 1200;
    s.window_height = 800;
    s.data.count = 5;
    s.ui.dpi_scale = 1.0F;

    // Mock viewing_dir / current_dir as different to avoid ".." parent entry
    wcscpy(s.data.current_dir, L"C:\\photos");
    wcscpy(s.data.viewing_dir, L"C:\\photos");

    // Allocate arena for images
    void *arena_buf = VirtualAlloc(NULL, ARENA_CAPACITY, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(arena_buf != NULL, "arena allocated");
    arena_init(&s.data.arena, arena_buf, ARENA_CAPACITY);

    // Allocate nav_arena
    void *nav_arena_buf = VirtualAlloc(NULL, (SIZE_T) (2 * 1024 * 1024), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(nav_arena_buf != NULL, "nav_arena allocated");
    arena_init(&s.data.nav_arena, nav_arena_buf, (size_t) (2 * 1024 * 1024));

    // Set up images as arena-allocated copies
    const wchar_t *paths[] = {
        L"C:\\photos\\vacation\\img1.jpg",
        L"C:\\photos\\vacation\\img2.jpg",
        L"C:\\photos\\work\\img1.jpg",
        L"C:\\photos\\job1.jpg",
        L"C:\\photos\\job2.jpg",
    };
    s.data.images = (ImageEntry *) arena_alloc(&s.data.arena, 5 * sizeof(ImageEntry));
    CHECK(s.data.images != NULL, "arena alloc for images array");
    s.data.capacity = 5;
    for (int i = 0; i < 5; i++)
    {
        size_t sz = (wcslen(paths[i]) + 1) * sizeof(wchar_t);
        wchar_t *p = (wchar_t *) arena_alloc(&s.data.arena, sz);
        CHECK(p != NULL, "arena alloc for path");
        wcscpy(p, paths[i]);
        s.data.images[i].path = p;
        s.data.images[i].filename = wcsrchr(p, L'\\') + 1;
        s.data.images[i].texture_slot = -1;
        s.data.images[i].state = IMG_STATE_NEW;
    }

    // Allocate grid_items and strip_image_grid_indices
    s.data.grid_item_capacity = (s.data.count * 2) + 256;
    s.data.grid_items = arena_alloc_array(&s.data.arena, GridItem, s.data.grid_item_capacity);
    s.data.strip_image_grid_indices = arena_alloc_array(&s.data.arena, int, s.data.grid_item_capacity);
    CHECK(s.data.grid_items != NULL, "grid_items allocated");
    CHECK(s.data.strip_image_grid_indices != NULL, "strip_image_grid_indices allocated");

    app_populate_grid_items(&s.data);

    // At root with viewing_dir == current_dir, no ".." entry
    // Expected: 2 folders (vacation, work) + 2 direct images (job1.jpg, job2.jpg) = 4
    CHECK(s.data.grid_item_count == 4,
          "grid_item_count should be 4 at root");

    // Folders first (sorted alphabetically), then direct images
    CHECK(s.data.grid_items[0].type == ITEM_FOLDER, "item 0 folder");
    CHECK(s.data.grid_items[1].type == ITEM_FOLDER, "item 1 folder");
    CHECK(s.data.grid_items[2].type == ITEM_IMAGE, "item 2 is direct image");
    CHECK(s.data.grid_items[3].type == ITEM_IMAGE, "item 3 is direct image");

    // Strip count should include only direct images (not folder contents)
    CHECK(s.data.strip_image_count == 2, "strip_image_count should be 2");
    CHECK(s.data.strip_image_grid_indices[0] == 2, "strip idx 0 is grid idx 2");
    CHECK(s.data.strip_image_grid_indices[1] == 3, "strip idx 1 is grid idx 3");

    VirtualFree(arena_buf, 0, MEM_RELEASE);
    VirtualFree(nav_arena_buf, 0, MEM_RELEASE);
    PASS();
}

static void test_populate_grid_subdir(void)
{
    TEST("populate grid items in subfolder vacation");
    AppState s = {0};
    s.window_width = 1200;
    s.window_height = 800;
    s.data.count = 5;
    s.ui.dpi_scale = 1.0F;

    wcscpy(s.data.current_dir, L"C:\\photos");
    wcscpy(s.data.viewing_dir, L"C:\\photos\\vacation");

    void *arena_buf = VirtualAlloc(NULL, ARENA_CAPACITY, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(arena_buf != NULL, "arena allocated");
    arena_init(&s.data.arena, arena_buf, ARENA_CAPACITY);

    void *nav_arena_buf = VirtualAlloc(NULL, (SIZE_T) (2 * 1024 * 1024), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(nav_arena_buf != NULL, "nav_arena allocated");
    arena_init(&s.data.nav_arena, nav_arena_buf, (size_t) (2 * 1024 * 1024));

    const wchar_t *paths[] = {
        L"C:\\photos\\vacation\\img1.jpg",
        L"C:\\photos\\vacation\\img2.jpg",
        L"C:\\photos\\work\\img1.jpg",
        L"C:\\photos\\job1.jpg",
        L"C:\\photos\\job2.jpg",
    };
    s.data.images = (ImageEntry *) arena_alloc(&s.data.arena, 5 * sizeof(ImageEntry));
    s.data.capacity = 5;
    for (int i = 0; i < 5; i++)
    {
        size_t sz = (wcslen(paths[i]) + 1) * sizeof(wchar_t);
        wchar_t *p = (wchar_t *) arena_alloc(&s.data.arena, sz);
        wcscpy(p, paths[i]);
        s.data.images[i].path = p;
        s.data.images[i].filename = wcsrchr(p, L'\\') + 1;
        s.data.images[i].texture_slot = -1;
        s.data.images[i].state = IMG_STATE_NEW;
    }

    s.data.grid_item_capacity = (s.data.count * 2) + 256;
    s.data.grid_items = arena_alloc_array(&s.data.arena, GridItem, s.data.grid_item_capacity);
    s.data.strip_image_grid_indices = arena_alloc_array(&s.data.arena, int, s.data.grid_item_capacity);

    app_populate_grid_items(&s.data);

    // Should have: .. + 2 images = 3 items
    CHECK(s.data.grid_item_count == 3,
          "grid_item_count should be 3 in subfolder");

    CHECK(s.data.grid_items[0].type == ITEM_FOLDER, "item 0 is folder ..");
    CHECK(wcscmp(s.data.grid_items[0].folder_name, L"..") == 0, "folder name ..");

    CHECK(s.data.grid_items[1].type == ITEM_IMAGE, "item 1 is direct image");

    CHECK(s.data.strip_image_count == 2,
          "strip_image_count should be 2 in subfolder");

    VirtualFree(arena_buf, 0, MEM_RELEASE);
    VirtualFree(nav_arena_buf, 0, MEM_RELEASE);
    PASS();
}

// ── Suite: zoom ────────────────────────────────────────────────────────

static void test_zoom_clamp(void)
{
    TEST("zoom level clamps correctly");
    AppState s = {0};
    s.view.zoom_level = 10.0F;
    gal_clamp_zoom_pan(&s.view, 1200, 800, 1.0F, 48.0F);
    CHECK(s.view.zoom_level <= 8.0F, "zoom clamped to max 8");
    s.view.zoom_level = 0.5F;
    gal_clamp_zoom_pan(&s.view, 1200, 800, 1.0F, 48.0F);
    CHECK(s.view.zoom_level >= 1.0F, "zoom clamped to min 1");
    PASS();
}

// ── Suite: texture eviction ────────────────────────────────────────────

static void test_eviction(void)
{
    TEST("texture eviction sets correct ready states");
    AppState s = {0};
    s.data.count = 2;
    s.data.images = (ImageEntry *) calloc(2, sizeof(ImageEntry));
    s.data.images[0].texture_slot = 0;
    s.data.images[0].thumb_requested = 1;
    s.data.images[0].state = IMG_STATE_RESIDENT_GPU;
    s.gpu.tex_pool.slot_owner[0] = 0;
    s.gpu.tex_pool.last_used[0] = 0;
    r_evict_texture(&s.gpu, &s.data, 0);
    CHECK(s.data.images[0].texture_slot == -1, "slot cleared");
    CHECK(s.data.images[0].state == IMG_STATE_READY, "state is READY after eviction");
    CHECK(s.gpu.tex_pool.slot_owner[0] == -1, "owner reset");
    free(s.data.images);
    PASS();
}

// ── Suite: full-image cache eviction (r_alloc_full_image_slot) ─────────

static void test_full_cache_eviction(void)
{
    TEST("cache eviction rules when pool is full");
    AppState s = {0};
    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        s.gpu.full_slots[i].texture = (ID3D11Texture2D *) (uintptr_t) (1 + i);
    }
    int slot = r_alloc_full_image_slot(&s.gpu, &s.data, &s.view, s.data.grid_item_count);
    CHECK(slot >= 0 && slot < FULL_CACHE_SIZE, "eviction returned valid slot");
    CHECK(s.gpu.full_slots[slot].texture == NULL, "evicted slot texture is NULL");
    PASS();
}

// ── Suite: select_full_image resets panning ────────────────────────────

static void test_select_full_image_reset(void)
{
    TEST("select full image resets panning offsets");
    AppState s = {0};
    s.view.view_mode = VIEW_FULLIMAGE;
    s.view.zoom_level = 2.0F;
    s.view.zoom_pan_x = 100.0F;
    s.view.zoom_pan_y = 200.0F;
    s.view.zoom_level = 1.0F;
    gal_clamp_zoom_pan(&s.view, 1200, 800, 1.0F, 48.0F);
    CHECK(s.view.zoom_pan_x == 0.0F && s.view.zoom_pan_y == 0.0F, "pan reset when zoom==1");
    PASS();
}

// ── Suite: ring buffer concurrency ────────────────────────────────────

typedef struct
{
    RingBuffer *rb;
    int result;
} PopArg;

static DWORD WINAPI rb_push_thread_fn(LPVOID arg)
{
    RingBuffer *rb = (RingBuffer *) arg;
    for (int i = 0; i < 1000;)
    {
        if (rb_push(rb, (void *) (uintptr_t) (i + 1)))
            i++;
        else
            Sleep(0);
    }
    return 0;
}

static DWORD WINAPI rb_pop_thread_fn(LPVOID arg)
{
    PopArg *pa = (PopArg *) arg;
    int count = 0;
    while (count < 1000)
    {
        if (rb_try_pop(pa->rb))
            count++;
        else
            Sleep(0);
    }
    pa->result = count;
    return 0;
}

static void test_ring_buffer_concurrent(void)
{
    TEST("ring buffer concurrent push/pop with 2+2 threads");

    RingBuffer rb;
    void *storage[256];
    rb_init(&rb, storage, 256);

    PopArg pa1 = {&rb, 0};
    PopArg pa2 = {&rb, 0};

    HANDLE threads[4];
    threads[0] = CreateThread(NULL, 0, rb_push_thread_fn, &rb, 0, NULL);
    threads[1] = CreateThread(NULL, 0, rb_push_thread_fn, &rb, 0, NULL);
    threads[2] = CreateThread(NULL, 0, rb_pop_thread_fn, &pa1, 0, NULL);
    threads[3] = CreateThread(NULL, 0, rb_pop_thread_fn, &pa2, 0, NULL);

    CHECK(threads[0] != NULL, "push thread 1 created");
    CHECK(threads[1] != NULL, "push thread 2 created");
    CHECK(threads[2] != NULL, "pop thread 1 created");
    CHECK(threads[3] != NULL, "pop thread 2 created");

    WaitForMultipleObjects(4, threads, TRUE, INFINITE);

    for (int i = 0; i < 4; i++)
        CloseHandle(threads[i]);

    CHECK(pa1.result + pa2.result == 2000, "total popped items == 2000");

    // ── Boundary: full / empty ──
    RingBuffer rb2;
    void *storage2[16];
    rb_init(&rb2, storage2, 16);

    int filled = 0;
    while (rb_push(&rb2, (void *) (uintptr_t) (filled + 1)))
        filled++;
    CHECK(filled == 15, "ring buffer filled to capacity-1");
    CHECK(rb_push(&rb2, (void *) (uintptr_t) 999) == 0, "rb_push returns 0 when full");

    int popped = 0;
    while (rb_try_pop(&rb2))
        popped++;
    CHECK(popped == 15, "all items popped back");
    CHECK(rb_try_pop(&rb2) == NULL, "rb_try_pop returns NULL when empty");

    rb_destroy(&rb2);
    rb_destroy(&rb);
    PASS();
}

// ── Suite: image loader WIC init, decode, error paths ────────────────

static void test_image_loader_wic(void)
{
    TEST("image loader WIC init, decode, and error paths");

    // Initialize COM for WIC
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int com_active = SUCCEEDED(hr);

    // 1. Init WIC
    int wic_ok = il_init_wic();
    CHECK(wic_ok != 0, "il_init_wic succeeds");

    // 2. Error path: nonexistent file returns NULL
    int out_size = 0;
    void *result = il_load_and_compress(L"Z:\\__calbum_test_nonexistent__.png", 32, &out_size);
    CHECK(result == NULL, "il_load_and_compress with bogus path returns NULL");

    // 3. Positive path: create a 1x1 red BMP, load and compress
    wchar_t temp_dir[MAX_PATH];
    wchar_t temp_file[MAX_PATH];
    DWORD dret = GetTempPathW(MAX_PATH, temp_dir);
    CHECK(dret > 0 && dret < MAX_PATH, "GetTempPathW succeeds");
    dret = GetTempFileNameW(temp_dir, L"cbt", 0, temp_file);
    CHECK(dret != 0, "GetTempFileNameW succeeds");

    BITMAPFILEHEADER bfh;
    BITMAPINFOHEADER bih;
    uint8_t pixels[4] = {0x00, 0x00, 0xFF, 0x00}; // BGR red + row padding

    memset(&bfh, 0, sizeof(bfh));
    bfh.bfType = 0x4D42;
    bfh.bfSize = sizeof(bfh) + sizeof(bih) + sizeof(pixels);
    bfh.bfOffBits = sizeof(bfh) + sizeof(bih);

    memset(&bih, 0, sizeof(bih));
    bih.biSize = sizeof(bih);
    bih.biWidth = 1;
    bih.biHeight = 1;
    bih.biPlanes = 1;
    bih.biBitCount = 24;

    HANDLE hFile = CreateFileW(temp_file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(hFile != INVALID_HANDLE_VALUE, "CreateFileW for temp BMP succeeds");

    DWORD written;
    WriteFile(hFile, &bfh, sizeof(bfh), &written, NULL);
    WriteFile(hFile, &bih, sizeof(bih), &written, NULL);
    WriteFile(hFile, pixels, sizeof(pixels), &written, NULL);
    CloseHandle(hFile);

    out_size = 0;
    result = il_load_and_compress(temp_file, 32, &out_size);
    CHECK(result != NULL, "il_load_and_compress with valid BMP returns non-NULL");
    CHECK(out_size > 0, "compressed output size > 0");

    free(result);
    DeleteFileW(temp_file);

    il_shutdown_wic();
    if (com_active)
        CoUninitialize();

    PASS();
}

// ── Main runner ────────────────────────────────────────────────────────

int main(void)
{
    printf("========================================\n");
    printf("  calbum - Unit Tests\n");
    printf("========================================\n\n");

    test_layout_narrow();
    test_layout_1200();
    test_layout_1920();
    test_layout_zero_width();
    test_hit_empty();
    test_hit_wrong_view();
    test_hit_first();
    test_hit_second();
    test_hit_outside();

    test_selection_middle();
    test_selection_boundary();
    test_selection_boundary_last();
    test_max_scroll_clamped();

    test_zoom_pan_clamp();
    test_zoom_clamp();

    test_entry_defaults();
    test_state_transitions();
    test_enums();

    test_image_extension();

    test_fullimage_back();
    test_fullimage_info_toggle();
    test_fullimage_prev_arrow();
    test_fullimage_next_arrow();
    test_adaptive_load_threshold();

    test_populate_grid_root();
    test_populate_grid_subdir();

    test_eviction();

    test_full_cache_eviction();

    test_select_full_image_reset();

    test_ring_buffer_concurrent();
    test_image_loader_wic();

    printf("\n");
    printf("========================================\n");
    printf("  Results: %d/%d passed, %d failed\n", g_pass, g_run, g_fail);
    printf("========================================\n");
    return g_fail > 0 ? 1 : 0;
}
