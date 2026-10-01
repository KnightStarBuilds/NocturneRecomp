#pragma once
#include <cstdio>
// Owns the underlying stream; positions are physical byte offsets in POD files.
FILE* nocturne_fopen_text(const char* path, const char* mode);
