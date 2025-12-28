#pragma once

#include <3ds.h>

#define HTTP_HOST "http://192.168.178.103:8009"

Result http_download(const char* url, const char* filepath, u32* out_size);
