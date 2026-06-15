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
    if (s->count == 0)
        return;

    // Save currently selected path
    wchar_t selected_path[MAX_PATH_LEN] = {0};
    if (s->selected_index >= 0)
    {
        int limit = s->grid_items ? s->grid_item_count : s->count;
        if (s->selected_index < limit)
        {
            if (s->grid_items)
            {
                if (s->grid_items[s->selected_index].type == ITEM_FOLDER)
                {
                    if (s->grid_items[s->selected_index].folder_path)
                    {
                        wcsncpy(selected_path, s->grid_items[s->selected_index].folder_path, MAX_PATH_LEN - 1);
                    }
                }
                else
                {
                    int img_idx = s->grid_items[s->selected_index].image_index;
                    if (img_idx >= 0 && img_idx < s->count)
                    {
                        wcsncpy(selected_path, s->images[img_idx].path, MAX_PATH_LEN - 1);
                    }
                }
            }
            else
            {
                wcsncpy(selected_path, s->images[s->selected_index].path, MAX_PATH_LEN - 1);
            }
        }
    }

    int (*cmp)(const void *, const void *) = cmp_date_created;
    if (s->sort_mode == SORT_DATE_MODIFIED)
        cmp = cmp_date_modified;
    if (s->sort_mode == SORT_SIZE)
        cmp = cmp_size;

    qsort(s->images, s->count, sizeof(ImageEntry), cmp);

    // Reverse if descending
    if (s->sort_descending)
    {
        for (int i = 0; i < s->count / 2; i++)
        {
            ImageEntry tmp = s->images[i];
            s->images[i] = s->images[s->count - 1 - i];
            s->images[s->count - 1 - i] = tmp;
        }
    }

    if (s->grid_items)
    {
        app_populate_grid_items(s);
    }

    // Restore selection
    if (selected_path[0])
    {
        if (s->grid_items)
        {
            for (int i = 0; i < s->grid_item_count; i++)
            {
                if (s->grid_items[i].type == ITEM_FOLDER)
                {
                    if (s->grid_items[i].folder_path && _wcsicmp(s->grid_items[i].folder_path, selected_path) == 0)
                    {
                        s->selected_index = i;
                        break;
                    }
                }
                else
                {
                    int img_idx = s->grid_items[i].image_index;
                    if (img_idx >= 0 && img_idx < s->count)
                    {
                        if (_wcsicmp(s->images[img_idx].path, selected_path) == 0)
                        {
                            s->selected_index = i;
                            break;
                        }
                    }
                }
            }
        }
        else
        {
            for (int i = 0; i < s->count; i++)
            {
                if (_wcsicmp(s->images[i].path, selected_path) == 0)
                {
                    s->selected_index = i;
                    break;
                }
            }
        }
    }
    s->needs_redraw = 1;
}
