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

void gal_apply_sort(DataState *data, ViewState *view)
{
    if (data->count == 0)
        return;

    // Save currently selected path
    wchar_t selected_path[MAX_PATH_LEN] = {0};
    if (view->selected_index >= 0)
    {
        int limit = data->grid_items ? data->grid_item_count : data->count;
        if (view->selected_index < limit)
        {
            if (data->grid_items)
            {
                if (data->grid_items[view->selected_index].type == ITEM_FOLDER)
                {
                    if (data->grid_items[view->selected_index].folder_path)
                    {
                        wcsncpy(selected_path, data->grid_items[view->selected_index].folder_path, MAX_PATH_LEN - 1);
                    }
                }
                else
                {
                    int img_idx = data->grid_items[view->selected_index].image_index;
                    if (img_idx >= 0 && img_idx < data->count)
                    {
                        wcsncpy(selected_path, data->images[img_idx].path, MAX_PATH_LEN - 1);
                    }
                }
            }
            else
            {
                wcsncpy(selected_path, data->images[view->selected_index].path, MAX_PATH_LEN - 1);
            }
        }
    }

    int (*cmp)(const void *, const void *) = cmp_date_created;
    if (view->sort_mode == SORT_DATE_MODIFIED)
        cmp = cmp_date_modified;
    if (view->sort_mode == SORT_SIZE)
        cmp = cmp_size;

    qsort(data->images, data->count, sizeof(ImageEntry), cmp);

    // Reverse if descending
    if (view->sort_descending)
    {
        for (int i = 0; i < data->count / 2; i++)
        {
            ImageEntry tmp = data->images[i];
            data->images[i] = data->images[data->count - 1 - i];
            data->images[data->count - 1 - i] = tmp;
        }
    }

    if (data->grid_items)
    {
        app_populate_grid_items(data);
    }

    // Restore selection
    if (selected_path[0])
    {
        if (data->grid_items)
        {
            for (int i = 0; i < data->grid_item_count; i++)
            {
                if (data->grid_items[i].type == ITEM_FOLDER)
                {
                    if (data->grid_items[i].folder_path &&
                        _wcsicmp(data->grid_items[i].folder_path, selected_path) == 0)
                    {
                        view->selected_index = i;
                        break;
                    }
                }
                else
                {
                    int img_idx = data->grid_items[i].image_index;
                    if (img_idx >= 0 && img_idx < data->count)
                    {
                        if (_wcsicmp(data->images[img_idx].path, selected_path) == 0)
                        {
                            view->selected_index = i;
                            break;
                        }
                    }
                }
            }
        }
        else
        {
            for (int i = 0; i < data->count; i++)
            {
                if (_wcsicmp(data->images[i].path, selected_path) == 0)
                {
                    view->selected_index = i;
                    break;
                }
            }
        }
    }
}
