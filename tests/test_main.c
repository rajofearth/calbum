// =========================================================================
// test_main.c - calbum unit test runner
//
// Tests pure functions from the codebase. Uses a unity-build approach:
// includes source files directly to access function definitions.
// Compiled as a console executable (no WinMain).
// =========================================================================
#include <windows.h>
#include <initguid.h>
#include <knownfolders.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Unity include the subsystems we test (in dependency order)
#define STB_DXT_IMPLEMENTATION
#include "lib/stb_dxt.h"
#include "src/types.h"
#include "src/renderer.c"
#include "src/file_scanner.c"
#define STB_IMAGE_IMPLEMENTATION
#include "lib/stb_image.h"
#include "src/image_loader.c"
#include "src/asset_worker.c"
#include "src/ui.c"
#include "src/gallery.c"

// ── Test framework ──────────────────────────────────────────────────────
static int g_run = 0, g_pass = 0, g_fail = 0;

#define TEST(name) \
    do { g_run++; printf("  TEST  %-50s ", name); fflush(stdout); } while (0)
#define PASS() \
    do { g_pass++; printf("PASS\n"); } while (0)
#define FAIL(msg) \
    do { g_fail++; printf("FAIL  (%s)\n", msg); } while (0)
#define CHECK(cond, msg) \
    do { if (!(cond)) { FAIL(msg); return; } } while (0)

// ── Helpers ────────────────────────────────────────────────────────────
static void setup(AppState *s, int count, int ww, int wh)
{
    memset(s, 0, sizeof(*s));
    s->count = count;
    s->window_width  = ww;
    s->window_height = wh;
    s->view_mode = VIEW_GALLERY;
}

// ── Tests ──────────────────────────────────────────────────────────────

static void test_gal_calc_layout(void)
{
    AppState s;
    GridLayout lay;

    TEST("narrow window -> 1 column");
    setup(&s, 10, 100, 600);
    gal_calc_layout(&s, &lay);
    CHECK(lay.cols == 1, "narrow -> 1");
    PASS();

    TEST("1200px window -> 7 columns");
    setup(&s, 50, 1200, 800);
    gal_calc_layout(&s, &lay);
    CHECK(lay.cols == 7, "1200 -> 7");
    PASS();

    TEST("1920px window -> >= 11 columns");
    setup(&s, 100, 1920, 1080);
    gal_calc_layout(&s, &lay);
    CHECK(lay.cols >= 11, "1920 -> >= 11");
    PASS();

    TEST("zero-width -> handled");
    setup(&s, 10, 0, 800);
    gal_calc_layout(&s, &lay);
    CHECK(lay.cols >= 1, "0-width -> 1");
    PASS();
}

static void test_gal_hit_test(void)
{
    AppState s;
    setup(&s, 4, 1200, 800);
    int idx = -1;

    TEST("empty gallery -> no hit");
    s.count = 0;
    CHECK(gal_hit_test(&s, 100, 100, &idx) == 0, "empty no hit");
    s.count = 4;
    PASS();

    TEST("wrong view mode -> no hit");
    s.view_mode = VIEW_FULLIMAGE;
    CHECK(gal_hit_test(&s, 100, 100, &idx) == 0, "fullimage no hit");
    s.view_mode = VIEW_GALLERY;
    PASS();

    GridLayout lay;
    gal_calc_layout(&s, &lay);

    TEST("hit first thumbnail");
    CHECK(gal_hit_test(&s, lay.left_margin + 50, GALLERY_PADDING + 50, &idx) == 1, "hit first");
    CHECK(idx == 0, "idx 0");
    PASS();

    TEST("hit second thumbnail");
    CHECK(gal_hit_test(&s, lay.left_margin + lay.pad + 50, GALLERY_PADDING + 50, &idx) == 1, "hit second");
    CHECK(idx == 1, "idx 1");
    PASS();

    TEST("click outside -> no hit");
    CHECK(gal_hit_test(&s, -10, -10, &idx) == 0, "outside no hit");
    CHECK(gal_hit_test(&s, 10000, 10000, &idx) == 0, "far no hit");
    PASS();
}

static void test_selection_bounds(void)
{
    AppState s;
    setup(&s, 5, 1200, 800);
    s.selected_index = 2;

    TEST("middle -> valid prev/next");
    CHECK(s.selected_index - 1 >= 0, "prev valid");
    CHECK(s.selected_index + 1 < s.count, "next valid");
    PASS();

    TEST("boundary at first");
    s.selected_index = 0;
    CHECK(s.selected_index - 1 < 0, "first prev invalid");
    PASS();

    TEST("boundary at last");
    s.selected_index = 4;
    CHECK(s.selected_index + 1 >= s.count, "last next invalid");
    PASS();
}

static void test_scroll_clamp(void)
{
    AppState s;
    setup(&s, 50, 1200, 800);

    TEST("excessive scroll clamped");
    s.scroll_target_y = 999999.0f;
    gal_update_layout(&s);
    CHECK(s.scroll_target_y >= 0.0f, "scroll clamped >= 0");
    PASS();
}

static void test_image_entry(void)
{
    ImageEntry e;
    memset(&e, 0, sizeof(e));
    e.texture_slot = -1;
    e.state = IMG_STATE_NEW;

    TEST("fresh entry invariants");
    CHECK(e.state == IMG_STATE_NEW, "state=NEW");
    CHECK(e.texture_slot == -1, "tex=-1");
    CHECK(e.thumb_requested == 0, "thumb_req=0");
    PASS();

    TEST("state transitions");
    e.state = IMG_STATE_LOADING;
    e.thumb_requested = 1;
    CHECK(e.state == IMG_STATE_LOADING, "state=LOADING");
    CHECK(e.thumb_requested == 1, "req=1");
    PASS();
}

