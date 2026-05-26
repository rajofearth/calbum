#include "types.h"
#include "utils.h"
#include <stdio.h>

void format_size(uint64_t bytes, wchar_t *buf, int len)
{
    if (bytes >= 1024ULL * 1024 * 1024) {
        swprintf(buf, len, L"%.2f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ULL * 1024) {
        swprintf(buf, len, L"%.2f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        swprintf(buf, len, L"%.2f KB", (double)bytes / 1024.0);
    } else {
        swprintf(buf, len, L"%llu Bytes", bytes);
    }
}

void format_filetime(uint64_t filetime, wchar_t *buf, int len)
{
    FILETIME ft;
    ft.dwHighDateTime = (DWORD)(filetime >> 32);
    ft.dwLowDateTime = (DWORD)(filetime & 0xFFFFFFFF);
    
    SYSTEMTIME stUTC, stLocal;
    if (FileTimeToSystemTime(&ft, &stUTC)) {
        if (SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal)) {
            swprintf(buf, len, L"%04d-%02d-%02d %02d:%02d:%02d",
                     stLocal.wYear, stLocal.wMonth, stLocal.wDay,
                     stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
            return;
        }
    }
    wcscpy(buf, L"Unknown");
}
