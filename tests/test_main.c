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

#include "src/utils.c"
#include "src/renderer.c"
#include "src/layout.c"
#include "src/file_scanner.c"
#define STB_IMAGE_IMPLEMENTATION
#include "lib/stb_image.h"
#include "src/image_loader.c"
#include "src/file_monitor.c"
#include "src/asset_worker.c"
#include "src/ui.c"
#include "src/gallery.c"
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

// ── Helpers ────────────────────────────────────────────────────────────
static void setup(AppState *s, int count, int ww, int wh)
{
    memset(s, 0, sizeof(*s));
    s->count = count;
    s->grid_item_count = count;
    s->window_width = ww;
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
    s.grid_item_count = 0;
    CHECK(gal_hit_test(&s, 100, 100, &idx) == 0, "empty no hit");
    s.count = 4;
    s.grid_item_count = 4;
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

static void test_zoom_clamp_on_update_layout(void)
{
    AppState s;
    setup(&s, 50, 1200, 800);
    s.view_mode = VIEW_FULLIMAGE;
    s.zoom_level = 2.0f;
    s.zoom_pan_x = 9999.0f;
    s.zoom_pan_y = 9999.0f;

    TEST("zoom pan clamped on layout update");
    gal_update_layout(&s);
    CHECK(s.zoom_pan_x < 9999.0f, "zoom_pan_x clamped");
    CHECK(s.zoom_pan_y < 9999.0f, "zoom_pan_y clamped");
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
    CHECK(VIEW_GALLERY == 0, "GALLERY=0");
    CHECK(VIEW_FULLIMAGE == 1, "FULLIMAGE=1");
    PASS();
}

static void test_extensions(void)
{
    TEST("has_image_extension");
    CHECK(fs_has_image_extension(L"photo.jpg") == 1, ".jpg ok");
    CHECK(fs_has_image_extension(L"photo.png") == 1, ".png ok");
    CHECK(fs_has_image_extension(L"photo.bmp") == 1, ".bmp ok");
    CHECK(fs_has_image_extension(L"photo.txt") == 0, ".txt not ok");
    CHECK(fs_has_image_extension(L"photo.JPG") == 1, ".JPG (case) ok");
    CHECK(fs_has_image_extension(L"photo") == 0, "no ext not ok");
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

    TEST("cache eviction rules when pool is full");
    s.count = 10;
    s.selected_index = 5;
    s.images = malloc(sizeof(ImageEntry) * 10);
    memset(s.images, 0, sizeof(ImageEntry) * 10);
    for (int i = 0; i < 10; i++)
    {
        wchar_t path_buf[32];
        swprintf(path_buf, 32, L"path%d.jpg", i);
        s.images[i].path = wcsdup(path_buf);
    }
    s.window_width = 1200;
    s.window_height = 800;

    memset(s.full_slots, 0, sizeof(s.full_slots));
    for (int i = 0; i < FULL_CACHE_SIZE; i++)
    {
        s.full_slots[i].texture = (void *) 1;
        if (i == 0)
        {
            wcscpy(s.full_slots[i].path, L"path9.jpg");
        }
        else
        {
            wcscpy(s.full_slots[i].path, L"path5.jpg");
        }
    }

    int allocated = r_alloc_full_image_slot(&s);
    CHECK(allocated == 0, "should evict slot 0 holding path9.jpg (off-strip)");
    CHECK(s.full_slots[0].texture == NULL, "slot 0 texture should be released");

    for (int i = 0; i < 10; i++)
    {
        free(s.images[i].path);
    }
    free(s.images);
    PASS();

    TEST("select full image resets panning offsets");
    s.images = malloc(sizeof(ImageEntry) * 5);
    memset(s.images, 0, sizeof(ImageEntry) * 5);
    s.images[0].path = L"path0.jpg";
    s.images[1].path = L"path1.jpg";

    s.zoom_pan_x = 100.0f;
    s.zoom_pan_y = -50.0f;
    s.is_panning = 1;
    s.zoom_level = 2.0f;

    gal_select_full_image(&s, 1);
    CHECK(s.zoom_pan_x == 0.0f, "zoom_pan_x reset");
    CHECK(s.zoom_pan_y == 0.0f, "zoom_pan_y reset");
    CHECK(s.is_panning == 0, "is_panning reset");
    CHECK(s.zoom_level == 1.0f, "zoom_level reset");

    free(s.images);
    PASS();
}

static void test_folders_and_strip_caching(void)
{
    AppState s;
    memset(&s, 0, sizeof(s));

    // Allocate arena
    void *arena_buf = malloc(1024 * 1024);
    arena_init(&s.arena, arena_buf, 1024 * 1024);

    // Allocate nav_arena
    void *nav_arena_buf = malloc(1024 * 1024);
    arena_init(&s.nav_arena, nav_arena_buf, 1024 * 1024);

    s.count = 5;
    s.capacity = 5;
    s.images = arena_alloc_array(&s.arena, ImageEntry, s.count);

    s.images[0].path = L"C:\\photos\\vacation\\img1.jpg";
    s.images[1].path = L"C:\\photos\\vacation\\img2.jpg";
    s.images[2].path = L"C:\\photos\\work\\projectA\\job1.jpg";
    s.images[3].path = L"C:\\photos\\job2.jpg";
    s.images[4].path = L"C:\\photos\\img_root.jpg";

    wcscpy(s.current_dir, L"C:\\photos");
    wcscpy(s.viewing_dir, L"C:\\photos");

    s.grid_item_capacity = 256;
    s.grid_items = arena_alloc_array(&s.arena, GridItem, s.grid_item_capacity);
    s.strip_image_grid_indices = arena_alloc_array(&s.arena, int, s.grid_item_capacity);

    TEST("populate grid items in root folder");
    app_populate_grid_items(&s);
    CHECK(s.grid_item_count == 4, "grid_item_count should be 4");

    // Grid items: 2 folders (vacation, work) then 2 images (job2.jpg, img_root.jpg)
    CHECK(s.grid_items[0].type == ITEM_FOLDER, "item 0 folder");
    CHECK(_wcsicmp(s.grid_items[0].folder_name, L"vacation") == 0, "folder name vacation");
    CHECK(_wcsicmp(s.grid_items[0].folder_path, L"C:\\photos\\vacation") == 0, "folder path vacation");
    CHECK(s.grid_items[0].image_count == 2, "vacation should contain 2 images");
    CHECK(s.grid_items[0].folder_count == 0, "vacation should contain 0 folders");

    CHECK(s.grid_items[1].type == ITEM_FOLDER, "item 1 folder");
    CHECK(_wcsicmp(s.grid_items[1].folder_name, L"work") == 0, "folder name work");
    CHECK(_wcsicmp(s.grid_items[1].folder_path, L"C:\\photos\\work") == 0, "folder path work");
    CHECK(s.grid_items[1].image_count == 1, "work should contain 1 image");
    CHECK(s.grid_items[1].folder_count == 1, "work should contain 1 folder");

    // NOTE: image_index values below correspond to the indices of job2.jpg and img_root.jpg
    // in the s.images array (indices 3 and 4). app_populate_grid_items appends direct images
    // in the exact order they are stored in the s.images array.
    CHECK(s.grid_items[2].type == ITEM_IMAGE, "item 2 image");
    CHECK(s.grid_items[2].image_index == 3, "item 2 image_index 3");

    CHECK(s.grid_items[3].type == ITEM_IMAGE, "item 3 image");
    CHECK(s.grid_items[3].image_index == 4, "item 3 image_index 4");

    // Strip count and cached strip index checks
    CHECK(s.strip_image_count == 2, "strip_image_count should be 2");
    CHECK(s.strip_image_grid_indices[0] == 2, "strip idx 0 is grid idx 2");
    CHECK(s.strip_image_grid_indices[1] == 3, "strip idx 1 is grid idx 3");
    PASS();

    TEST("populate grid items in subfolder vacation");
    wcscpy(s.viewing_dir, L"C:\\photos\\vacation");
    app_populate_grid_items(&s);

    // Grid items: 1 folder (..), 2 images (img1.jpg, img2.jpg)
    CHECK(s.grid_item_count == 3, "grid_item_count should be 3 in subfolder");
    CHECK(s.grid_items[0].type == ITEM_FOLDER, "item 0 is folder ..");
    CHECK(_wcsicmp(s.grid_items[0].folder_name, L"..") == 0, "folder name ..");
    CHECK(_wcsicmp(s.grid_items[0].folder_path, L"C:\\photos") == 0, "parent path C:\\photos");

    CHECK(s.grid_items[1].type == ITEM_IMAGE, "item 1 image");
    CHECK(s.grid_items[1].image_index == 0, "item 1 image_index 0");

    CHECK(s.grid_items[2].type == ITEM_IMAGE, "item 2 image");
    CHECK(s.grid_items[2].image_index == 1, "item 2 image_index 1");

    CHECK(s.strip_image_count == 2, "strip count should be 2");
    CHECK(s.strip_image_grid_indices[0] == 1, "strip idx 0 is grid idx 1");
    CHECK(s.strip_image_grid_indices[1] == 2, "strip idx 1 is grid idx 2");
    PASS();

    free(arena_buf);
    free(nav_arena_buf);
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
    test_zoom_clamp_on_update_layout();
    test_image_entry();
    test_viewmode();
    test_extensions();
    test_fullimage_interactions();
    test_zoom_and_recovery();
    test_folders_and_strip_caching();

    printf("\n========================================\n");
    printf("  Results: %d/%d passed, %d failed\n", g_pass, g_run, g_fail);
    printf("========================================\n");
    return g_fail > 0 ? 1 : 0;
}