static void test_viewmode(void)
{
    TEST("enum values");
    CHECK(VIEW_GALLERY   == 0, "GALLERY=0");
    CHECK(VIEW_FULLIMAGE == 1, "FULLIMAGE=1");
    PASS();
}

static void test_extensions(void)
{
    TEST("has_image_extension");
    CHECK(fs_has_image_extension(L"photo.jpg")  == 1, ".jpg ok");
    CHECK(fs_has_image_extension(L"photo.png")  == 1, ".png ok");
    CHECK(fs_has_image_extension(L"photo.bmp")  == 1, ".bmp ok");
    CHECK(fs_has_image_extension(L"photo.txt")  == 0, ".txt not ok");
    CHECK(fs_has_image_extension(L"photo.JPG")  == 1, ".JPG (case) ok");
    CHECK(fs_has_image_extension(L"photo")      == 0, "no ext not ok");
    PASS();
}

static void test_fullimage_interactions(void)
{
    AppState s;
    setup(&s, 5, 1200, 800);
    s.view_mode = VIEW_FULLIMAGE;
    s.selected_index = 2;
    s.info_open = 0;

    TEST("fullimage hit back button -> gallery mode");
    CHECK(gal_handle_fullimage_click(&s, 50, 35) == 1, "click back handled");
    CHECK(s.view_mode == VIEW_GALLERY, "back -> VIEW_GALLERY");
    s.view_mode = VIEW_FULLIMAGE;
    PASS();

    TEST("fullimage hit info button -> toggles info_open");
    CHECK(gal_handle_fullimage_click(&s, 1200 - 70, 35) == 1, "click info handled");
    CHECK(s.info_open == 1, "info toggled to 1");
    CHECK(gal_handle_fullimage_click(&s, 1200 - 70, 35) == 1, "click info handled");
    CHECK(s.info_open == 0, "info toggled to 0");
    PASS();

    TEST("fullimage hit prev arrow -> moves selected_index");
    CHECK(gal_handle_fullimage_click(&s, 35, 800 - 130 + 50) == 1, "click prev handled");
    CHECK(s.selected_index == 1, "selected_index 2 -> 1");
    PASS();

    TEST("fullimage hit next arrow -> moves selected_index");
    CHECK(gal_handle_fullimage_click(&s, 1200 - 35, 800 - 130 + 50) == 1, "click next handled");
    CHECK(s.selected_index == 2, "selected_index 1 -> 2");
    PASS();
}

static void test_zoom_and_recovery(void)
{
    AppState s;
    setup(&s, 5, 1200, 800);
    s.view_mode = VIEW_FULLIMAGE;
    s.selected_index = 2;
    s.zoom_level = 1.0f;
    s.zoom_ui_timer = 0.0f;

    TEST("zoom level clamps correctly");
    s.zoom_level = 1.5f;
    s.zoom_ui_timer = 2.0f;
    CHECK(gal_handle_fullimage_click(&s, 600, 35) == 1, "click zoom badge handled");
    CHECK(s.zoom_level == 1.0f, "zoom level reset to 1.0x");
    CHECK(s.zoom_ui_timer == 0.0f, "zoom ui timer cleared");
    PASS();

    TEST("texture eviction sets correct ready states");
    s.images = malloc(sizeof(ImageEntry) * 5);
    memset(s.images, 0, sizeof(ImageEntry) * 5);
    s.images[2].path = L"mock_photo.png";
    s.images[2].texture_slot = 14;
    s.images[2].state = IMG_STATE_RESIDENT_GPU;
    s.images[2].thumb_requested = 1;

    r_evict_texture(&s, 14);
    CHECK(s.images[2].texture_slot == -1, "slot reset to -1");
    CHECK(s.images[2].thumb_requested == 0, "thumb_requested reset to 0");
    CHECK(s.images[2].state == IMG_STATE_READY, "state set to READY");
    free(s.images);
    PASS();

    TEST("adaptive load sizing threshold");
    s.images = malloc(sizeof(ImageEntry) * 2);
    memset(s.images, 0, sizeof(ImageEntry) * 2);
    s.images[0].path = L"small.jpg";
    s.images[0].file_size = 1024ULL * 1024ULL; // 1MB
    s.images[1].path = L"large.jpg";
    s.images[1].file_size = 10ULL * 1024ULL * 1024ULL; // 10MB
    
    gal_select_full_image(&s, 0);
    CHECK(s.full_load_timer == 0.0, "small file loaded instantly");

    gal_select_full_image(&s, 1);
    CHECK(s.full_load_timer == 0.15, "large file debounces for 150ms");
    
    free(s.images);
    PASS();
}

// ── Main ────────────────────────────────────────────────────────────────
int main(void)
{
    printf("========================================\n");
    printf("  calbum - Unit Tests\n");
    printf("========================================\n\n");

    test_gal_calc_layout();
    test_gal_hit_test();
    test_selection_bounds();
    test_scroll_clamp();
    test_image_entry();
    test_viewmode();
    test_extensions();
    test_fullimage_interactions();
    test_zoom_and_recovery();

    printf("\n========================================\n");
    printf("  Results: %d/%d passed, %d failed\n", g_pass, g_run, g_fail);
    printf("========================================\n");
    return g_fail > 0 ? 1 : 0;
}
