#pragma once

#include <3ds.h>

Result http_download(const char* url, const char* filepath, u32* out_size);
