// =========================================================================
// file_scanner.c — Recursive directory scan via FindFirstFileW
//
// Results are written into AppState's arena-backed flat array.
// =========================================================================
#include "types.h"

int fs_has_image_extension(const wchar_t *name)
{
    static const wchar_t *exts[] = {
        L".jpg",L".jpeg",L".png",L".bmp",L".gif",
        L".webp",L".tga",L".tiff",L".tif",L".psd"
    };
    const wchar_t *dot = wcsrchr(name, L'.');
    if (!dot) return 0;
    for (size_t i = 0; i < sizeof(exts)/sizeof(exts[0]); i++)
        if (_wcsicmp(dot, exts[i]) == 0) return 1;
    return 0;
}

static int skip_dir(const wchar_t *name) {
    return (name[0] == L'.' || name[0] == L'$');
}

static int append_entry(AppState *s, const wchar_t *full, const wchar_t *name)
{
    if (s->count >= s->capacity) {
        int new_cap = s->capacity ? s->capacity * 2 : 256;
        size_t sz   = new_cap * sizeof(ImageEntry);
        size_t align = 4096, mask = align - 1;
        size_t off = (s->arena.offset + mask) & ~mask;
        if (off + sz > ARENA_CAPACITY) return 0;

        s->images = (ImageEntry *)(s->arena.buf + off);
        s->arena.offset = off + sz;
        s->capacity = new_cap;
    }
    ImageEntry *e = &s->images[s->count];
    wcsncpy(e->path, full, MAX_PATH_LEN-1)[MAX_PATH_LEN-1] = L'\0';
    wcsncpy(e->filename, name, MAX_PATH_LEN-1)[MAX_PATH_LEN-1] = L'\0';
    e->thumbnail = e->full_image = NULL;
    e->loaded_thumb = e->loaded_full = 0;
    s->count++;
    return 1;
}

static void scan_recursive(const wchar_t *dir, AppState *s)
{
    wchar_t search[MAX_PATH_LEN];
    wcsncpy(search, dir, MAX_PATH_LEN-3)[MAX_PATH_LEN-3] = L'\0';
    size_t len = wcslen(search);
    wcscat(search, (len && search[len-1] != L'\\') ? L"\\*" : L"*");

    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(search, &ffd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (!wcscmp(ffd.cFileName, L".") || !wcscmp(ffd.cFileName, L".."))
            continue;
        wchar_t full[MAX_PATH_LEN];
        wcsncpy(full, dir, MAX_PATH_LEN-2)[MAX_PATH_LEN-2] = L'\0';
        len = wcslen(full);
        if (len && full[len-1] != L'\\') wcscat(full, L"\\");
        wcscat(full, ffd.cFileName);

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!skip_dir(ffd.cFileName)) scan_recursive(full, s);
        } else {
            if (fs_has_image_extension(ffd.cFileName))
                append_entry(s, full, ffd.cFileName);
        }
    } while (FindNextFileW(h, &ffd));
    FindClose(h);
}

int fs_scan_directory(const wchar_t *path, AppState *s)
{
    scan_recursive(path, s);
    wcsncpy(s->current_dir, path, MAX_PATH_LEN-1)[MAX_PATH_LEN-1] = L'\0';
    return s->count;
}
