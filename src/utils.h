#pragma once
#include <stdint.h>
#include <wchar.h>

void format_size(uint64_t bytes, wchar_t *buf, int len);
void format_filetime(uint64_t filetime, wchar_t *buf, int len);
