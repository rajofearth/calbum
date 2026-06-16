#ifndef CALBUM_UTILS_H
#define CALBUM_UTILS_H
#include "core.h"
void format_size(uint64_t bytes, wchar_t *buf, int len);
void format_filetime(uint64_t filetime, wchar_t *buf, int len);
#endif
