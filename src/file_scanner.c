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

static int append_entry(AppState *s, const wchar_t *full, WIN32_FIND_DATAW *ffd)
{
    uint64_t file_size = ((uint64_t)ffd->nFileSizeHigh << 32) | ffd->nFileSizeLow;
    uint64_t last_modified = ((uint64_t)ffd->ftLastWriteTime.dwHighDateTime << 32) | ffd->ftLastWriteTime.dwLowDateTime;
    uint64_t created_time = ((uint64_t)ffd->ftCreationTime.dwHighDateTime << 32) | ffd->ftCreationTime.dwLowDateTime;
    ImageEntry *e = app_append_image_entry(s, full, ffd->cFileName, file_size, last_modified, created_time);
    return e != NULL;
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
                append_entry(s, full, &ffd);
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
