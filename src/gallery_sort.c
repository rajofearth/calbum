// =========================================================================
// gallery_sort.c — Sort comparators and apply-sort logic
// =========================================================================
#include "types.h"
#include <math.h>

// qsort comparator
static int cmp_date_created(const void *a, const void *b)
{
    ImageEntry *ea = (ImageEntry *) a;
    ImageEntry *eb = (ImageEntry *) b;
    if (ea->created_time < eb->created_time)
        return -1;
    if (ea->created_time > eb->created_time)
        return 1;
    return _wcsicmp(ea->path, eb->path);
}

// qsort comparator
static int cmp_date_modified(const void *a, const void *b)
{
    ImageEntry *ea = (ImageEntry *) a;
    ImageEntry *eb = (ImageEntry *) b;
    if (ea->last_modified < eb->last_modified)
        return -1;
    if (ea->last_modified > eb->last_modified)
        return 1;
    return _wcsicmp(ea->path, eb->path);
}

// qsort comparator
static int cmp_size(const void *a, const void *b)
{
    ImageEntry *ea = (ImageEntry *) a;
    ImageEntry *eb = (ImageEntry *) b;
    if (ea->file_size < eb->file_size)
        return -1;
    if (ea->file_size > eb->file_size)
        return 1;
    return _wcsicmp(ea->path, eb->path);
}

void gal_apply_sort(AppState *s)
{
    if (s->data.count == 0)
        return;

    // Save currently selected path
    wchar_t selected_path[MAX_PATH_LEN] = {0};
    if (s->view.selected_index >= 0)
    {
        int limit = s->data.grid_items ? s->data.grid_item_count : s->data.count;
        if (s->view.selected_index < limit)
        {
            if (s->data.grid_items)
            {
                if (s->data.grid_items[s->view.selected_index].type == ITEM_FOLDER)
                {
                    if (s->data.grid_items[s->view.selected_index].folder_path)
                    {
                        wcsncpy(selected_path, s->data.grid_items[s->view.selected_index].folder_path,
                                MAX_PATH_LEN - 1);
                    }
                }
                else
                {
                    int img_idx = s->data.grid_items[s->view.selected_index].image_index;
                    if (img_idx >= 0 && img_idx < s->data.count)
                    {
                        wcsncpy(selected_path, s->data.images[img_idx].path, MAX_PATH_LEN - 1);
                    }
                }
            }
            else
            {
                wcsncpy(selected_path, s->data.images[s->view.selected_index].path, MAX_PATH_LEN - 1);
            }
        }
    }

    int (*cmp)(const void *, const void *) = cmp_date_created;
    if (s->view.sort_mode == SORT_DATE_MODIFIED)
        cmp = cmp_date_modified;
    if (s->view.sort_mode == SORT_SIZE)
        cmp = cmp_size;

    qsort(s->data.images, s->data.count, sizeof(ImageEntry), cmp);

    // Reverse if descending
    if (s->view.sort_descending)
    {
        for (int i = 0; i < s->data.count / 2; i++)
        {
            ImageEntry tmp = s->data.images[i];
            s->data.images[i] = s->data.images[s->data.count - 1 - i];
            s->data.images[s->data.count - 1 - i] = tmp;
        }
    }

    if (s->data.grid_items)
    {
        app_populate_grid_items(s);
    }

    // Restore selection
    if (selected_path[0])
    {
        if (s->data.grid_items)
        {
            for (int i = 0; i < s->data.grid_item_count; i++)
            {
                if (s->data.grid_items[i].type == ITEM_FOLDER)
                {
                    if (s->data.grid_items[i].folder_path &&
                        _wcsicmp(s->data.grid_items[i].folder_path, selected_path) == 0)
                    {
                        s->view.selected_index = i;
                        break;
                    }
                }
                else
                {
                    int img_idx = s->data.grid_items[i].image_index;
                    if (img_idx >= 0 && img_idx < s->data.count)
                    {
                        if (_wcsicmp(s->data.images[img_idx].path, selected_path) == 0)
                        {
                            s->view.selected_index = i;
                            break;
                        }
                    }
                }
            }
        }
        else
        {
            for (int i = 0; i < s->data.count; i++)
            {
                if (_wcsicmp(s->data.images[i].path, selected_path) == 0)
                {
                    s->view.selected_index = i;
                    break;
                }
            }
        }
    }
    s->needs_redraw = 1;
}
