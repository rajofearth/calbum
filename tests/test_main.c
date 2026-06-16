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

    test_eviction();

    test_full_cache_eviction();

    test_select_full_image_reset();

    printf("\n");
    printf("========================================\n");
    printf("  Results: %d/%d passed, %d failed\n", g_pass, g_run, g_fail);
    printf("========================================\n");
    return g_fail > 0 ? 1 : 0;
}
