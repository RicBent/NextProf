#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>

Result http_download(const char* url, const char* filepath, u32* out_size)
{
    Result r;
    FILE* file = NULL;
    httpcContext context;
    u32 statuscode = 0;
    u32 size = 0;
    u32 readsize = 0;
    u8* buffer = NULL;

    if (out_size != NULL)
        *out_size = 0;

    file = fopen(filepath, "wb");
    if (file == NULL)
        return -3;

    httpcOpenContext(&context, HTTPC_METHOD_GET, url, 1);
    httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&context, "User-Agent", "nextprof-app/1.0.0");
    httpcAddRequestHeaderField(&context, "Connection", "keep-alive");

    r = httpcBeginRequest(&context);
    if (R_FAILED(r))
    {
        httpcCloseContext(&context);
        return r;
    }

    r = httpcGetResponseStatusCode(&context, &statuscode);
    if (R_FAILED(r))
    {
        httpcCloseContext(&context);
        return r;
    }
    if (statuscode != 200)
    {
        httpcCloseContext(&context);
        return -2;
    }

    buffer = (u8*)malloc(0x1000);
    if (buffer==NULL){
        httpcCloseContext(&context);
        return -1;
    }

    do {
        readsize = 0;
        r = httpcDownloadData(&context, buffer, 0x1000, &readsize);
        size += readsize; 
        if (readsize > 0)
            fwrite(buffer, 1, readsize, file);
    } while (r == (s32)HTTPC_RESULTCODE_DOWNLOADPENDING);

    httpcCloseContext(&context);
    free(buffer);
    fclose(file);

    if (out_size)
        *out_size = size;

    return r;
}

